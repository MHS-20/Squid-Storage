#include "client.hpp"

#include <iostream>
#include <stdexcept>

#include "filesystem/syncplanner.hpp"
#include "filesystem/filemanager.hpp"

Client::Client(const std::string &serverIp, int serverPort, const std::string &processName)
    : serverIp_(serverIp), serverPort_(serverPort), processName_(processName)
{
}

Client::~Client()
{
    disconnect();
}

void Client::setPushHandler(PushHandler handler)
{
    pushHandler_ = std::move(handler);
}

void Client::doHandshake(SquidProtocol &proto)
{
    Message identify = proto.receiveAndParse();
    if (identify.opcode != Opcode::IDENTIFY)
        throw std::runtime_error("Client: expected IDENTIFY, got " + identify.toString());

    proto.response(std::string("CLIENT"), processName_);

    Message ack = proto.receiveAndParse();
    if (!ack.isAck())
        throw std::runtime_error("Client: handshake ACK not received");

    std::cout << "[Client]: Handshake complete with server" << std::endl;
}

void Client::connect()
{
    auto channel = std::make_shared<TCPConnectorChannel>(serverIp_, serverPort_, 60, 2);
    std::cout << "[Client]: Connected to server" << std::endl;

    SquidProtocol proto(channel, "CLIENT", processName_);
    doHandshake(proto);

    session_ = std::make_shared<ConnectionSession>(
        channel, "CLIENT", processName_,
        [this](ConnectionSession &s, const Message &m) { handlePush(s, m); });

    session_->start(true);
}

void Client::disconnect()
{
    if (!session_)
        return;

    if (session_->isAlive())
    {
        session_->call([](SquidProtocol &proto) {
            return proto.closeConn();
        });
    }

    session_->stop();
    session_.reset();
}

bool Client::isAlive() const
{
    return session_ && session_->isAlive();
}

void Client::handlePush(ConnectionSession &session, const Message &message)
{
    if (message.isResponse())
        return;

    switch (message.opcode)
    {
    case Opcode::CREATE_FILE:
    case Opcode::UPDATE_FILE:
    case Opcode::DELETE_FILE:
    case Opcode::RELEASE_LOCK:
        if (pushHandler_)
            pushHandler_(message);
        break;
    case Opcode::CLOSE:
        session.setIsAlive(false);
        break;
    case Opcode::HEARTBEAT:
        session.post([](SquidProtocol &proto) { proto.response(true); });
        break;
    default:
        std::cerr << "[Client]: Unexpected push opcode: " << message.toString() << std::endl;
        break;
    }
}

Message Client::createFile(const std::string &filePath, const std::vector<uint8_t> &data, int version)
{
    if (!session_ || !session_->isAlive())
        return {};

    return session_->call([&](SquidProtocol &proto) {
        return proto.createFile(filePath, version, data);
    });
}

Message Client::readFile(const std::string &filePath, std::vector<uint8_t> &dataOut)
{
    if (!session_ || !session_->isAlive())
        return {};

    return session_->call([&](SquidProtocol &proto) {
        return proto.readFile(filePath, dataOut);
    });
}

Message Client::updateFile(const std::string &filePath, const std::vector<uint8_t> &data, int version)
{
    if (!session_ || !session_->isAlive())
        return {};

    return session_->call([&](SquidProtocol &proto) {
        return proto.updateFile(filePath, version, data);
    });
}

Message Client::deleteFile(const std::string &filePath)
{
    if (!session_ || !session_->isAlive())
        return {};

    return session_->call([&](SquidProtocol &proto) {
        return proto.deleteFile(filePath);
    });
}

Message Client::acquireLock(const std::string &filePath)
{
    if (!session_ || !session_->isAlive())
        return {};

    return session_->call([&](SquidProtocol &proto) {
        return proto.acquireLock(filePath);
    });
}

Message Client::releaseLock(const std::string &filePath)
{
    if (!session_ || !session_->isAlive())
        return {};

    return session_->call([&](SquidProtocol &proto) {
        return proto.releaseLock(filePath);
    });
}

Message Client::syncStatus()
{
    if (!session_ || !session_->isAlive())
        return {};

    Message response = session_->call([](SquidProtocol &proto) {
        return proto.syncStatus();
    });

    if (!response.isResponse() || response.isAck())
        return response;

    auto remoteMap = response.getFileVersionMap();
    auto localMap  = FileManager::getInstance().getFileVersionMap(FileManager::storageRoot().string());
    auto ops       = planSync(localMap, remoteMap);

    for (const auto &op : ops)
    {
        switch (op.action)
        {
        case SyncAction::UPLOAD:
        {
            std::vector<uint8_t> data;
            session_->call([&](SquidProtocol &proto) {
                return proto.updateFile(op.filePath, op.version);
            });
            break;
        }
        case SyncAction::CREATE_REMOTE:
        {
            session_->call([&](SquidProtocol &proto) {
                return proto.createFile(op.filePath, op.version);
            });
            break;
        }
        case SyncAction::DOWNLOAD:
        {
            session_->call([&](SquidProtocol &proto) {
                Message r = proto.readFile(op.filePath);
                return r;
            });
            FileManager::getInstance().setFileVersion(op.filePath, op.version);
            break;
        }
        }
    }

    return response;
}

Message Client::heartbeat()
{
    if (!session_ || !session_->isAlive())
        return {};

    return session_->call([](SquidProtocol &proto) {
        return proto.heartbeat();
    });
}
