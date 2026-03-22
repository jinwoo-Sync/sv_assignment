#include <cerrno>
#include <memory>
#include <string>
#include <unordered_map>

#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "iframe_handler.h"
#include "socket_utils.h"
#include "stream_buffer.h"
#include "logger_factory.h"
#include "PolicyEngine.h"

std::string extract_str(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\":\"";
    auto        pos    = json.find(search);
    if (pos == std::string::npos)
        return "";
    auto start = pos + search.size();
    auto end   = json.find('"', start);
    return json.substr(start, end - start);
}

double extract_num(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\":";
    auto        pos    = json.find(search);
    if (pos == std::string::npos)
        return 0.0;
    auto start = pos + search.size();
    auto end   = json.find_first_of(",}", start);
    if (end == std::string::npos)
        return 0.0;
    try { return std::stod(json.substr(start, end - start)); }
    catch (...) { return 0.0; }
}

// ── ControllerFrameHandler ───────────────────────────────────────
// Agent → Controller 방향 수신 콜백. IFrameHandler::onFrame이 SvStreamBuffer에 직접 등록됨.
// Frame이 완성될 때마다 onXxx()가 바로 호출.
class ControllerFrameHandler : public sv::IFrameHandler {
public:
    ControllerFrameHandler(int fd, sv::TcpProtocol& protocol,
                           std::string& agentId, std::string& group,
                           double& cpu_percent, double& temperature, double& mem_percent)
        : m_protocol(protocol), m_agentId(agentId), m_group(group)
        , m_cpu_percent(cpu_percent), m_temperature(temperature), m_mem_percent(mem_percent)
    {
        m_fd = fd;
    }

protected:
    void onHello(const sv::Frame& frame) override {
        const std::string payload(frame.payload.begin(), frame.payload.end());

        m_agentId = extract_str(payload, "agent_id");
        m_group   = extract_str(payload, "group");

        LOG_INFO("Controller", "HELLO",
                 ("{\"fd\":"        + std::to_string(m_fd) +
                  ",\"seq\":"       + std::to_string(frame.seq) +
                  ",\"agent_id\":\"" + m_agentId +
                  "\",\"group\":\""  + m_group + "\"}").c_str());
        sv::send_frame(m_fd, m_protocol, sv::MessageType::ACK,
                       frame.seq, "{\"status\":\"ok\"}");
    }

    void onHeartbeat(const sv::Frame& frame) override {
        const std::string payload(frame.payload.begin(), frame.payload.end());
        LOG_INFO("Controller", "HEARTBEAT",
                 ("{\"fd\":" + std::to_string(m_fd) +
                  ",\"seq\":" + std::to_string(frame.seq) +
                  ",\"payload\":\"" + payload + "\"}").c_str());
        sv::send_frame(m_fd, m_protocol, sv::MessageType::ACK,
                       frame.seq, "{\"heartbeat\":\"ok\"}");
    }

    void onState(const sv::Frame& frame) override {
        const std::string payload(frame.payload.begin(), frame.payload.end());

        m_cpu_percent  = extract_num(payload, "cpu_percent");
        m_temperature  = extract_num(payload, "temperature");
        m_mem_percent  = extract_num(payload, "mem_percent");

        LOG_INFO("Controller", "STATE",
                 ("{\"fd\":"             + std::to_string(m_fd) +
                  ",\"seq\":"            + std::to_string(frame.seq) +
                  ",\"cpu_percent\":"    + std::to_string(m_cpu_percent) +
                  ",\"temperature\":"    + std::to_string(m_temperature) +
                  ",\"mem_percent\":"    + std::to_string(m_mem_percent) + "}").c_str());
        sv::send_frame(m_fd, m_protocol, sv::MessageType::ACK,
                       frame.seq, "{\"state\":\"ok\"}");
    }

    void onNack(const sv::Frame& frame) override {
        const std::string payload(frame.payload.begin(), frame.payload.end());
        LOG_WARN("Controller", "NACK",
                 ("{\"fd\":" + std::to_string(m_fd) +
                  ",\"payload\":\"" + payload + "\"}").c_str());
    }

    void onError(const sv::Frame& frame) override {
        const std::string payload(frame.payload.begin(), frame.payload.end());
        LOG_ERROR("Controller", "ERROR",
                  ("{\"fd\":" + std::to_string(m_fd) +
                   ",\"payload\":\"" + payload + "\"}").c_str());
    }

private:
    sv::TcpProtocol& m_protocol;
    std::string&     m_agentId;
    std::string&     m_group;
    double&          m_cpu_percent;
    double&          m_temperature;
    double&          m_mem_percent;
};

// ── AgentStream ──────────────────────────────────────────────────
// 연결당 1개 생성. ControllerFrameHandler와 SvStreamBuffer를 묶어 수명을 같이 관리.
// SvStreamBuffer가 handler의 주소를 보관하므로 반드시 같이 있어야 함.
struct AgentStream {
    std::string            agentId;
    std::string            group;
    double                 cpu_percent  = 0.0;
    double                 temperature  = 0.0;
    double                 mem_percent  = 0.0;
    ControllerFrameHandler handler;
    sv::SvStreamBuffer     stream;

