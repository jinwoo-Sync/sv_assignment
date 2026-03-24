#pragma once

#include <cstdlib>
#include <cerrno>
#include <ctime>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>

#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
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
class ControllerFrameHandler : public sv::IFrameHandler {
public:
    ControllerFrameHandler(int fd, sv::TcpProtocol& protocol,
                           std::string& agentId, std::string& group,
                           double& cpu_percent, double& temperature, double& mem_percent,
                           time_t& lastHeartbeat)
        : m_protocol(protocol), m_agentId(agentId), m_group(group)
        , m_cpu_percent(cpu_percent), m_temperature(temperature), m_mem_percent(mem_percent)
        , m_lastHeartbeat(lastHeartbeat)
    {
        m_fd = fd;
    }

protected:
    void onHello(const sv::Frame& frame) override {
        m_lastHeartbeat = time(nullptr);
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
        m_lastHeartbeat = time(nullptr);
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
    time_t&          m_lastHeartbeat;
};

// ── AgentStream ──────────────────────────────────────────────────
struct AgentStream {
    std::string                           agentId;
    std::string                           group;
    double                                cpu_percent   = 0.0;
    double                                temperature   = 0.0;
    double                                mem_percent   = 0.0;
    time_t                                lastHeartbeat = time(nullptr);
    bool                                  isUnhealthy   = false;
    ControllerFrameHandler                handler;
    sv::SvStreamBuffer                    stream;

    AgentStream(int fd, sv::TcpProtocol& protocol)
        : handler(fd, protocol, agentId, group,
                  cpu_percent, temperature, mem_percent, lastHeartbeat)
        , stream(protocol, sv::IFrameHandler::onFrame, &handler) {}
};

// ── 그룹 평균 부하 계산 ──────────────────────────────────────────
double calcGroupAvgLoad(const std::string& group,
                        const std::unordered_map<int, std::unique_ptr<AgentStream>>& agentStreamMap)
{
    double sum   = 0.0;
    int    count = 0;
    for (const auto& agentStreamMap_entry : agentStreamMap)
    {
        if (agentStreamMap_entry.second->group != group) continue;
        sum += agentStreamMap_entry.second->cpu_percent
             + agentStreamMap_entry.second->temperature
             + agentStreamMap_entry.second->mem_percent;
        ++count;
    }
    return (count > 0) ? (sum / (3.0 * count)) : 0.0;
}

// ── 그룹 내 전체 agent에 CMD_SET_MODE 전송 ──────────────────────
void broadcast_set_mode(const std::string& group, const std::string& mode,
                        const std::unordered_map<int, std::unique_ptr<AgentStream>>& agentStreamMap,
                        sv::TcpProtocol& protocol)
{
    for (const auto& agentStreamMap_entry : agentStreamMap)
    {
        if (agentStreamMap_entry.second->group != group) continue;

        const int fd = agentStreamMap_entry.first;
        bool ok = sv::send_frame(fd, protocol, sv::MessageType::CMD_SET_MODE, 0,
                                 "{\"mode\":\"" + mode + "\"}");
        if (!ok)
            LOG_WARN("Controller", "CMD_SET_MODE send failed",
                     ("{\"fd\":" + std::to_string(fd) + ",\"group\":\"" + group + "\"}").c_str());
    }
}

// ── 헬스체크 타임아웃 감지
//    3s~10s : Unhealthy — 연결 유지, agent 자가 회복 대기
//    10s 초과: 연결 강제 종료 → dead_agents 등록 → docker restart
void check_heartbeat_timeouts(int epoll_fd,
                               std::unordered_map<int, std::unique_ptr<AgentStream>>& agentStreamMap,
                               std::unordered_map<std::string, time_t>& dead_agents)
{
    for (auto agentMap_iterator = agentStreamMap.begin(); agentMap_iterator != agentStreamMap.end(); )
    {
        if (agentMap_iterator->second->agentId.empty())
        {
            agentMap_iterator = std::next(agentMap_iterator);
            continue;
        }

        const time_t elapsed = time(nullptr) - agentMap_iterator->second->lastHeartbeat;

        if (elapsed >= 10)
        {
            // 10초 초과 → 강제 종료 + docker restart
            LOG_WARN("Controller", "Heartbeat timeout, force restart",
                     ("{\"fd\":"         + std::to_string(agentMap_iterator->first) +
                      ",\"agent_id\":\"" + agentMap_iterator->second->agentId +
                      "\",\"elapsed\":"  + std::to_string(elapsed) + "}").c_str());
            dead_agents[agentMap_iterator->second->agentId] = time(nullptr);
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, agentMap_iterator->first, nullptr);
            close(agentMap_iterator->first);
            agentMap_iterator = agentStreamMap.erase(agentMap_iterator);
        }
        else if (elapsed >= 3)
        {
            // 3s~10s → Unhealthy, 첫 진입 시에만 로그
            if (!agentMap_iterator->second->isUnhealthy)
            {
                agentMap_iterator->second->isUnhealthy = true;
                LOG_WARN("Controller", "Unhealthy",
                         ("{\"fd\":"         + std::to_string(agentMap_iterator->first) +
                          ",\"agent_id\":\"" + agentMap_iterator->second->agentId +
                          "\",\"elapsed\":"  + std::to_string(elapsed) + "}").c_str());
            }
            agentMap_iterator = std::next(agentMap_iterator);
        }
        else
        {
            // elapsed < 3s → 정상 or 회복
            if (agentMap_iterator->second->isUnhealthy)
            {
                agentMap_iterator->second->isUnhealthy = false;
                LOG_INFO("Controller", "Recovered",
                         ("{\"fd\":"         + std::to_string(agentMap_iterator->first) +
                          ",\"agent_id\":\"" + agentMap_iterator->second->agentId + "}").c_str());
            }
            agentMap_iterator = std::next(agentMap_iterator);
        }
    }
}

