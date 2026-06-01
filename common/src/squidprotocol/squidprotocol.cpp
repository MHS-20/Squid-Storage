#include "squidprotocol.hpp"
#include <iostream>
#include <cerrno>

SquidProtocol::SquidProtocol() {}

SquidProtocol::SquidProtocol(int socket_fd, std::string nodeType, std::string processName)
    : socket_fd_(socket_fd),
      alive_(true),
      processName_(std::move(processName)),
      nodeType_(std::move(nodeType)),
      formatter_(nodeType_)
{
    signal(SIGPIPE, SIG_IGN);
    fileTransfer_ = FileTransfer();
}

SquidProtocol::~SquidProtocol() {}

std::string SquidProtocol::toString() const
{
    return "Protocol{" + nodeType_ + ":" + processName_ + "}";
}

bool SquidProtocol::recvExact(uint8_t *buf, size_t n)
{
    size_t total = 0;
    while (total < n)
    {
        ssize_t r = recv(socket_fd_, buf + total, n - total, 0);
        if (!handleRecvError(r)) return false;
        total += static_cast<size_t>(r);
    }
    return true;
}

bool SquidProtocol::handleRecvError(ssize_t bytes)
{
    if (bytes == 0)
    {
        std::cout << nodeType_ + ": Connection closed by peer" << std::endl;
        alive_ = false;
        return false;
    }
    if (bytes < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            std::cout << nodeType_ + ": Socket timeout" << std::endl;
        alive_ = false;
        return false;
    }
    return true;
}

void SquidProtocol::sendFrame(const std::vector<uint8_t> &frame)
{
    size_t total = 0;
    while (total < frame.size())
    {
        ssize_t sent = send(socket_fd_, frame.data() + total, frame.size() - total, 0);
        if (sent <= 0)
        {
            alive_ = false;
            return;
        }
        total += static_cast<size_t>(sent);
    }
}

std::vector<uint8_t> SquidProtocol::receiveFrame()
{
    uint8_t header[FRAME_HEADER_SIZE];
    if (!recvExact(header, FRAME_HEADER_SIZE))
        return {};

    uint16_t magic = (uint16_t(header[0]) << 8) | header[1];
    if (magic != SQUID_MAGIC)
    {
        std::cerr << nodeType_ + ": Bad frame magic" << std::endl;
        alive_ = false;
        return {};
    }

    uint8_t numFields  = header[4];
    uint32_t payloadLen = (uint32_t(header[5]) << 24) | (uint32_t(header[6]) << 16) |
                          (uint32_t(header[7]) <<  8) |  uint32_t(header[8]);

    std::vector<uint8_t> frame(header, header + FRAME_HEADER_SIZE);

    for (uint8_t i = 0; i < numFields; ++i)
    {
        uint8_t fhdr[3];
        if (!recvExact(fhdr, 3)) return {};
        uint16_t vlen = (uint16_t(fhdr[1]) << 8) | fhdr[2];
        frame.push_back(fhdr[0]);
        frame.push_back(fhdr[1]);
        frame.push_back(fhdr[2]);
        if (vlen > 0)
        {
            size_t before = frame.size();
            frame.resize(before + vlen);
            if (!recvExact(frame.data() + before, vlen)) return {};
        }
    }

    if (payloadLen > 0)
    {
        size_t before = frame.size();
        frame.resize(before + payloadLen);
        if (!recvExact(frame.data() + before, payloadLen)) return {};
    }

    return frame;
}

Message SquidProtocol::receiveAndParse()
{
    std::vector<uint8_t> raw = receiveFrame();
    if (raw.empty()) return formatter_.makeNack();
    try { return formatter_.parseMessage(raw); }
    catch (const std::exception &e)
    {
        std::cerr << nodeType_ + ": parse error: " << e.what() << std::endl;
        return formatter_.makeNack();
    }
}

void SquidProtocol::sendFileAfterAck(const std::string &filePath, const Message &ackMsg)
{
    if (ackMsg.isAck())
        fileTransfer_.sendFile(socket_fd_, processName_.c_str(), filePath.c_str());
    else
        std::cerr << nodeType_ + ": Error while transferring file: " + filePath << std::endl;
}

Message SquidProtocol::identify()
{
    sendFrame(formatter_.identifyFormat());
    return receiveAndParse();
}

Message SquidProtocol::connectServer()
{
    std::cout << nodeType_ + ": sending connect server request" << std::endl;
    sendFrame(formatter_.connectServerFormat());
    Message r = receiveAndParse();
    std::cout << nodeType_ + ": received connect server response" << std::endl;
    return r;
}

