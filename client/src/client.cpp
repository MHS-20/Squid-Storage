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

void Client::handleRequest(Message mex)
{
    try
    {
        std::cout << "[CLIENT]: Received message: " + mex.keyword << std::endl;
    }
    catch (std::exception &e)
    {
        std::cerr << "[CLIENT]: Error receiving message: " << e.what() << std::endl;
    }
    squidProtocol.responseDispatcher(mex);
}

void Client::run()
{
    this->connectToServer();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    Message mex = squidProtocol.receiveAndParseMessage();
    std::cout << "[CLIENT]: Identify  request received from server: " + mex.keyword << std::endl;

    squidProtocol.response(std::string("client"), std::string("CLIENT"));
    mex = squidProtocol.receiveAndParseMessage();

    if (mex.args["ACK"] == "ACK")
        std::cout << "[CLIENT]: ACK received" << std::endl;

    handleRequest(squidProtocol.createFile("./test_txt/clientfile.txt"));
    handleRequest(squidProtocol.updateFile("./test_txt/clientfile.txt"));
    handleRequest(squidProtocol.acquireLock("./test_txt/clientfile.txt"));
    handleRequest(squidProtocol.releaseLock("./test_txt/clientfile.txt"));
    handleRequest(squidProtocol.heartbeat());
    handleRequest(squidProtocol.deleteFile("./test_txt/clientfile.txt"));
    handleRequest(squidProtocol.readFile("./test_txt/clientfile.txt"));
    // handleRequest(squidProtocol.syncStatus());


    // if (squidProtocol.createFile("./test_txt/clientfile.txt") != "ACK")
    //     perror("[CLIENT]: Error while creating file on server");
    // else
    //     std::cout << "[CLIENT]: Created file successfully on server" << std::endl;

    // if (squidProtocol.acquireLock("./test_txt/clientfile.txt") == "true")
    //     perror("[CLIENT]: Lock acquisition failed");
    // else
    //     std::cout << "[CLIENT]: Lock acquired successfully on server" << std::endl;

    // if (squidProtocol.releaseLock("./test_txt/clientfile.txt") != "ACK")
    //     perror("[CLIENT]: Lock release failed");
    // else
    //     std::cout << "[CLIENT]: Lock released successfully on server" << std::endl;

    // if (squidProtocol.updateFile("./test_txt/clientfile.txt") != "ACK")
    //     perror("[CLIENT]: Error while updating file on server");
    // else
    //     std::cout << "[CLIENT]: Updated file successfully on server" << std::endl;

    // if (squidProtocol.readFile("./test_txt/clientfile.txt") != "ACK")
    //     perror("[CLIENT]: Error while reading file on server");
    // else
    //     std::cout << "[CLIENT]: Read file successfully from server" << std::endl;

    // if (squidProtocol.heartbeat() != "ACK")
    //     perror("[CLIENT]: Error missing hearbeat from server");
    // else
    //     std::cout << "[CLIENT]: Hearbeat received from server" << std::endl;

    // if (squidProtocol.syncStatus() != "ACK")
    //     perror("[CLIENT]: Error while synchronizing status with server");
    // else
    //     std::cout << "[CLIENT]: Synchronization with server successful" << std::endl;

    // if (squidProtocol.deleteFile("./test_txt/clientfile.txt") != "ACK")
    //     perror("[CLIENT]: Error while deleting file on server");
    // else
    //     std::cout << "[CLIENT]: Deleted file successfully on server" << std::endl;
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
