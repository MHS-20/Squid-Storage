#include "datanode.hpp"

DataNode::DataNode() : DataNode(SERVER_IP, SERVER_PORT) {}

DataNode::DataNode(int port) : Peer(SERVER_IP, port, "DATANODE", "DATANODE") {}
DataNode::DataNode(const char *server_ip, int port) : Peer(server_ip, port, "DATANODE", "DATANODE") {}
DataNode::DataNode(std::string nodeType, std::string processName) : Peer(nodeType, processName) {}
DataNode::DataNode(int port, std::string nodeType, std::string processName) : Peer(port, nodeType, processName) {}
DataNode::DataNode(const char *server_ip, int port, std::string nodeType, std::string processName) : Peer(server_ip, port, nodeType, processName) {}

// passive daemon
void DataNode::run()
{
    this->connectToServer();

    while (true)
    {
        if (!squidProtocol.isAlive())
        {
            std::cout << "[DATANODE]: Connection closed. Retrying..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));
            this->connectToServer();
            continue;
        }

        std::cout << "[DATANODE]: Waiting for messages..." << std::endl;

        Message mex;
        try
        {
            mex = squidProtocol.receiveAndParse();
            std::cout << "[DATANODE]: Received message: " << mex.toString() << std::endl;
        }
        catch (const std::exception &e)
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

    // Expect an IDENTIFY request from the server
    Message mex = squidProtocol.receiveAndParse();
    std::cout << "[DATANODE]: Identify request received from server: " << opcodeToString(mex.opcode) << std::endl;

    // Respond with our identity
    squidProtocol.response(std::string("DATANODE2"), std::string("DATANODE2"));

    // Expect ACK
    mex = squidProtocol.receiveAndParse();
    if (mex.isAck())
        std::cout << "[DATANODE]: ACK received" << std::endl;
    else
        std::cerr << "[DATANODE]: Expected ACK, got: " << mex.toString() << std::endl;

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
