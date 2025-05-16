#include "squidprotocol.hpp"
using namespace std;

SquidProtocol::SquidProtocol() {}

SquidProtocol::SquidProtocol(int socket_fd, string nodeType, string processName)
{
    this->socket_fd = socket_fd;
    this->processName = processName;
    this->nodeType = nodeType;
    this->alive = true;

    signal(SIGPIPE, SIG_IGN);
    this->fileTransfer = FileTransfer();
    this->formatter = SquidProtocolFormatter(nodeType);
}

SquidProtocol::~SquidProtocol() {}

bool SquidProtocol::isAlive()
{
    return alive;
}

void SquidProtocol::setIsAlive(bool isAlive)
{
    this->alive = isAlive;
}

int SquidProtocol::getSocket()
{
    return socket_fd;
}

void SquidProtocol::setSocket(int socket_fd)
{
    this->socket_fd = socket_fd;
}

string SquidProtocol::toString() const
{
    return "Protocol{" + nodeType + ":" + processName + "}";
}

Message SquidProtocol::closeConn()
{
    this->sendMessage(this->formatter.closeFormat());
    return receiveAndParseMessage();
}

Message SquidProtocol::identify()
{
    this->sendMessage(this->formatter.identifyFormat());
    return this->receiveAndParseMessage();
}

// ----------------------------
// --------- REQUESTS ---------
// ----------------------------

Message SquidProtocol::createFile(string filePath)
{
    cout << "file name: " + filePath << endl;
    this->sendMessage(this->formatter.createFileFormat(filePath));
    Message response = receiveAndParseMessage();
    cout << nodeType + ": received create file response" << endl;
    transferFile(filePath, response);
    return receiveAndParseMessage();
}
Message SquidProtocol::createFile(string filePath, int version)
{
    cout << "file name: " + filePath << endl;
    this->sendMessage(this->formatter.createFileFormat(filePath, version));
    Message response = receiveAndParseMessage();
    cout << nodeType + ": received create file response" << endl;
    transferFile(filePath, response);
    return receiveAndParseMessage();
}



Message SquidProtocol::updateFile(string filePath)
{
    this->sendMessage(this->formatter.updateFileFormat(filePath));
    Message response = receiveAndParseMessage();
    transferFile(filePath, response);
    return receiveAndParseMessage();
}

Message SquidProtocol::updateFile(string filePath, int version)
{
    this->sendMessage(this->formatter.updateFileFormat(filePath, version));
    Message response = receiveAndParseMessage();
    transferFile(filePath, response);
    return receiveAndParseMessage();
}

void SquidProtocol::transferFile(string filePath, Message response)
{
    cout << nodeType + ": trying transfering file" << endl;
    cout << response.toString() << endl;

    if (response.keyword == RESPONSE && response.args["ACK"] == "ACK")
        this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), filePath.c_str());
    else
        cerr << nodeType + ": Error while transfering file: " + filePath << endl;
    return;
}

Message SquidProtocol::readFile(string filePath)
{
    cout << nodeType + ": sending read file request" << endl;
    this->sendMessage(this->formatter.readFileFormat(filePath));
    Message response = receiveAndParseMessage();
    if (response.keyword == RESPONSE && response.args["ACK"] == "ACK")
        this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), filePath.c_str());
    return receiveAndParseMessage();
}

Message SquidProtocol::deleteFile(string filePath)
{
    this->sendMessage(this->formatter.deleteFileFormat(filePath));
    return receiveAndParseMessage();
}

Message SquidProtocol::acquireLock(string filePath)
{
    cout << nodeType + ": sending acquire lock request for " << filePath << endl;
    this->sendMessage(this->formatter.acquireLockFormat(filePath));
    return receiveAndParseMessage();
}

Message SquidProtocol::releaseLock(string filePath)
{
    this->sendMessage(this->formatter.releaseLockFormat(filePath));
    return receiveAndParseMessage();
}

