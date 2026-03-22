#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>

#include <netdb.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "iframe_handler.h"
#include "socket_utils.h"
#include "stream_buffer.h"
#include "logger_factory.h"

static std::string make_agent_id() {
    const char* env = std::getenv("AGENT_ID");
    return (env && *env) ? env : "agent-" + std::to_string(getpid());
}

// ── AgentSender ──────────────────────────────────────────────────
// Controller에 보내는 프레임 전송 담당.
class AgentSender {
public:
    AgentSender(int sock, sv::TcpProtocol& protocol, const std::string& agentId)
        : m_sock(sock), m_protocol(protocol), m_agentId(agentId), m_seq(1) {}

    bool sendHello() {
        return sv::send_frame(m_sock, m_protocol, sv::MessageType::HELLO, m_seq++,
                              "{\"agent_id\":\"" + m_agentId + "\",\"group\":\"default\"}");
    }

    bool sendHeartbeat() {
        auto now_ms = std::chrono::steady_clock::now().time_since_epoch();
        long long timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_ms).count();

        return sv::send_frame(m_sock, m_protocol, sv::MessageType::HEARTBEAT, m_seq++,
                              "{\"agent_id\":\"" + m_agentId +
                              "\",\"ts\":" + std::to_string(timestamp_ms) + "}");
    }

    bool sendState() {
        auto random_pct = []() {
            return (rand() % 10001) / 100.0; // 0.00 ~ 100.00
        };

        double   cpu_pc         = random_pct();
        double   temperature = random_pct();
        double   mem         = random_pct();
        double   cpu_avail   = std::max(0.0, 100.0 - cpu_pc);
        uint32_t current_seq = m_seq++;

        return sv::send_frame(m_sock, m_protocol, sv::MessageType::STATE, current_seq,
                              "{\"agent_id\":\"" + m_agentId +
                              "\",\"seq\":"      + std::to_string(current_seq) +
                              ",\"mode\":\"Active\""
                              ",\"cpu_pct\":"          + std::to_string(cpu_pc)  +
                              ",\"temperature\":"      + std::to_string(temperature) +
                              ",\"mem_pct\":"          + std::to_string(mem)  +
                              ",\"cpu_available_pct\":"+ std::to_string(cpu_avail) + "}");
    }

private:
    int              m_sock;
    sv::TcpProtocol& m_protocol;
    std::string      m_agentId;
    uint32_t         m_seq;
};

// ── AgentFrameHandler ────────────────────────────────────────────
// Controller → Agent 방향의 수신 콜백. IFrameHandler를 상속.
// operator()와 switch 분기는 IFrameHandler가 담당하므로
// 여기서는 필요한 onXxx()만 오버라이드.
class AgentFrameHandler : public sv::IFrameHandler {
public:
    AgentFrameHandler(const std::string& agentId, AgentSender& sender)
        : m_agentId(agentId), m_sender(sender) {}

protected:
    void onAck(const sv::Frame& frame) override {
        const std::string payload(frame.payload.begin(), frame.payload.end());
        LOG_INFO("Agent", "ACK",
                 ("{\"agent_id\":\"" + m_agentId +
                  "\",\"seq\":"     + std::to_string(frame.seq) +
                  ",\"payload\":\""  + payload + "\"}").c_str());
    }

    void onNack(const sv::Frame& frame) override {
        const std::string payload(frame.payload.begin(), frame.payload.end());
        LOG_WARN("Agent", "NACK",
                 ("{\"agent_id\":\"" + m_agentId +
                  "\",\"seq\":"     + std::to_string(frame.seq) +
                  ",\"payload\":\""  + payload + "\"}").c_str());
    }

    void onCmdStart(const sv::Frame& frame) override {
        LOG_INFO("Agent", "CMD_START", ("{\"agent_id\":\"" + m_agentId + "\"}").c_str());
        m_sender.sendHeartbeat();
    }

    void onCmdStop(const sv::Frame& frame) override {
        LOG_INFO("Agent", "CMD_STOP", ("{\"agent_id\":\"" + m_agentId + "\"}").c_str());
    }

