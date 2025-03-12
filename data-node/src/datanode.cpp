#include "datanode.hpp"
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int socket_fd = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};

    std::cout << "Client starting..." << std::endl;

    // 1. Create
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("socket creation failed");
        return 1;
    }
    
     // 2. Server Address Configuration
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_port = htons(SERVER_PORT);
     if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
         perror("invalid server address");
         return 1;
     }
 
     // 3. Connect
     if (connect(socket_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
         perror("connection to server failed");
         return 1;
     }
 
     // 4. Send
     const char* msg = "Hello from client";
     send(socket_fd, msg, strlen(msg), 0);
     std::cout << "Message sent\n";
 
     // 5. Receive
     read(socket_fd, buffer, 1024);
     std::cout << "Server reply: " << buffer << std::endl;
 
     close(socket_fd);
     return 0;
}