Message SquidProtocol::closeConn()
{
    sendFrame(formatter_.closeFormat());
    return receiveAndParse();
}

Message SquidProtocol::listFiles()
{
    std::cout << nodeType_ + ": sending list files request" << std::endl;
    sendFrame(formatter_.syncStatusFormat());
    Message r = receiveAndParse();
    std::cout << nodeType_ + ": received list files response" << std::endl;
    return r;
}

Message SquidProtocol::syncStatus()
{
    std::cout << nodeType_ + ": sending sync status request" << std::endl;
    sendFrame(formatter_.syncStatusFormat());
    Message response = receiveAndParse();

    if (!response.isResponse()) return response;

    if (response.isAck())
    {
        std::cout << nodeType_ + ": received NACK from server" << std::endl;
        return response;
    }

    std::cout << nodeType_ + ": received sync status response" << std::endl;
    std::map<std::string, int> serverMap = response.getFileVersionMap();
    std::map<std::string, int> localMap  = FileManager::getInstance().getFileVersionMap(DEFAULT_FOLDER_PATH);

    for (auto &localFile : localMap)
    {
        auto it = serverMap.find(localFile.first);
        if (it != serverMap.end())
        {
            if (localFile.second > it->second)
            {
                std::cout << nodeType_ + ": server needs to update the file: " + localFile.first << std::endl;
                updateFile(localFile.first, localFile.second);
            }
            else if (localFile.second < it->second)
            {
                std::cout << nodeType_ + ": client needs to update the file: " + localFile.first << std::endl;
                readFile(localFile.first);
                FileManager::getInstance().setFileVersion(localFile.first, it->second);
            }
            serverMap.erase(it);
        }
        else
        {
            std::cout << nodeType_ + ": server needs to create the file: " + localFile.first << std::endl;
            createFile(localFile.first, localFile.second);
        }
    }

    for (auto &remoteFile : serverMap)
    {
        std::cout << nodeType_ + ": client needs to create the file: " + remoteFile.first << std::endl;
        readFile(remoteFile.first);
        FileManager::getInstance().setFileVersion(remoteFile.first, remoteFile.second);
    }

    return formatter_.makeNack();
}

Message SquidProtocol::createFile(const std::string &filePath)
{
    std::cout << "file name: " + filePath << std::endl;
    sendFrame(formatter_.createFileFormat(filePath));
    Message ack = receiveAndParse();
    std::cout << nodeType_ + ": received create file response" << std::endl;
    sendFileAfterAck(filePath, ack);
    return receiveAndParse();
}

Message SquidProtocol::createFile(const std::string &filePath, int version)
{
    std::cout << "file name: " + filePath << std::endl;
    sendFrame(formatter_.createFileFormat(filePath, version));
    std::cout << nodeType_ + ": sent create file request" << std::endl;
    Message ack = receiveAndParse();
    std::cout << nodeType_ + ": received create file response" << std::endl;
    sendFileAfterAck(filePath, ack);
    return receiveAndParse();
}

Message SquidProtocol::readFile(const std::string &filePath)
{
    std::cout << nodeType_ + ": sending read file request" << std::endl;
    sendFrame(formatter_.readFileFormat(filePath));
    Message ack = receiveAndParse();
    if (ack.isAck())
        fileTransfer_.receiveFile(socket_fd_, processName_.c_str(), filePath.c_str());
    return receiveAndParse();
}

Message SquidProtocol::updateFile(const std::string &filePath)
{
    sendFrame(formatter_.updateFileFormat(filePath));
    Message ack = receiveAndParse();
    sendFileAfterAck(filePath, ack);
    return receiveAndParse();
}

Message SquidProtocol::updateFile(const std::string &filePath, int version)
{
    sendFrame(formatter_.updateFileFormat(filePath, version));
    Message ack = receiveAndParse();
    sendFileAfterAck(filePath, ack);
    return receiveAndParse();
}

Message SquidProtocol::deleteFile(const std::string &filePath)
{
    sendFrame(formatter_.deleteFileFormat(filePath));
    return receiveAndParse();
}

Message SquidProtocol::acquireLock(const std::string &filePath)
{
    std::cout << nodeType_ + ": sending acquire lock request for " << filePath << std::endl;
    sendFrame(formatter_.acquireLockFormat(filePath));
    return receiveAndParse();
}

Message SquidProtocol::releaseLock(const std::string &filePath)
{
    sendFrame(formatter_.releaseLockFormat(filePath));
    return receiveAndParse();
}

Message SquidProtocol::heartbeat()
{
    sendFrame(formatter_.heartbeatFormat());
    return receiveAndParse();
}

