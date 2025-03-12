#include "server.hpp"


Server::Server() : Server(DEFAULT_PORT) {}

Server::Server(int port) {
    this->port = port;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons(port);
}

Server::~Server() {
    close(server_fd);
}

void Server::start() {
    std::cout << "Server Starting..." << std::endl;
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return;
    }

    listen(server_fd, 3);
    std::cout << "Server listening on " << this->port << "...\n";

    while (true) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket < 0) {
            perror("Accept failed");
            exit(EXIT_FAILURE);
        }

        handleClient(new_socket);
        close(new_socket);
    }
}

void Server::handleClient(int client_socket) {
    char buffer[BUFFER_SIZE] = {0};

    read(client_socket, buffer, sizeof(buffer));
    std::cout << "Received: " << buffer << std::endl;

    const char* message = "Hello from server";
    send(client_socket, message, strlen(message), 0);
    std::cout << "Message sent\n";
}