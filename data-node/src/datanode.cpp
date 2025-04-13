#include "datanode.hpp"

DataNode::DataNode() : DataNode(SERVER_IP, SERVER_PORT) {}

DataNode::DataNode(const char *server_ip, int port) : Peer(server_ip, port, "DATANODE", "DATANODE") {}
DataNode::DataNode(std::string nodeType, std::string processName) : Peer(nodeType, processName) {}
DataNode::DataNode(const char *server_ip, int port, std::string nodeType, std::string processName) : Peer(server_ip, port, nodeType, processName) {}

// void DataNode::handleRequest(Message mex)
// {
//     try
//     {
//         std::cout << "[DATANODE]: Received message: " + mex.keyword << std::endl;
//     }
//     catch (std::exception &e)
//     {
//         std::cerr << "[DATANODE]: Error receiving message: " << e.what() << std::endl;
//     }
//     squidProtocol.responseDispatcher(mex);
// }

void DataNode::run()
{
    Message mex;
    this->connectToServer();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    while (true)
    {
        std::cout << "[DATANODE]: Waiting for messages..." << std::endl;
        if (squidProtocol.getSocket() < 0) // connection closed
        {
            std::cout << "[DATANODE]: Closing & Terminating" << std::endl;
            break;
        }

        try
        {
            mex = squidProtocol.receiveAndParseMessage();
            std::cout << "[DATANODE]: Received message: " + mex.keyword << std::endl;
        }
        catch (std::exception &e)
        {
            std::cerr << "[DATANODE]: Error receiving message: " << e.what() << std::endl;
            break;
        }

        squidProtocol.requestDispatcher(mex);
    }
}

void DataNode::testing()
{
    this->connectToServer();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    Message mex = squidProtocol.receiveAndParseMessage();
    std::cout << "[DATANODE]: Identify  request received from server: " + mex.keyword << std::endl;

    squidProtocol.response(std::string("DATANODE"), std::string("DATANODE"));
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