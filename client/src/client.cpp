#include "client.hpp"
#include <thread>

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

    this->fileTransfer = FileTransfer();
    this->squidProtocol = SquidProtocol(socket_fd, "client", "CLIENT");
}

Client::~Client()
{
    close(socket_fd);
}

int Client::getSocket()
{
    return socket_fd;
}

void Client::connectToServer()
{
    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("[CLIENT]: connection to server failed");
        exit(EXIT_FAILURE);
    }
    std::cout << "[CLIENT]: Connected to server...\n";
    // sendName(socket_fd);
}

void Client::sendName(int socket_fd)
{
    const char *name = "CLIENT";
    send(socket_fd, name, strlen(name), 0);
    std::cout << "[CLIENT]: Name sent: " << name << std::endl;
}

void Client::run()
{
    this->connectToServer();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    Message mex = squidProtocol.receiveAndParseMessage();
    std::cout << "[CLIENT]: Identify  request received from server: " + mex.keyword << std::endl;

    squidProtocol.response(std::string("client"), std::string("CLIENT"));
    mex = squidProtocol.receiveAndParseMessage();

    if(mex.args["ACK"] == "ACK")
        std::cout<< "[CLIENT]: ACK received" <<std::endl;

    if (squidProtocol.createFile("./test_txt/clientfile.txt") != "ACK")
        perror("Error while creating file on server");
    else
        std::cout << "Created file successfully on server" << std::endl;

    // squidProtocol.readFile("./test_txt/test.txt");
    // squidProtocol.deleteFile("./test_txt/test.txt");
    // squidProtocol.close();
}

/* ---- MESSAGE API ----- */
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

/* ---- FILE TRANSFER API ----- */

void Client::sendFile(const char *filepath)
{
    this->fileTransfer.sendFile(this->socket_fd, "[CLIENT]", filepath);
}

void Client::retriveFile(const char *outputpath)
{
    this->fileTransfer.receiveFile(this->socket_fd, "[CLIENT]", outputpath);
}
