#pragma once

#include <cstdlib>
#include <cerrno>
#include <ctime>
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
    ControllerFrameHandler                handler;
    sv::SvStreamBuffer                    stream;

    AgentStream(int fd, sv::TcpProtocol& protocol)
        : handler(fd, protocol, agentId, group,
                  cpu_percent, temperature, mem_percent, lastHeartbeat)
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

bool checkHeartbeat(const AgentStream& agent)
{
    return (time(nullptr) - agent.lastHeartbeat) < 3;
}

void restart_agent_container(const std::string& agentId)
{
    const std::string container = "sv-assignment-" + agentId;
    LOG_WARN("Controller", "Restarting agent container",
             ("{\"container\":\"" + container + "\"}").c_str());
    system(("docker restart " + container).c_str());
}

void close_connection(int fd, int epoll_fd,
                             std::unordered_map<int, std::unique_ptr<AgentStream>>& agentStreamMap,
                             std::unordered_map<std::string, time_t>& dead_agents)
{
    auto agentMap_iterator = agentStreamMap.find(fd);
    std::string agentId = (agentMap_iterator != agentStreamMap.end()) ? agentMap_iterator->second->agentId : "";

    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    agentStreamMap.erase(fd);
    LOG_INFO("Controller", "Agent disconnected",
             ("{\"fd\":" + std::to_string(fd) + "}").c_str());

    if (!agentId.empty())
        dead_agents[agentId] = time(nullptr);
}
