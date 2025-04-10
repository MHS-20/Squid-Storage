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
    this->squidProtocol = SquidProtocol(socket_fd, "[CLIENT]", "CLIENT");
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

    squidProtocol.response(std::string("CLIENT"), std::string("CLIENT"));
    mex = squidProtocol.receiveAndParseMessage();

    if (mex.args["ACK"] == "ACK")
        std::cout << "[CLIENT]: ACK received" << std::endl;

    handleRequest(squidProtocol.createFile("./test_txt/clientfile.txt"));
    //handleRequest(squidProtocol.updateFile("./test_txt/clientfile.txt"));
    handleRequest(squidProtocol.acquireLock("./test_txt/clientfile.txt"));
    handleRequest(squidProtocol.releaseLock("./test_txt/clientfile.txt"));
    //handleRequest(squidProtocol.heartbeat());
    //handleRequest(squidProtocol.readFile("./test_txt/clientfile.txt"));
    //handleRequest(squidProtocol.deleteFile("./test_txt/clientfile.txt"));
    //handleRequest(squidProtocol.syncStatus());
    handleRequest(squidProtocol.closeConn());
}