// ── dead agent 컨테이너 재시작 요청 ─────────────────────────────
void restart_agent_container(const std::string& agentId)
{
    const std::string container = "sv-assignment-" + agentId;
    LOG_WARN("Controller", "Restarting agent container",
             ("{\"container\":\"" + container + "\"}").c_str());
    system(("docker restart " + container).c_str());
}

// ── 연결 종료 및 agentStreamMap 정리 ────────────────────────────
void close_connection(int fd, int epoll_fd,
                      std::unordered_map<int, std::unique_ptr<AgentStream>>& agentStreamMap,
                      std::unordered_map<std::string, time_t>& dead_agents)
{
    auto agentMap_iterator = agentStreamMap.find(fd);
    const std::string agentId = (agentMap_iterator != agentStreamMap.end())
                                ? agentMap_iterator->second->agentId : "";

    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    agentStreamMap.erase(fd);
    LOG_INFO("Controller", "Agent disconnected",
             ("{\"fd\":" + std::to_string(fd) + "}").c_str());

    if (!agentId.empty())
        dead_agents[agentId] = time(nullptr);
}

// ── 신규 연결 수락 및 epoll 등록 ────────────────────────────────
void handle_new_connection(int server_fd, int epoll_fd,
                            std::unordered_map<int, std::unique_ptr<AgentStream>>& agentStreamMap,
                            sv::TcpProtocol& protocol)
{
    int client_fd = accept(server_fd, nullptr, nullptr);
    if (client_fd < 0) return;

    sv::set_nonblocking(client_fd);

    struct epoll_event client_event{};
    client_event.events  = EPOLLIN | EPOLLET;
    client_event.data.fd = client_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) < 0)
    {
        close(client_fd); return;
    }

    agentStreamMap[client_fd] = std::make_unique<AgentStream>(client_fd, protocol);
    LOG_INFO("Controller", "Agent connected",
             ("{\"fd\":" + std::to_string(client_fd) + "}").c_str());
}

// ── policy.json mtime 변경 감지 → 변경됐으면 true 반환 ──────────
bool check_config_mtime(const std::string& path, time_t& last_mtime)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    if (st.st_mtime == last_mtime)
        return false;
    last_mtime = st.st_mtime;
    return true;
}
