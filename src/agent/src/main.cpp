#include <iostream>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <netdb.h>

int main(int argc, char const *argv[]) {
    struct sockaddr_in serv_addr;
    const char* hello = "hello from agent";
    char buffer[1024] = {0};
    const char* target_host = "controller";

    if (argc > 1) {
        target_host = argv[1];
    }

    int sock = 0;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(9090);

    std::cout << "Target host: " << target_host << std::endl;

    while (true) {
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            std::cout << "\n Socket creation error \n";
            return -1;
        }

        struct hostent *he;
        if ((he = gethostbyname(target_host)) == NULL) {
            std::cout << "gethostbyname failed for " << target_host << ". Retrying..." << std::endl;
            close(sock);
            sleep(1);
            continue;
        }
        memcpy(&serv_addr.sin_addr, he->h_addr_list[0], he->h_length);

        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            std::cout << "Connection Failed. Retrying in 1s..." << std::endl;
            close(sock);
            sleep(1);
            continue;
        }

        send(sock, hello, strlen(hello), 0);
        std::cout << "Hello message sent: " << hello << std::endl;

        int valread = read(sock, buffer, 1024);
        if (valread > 0) {
            std::cout << "Response from controller: " << buffer << std::endl;
        }

        close(sock);
        break; 
    }

    return 0;
}