Message SquidProtocol::heartbeat()
{
    this->sendMessage(this->formatter.heartbeatFormat());
    return receiveAndParseMessage();
}

Message SquidProtocol::connectServer()
{
    cout << nodeType + ": sending connect server request" << endl;
    this->sendMessage(this->formatter.connectServerFormat());
    Message response = receiveAndParseMessage();
    cout << nodeType + ": received connect server response" << endl;
    return response;
}

// executed by server
Message SquidProtocol::listFiles()
{
    cout << nodeType + ": sending list files request" << endl;
    this->sendMessage(this->formatter.syncStatusFormat());
    Message response = receiveAndParseMessage();
    cout << nodeType + ": received list files response" << endl;
    return response;
}

// executed by client
Message SquidProtocol::syncStatus()
{
    cout << nodeType + ": sending sync status request" << endl;
    this->sendMessage(this->formatter.syncStatusFormat());
    Message response = receiveAndParseMessage();
    if (response.keyword == RESPONSE)
    {
        if (response.args["ACK"] == "NACK")
        {
            cout << nodeType + ": received NACK from server" << endl;
            return response;
        }
        cout << nodeType + ": received sync status response" << endl;
        map<string, int> fileVersionMap;
        fileVersionMap = FileManager::getInstance().getFileVersionMap(DEFAULT_FOLDER_PATH);
        cout << nodeType + ": checking files: " << endl;
        for (auto localFile : fileVersionMap)
        {
            if (response.args.find(localFile.first) != response.args.end())
            {
                cout << nodeType + ": found file that already exists on server " << localFile.first << endl;
                if (localFile.second  > stoi(response.args[localFile.first]))
                {
                    // in this case server needs to update the file
                    cout << nodeType + ": server needs to update the file: " + localFile.first << endl;
                    this->updateFile(localFile.first, localFile.second);
                    // this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), localFile.first.c_str());
                }
                else if (localFile.second < stoi(response.args[localFile.first]))
                {
                    // in this case client needs to update the file
                    // this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), localFile.first.c_str());
                    cout << nodeType + ": client needs to update the file: " + localFile.first << endl;
                    this->readFile(localFile.first);
                    FileManager::getInstance().setFileVersion(localFile.first, stoi(response.args[localFile.first]));
                }
                response.args.erase(localFile.first);
            }
            else
            {
                // in this case server needs to create the file
                cout << nodeType + ": server needs to create the file: " + localFile.first << endl;
                this->createFile(localFile.first, localFile.second);
            }
        }
        if (response.args.size() > 0)
        {
            for (auto remoteFile : response.args)
            {
                // in this case client needs to create the file
                // this->fileManager.deleteFile(remoteFile.first);
                cout << nodeType + ": client needs to create the file: " + remoteFile.first << endl;
                this->readFile(remoteFile.first);
            }
        }
    }
    return formatter.parseMessage(formatter.responseFormat(string("ACK")));
}



// ---------------------------
// --------- PARSING ---------
// ---------------------------
Message SquidProtocol::receiveAndParseMessage()
{
    string receivedMessage = receiveMessageWithLength();
    return this->formatter.parseMessage(receivedMessage);
}

string SquidProtocol::receiveMessageWithLength()
{
    // Read the length of the message
    uint32_t messageLength;
    ssize_t bytesRead = recv(socket_fd, &messageLength, sizeof(messageLength), 0);
    if (!handleErrors(bytesRead))
    {
        return formatter.responseFormat(string("NACK"));
    }

    messageLength = ntohl(messageLength);
    // cout << nodeType + ": Expecting message of length: " << messageLength << endl;

    // Read the actual message
    char *buffer = new char[messageLength + 1];
    bytesRead = recv(socket_fd, buffer, messageLength, 0);
    if (!handleErrors(bytesRead))
    {
        return formatter.responseFormat(string("NACK"));
    }

    buffer[messageLength] = '\0';
    string message(buffer);
    delete[] buffer;

    cout << "[INFO]: Received message: " << message << endl;
    return message;
}

