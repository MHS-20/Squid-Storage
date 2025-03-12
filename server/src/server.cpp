#include "server.hpp"

Server::Server() : Server(DEFAULT_PORT) {}

Server::Server(int port)
{
    this->port = port;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0)
    {
        perror("[SERVER]: Socket failed");
        exit(EXIT_FAILURE);
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    this->fileTransfer = FileTransfer();
}

Server::~Server()
{
    close(server_fd);
}

void Server::start()
{
    std::cout << "[SERVER]: Server Starting..." << std::endl;
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        return;
    }

    listen(server_fd, 3);
    std::cout << "[SERVER]: Server listening on " << this->port << "...\n";

    while (true)
    {
        new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket < 0)
        {
            perror("[SERVER]: Accept failed");
            exit(EXIT_FAILURE);
        }

        handleClient(new_socket);
        close(new_socket);
    }
}

void Server::handleClient(int client_socket) {
    receiveFile(client_socket, "received_from_client.txt");
    sendFile(client_socket, "received_from_client.txt");
}

void Server::handleClientMessages(int client_socket)
{
    char buffer[BUFFER_SIZE] = {0};

    read(client_socket, buffer, sizeof(buffer));
    std::cout << "[SERVER]: Received: " << buffer << std::endl;

    const char *message = "Hello from server";
    send(client_socket, message, strlen(message), 0);
    std::cout << "[SERVER]: Reply sent\n";
}

/* ---- FILE TRANSFER API ----- */
void Server::sendFile(int client_socket, const char *filepath){
    this->fileTransfer.sendFile(client_socket, "[SERVER]", filepath);
}

void Server::receiveFile(int client_socket, const char *outputpath){
    this->fileTransfer.receiveFile(client_socket, "[SERVER]", outputpath);
}