    void onCmdSetMode(const sv::Frame& frame) override {
        const std::string payload(frame.payload.begin(), frame.payload.end());
        LOG_INFO("Agent", "CMD_SET_MODE",
                 ("{\"agent_id\":\"" + m_agentId +
                  "\",\"payload\":\""  + payload + "\"}").c_str());
    }

    void onError(const sv::Frame& frame) override {
        const std::string payload(frame.payload.begin(), frame.payload.end());
        LOG_ERROR("Agent", "ERROR",
                  ("{\"agent_id\":\"" + m_agentId +
                   "\",\"payload\":\""  + payload + "\"}").c_str());
    }

private:
    std::string  m_agentId;
    AgentSender& m_sender;
};

// ── main ─────────────────────────────────────────────────────────

int main(int argc, char const* argv[]) {
    const char*       host    = (argc > 1) ? argv[1] : "controller";
    const std::string agentId = make_agent_id();

    sv::LoggerFactory::instance().init(sv::LogLevel::INFO);
    srand((unsigned int)time(nullptr));

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("socket"); return 1;
    }
    sv::set_nonblocking(sock);

    struct hostent* host_entry = nullptr;
    while (!(host_entry = gethostbyname(host))) {
        LOG_WARN("Agent", "DNS lookup failed",
                 ("{\"target\":\"" + std::string(host) + "\"}").c_str());
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(9090);
    std::memcpy(&addr.sin_addr, host_entry->h_addr_list[0], host_entry->h_length);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        if (errno != EINPROGRESS)
        {
            perror("connect"); return 1;
        }
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
    {
        perror("epoll_create1"); return 1;
    }

    struct epoll_event epoll_ev{};
    epoll_ev.events  = EPOLLOUT | EPOLLET;
    epoll_ev.data.fd = sock;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &epoll_ev);

    sv::TcpProtocol   protocol;
    AgentSender       sender(sock, protocol, agentId);
    AgentFrameHandler handler(agentId, sender);

    // IFrameHandler::onFrame: static 함수로 void* → AgentFrameHandler* 복원 후 dispatch.
    sv::SvStreamBuffer stream(protocol, sv::IFrameHandler::onFrame, &handler);

    bool connected = false;
    bool helloSent = false;
    auto lastHeartbeat = std::chrono::steady_clock::now();
    auto lastState     = std::chrono::steady_clock::now();

    LOG_INFO("Agent", "Connecting",
             ("{\"target\":\"" + std::string(host) +
              "\",\"agent_id\":\"" + agentId + "\"}").c_str());

    struct epoll_event events[10];
    while (true) {
        int num_events = epoll_wait(epoll_fd, events, 10, 100);

        for (int i = 0; i < num_events; ++i) {
            if ((events[i].events & EPOLLOUT) && !connected)
            {
                int       error = 0;
                socklen_t len   = sizeof(error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);
                if (error != 0)
                {
                    LOG_ERROR("Agent", "Connect error",
                              ("{\"error\":\"" + std::string(strerror(error)) + "\"}").c_str());
                    return 1;
                }
                connected        = true;
                epoll_ev.events  = EPOLLIN | EPOLLET;
                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sock, &epoll_ev);
                LOG_INFO("Agent", "Connected",
                         ("{\"agent_id\":\"" + agentId + "\"}").c_str());
            }

            if (events[i].events & EPOLLIN)
            {
                uint8_t recv_buffer[4096];
                bool    is_alive = true;

                while (true) {
                    ssize_t bytes_received = recv(sock, recv_buffer, sizeof(recv_buffer), 0);
                    if (bytes_received > 0)
                    {
                        stream.appendReceivedBytes(recv_buffer, static_cast<size_t>(bytes_received));
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
                    LOG_WARN("Agent", "Controller disconnected",
                             ("{\"agent_id\":\"" + agentId + "\"}").c_str());
                    close(sock);
                    return 0;
                }
            }
        }

        if (connected)
        {
            if (!helloSent)
            {
                helloSent = sender.sendHello();
            }
            auto now = std::chrono::steady_clock::now();
            if (now - lastHeartbeat >= std::chrono::seconds(1))
            {
                sender.sendHeartbeat();
                lastHeartbeat = now;
            }
            if (now - lastState >= std::chrono::seconds(3))
            {
                sender.sendState();
                lastState = now;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return 0;
}