bool SquidProtocol::handleErrors(ssize_t bytes)
{
    if (bytes == 0)
    {
        cout << nodeType + ": Connection closed by peer" << endl;
        // close(socket_fd);
        alive = false;
        return false;
    }
    else if (bytes < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            cout << nodeType + "Socket timeout" << endl;
        }
        // close(socket_fd);
        alive = false;
        return false;
    }

    return true;
}

// -----------------------------
// --------- RESPONSES ---------
// -----------------------------

void SquidProtocol::response(string ack)
{
    cout << nodeType + "Sending response: " << ack << endl;
    this->sendMessage(this->formatter.responseFormat(ack));
}

void SquidProtocol::response(int port)
{
    this->sendMessage(this->formatter.responseFormat(port));
}

void SquidProtocol::response(string nodeType, string processName)
{
    this->sendMessage(this->formatter.responseFormat(nodeType, processName));
}

// deprecated
void SquidProtocol::response(map<string, fs::file_time_type> filesLastWrite)
{
    this->sendMessage(this->formatter.responseFormat(filesLastWrite));
}

void SquidProtocol::response(map<string, int> fileVersionMap)
{
    this->sendMessage(this->formatter.responseFormat(fileVersionMap));
}

void SquidProtocol::response(map<string, long long> fileTimeMap)
{
    this->sendMessage(this->formatter.responseFormat(fileTimeMap));
}

void SquidProtocol::response(bool lock)
{
    cout << nodeType + "Sending response: " << lock << endl;
    this->sendMessage(this->formatter.responseFormat(lock));
}

// --------------------------------
// --------- SEND MESSAGE ---------
// --------------------------------

void SquidProtocol::sendMessage(string message)
{
    sendMessageWithLength(message);
}

void SquidProtocol::sendMessageWithLength(string &message)
{
    // Send the length of the message
    uint32_t messageLength = htonl(message.size());
    ssize_t bytesSent = send(socket_fd, &messageLength, sizeof(messageLength), 0);
    if (!handleErrors(bytesSent))
        return;

    // Send the actual message
    bytesSent = send(socket_fd, message.c_str(), message.size(), 0);
    if (!handleErrors(bytesSent))
        return;
    // cout << nodeType + ": Sent message with length: " << message.size() << endl;
}

// -------------------------------
// --------- DISPATCHERS ---------
// -------------------------------
void SquidProtocol::requestDispatcher(Message message)
{
    switch (message.keyword)
    {
    case CREATE_FILE:
        cout << nodeType + ": received create file request\n";
        this->response(string("ACK"));
        cout << nodeType + ": Receiving file" << endl;
        this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
        FileManager::getInstance().setFileVersion(message.args["filePath"], stoi(message.args["fileVersion"]));
        this->response(string("ACK"));
        break;
    case TRANSFER_FILE:
        this->response(string("ACK"));
        this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
        this->response(string("ACK"));
        break;
    case READ_FILE:
        cout << nodeType + ": received read file request\n";
        this->response(string("ACK"));
        this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
        this->response(string("ACK"));
        break;
    case UPDATE_FILE:
        cout << nodeType + ": received update file request\n";
        this->response(string("ACK"));
        this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
        FileManager::getInstance().setFileVersion(message.args["filePath"], stoi(message.args["fileVersion"]));
        this->response(string("ACK"));
        break;
    case DELETE_FILE:
        cout << nodeType + ": received delete file request\n";
        FileManager::getInstance().deleteFileAndVersion(message.args["filePath"]);
        this->response(string("ACK"));
        break;
    // case ACQUIRE_LOCK:
    //     cout << nodeType + ": received acquire lock request for " << message.args["filePath"] << endl;
    //     this->response(FileManager::getInstance().acquireLock(message.args["filePath"]));
    //     break;
    case RELEASE_LOCK:
        // FileManager::getInstance().releaseLock(message.args["filePath"]);
        this->response(string("ACK"));
        break;
    case HEARTBEAT:
        this->response(string("ACK"));
        break;
    case SYNC_STATUS:
        cout << nodeType + ": received sync status request\n";
        this->response(FileManager::getInstance().getFileVersionMap(DEFAULT_FOLDER_PATH));
        break;
    case IDENTIFY:
        this->response(this->nodeType, this->processName);
        break;
    case RESPONSE:
        cerr << "Connection lost, aborting operation" << endl;
        alive = false;
        break;
    case CLOSE:
        this->response(string("ACK"));
        close(this->socket_fd);
        // socket_fd = -1;
        alive = false;
        cout << nodeType + ": Connection closed" << endl;
        break;
    default:
        cerr << nodeType + ": Unknown request: " + message.toString() << endl;
        break;
    }
}

