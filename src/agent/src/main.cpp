#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <netdb.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "socket_utils.h"
#include "tcp_protocol.h"
#include "logger_factory.h"

namespace {

std::vector<uint8_t> to_bytes(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::string make_agent_id() {
    const char* envId = std::getenv("AGENT_ID");
    if (envId && *envId) {
        return envId;
    }
    return "agent-" + std::to_string(getpid());
}

bool send_frame(int fd, sv::TcpProtocol& protocol, sv::MessageType type,
                uint32_t seq, const std::string& payload) {
    sv::Frame frame;
    frame.type = type;
    frame.seq  = seq;
    frame.payload = to_bytes(payload);

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
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char const* argv[]) {
    const char* target_host = (argc > 1) ? argv[1] : "controller";
    const std::string agentId = make_agent_id();
    sv::LoggerFactory::instance().init(sv::LogLevel::INFO);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket failed");
        return 1;
    }
    sv::set_nonblocking(sock);

    struct hostent* he = nullptr;
    while (true) {
        he = gethostbyname(target_host);
        if (he != nullptr) {
            break;
        }
        LOG_WARN("Agent", "DNS lookup failed",
                 ("{\"target\":\"" + std::string(target_host) + "\"}").c_str());
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9090);
    std::memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
            perror("connect failed");
            return 1;
        }
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1 failed");
        return 1;
    }

    struct epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLOUT | EPOLLET;
    ev.data.fd = sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &ev) < 0) {
        perror("epoll_ctl failed");
        return 1;
    }

    sv::TcpProtocol protocol;
    std::vector<uint8_t> recvBuffer;
    uint32_t seqCounter = 1;
    bool connected = false;
    bool helloSent = false;

    auto lastHeartbeat = std::chrono::steady_clock::now();
    auto lastState     = std::chrono::steady_clock::now();

    const auto send_hello = [&]() {
        std::string payload =
            "{\"agent_id\":\"" + agentId + "\",\"group\":\"default\"}";
        return send_frame(sock, protocol, sv::MessageType::HELLO, seqCounter++,
                          payload);
    };

    const auto send_heartbeat = [&]() {
        std::string payload =
            "{\"agent_id\":\"" + agentId + "\",\"ts\":" +
            std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count()) +
            "}";
        return send_frame(sock, protocol, sv::MessageType::HEARTBEAT,
                          seqCounter++, payload);
    };

    const auto send_state = [&]() {
        double cpu = 20.0 + (seqCounter % 5) * 5.0;
        double temp = 35.0 + (seqCounter % 3) * 2.0;
        double load = 0.5 + (seqCounter % 4) * 0.1;
        std::string payload =
            "{\"agent_id\":\"" + agentId + "\",\"mode\":\"Active\","
            "\"cpu_pct\":" + std::to_string(cpu) + ","
            "\"temperature\":" + std::to_string(temp) + ","
            "\"load_avg\":" + std::to_string(load) + "}";
        return send_frame(sock, protocol, sv::MessageType::STATE, seqCounter++,
                          payload);
    };

    LOG_INFO("Agent", "Connecting",
             ("{\"target\":\"" + std::string(target_host) +
              "\",\"agent_id\":\"" + agentId + "\"}")
                 .c_str());

    struct epoll_event events[10];
    while (true) {
        int nfds = epoll_wait(epoll_fd, events, 10, 100);
        for (int i = 0; i < nfds; ++i) {
            if ((events[i].events & EPOLLOUT) && !connected) {
                int error = 0;
                socklen_t len = sizeof(error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);
                if (error == 0) {
                    LOG_INFO("Agent", "Connected", ("{\"agent_id\":\"" + agentId + "\"}").c_str());
                    connected = true;
                    ev.events = EPOLLIN | EPOLLET;
                    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sock, &ev);
                } else {
                    LOG_ERROR("Agent", "Connect error", ("{\"error\":\"" + std::string(strerror(error)) + "\"}").c_str());
                    return 1;
                }
            }

            if (events[i].events & EPOLLIN) {
                uint8_t buf[1024];
                bool alive = true;
                while (true) {
                    ssize_t bytes = recv(sock, buf, sizeof(buf), 0);
                    if (bytes > 0) {
                        recvBuffer.insert(recvBuffer.end(), buf, buf + bytes);
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
                    LOG_WARN("Agent", "Controller disconnected", ("{\"agent_id\":\"" + agentId + "\"}").c_str());
                    close(sock);
                    return 0;
                }

                size_t offset = 0;
                while (offset < recvBuffer.size()) {
                    size_t consumed = 0;
                    std::unique_ptr<sv::Frame> frame =
                        protocol.decode(recvBuffer.data() + offset,
                                        recvBuffer.size() - offset, consumed);
                    if (!frame) {
                        if (consumed == 0) {
                            break;
                        }
                        offset += consumed;
                        continue;
                    }
                    offset += consumed;

                    std::string payload(frame->payload.begin(),
                                        frame->payload.end());
                    std::string fields = "{\"type\":" +
                                         std::to_string(static_cast<int>(frame->type)) +
                                         ",\"seq\":" + std::to_string(frame->seq) +
                                         ",\"payload\":\"" + payload + "\"}";
                    LOG_INFO("Agent", "Frame received", fields.c_str());
                }

                if (offset > 0 && offset <= recvBuffer.size()) {
                    recvBuffer.erase(
                        recvBuffer.begin(),
                        recvBuffer.begin() + static_cast<long>(offset));
                }
            }
        }

        if (connected) {
            if (!helloSent) {
                helloSent = send_hello();
            }

            auto now = std::chrono::steady_clock::now();
            if (now - lastHeartbeat >= std::chrono::seconds(1)) {
                send_heartbeat();
                lastHeartbeat = now;
            }
            if (now - lastState >= std::chrono::seconds(3)) {
                send_state();
                lastState = now;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return 0;
}
