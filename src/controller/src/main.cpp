#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <cstring>
#include "socket_utils.h"

#define MAX_EVENTS 100
#define PORT 9090

int main() {
    // 1. 소켓 생성 및 에러 체크
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

    if (!sv::core::set_nonblocking(server_fd)) {
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

    std::cout << "Controller (Stable Bare minimum) started on port " << PORT << std::endl;

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

                sv::core::set_nonblocking(client_fd);
                struct epoll_event client_ev = {EPOLLIN | EPOLLET, {.fd = client_fd}};
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) < 0) {
                    perror("epoll_ctl client failed");
                    close(client_fd);
                    continue;
                }
                std::cout << "New agent connected (fd: " << client_fd << ")" << std::endl;
            } else {
                char buffer[1024] = {0};
                int fd = events[i].data.fd;
                ssize_t bytes = recv(fd, buffer, sizeof(buffer), 0);
                if (bytes > 0) {
                    std::cout << "Received from fd " << fd << ": " << buffer << std::endl;
                    send(fd, "tcp connection ok", 17, 0);
                } else {
                    std::cout << "Agent disconnected (fd: " << fd << ")" << std::endl;
                    close(fd);
                }
            }
        }
    }
    return 0;
}