void SquidProtocol::response(bool isAck)
{
    std::cout << nodeType_ + ": Sending response: " << isAck << std::endl;
    sendFrame(formatter_.responseAck(isAck));
}

void SquidProtocol::response(int port)
{
    sendFrame(formatter_.responsePort(port));
}

void SquidProtocol::response(const std::string &ack)
{
    std::cout << nodeType_ + ": Sending response: " << ack << std::endl;
    sendFrame(formatter_.responseFormat(ack));
}

void SquidProtocol::response(const std::string &nodeType, const std::string &processName)
{
    sendFrame(formatter_.responseIdentity(nodeType, processName));
}

void SquidProtocol::response(const std::map<std::string, int> &fileVersionMap)
{
    sendFrame(formatter_.responseFileVersionMap(fileVersionMap));
}

void SquidProtocol::response(const std::map<std::string, long long> &fileTimeMap)
{
    sendFrame(formatter_.responseFormat(fileTimeMap));
}

void SquidProtocol::response(const std::map<std::string, fs::file_time_type> &filesLastWrite)
{
    sendFrame(formatter_.responseFormat(filesLastWrite));
}

void SquidProtocol::requestDispatcher(const Message &message)
{
    std::string path = message.getString(FieldID::FILE_PATH);
    int version      = static_cast<int>(message.getUint32(FieldID::FILE_VERSION, 0));

    switch (message.opcode)
    {
    case Opcode::CREATE_FILE:
        std::cout << nodeType_ + ": received create file request\n";
        response(true);
        std::cout << nodeType_ + ": Receiving file" << std::endl;
        fileTransfer_.receiveFile(socket_fd_, processName_.c_str(), path.c_str());
        FileManager::getInstance().setFileVersion(path, version);
        response(true);
        break;

    case Opcode::TRANSFER_FILE:
        response(true);
        fileTransfer_.sendFile(socket_fd_, processName_.c_str(), path.c_str());
        response(true);
        break;

    case Opcode::READ_FILE:
        std::cout << nodeType_ + ": received read file request\n";
        response(true);
        fileTransfer_.sendFile(socket_fd_, processName_.c_str(), path.c_str());
        response(true);
        break;

    case Opcode::UPDATE_FILE:
        std::cout << nodeType_ + ": received update file request\n";
        response(true);
        fileTransfer_.receiveFile(socket_fd_, processName_.c_str(), path.c_str());
        FileManager::getInstance().setFileVersion(path, version);
        response(true);
        break;

    case Opcode::DELETE_FILE:
        std::cout << nodeType_ + ": received delete file request\n";
        FileManager::getInstance().deleteFileAndVersion(path);
        response(true);
        break;

    case Opcode::RELEASE_LOCK:
        response(true);
        break;

    case Opcode::HEARTBEAT:
        response(true);
        break;

    case Opcode::SYNC_STATUS:
        std::cout << nodeType_ + ": received sync status request\n";
        this->response(FileManager::getInstance().getFileVersionMap(DEFAULT_FOLDER_PATH));
        break;

    case Opcode::IDENTIFY:
        this->response(nodeType_, processName_);
        break;

    case Opcode::RESPONSE:
        std::cerr << "Connection lost, aborting operation" << std::endl;
        alive_ = false;
        break;

    case Opcode::CLOSE:
        response(true);
        close(socket_fd_);
        alive_ = false;
        std::cout << nodeType_ + ": Connection closed" << std::endl;
        break;

    default:
        std::cerr << nodeType_ + ": Unknown request: " + message.toString() << std::endl;
        break;
    }
}

void SquidProtocol::responseDispatcher(const Message &response)
{
    if (response.opcode != Opcode::RESPONSE) return;

    bool ack = response.isAck();

    if (!ack)
        std::cerr << nodeType_ + ": Error in response " + response.toString() << std::endl;
    else
        std::cout << nodeType_ + ": Operation performed" << std::endl;

    if (response.opcode == Opcode::HEARTBEAT)
    {
        alive_ = ack;
        if (!ack) std::cerr << nodeType_ + ": Heartbeat error" << std::endl;
        else      std::cout << nodeType_ + ": Received heartbeat successfully from server" << std::endl;
    }

    if (!ack) return;

    if (response.findField(FieldID::IS_LOCKED))
    {
        if (!response.getBool(FieldID::IS_LOCKED))
            std::cerr << nodeType_ + ": Lock refused" << std::endl;
        else
            std::cout << nodeType_ + ": Acquired lock successfully" << std::endl;
    }
}
