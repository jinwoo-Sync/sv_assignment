#include <cerrno>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "socket_utils.h"
#include "tcp_protocol.h"
#include "logger_factory.h"

int main() {
    const int PORT = 9090;
    const int MAX_EVENTS = 100;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
    }
    
    struct sockaddr_in address = {AF_INET, htons(PORT), INADDR_ANY};
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return 1;
    }

    if (listen(server_fd, 50) < 0) {
        perror("listen failed");
        return 1;
    }

    if (!sv::set_nonblocking(server_fd)) {
        perror("set_nonblocking failed");
    }

    // 2. epoll 생성 및 등록
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1 failed");
        return 1;
    }

    struct epoll_event ev = {EPOLLIN, {.fd = server_fd}}, events[MAX_EVENTS];
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        perror("epoll_ctl failed");
        return 1;
    }

    sv::LoggerFactory::instance().init(sv::LogLevel::INFO);
    LOG_INFO("Controller", "Started", ("{\"port\":" + std::to_string(PORT) + "}").c_str());

    sv::TcpProtocol protocol;
    std::unordered_map<int, std::vector<uint8_t>> recvBuffers;

    auto close_connection = [&](int fd) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        recvBuffers.erase(fd);
        LOG_INFO("Controller", "Agent disconnected",
                 ("{\"fd\":" + std::to_string(fd) + "}").c_str());
    };

    auto send_frame = [&](int fd, sv::MessageType type, uint32_t seq,
                          const std::string& payload) {
        sv::Frame frame;
        frame.type = type;
        frame.seq  = seq;
        frame.payload.assign(payload.begin(), payload.end());
        const std::vector<uint8_t> bytes = protocol.encode(frame);

        size_t offset = 0;
        while (offset < bytes.size()) {
            ssize_t written =
                send(fd, bytes.data() + offset, bytes.size() - offset, 0);
            if (written > 0) {
                offset += static_cast<size_t>(written);
            } else if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue;
            } else {
                close_connection(fd);
                break;
            }
        }
    };

    auto handle_frame = [&](int fd, const sv::Frame& frame) {
        const std::string payload(frame.payload.begin(), frame.payload.end());
        std::string fields = "{\"fd\":" + std::to_string(fd) +
                             ",\"seq\":" + std::to_string(frame.seq) +
                             ",\"payload\":\"" + payload + "\"}";
        switch (frame.type) {
        case sv::MessageType::HELLO:
            LOG_INFO("Controller", "HELLO received", fields.c_str());
            send_frame(fd, sv::MessageType::ACK, frame.seq, "{\"status\":\"ok\"}");
            break;
        case sv::MessageType::HEARTBEAT:
            LOG_INFO("Controller", "HEARTBEAT received", fields.c_str());
            send_frame(fd, sv::MessageType::ACK, frame.seq, "{\"heartbeat\":\"ok\"}");
            break;
        case sv::MessageType::STATE:
            LOG_INFO("Controller", "STATE received", fields.c_str());
            send_frame(fd, sv::MessageType::ACK, frame.seq, "{\"state\":\"ok\"}");
            break;
        default:
            LOG_INFO("Controller", "Frame received", fields.c_str());
            break;
        }
    };

    auto process_buffer = [&](int fd) {
        auto& buffer = recvBuffers[fd];
        size_t offset = 0;
        while (offset < buffer.size()) {
            size_t consumed = 0;
            std::unique_ptr<sv::Frame> frame =
                protocol.decode(buffer.data() + offset, buffer.size() - offset,
                                consumed);
            if (!frame) {
                if (consumed == 0) {
                    break;
                }
                offset += consumed;
                continue;
            }

            offset += consumed;
            handle_frame(fd, *frame);
        }

        if (offset > 0 && offset <= buffer.size()) {
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<long>(offset));
        }
    };

    while (true) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            perror("epoll_wait failed");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == server_fd) {
                int client_fd = accept(server_fd, NULL, NULL);
                if (client_fd < 0) continue;

                sv::set_nonblocking(client_fd);
                struct epoll_event client_ev = {EPOLLIN | EPOLLET, {.fd = client_fd}};
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) < 0) {
                    perror("epoll_ctl client failed");
                    close(client_fd);
                    continue;
                }
                recvBuffers[client_fd] = {};
                LOG_INFO("Controller", "Agent connected",
                         ("{\"fd\":" + std::to_string(client_fd) + "}").c_str());
            } else {
                int fd = events[i].data.fd;
                auto& buffer = recvBuffers[fd];
                uint8_t temp[1024];
                bool alive = true;

                while (true) {
                    ssize_t bytes = recv(fd, temp, sizeof(temp), 0);
                    if (bytes > 0) {
                        buffer.insert(buffer.end(), temp, temp + bytes);
                    } else if (bytes == 0) {
                        alive = false;
                        break;
                    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    } else {
                        alive = false;
                        break;
                    }
                }

                if (!alive) {
                    close_connection(fd);
                    continue;
                }

                process_buffer(fd);
            }
        }
    }
    return 0;
}
