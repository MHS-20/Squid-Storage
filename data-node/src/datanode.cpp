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
}

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

/* ---- FILE TRANSFER API ----- */
void DataNode::sendFile(const char* filepath) {
    std::cout << "[DATANODE]: Attempting to open file: " << filepath << std::endl;
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        perror("[DATANODE]: Error opening file");
        return;
    }

    std::streamsize filesize = file.tellg();
    file.seekg(0, std::ios::beg);

    send(socket_fd, &filesize, sizeof(filesize), 0);

    char buffer[BUFFER_SIZE];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        send(socket_fd, buffer, file.gcount(), 0);
    }

    std::cout << "[DATANODE]: File sent to server\n";
    file.close();
}

void DataNode::retriveFile(const char* outputpath) {
    std::ofstream outfile(outputpath, std::ios::binary);
    if (!outfile) {
        perror("[DATANODE]: Error creating file");
        return;
    }

    std::streamsize filesize;
    read(socket_fd, &filesize, sizeof(filesize));

    char buffer[BUFFER_SIZE];
    while (filesize > 0) {
        int bytes_to_read = (filesize > BUFFER_SIZE) ? BUFFER_SIZE : filesize;
        int received = read(socket_fd, buffer, bytes_to_read);
        if (received <= 0) break;

        outfile.write(buffer, received);
        filesize -= received;
    }

    std::cout << "[DATANODE]: Retrive file from server\n";
    outfile.close();
}