#include "client.hpp"
using namespace std;

Client::Client() : Peer() {}
Client::Client(const char *server_ip, int port) : Peer(server_ip, port, "CLIENT", "CLIENT") {}
Client::Client(string nodeType, string processName) : Peer(nodeType, processName) {}
Client::Client(const char *server_ip, int port, string nodeType, string processName) : Peer(server_ip, port, nodeType, processName) {}

void Client::run()
{
    Message mex;
    cout << "[CLIENT]: Secondary socket: thread started" << endl;
    while (true)
    {
        while (!secondarySquidProtocol.isAlive()) // connection lost
        {
            cout << "[CLIENT]: Secondary socket: Connection lost" << endl;
            this_thread::sleep_for(chrono::seconds(5));
            this->initiateConnection();
            if(secondarySquidProtocol.isAlive()) this->syncStatus();
        }
    
        cout << "[CLIENT]: Secondary socket: waiting for messages..." << endl;
        try
        {
            mex = secondarySquidProtocol.receiveAndParseMessage();
            cout << "[CLIENT]: Secondary socket: Received message: " + mex.keyword << endl;
            secondarySquidProtocol.requestDispatcher(mex);
        }
        catch (exception &e)
        {
            cerr << "[CLIENT]: Secondary socket: Error receiving message: " << e.what() << endl;
            break;
        }
    }
}

void Client::checkSecondarySocket()
{
    if (!secondarySquidProtocol.isAlive())
    {
        cout << "[CLIENT]: Lost connection, reconnecting..." << endl;
        return;
        // this->initiateConnection();
        // if(secondarySquidProtocol.isAlive()) this->syncStatus();
    }

    // select mask
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(secondarySquidProtocol.getSocket(), &readfds);

    // timeout of 0 seconds to make the call non-blocking
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    int activity = select(secondarySquidProtocol.getSocket() + 1, &readfds, NULL, NULL, &timeout);
    if (activity < 0)
    {
        perror("[CLIENT]: Select error");
        return;
    }

    // if data on second socket
    if (FD_ISSET(secondarySquidProtocol.getSocket(), &readfds))
    {
        try
        {
            Message mex = secondarySquidProtocol.receiveAndParseMessage();
            cout << "[CLIENT]: Received message on secondary socket: " + mex.keyword << endl;
            if (mex.keyword == RELEASE_LOCK)
            {
                cout << "[CLIENT]: Received request for FileLock release" << endl;
                FileManager::getInstance().getFileLock().setIsLocked(true);
            }
            else 
            {
                secondarySquidProtocol.requestDispatcher(mex);
            }
            
        }
        catch (exception &e)
        {
            cerr << "[CLIENT]: Error receiving message on secondary socket: " << e.what() << endl;
        }
    }
    else
    {
        return;
        // cout << "[CLIENT]: No message available on secondary socket" << endl;
    }
}

void Client::initiateConnection()
{
    this->connectToServer();

    // indetifying to server
    Message mex = squidProtocol.receiveAndParseMessage();
    cout << "[CLIENT]: Identify request received from server: " + mex.keyword << endl;
    squidProtocol.response(string("CLIENT"), string(processName));
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
    // secondary_addr.sin_port = htons(CLIENT_PORT);
    secondary_addr.sin_port = 0;
    secondary_addr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(secondary_fd, (struct sockaddr *)&secondary_addr, sizeof(secondary_addr)) < 0)
    {
        cerr << "[CLIENT]: Bind failed" << endl;
        return;
    }
    if (listen(secondary_fd, 3) < 0)
    {
        cerr << "[CLIENT]: Listen failed" << endl;
        return;
    }

    socklen_t addrlen = sizeof(secondary_addr);
    if (getsockname(secondary_fd, (struct sockaddr *)&secondary_addr, &addrlen) == -1)
    {
        perror("[CLIENT]: getsockname failed");
        return;
    }

    int assignedPort = ntohs(secondary_addr.sin_port);
    cout << "[CLIENT]: Listening on port: " << assignedPort << endl;

    mex = squidProtocol.receiveAndParseMessage();
    cout << mex.keyword << endl;
    squidProtocol.response(assignedPort);

    // accept connection
    addrlen = sizeof(server_addr);
    int new_socket = accept(secondary_fd, (struct sockaddr *)&server_addr, (socklen_t *)&addrlen);
    if (new_socket < 0)
    {
        cerr << "[CLIENT]: Accept failed" << endl;
        return;
    }
    cout << "[CLIENT]: Accepted connection from server" << endl;
    secondarySquidProtocol = SquidProtocol(new_socket, "CLIENT_SECONDARY", "CLIENT_SECONDARY");
    secondarySquidProtocol.setIsAlive(true);
}

void Client::createFile(string filePath)
{
    handleRequest(squidProtocol.createFile(filePath));
}
void Client::createFile(string filePath, int version)
{
    handleRequest(squidProtocol.createFile(filePath, version));
}

void Client::deleteFile(string filePath)
{
    handleRequest(squidProtocol.deleteFile(filePath));
}

void Client::updateFile(string filePath)
{
    handleRequest(squidProtocol.updateFile(filePath));
}

void Client::updateFile(std::string filePath, int version)
{
    handleRequest(squidProtocol.updateFile(filePath, version));
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

bool Client::isSecondarySocketAlive()
{
    return secondarySquidProtocol.isAlive();
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