void SquidProtocol::responseDispatcher(Message response)
{
    switch (response.keyword)
    {
    case RESPONSE:
        if (response.args["ACK"] != "ACK")
            cerr << nodeType + ": Error in response " + response.toString() << endl;
        else
            cout << nodeType + ": Operation performed" << endl;
        break;
    case CREATE_FILE:
        if (response.args["ACK"] != "ACK")
            cerr << nodeType + ": Error while creating file: " + response.args["filePath"] << endl;
        else
            cout << nodeType + ": Created file successfully on server" << endl;
        break;
    case TRANSFER_FILE:
        cout << "tf resp: " + response.args["ACK"] << endl;
        if (response.args["ACK"] != "ACK")
            cerr << nodeType + ": Error while transfering file: " + response.args["ACK"] << endl;
        else
            cout << nodeType + ": Transfered file successfully on server" << endl;
        break;
    case READ_FILE:
        if (response.args["ACK"] != "ACK")
            cerr << nodeType + ": Error while reading file: " + response.args["filePath"] << endl;
        else
            cout << nodeType + ": Read file successfully on server" << endl;
        break;
    case UPDATE_FILE:
        if (response.args["ACK"] != "ACK")
            cerr << nodeType + ": Error while updating file: " + response.args["filePath"] << endl;
        else
            cout << nodeType + ": Updated file successfully on server" << endl;
        break;
    case DELETE_FILE:
        if (response.args["ACK"] != "ACK")
            cerr << nodeType + ": Error while deleting file: " + response.args["filePath"] << endl;
        else
            cout << nodeType + ": Deleted file successfully on server" << endl;
        break;
    case ACQUIRE_LOCK:
        if (response.args["LOCK"] != "1")
            cerr << nodeType + ": Lock refused for file: " + response.args["filePath"] << endl;
        else
            cout << nodeType + ": Acquired lock successfully on server" << endl;
        break;
    case RELEASE_LOCK:
        if (response.args["ACK"] != "ACK")
            cerr << nodeType + ": Error while releasing lock for file: " + response.args["filePath"] << endl;
        else
            cout << nodeType + ": Released lock successfully on server" << endl;
        break;
    case HEARTBEAT:
        if (response.args["ACK"] != "ACK")
        {
            alive = false;
            cerr << nodeType + ": Heartbeat error" << endl;
        }
        else
        {
            alive = true;
            cout << nodeType + ": Received heartbeat successfully from server" << endl;
        }
        break;
    case SYNC_STATUS:
        if (response.args["ACK"] != "ACK")
            cerr << nodeType + ": Error while synchronizing state" << endl;
        else
            cout << nodeType + ": Synchronization with server successful" << endl;
        break;
    case IDENTIFY:
        if (response.args["ACK"] != "ACK")
            cerr << nodeType + ": Error while identifying" << endl;
        else
            cout << nodeType + ": Identified successfully on server" << endl;
        break;
    case CLOSE:
        if (response.args["ACK"] != "ACK")
            cerr << nodeType + ": Error while closing connection" << endl;
        else
        {
            close(this->socket_fd);
            alive = false;
            cout << nodeType + ": Connection closed successfully" << endl;
        }
        break;
    default:
        break;
    }
}