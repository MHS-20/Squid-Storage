#include "client.hpp"

Client::Client() : Client(SERVER_IP, SERVER_PORT) {}

Client::Client(const char *server_ip, int port)
{
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        perror("[CLIENT]: Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0)
    {
        perror("[CLIENT]: Invalid address");
        exit(EXIT_FAILURE);
    }
}

Client::~Client()
{
    close(socket_fd);
}

void Client::connectToServer()
{
    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("[CLIENT]: connection to server failed");
        exit(EXIT_FAILURE);
    }
    std::cout << "[CLIENT]: Connected to server...\n";
}

void Client::sendMessage(const char *message)
{
    send(socket_fd, message, strlen(message), 0);
    std::cout << "[CLIENT]: Message sent: " << message << std::endl;
}

void Client::receiveMessage()
{
    read(socket_fd, buffer, sizeof(buffer));
    std::cout << "[CLIENT]: Server Reply: " << buffer << std::endl;
}
