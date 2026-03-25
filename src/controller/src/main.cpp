#include "main.h"

int main() 
{
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

    // policy.json 핫-리로드용 마지막 mtime (초기값 0 → 첫 루프에서 갱신만, reload는 스킵)
    time_t config_mtime      = 0;
    time_t last_mtime_check  = 0;
    check_config_mtime("configs/policy.json", config_mtime);

    struct epoll_event events[MAX_EVENTS];
    while (true) {
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        if (num_events < 0)
        {
            perror("epoll_wait"); 
            break;
        }

        // 1초 마다 실행 — hot-reload / NACK retry / 헬스체크 / dead agent 재시작
        // num_events==0 한정이면 heartbeat 때문에 거의 안 돌아서 여기서 처리
        if (time(nullptr) - last_mtime_check >= 1)
        {
            last_mtime_check = time(nullptr);
            if (check_config_mtime("configs/policy.json", config_mtime))
            {
                LOG_INFO("Controller", "hot reload", "{\"path\":\"configs/policy.json\"}");
                policyEngine.reload();
            }
            process_nack_retries(agentStreamMap, tcpProtocolCodec);
            check_heartbeat_timeouts(epoll_fd, agentStreamMap, dead_agents);

            for (auto dead_agent_iterator = dead_agents.begin(); dead_agent_iterator != dead_agents.end(); )
            {
                if (time(nullptr) - dead_agent_iterator->second >= 3)
                {
                    restart_agent_container(dead_agent_iterator->first);
                    dead_agent_iterator = dead_agents.erase(dead_agent_iterator);
                }
                else
                {
                    dead_agent_iterator = std::next(dead_agent_iterator);
                }
            }
        }

        // ── 1초 타임아웃: 정책 평가 ──
        if (num_events == 0)
        {

            for (const auto& agentStreamMap_entry : agentStreamMap)
            {
                if (agentStreamMap_entry.second->group.empty()) 
                    continue;
                const std::string& group = agentStreamMap_entry.second->group;
                const std::string  mode  = policyEngine.evaluate(group, calcGroupAvgLoad(group, agentStreamMap));
                if (!mode.empty())
                {
                    broadcast_set_mode(group, mode, agentStreamMap, tcpProtocolCodec);
                }
                    
            }
            continue;
        }

        for (int i = 0; i < num_events; i++)
        {
            const int event_fd = events[i].data.fd;

            if (event_fd == server_fd)
            {
                handle_new_connection(server_fd, epoll_fd, agentStreamMap, tcpProtocolCodec);
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
                    is_alive = false; 
                    break;
                }
                else if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                else
                {
                    is_alive = false; 
                    break;
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
                    const std::string mode = policyEngine.evaluate(group, calcGroupAvgLoad(group, agentStreamMap));
                    if (!mode.empty())
                    {
                        broadcast_set_mode(group, mode, agentStreamMap, tcpProtocolCodec);
                    }
                }
            }
        }
    }

    return 0;
}
