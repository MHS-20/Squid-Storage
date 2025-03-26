#include "datanode.hpp"

DataNode::DataNode() : DataNode(SERVER_IP, SERVER_PORT) {}

DataNode::DataNode(const char *server_ip, int port)
{
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        perror("[DATANODE]: Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0)
    {
        perror("[DATANODE]: Invalid address");
        exit(EXIT_FAILURE);
    }

    this->fileTransfer = FileTransfer();
    this->squidProtocol = SquidProtocol(socket_fd, "datanode", "DATANODE");
}

DataNode::~DataNode()
{
    close(socket_fd);
}

void DataNode::connectToServer()
{
    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("[DATANODE]: connection to server failed");
        exit(EXIT_FAILURE);
    }
    std::cout << "[DATANODE]: Connected to server...\n";
    //sendName(socket_fd);
}

void DataNode::sendName(int socket_fd)
{
    const char *name = "DATANODE";
    send(socket_fd, name, strlen(name), 0);
    std::cout << "[DATANODE]: Name sent: " << name << std::endl;
}

// /* ---- MESSAGE API ----- */
void DataNode::sendMessage(const char *message)
{
    send(socket_fd, message, strlen(message), 0);
    std::cout << "[DATANODE]: Message sent: " << message << std::endl;
}

void DataNode::receiveMessage()
{
    read(socket_fd, buffer, sizeof(buffer));
    std::cout << "[DATANODE]: Server Reply: " << buffer << std::endl;
}

// /* ---- FILE TRANSFER API ----- */
void DataNode::sendFile(const char *filepath)
{
    this->fileTransfer.sendFile(this->socket_fd, "[DATANODE]", filepath);
}

void DataNode::retriveFile(const char *outputpath)
{
    this->fileTransfer.receiveFile(this->socket_fd, "[DATANODE]", outputpath);
}