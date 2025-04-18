#include "client.hpp"
using namespace std;

Client::Client() : Peer() {}
Client::Client(const char *server_ip, int port) : Peer(server_ip, port, "CLIENT", "CLIENT") {}
Client::Client(string nodeType, string processName) : Peer(nodeType, processName) {}
Client::Client(const char *server_ip, int port, string nodeType, string processName) : Peer(server_ip, port, nodeType, processName) {}

// for testing
void Client::run()
{
    Message mex;
    this->connectToServer();
    this_thread::sleep_for(chrono::seconds(1));

    while (true)
    {
        cout << "[CLIENT]: Waiting for messages..." << endl;
        if (squidProtocol.getSocket() < 0) // connection closed
        {
            cout << "[CLIENT]: Closing & Terminating" << endl;
            break;
        }

        try
        {
            mex = squidProtocol.receiveAndParseMessage();
            cout << "[CLIENT]: Received message: " + mex.keyword << endl;
        }
        catch (exception &e)
        {
            cerr << "[CLIENT]: Error receiving message: " << e.what() << endl;
            break;
        }
        squidProtocol.requestDispatcher(mex);
    }
}

void Client::initiateConnection()
{
    this->connectToServer();
    this_thread::sleep_for(chrono::seconds(1));

    // indetifying to server
    Message mex = squidProtocol.receiveAndParseMessage();
    cout << "[CLIENT]: Identify request received from server: " + mex.keyword << endl;
    squidProtocol.response(string("CLIENT"), string("CLIENT"));
    mex = squidProtocol.receiveAndParseMessage();
    if (mex.args["ACK"] == "ACK")
        cout << "[CLIENT]: ACK received" << endl;
    else
        cerr << "[CLIENT]: Error: ACK not received" << endl;

    // accepting connection from server
    // listen on port
    int secondary_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in secondary_addr{};
    secondary_addr.sin_family = AF_INET;
    secondary_addr.sin_port = htons(CLIENT_PORT);
    secondary_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(secondary_fd, (struct sockaddr *)&secondary_addr, sizeof(secondary_addr)) < 0)
    {
        cerr << "[CLIENT]: Bind failed" << endl;
        return;
    }
    if (listen(secondary_fd, 3) < 0)
    {
        cerr << "[CLIENT]: Listen failed" << endl;
        return;
    }
    cout << "[CLIENT]: Listening on port: " << CLIENT_PORT << endl;

    mex = squidProtocol.receiveAndParseMessage();
    cout << mex.keyword << endl;
    squidProtocol.response(CLIENT_PORT);

    // accept connection
    socklen_t addrlen = sizeof(server_addr);
    int new_socket = accept(secondary_fd, (struct sockaddr *)&server_addr, (socklen_t *)&addrlen);
    if (new_socket < 0)
    {
        cerr << "[CLIENT]: Accept failed" << endl;
        return;
    }
    cout << "[CLIENT]: Accepted connection from server" << endl;
    secondarySquidProtocol = SquidProtocol(new_socket, "CLIENT_SECONDARY", "CLIENT_SECONDARY");
}

void Client::createFile(string filePath)
{
    handleRequest(squidProtocol.createFile(filePath));
}

void Client::deleteFile(string filePath)
{
    handleRequest(squidProtocol.deleteFile(filePath));
}

void Client::updateFile(string filePath)
{
    handleRequest(squidProtocol.updateFile(filePath));
}

void Client::syncStatus()
{
    handleRequest(squidProtocol.syncStatus());
}

bool Client::acquireLock(string filePath)
{
    return squidProtocol.acquireLock(filePath).args["isLocked"] == "1";
}

void Client::releaseLock(string filePath)
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
