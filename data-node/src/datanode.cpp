#include "datanode.hpp"
#include <thread>

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

int DataNode::getSocket()
{
    return socket_fd;
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

void DataNode::handleRequest(Message mex)
{
    try
    {
        std::cout << "[DATANODE]: Received message: " + mex.keyword << std::endl;
    }
    catch (std::exception &e)
    {
        std::cerr << "[DATANODE]: Error receiving message: " << e.what() << std::endl;
    }
    squidProtocol.responseDispatcher(mex);
}

void DataNode::run()
{
    this->connectToServer();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    Message mex = squidProtocol.receiveAndParseMessage();
    std::cout << "[DATANODE]: Identify  request received from server: " + mex.keyword << std::endl;

    squidProtocol.response(std::string("datanode"), std::string("DATANODE"));
    mex = squidProtocol.receiveAndParseMessage();

    if (mex.args["ACK"] == "ACK")
        std::cout << "[DATANODE]: ACK received" << std::endl;

    handleRequest(squidProtocol.createFile("./test_txt/datanodefile.txt"));
    handleRequest(squidProtocol.updateFile("./test_txt/datanodefile.txt"));
    handleRequest(squidProtocol.acquireLock("./test_txt/datanodefile.txt"));
    handleRequest(squidProtocol.releaseLock("./test_txt/datanodefile.txt"));
    handleRequest(squidProtocol.heartbeat());
    handleRequest(squidProtocol.readFile("./test_txt/datanodefile.txt"));
    handleRequest(squidProtocol.deleteFile("./test_txt/datanodefile.txt"));
    handleRequest(squidProtocol.syncStatus());
    handleRequest(squidProtocol.closeConn());
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