#include "server.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

    std::cout << "Server Starting..." << std::endl;

    // 1. Create Socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        return 1;
    }

    // 2. Socket Options
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    // 3. Address Configuration 
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // 4. Bind
     if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return 1;
    }

    // 5. Listen
    listen(server_fd, 3);
    std::cout << "Server listening on " << PORT << "...\n";

    // 6. Accept
    new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
    if (new_socket < 0) {
        perror("accept failed");
        return 1;
    }

    // 7. Receive
    read(new_socket, buffer, BUFFER_SIZE);
    std::cout << "Received: " << buffer << std::endl;

    // 8. Reply
    const char* msg = "Hello from server";
    send(new_socket, msg, strlen(msg), 0);
    std::cout << "Message sent\n";

    close(new_socket);
    close(server_fd);
    return 0;
}