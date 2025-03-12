#include "idatanode.hpp"
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

class DataNode: public IDataNode{
    private: 
        int socket_fd = 0;
        struct sockaddr_in server_addr;
        char buffer[BUFFER_SIZE] = {0};

    public: 
        DataNode() : DataNode(SERVER_IP, SERVER_PORT) {}

        DataNode(const char* server_ip, int port) {
            socket_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (socket_fd < 0) {
                perror("Socket creation failed");
                exit(EXIT_FAILURE);
            }
        
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(port);

            if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
                perror("Invalid address");
                exit(EXIT_FAILURE);
            }
        }

        ~DataNode() {
            close(socket_fd);
        }

        void connectToServer() {
            if (connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                perror("connection to server failed");
                exit(EXIT_FAILURE);
            }
            std::cout << "Connected to server...\n";
        }
        
        void sendMessage(const char* message) {
            send(socket_fd, message, strlen(message), 0);
            std::cout << "Message sent: " << message << std::endl;
        }
        
        void receiveMessage() {
            read(socket_fd, buffer, sizeof(buffer));
            std::cout << "Server Reply: " << buffer << std::endl;
        }

}; 