    AgentStream(int fd, sv::TcpProtocol& protocol)
        : handler(fd, protocol, agentId, group, cpu_percent, temperature, mem_percent)
        , stream(protocol, sv::IFrameHandler::onFrame, &handler) {}
};

double calcGroupAvgLoad(const std::string& group,
                        const std::unordered_map<int, std::unique_ptr<AgentStream>>& agentStreamMap)
{
    double sum   = 0.0;
    int    count = 0;
    for (const auto& agentEntry : agentStreamMap)
    {
        if (agentEntry.second->group != group)
            continue;
        sum += agentEntry.second->cpu_percent
             + agentEntry.second->temperature
             + agentEntry.second->mem_percent;
        ++count;
    }
    return (count > 0) ? (sum / (3.0 * count)) : 0.0;
}

void broadcast_set_mode(const std::string& group, const std::string& mode,
                               std::unordered_map<int, std::unique_ptr<AgentStream>>& agentStreamMap,
                               sv::TcpProtocol& protocol)
{
    for (auto& agentEntry : agentStreamMap)
    {
        if (agentEntry.second->group != group)
            continue;

        const int fd = agentEntry.first;
        bool ok = sv::send_frame(fd, protocol, sv::MessageType::CMD_SET_MODE, 0,
                                 "{\"mode\":\"" + mode + "\"}");
        if (!ok)
        {
            LOG_WARN("Controller", "CMD_SET_MODE send failed",
                     ("{\"fd\":" + std::to_string(fd) + ",\"group\":\"" + group + "\"}").c_str());
        }
    }
}

void close_connection(int fd, int epoll_fd,
                             std::unordered_map<int, std::unique_ptr<AgentStream>>& agentStreamMap) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    agentStreamMap.erase(fd);
    LOG_INFO("Controller", "Agent disconnected",
             ("{\"fd\":" + std::to_string(fd) + "}").c_str());
}

// ── main ─────────────────────────────────────────────────────────

int main() {
    const int PORT       = 9090;
    const int MAX_EVENTS = 100;

    sv::LoggerFactory::instance().init(sv::LogLevel::INFO);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket"); return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("bind"); return 1;
    }
    if (listen(server_fd, 50) < 0)
    {
        perror("listen"); return 1;
    }
    sv::set_nonblocking(server_fd);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
    {
        perror("epoll_create1"); return 1;
    }

    struct epoll_event server_event{};
    server_event.events  = EPOLLIN;
    server_event.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &server_event);

    LOG_INFO("Controller", "Started",
             ("{\"port\":" + std::to_string(PORT) + "}").c_str());

    sv::TcpProtocol tcpProtocolCodec;

    // fd → AgentStream (ControllerFrameHandler + SvStreamBuffer 묶음)
    std::unordered_map<int, std::unique_ptr<AgentStream>> agentStreamMap;

    sv::PolicyEngine policyEngine;
    policyEngine.setConfigPath("configs/policy.json");
    policyEngine.reload();


    struct epoll_event events[MAX_EVENTS];
    while (true) {
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (num_events < 0)
        {
            perror("epoll_wait"); break;
        }

        for (int i = 0; i < num_events; ++i) {
            int event_fd = events[i].data.fd;

            // 새 연결
            if (event_fd == server_fd)
            {
                int client_fd = accept(server_fd, nullptr, nullptr);
                if (client_fd < 0)
                {
                    continue;
                }

                sv::set_nonblocking(client_fd);

                struct epoll_event client_event{};
                client_event.events  = EPOLLIN | EPOLLET;
                client_event.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) < 0)
                {
                    close(client_fd); continue;
                }

                // 연결당 AgentStream 1개 생성 (ControllerFrameHandler + SvStreamBuffer)
                agentStreamMap[client_fd] = std::make_unique<AgentStream>(client_fd, tcpProtocolCodec);

                LOG_INFO("Controller", "Agent connected",
                         ("{\"fd\":" + std::to_string(client_fd) + "}").c_str());
                continue;
            }

            // 데이터 수신: OS 버퍼를 EAGAIN까지 완전히 드레인
            uint8_t recv_buffer[4096];
            bool    is_alive = true;

            while (true) {
                ssize_t bytes_received = recv(event_fd, recv_buffer, sizeof(recv_buffer), 0);
                if (bytes_received > 0)
                {
                    agentStreamMap[event_fd]->stream.appendReceivedBytes(
                        recv_buffer, (size_t)bytes_received);
                }
                else if (bytes_received == 0)
                {
                    is_alive = false; break;
                }
                else if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                else
                {
                    is_alive = false; break;
                }
            }

            if (!is_alive)
            {
                close_connection(event_fd, epoll_fd, agentStreamMap);
            }
            else
            {
                const std::string& group = agentStreamMap[event_fd]->group;
                if (!group.empty())
                {
                    double      avgLoad = calcGroupAvgLoad(group, agentStreamMap);
                    std::string mode    = policyEngine.decide(group, avgLoad);
                    if (!mode.empty())
                        broadcast_set_mode(group, mode, agentStreamMap, tcpProtocolCodec);
                }
            }
        }
    }

    return 0;
}
