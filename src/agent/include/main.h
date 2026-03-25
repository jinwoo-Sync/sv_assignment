#pragma once

#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <netdb.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "iframe_handler.h"
#include "socket_utils.h"
#include "stream_buffer.h"
#include "logger_factory.h"

std::string make_agent_id()
{
    const char* env = std::getenv("AGENT_ID");
    if (env && *env) return env;
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) return hostname;
    return "agent-" + std::to_string(getpid());
}

std::string make_group()
{
    const char* env = std::getenv("AGENT_GROUP");
    return (env && *env) ? env : "pc";
}

// ── AgentSender ──────────────────────────────────────────────────
class AgentSender {
public:
    AgentSender(int sock, sv::TcpProtocol& protocol,
                const std::string& agentId, const std::string& group)
        : m_sock(sock), m_protocol(protocol), m_agentId(agentId), m_group(group), m_seq(1) {}

    void setMode(const std::string& mode) { m_mode = mode; }

    bool sendAck(uint32_t seq, const std::string& mode) {
        return sv::send_frame(m_sock, m_protocol, sv::MessageType::ACK, seq,
                              "{\"agent_id\":\"" + m_agentId + "\",\"mode\":\"" + mode + "\"}");
    }

    bool sendNack(uint32_t seq, const std::string& reason = "retry") {
        return sv::send_frame(m_sock, m_protocol, sv::MessageType::NACK, seq,
                              "{\"agent_id\":\"" + m_agentId + "\",\"reason\":\"" + reason + "\"}");
    }

    bool sendHello() {
        return sv::send_frame(m_sock, m_protocol, sv::MessageType::HELLO, m_seq++,
                              "{\"agent_id\":\"" + m_agentId + "\",\"group\":\"" + m_group + "\"}");
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
            return (rand() % 10001) / 100.0;
        };

        double   cpu_percent           = random_pct();
        double   temperature           = random_pct();
        double   mem_percent           = random_pct();
        double   cpu_available_percent = std::max(0.0, 100.0 - cpu_percent);
        uint32_t current_seq           = m_seq++;

        return sv::send_frame(m_sock, m_protocol, sv::MessageType::STATE, current_seq,
                              "{\"agent_id\":\"" + m_agentId +
                              "\",\"seq\":"                    + std::to_string(current_seq) +
                              ",\"mode\":\""                   + m_mode + "\""
                              ",\"cpu_percent\":"              + std::to_string(cpu_percent) +
                              ",\"temperature\":"              + std::to_string(temperature) +
                              ",\"mem_percent\":"              + std::to_string(mem_percent) +
                              ",\"cpu_available_percent\":"    + std::to_string(cpu_available_percent) + "}");
    }

private:
    int              m_sock;
    sv::TcpProtocol& m_protocol;
    std::string      m_agentId;
    std::string      m_group;
    uint32_t         m_seq;
    std::string      m_mode{"normal"};
};

// ── AgentFrameHandler ────────────────────────────────────────────
class AgentFrameHandler : public sv::IFrameHandler {
public:
    AgentFrameHandler(const std::string& agentId, AgentSender& sender)
        : m_agentId(agentId), m_sender(sender) {}

protected:
    void onAck(const sv::Frame& frame) override {
        const std::string payload(frame.payload.begin(), frame.payload.end());
        LOG_INFO("Agent", "ACK",
                 ("{\"agent_id\":\"" + m_agentId +
                  "\",\"seq\":"      + std::to_string(frame.seq) +
                  ",\"payload\":\""  + payload + "\"}").c_str());
    }

    void onNack(const sv::Frame& frame) override {
        const std::string payload(frame.payload.begin(), frame.payload.end());
        LOG_WARN("Agent", "NACK",
                 ("{\"agent_id\":\"" + m_agentId +
                  "\",\"seq\":"      + std::to_string(frame.seq) +
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
        const char* fault = std::getenv("FAULT_MODE");
        if (fault && std::string(fault) == "nack")
        {
            LOG_WARN("Agent", "NACK injected", ("{\"agent_id\":\"" + m_agentId + "\"}").c_str());
            m_sender.sendNack(frame.seq, "always");
            return;
        }
        const std::string payload(frame.payload.begin(), frame.payload.end());

        const std::string search = "\"mode\":\"";
        auto pos = payload.find(search);
        if (pos != std::string::npos)
        {
            auto start = pos + search.size();
            auto end   = payload.find('"', start);
            if (end != std::string::npos)
            {
                const std::string mode = payload.substr(start, end - start);
                m_sender.setMode(mode);
                m_sender.sendAck(frame.seq, mode);
            }
        }

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
