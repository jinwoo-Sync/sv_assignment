#include <iostream>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <errno.h>
#include "socket_utils.h"

int main(int argc, char const *argv[]) {
    const char* target_host = (argc > 1) ? argv[1] : "controller";
    
    // 1. 소켓 생성 및 에러 체크
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket failed");
        return 1;
    }
    sv::core::set_nonblocking(sock);

    // 2. DNS 확인 및 재시도
    struct hostent *he = nullptr;
    while (true) {
        he = gethostbyname(target_host);
        if (he != nullptr) break;
        
        std::cerr << "DNS lookup failed for " << target_host << ". Retrying in 1s..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    struct sockaddr_in addr = {AF_INET, htons(9090)};
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    // 3. 비동기 연결 시도
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
            perror("connect failed");
            return 1;
        }
    }

    // 4. epoll 생성 및 초기 등록 (연결 완료 감시를 위해 EPOLLOUT)
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1 failed");
        return 1;
    }

    struct epoll_event ev = {EPOLLOUT | EPOLLET, {.fd = sock}}, events[10];
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &ev) < 0) {
        perror("epoll_ctl failed");
        return 1;
    }

    bool connected = false;
    std::cout << "Agent (Stable) targeting " << target_host << "..." << std::endl;

    while (true) {
        int nfds = epoll_wait(epoll_fd, events, 10, 100);
        for (int i = 0; i < nfds; ++i) {
            // 연결 완료 이벤트 확인 (EPOLLOUT)
            if (events[i].events & EPOLLOUT && !connected) {
                int error = 0;
                socklen_t len = sizeof(error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);
                if (error == 0) {
                    std::cout << "Connected to controller!" << std::endl;
                    connected = true;
                    // 연결 성공 시 수신 대기(EPOLLIN)로 상태 전환
                    ev.events = EPOLLIN | EPOLLET;
                    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sock, &ev);
                } else {
                    std::cerr << "Connect error: " << strerror(error) << std::endl;
                    return 1;
                }
            }

            // 데이터 수신 이벤트 (EPOLLIN)
            if (events[i].events & EPOLLIN) {
                char buf[1024] = {0};
                ssize_t bytes = recv(sock, buf, sizeof(buf), 0);
                if (bytes > 0) {
                    std::cout << "Server: " << buf << std::endl;
                } else if (bytes == 0) {
                    std::cout << "Server disconnected." << std::endl;
                    return 0;
                }
            }
        }

        // 연결된 경우에만 주기적 전송
        if (connected) {
            send(sock, "hello from agent", 16, 0);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    return 0;
}
