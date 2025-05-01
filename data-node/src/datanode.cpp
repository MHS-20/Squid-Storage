#include "datanode.hpp"

DataNode::DataNode() : DataNode(SERVER_IP, SERVER_PORT) {}

DataNode::DataNode(int port) : Peer(SERVER_IP, port, "DATANODE", "DATANODE") {}
DataNode::DataNode(const char *server_ip, int port) : Peer(server_ip, port, "DATANODE", "DATANODE") {}
DataNode::DataNode(std::string nodeType, std::string processName) : Peer(nodeType, processName) {}
DataNode::DataNode(int port, std::string nodeType, std::string processName) : Peer(port, nodeType, processName) {}
DataNode::DataNode(const char *server_ip, int port, std::string nodeType, std::string processName) : Peer(server_ip, port, nodeType, processName) {}

// passive deamon
void DataNode::run()
{
    Message mex;
    this->connectToServer();

    while (true)
    {
        if (!squidProtocol.isAlive()) // connection lost
        {
            std::cout << "[DATANODE]: Connection closed. Retrying..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));
            this->connectToServer();

            continue;
        }

        std::cout << "[DATANODE]: Waiting for messages..." << std::endl;

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

    squidProtocol.response(std::string("DATANODE2"), std::string("DATANODE2"));
    mex = squidProtocol.receiveAndParseMessage();

    if (mex.args["ACK"] == "ACK")
        std::cout << "[DATANODE]: ACK received" << std::endl;

    handleRequest(squidProtocol.createFile("./test_txt/test_datanode/datanodefile.txt"));
    std::this_thread::sleep_for(std::chrono::seconds(2));
    handleRequest(squidProtocol.updateFile("./test_txt/test_datanode/datanodefile.txt"));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    handleRequest(squidProtocol.acquireLock("./test_txt/test_datanode/datanodefile.txt"));
    handleRequest(squidProtocol.releaseLock("./test_txt/test_datanode/datanodefile.txt"));
    handleRequest(squidProtocol.heartbeat());
    handleRequest(squidProtocol.readFile("./test_txt/test_datanode/datanodefile.txt"));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    handleRequest(squidProtocol.syncStatus());
    std::this_thread::sleep_for(std::chrono::seconds(1));
    handleRequest(squidProtocol.deleteFile("./test_txt/test_datanode/datanodefile.txt"));
    handleRequest(squidProtocol.closeConn());
}