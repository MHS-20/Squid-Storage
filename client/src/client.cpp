#include "client.hpp"

Client::Client() : Peer() {}

Client::Client(const char *server_ip, int port) : Peer(server_ip, port, "CLIENT", "CLIENT") {}
Client::Client(std::string nodeType, std::string processName) : Peer(nodeType, processName) {}
Client::Client(const char *server_ip, int port, std::string nodeType, std::string processName) : Peer(server_ip, port, nodeType, processName) {}

// void Client::handleRequest(Message mex)
// {
//     try
//     {
//         std::cout << "[CLIENT]: Received message: " + mex.keyword << std::endl;
//     }
//     catch (std::exception &e)
//     {
//         std::cerr << "[CLIENT]: Error receiving message: " << e.what() << std::endl;
//     }
//     squidProtocol.responseDispatcher(mex);
// }

// passive deamon (temp)
void Client::run()
{
    Message mex;
    this->connectToServer();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    while (true)
    {
        std::cout << "[CLIENT]: Waiting for messages..." << std::endl;
        if (squidProtocol.getSocket() < 0) // connection closed
        {
            std::cout << "[CLIENT]: Closing & Terminating" << std::endl;
            break;
        }

        try
        {
            mex = squidProtocol.receiveAndParseMessage();
            std::cout << "[CLIENT]: Received message: " + mex.keyword << std::endl;
        }
        catch (std::exception &e)
        {
            std::cerr << "[CLIENT]: Error receiving message: " << e.what() << std::endl;
            break;
        }
        squidProtocol.requestDispatcher(mex);
    }
}

void Client::initiateConnection()
{
    this->connectToServer();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    Message mex = squidProtocol.receiveAndParseMessage();
    std::cout << "[CLIENT]: Identify request received from server: " + mex.keyword << std::endl;
    squidProtocol.response(std::string("CLIENT"), std::string("CLIENT"));
    mex = squidProtocol.receiveAndParseMessage();
    if (mex.args["ACK"] == "ACK")
        std::cout << "[CLIENT]: ACK received" << std::endl;
    else
        std::cerr << "[CLIENT]: Error: ACK not received" << std::endl;
}

void Client::createFile(std::string filePath)
{
    handleRequest(squidProtocol.createFile(filePath));
}

void Client::deleteFile(std::string filePath)
{
    handleRequest(squidProtocol.deleteFile(filePath));
}

void Client::updateFile(std::string filePath)
{
    handleRequest(squidProtocol.updateFile(filePath));
}

void Client::syncStatus()
{
    handleRequest(squidProtocol.syncStatus());
}

bool Client::acquireLock(std::string filePath)
{
    return squidProtocol.acquireLock(filePath).args["isLocked"] == "1";
}

void Client::releaseLock(std::string filePath)
{
    handleRequest(squidProtocol.releaseLock(filePath));
}

void Client::testing()
{
    this->initiateConnection();

    handleRequest(squidProtocol.createFile("./test_txt/test_client/clientfile.txt"));
    handleRequest(squidProtocol.updateFile("./test_txt/test_client/clientfile.txt"));
    handleRequest(squidProtocol.acquireLock("./test_txt/test_client/clientfile.txt"));
    handleRequest(squidProtocol.releaseLock("./test_txt/test_client/clientfile.txt"));
    handleRequest(squidProtocol.heartbeat());
    handleRequest(squidProtocol.syncStatus());
    handleRequest(squidProtocol.readFile("./test_txt/test_client/clientfile.txt"));
    handleRequest(squidProtocol.deleteFile("./test_txt/test_client/clientfile.txt"));
    handleRequest(squidProtocol.closeConn());
}
