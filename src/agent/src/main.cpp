#include "main.h"

int main(int argc, char* argv[]) {
    const char* host    = (argc > 1) ? argv[1] : "controller";
    std::string agentId = make_agent_id();
    std::string group   = make_group();

    sv::LoggerFactory::instance().init(sv::LogLevel::INFO);
    srand((unsigned int)time(nullptr));

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        LOG_ERROR("Agent", "socket failed",
                  ("{\"error\":\"" + std::string(strerror(errno)) + "\"}").c_str());
        return 1;
    }
    sv::set_nonblocking(sock);

    struct addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    while (getaddrinfo(host, "9090", &hints, &res) != 0)
    {
        LOG_WARN("Agent", "DNS lookup failed",
                 ("{\"target\":\"" + std::string(host) + "\"}").c_str());
        sleep(1);
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0)
    {
        if (errno != EINPROGRESS)
        {
            LOG_ERROR("Agent", "connect failed",
                      ("{\"error\":\"" + std::string(strerror(errno)) + "\"}").c_str());
            freeaddrinfo(res);
            close(sock);
            return 1;
        }
    }
    freeaddrinfo(res);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
    {
        LOG_ERROR("Agent", "epoll_create1 failed",
                  ("{\"error\":\"" + std::string(strerror(errno)) + "\"}").c_str());
        close(sock);
        return 1;
    }

    struct epoll_event epoll_ev{};
    epoll_ev.events  = EPOLLOUT | EPOLLET;
    epoll_ev.data.fd = sock;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &epoll_ev);

    sv::TcpProtocol   protocol;
    AgentSender       sender(sock, protocol, agentId, group);
    AgentFrameHandler handler(agentId, sender);
    sv::SvStreamBuffer stream(protocol, sv::IFrameHandler::onFrame, &handler);

    bool connected = false;
    bool helloSent = false;
    auto lastHeartbeat = std::chrono::steady_clock::now();
    auto lastState     = std::chrono::steady_clock::now();

    LOG_INFO("Agent", "Connecting",
             ("{\"target\":\"" + std::string(host) +
              "\",\"agent_id\":\"" + agentId + "\"}").c_str());

    struct epoll_event events[10];
    while (true)
    {
        int num_events = epoll_wait(epoll_fd, events, 10, 100);

        for (int i = 0; i < num_events; i++)
        {
            if ((events[i].events & EPOLLOUT) && !connected)
            {
                int       error = 0;
                socklen_t len   = sizeof(error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);
                if (error != 0)
                {
                    LOG_ERROR("Agent", "Connect error",
                              ("{\"error\":\"" + std::string(strerror(error)) + "\"}").c_str());
                    close(sock);
                    return 1;
                }
                connected       = true;
                epoll_ev.events = EPOLLIN | EPOLLET;
                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sock, &epoll_ev);
                LOG_INFO("Agent", "Connected",
                         ("{\"agent_id\":\"" + agentId + "\"}").c_str());
            }

            if (events[i].events & EPOLLIN)
            {
                uint8_t recv_buffer[4096];
                bool    is_alive = true;

                while (true)
                {
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
    }

    return 0;
}
