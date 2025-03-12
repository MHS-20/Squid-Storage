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
void Server::sendFile(int client_socket, const char *filepath)
{
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file)
    {
        perror("[SERVER]: Error opening file");
        return;
    }

    std::streamsize filesize = file.tellg();
    file.seekg(0, std::ios::beg);

    send(client_socket, &filesize, sizeof(filesize), 0);

    char buffer[BUFFER_SIZE];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
    {
        send(client_socket, buffer, file.gcount(), 0);
    }

    std::cout << "[SERVER]: File sent to client.\n";
    file.close();
}


void Server::receiveFile(int client_socket, const char *outputpath)
{
    std::ofstream outfile(outputpath, std::ios::binary);
    if (!outfile)
    {
        perror("[SERVER]: Error creating file");
        return;
    }

    std::streamsize filesize;
    read(client_socket, &filesize, sizeof(filesize));

    char buffer[BUFFER_SIZE];
    while (filesize > 0)
    {
        int bytes_to_read = (filesize > BUFFER_SIZE) ? BUFFER_SIZE : filesize;
        int received = read(client_socket, buffer, bytes_to_read);
        if (received <= 0)
            break;

        outfile.write(buffer, received);
        filesize -= received;
    }

    std::cout << "[SERVER]: File received by server.\n";
    outfile.close();
}

