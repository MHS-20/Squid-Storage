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
    handleRequest(squidProtocol.updateFile("./test_txt/clientfile.txt"));
    handleRequest(squidProtocol.acquireLock("./test_txt/clientfile.txt"));
    handleRequest(squidProtocol.releaseLock("./test_txt/clientfile.txt"));
    handleRequest(squidProtocol.heartbeat());
    handleRequest(squidProtocol.readFile("./test_txt/clientfile.txt"));
    handleRequest(squidProtocol.deleteFile("./test_txt/clientfile.txt"));
    handleRequest(squidProtocol.syncStatus());
    handleRequest(squidProtocol.closeConn());
}
