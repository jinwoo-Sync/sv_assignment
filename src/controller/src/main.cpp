#include "main.h"

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

    // 연결 끊긴 agent: agentId → 끊긴 시각 (3초 후 재시작)
    std::unordered_map<std::string, time_t> dead_agents;

    sv::PolicyEngine policyEngine;
    policyEngine.setConfigPath("configs/policy.json");
    policyEngine.reload();

    struct epoll_event events[MAX_EVENTS];
    while (true) {
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        if (num_events < 0)
        {
            perror("epoll_wait"); break;
        }

        if (num_events == 0)
        {
            for (auto agentMap_iterator = agentStreamMap.begin(); agentMap_iterator != agentStreamMap.end(); )
            {
                if (!agentMap_iterator->second->agentId.empty() && !checkHeartbeat(*agentMap_iterator->second))
                {
                    LOG_WARN("Controller", "Heartbeat timeout",
                             ("{\"fd\":"         + std::to_string(agentMap_iterator->first) +
                              ",\"agent_id\":\"" + agentMap_iterator->second->agentId +
                              "\",\"elapsed\":"  + std::to_string(time(nullptr) - agentMap_iterator->second->lastHeartbeat) + "}").c_str());
                    dead_agents[agentMap_iterator->second->agentId] = time(nullptr);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, agentMap_iterator->first, nullptr);
                    close(agentMap_iterator->first);
                    agentMap_iterator = agentStreamMap.erase(agentMap_iterator);
                }
                else
                {
                    ++agentMap_iterator;
                }
            }

            for (auto dead_agent_iterator = dead_agents.begin(); dead_agent_iterator != dead_agents.end(); )
            {
                if (time(nullptr) - dead_agent_iterator->second >= 3)
                {
                    restart_agent_container(dead_agent_iterator->first);
                    dead_agent_iterator = dead_agents.erase(dead_agent_iterator);
                }
                else { ++dead_agent_iterator; }
            }

            for (const auto& entry : agentStreamMap)
            {
                if (entry.second->group.empty()) continue;
                const std::string& g    = entry.second->group;
                std::string        mode = policyEngine.evaluate(g, calcGroupAvgLoad(g, agentStreamMap));
                if (!mode.empty())
                    broadcast_set_mode(g, mode, agentStreamMap, tcpProtocolCodec);
            }
            continue;
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
                close_connection(event_fd, epoll_fd, agentStreamMap, dead_agents);
            }
            else
            {
                const std::string& group = agentStreamMap[event_fd]->group;
                if (!group.empty())
                {
                    double      avgLoad = calcGroupAvgLoad(group, agentStreamMap);
                    std::string mode    = policyEngine.evaluate(group, avgLoad);
                    if (!mode.empty())
                        broadcast_set_mode(group, mode, agentStreamMap, tcpProtocolCodec);
                }
            }
        }
    }

    return 0;
}
