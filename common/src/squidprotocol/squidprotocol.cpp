#include "squidprotocol.hpp"

SquidProtocol::SquidProtocol(){}

SquidProtocol::SquidProtocol(int socket_fd, std::string nodeType, std::string processName)
{
    this->socket_fd = socket_fd;
    this->processName = processName;
    this->nodeType = nodeType;
    
    this->fileTransfer = FileTransfer();
    this->formatter = SquidProtocolFormatter(nodeType);

    this->communicator = SquidProtocolCommunicator(socket_fd, nodeType, processName);
    this->passive = SquidProtocolPassive(socket_fd, nodeType, processName, communicator);
    this->active = SquidProtocolActive(socket_fd, nodeType, processName, communicator);
}

SquidProtocol::~SquidProtocol() {}

int SquidProtocol::getSocket()
{
    return socket_fd;
}


std::string SquidProtocol::toString() const
{
    return "Protocol{" + nodeType + ":" + processName + "}";
}

// Message SquidProtocol::closeConn()
// {
//     this->sendMessage(this->formatter.closeFormat());
//     return receiveAndParseMessage();
// }

// Message SquidProtocol::identify()
// {
//     this->sendMessage(this->formatter.identifyFormat());
//     return this->receiveAndParseMessage();
// }

// ----------------------------
// --------- REQUESTS ---------
// ----------------------------

// Message SquidProtocol::createFile(std::string filePath)
// {
//     std::cout << nodeType + ": sending create file request" << std::endl;
//     this->sendMessage(this->formatter.createFileFormat(filePath));
//     // std::cout << nodeType + ": sent create file request" << std::endl;
//     Message response = receiveAndParseMessage();
//     std::cout << nodeType + ": received create file response" << std::endl;
//     transferFile(filePath, response);
//     return receiveAndParseMessage();
// }

// Message SquidProtocol::updateFile(std::string filePath)
// {
//     this->sendMessage(this->formatter.updateFileFormat(filePath));
//     Message response = receiveAndParseMessage();
//     transferFile(filePath, response);
//     return receiveAndParseMessage();
// }

// void SquidProtocol::transferFile(std::string filePath, Message response)
// {
//     // std::cout << nodeType + ": trying transfering file" << std::endl;
//     if (response.keyword == RESPONSE && response.args["ACK"] == "ACK")
//         this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), filePath.c_str());
//     else
//         perror(std::string("Error while transfering file: " + filePath).c_str());
//     return;
// }

// Message SquidProtocol::readFile(std::string filePath)
// {
//     this->sendMessage(this->formatter.readFileFormat(filePath));
//     Message response = receiveAndParseMessage();
//     if (response.keyword == RESPONSE && response.args["ACK"] == "ACK")
//         this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), filePath.c_str());
//     return receiveAndParseMessage();
// }

// Message SquidProtocol::deleteFile(std::string filePath)
// {
//     this->sendMessage(this->formatter.deleteFileFormat(filePath));
//     return receiveAndParseMessage();
// }

// Message SquidProtocol::acquireLock(std::string filePath)
// {
//     this->sendMessage(this->formatter.acquireLockFormat(filePath));
//     return receiveAndParseMessage();
// }

// Message SquidProtocol::releaseLock(std::string filePath)
// {
//     this->sendMessage(this->formatter.releaseLockFormat(filePath));
//     return receiveAndParseMessage();
// }

// Message SquidProtocol::heartbeat()
// {
//     this->sendMessage(this->formatter.heartbeatFormat());
//     return receiveAndParseMessage();
// }

// Message SquidProtocol::syncStatus()
// {
//     this->sendMessage(this->formatter.syncStatusFormat());
//     Message response = receiveAndParseMessage();
//     if (response.keyword == RESPONSE)
//     {
//         std::map<std::string, fs::file_time_type> filesLastWrite;
//         filesLastWrite = FileManager::getInstance().getFilesLastWrite(DEFAULT_FOLDER_PATH);
//         for (auto localFile : filesLastWrite)
//         {
//             if (response.args.find(localFile.first) != response.args.end())
//             {
//                 if (localFile.second.time_since_epoch().count() > std::stoll(response.args[localFile.first]))
//                 {
//                     // in this case server needs to update the file
//                     this->updateFile(localFile.first);
//                     this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), localFile.first.c_str());
//                 }
//                 else if (localFile.second.time_since_epoch().count() < std::stoll(response.args[localFile.first]))
//                 {
//                     // in this case client needs to update the file
//                     this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), localFile.first.c_str());
//                 }
//                 response.args.erase(localFile.first);
//             }
//             else
//             {
//                 // in this case server needs to create the file
//                 this->createFile(localFile.first);
//             }
//         }
//         if (response.args.size() > 0)
//         {
//             for (auto remoteFile : response.args)
//             {
//                 // in this case client needs to delete the file
//                 this->readFile(remoteFile.first);
//             }
//         }
//     }
//     // return "ACK";
//     return formatter.parseMessage(formatter.responseFormat("ACK"));
// }

// ---------------------------
// --------- PARSING ---------
// ---------------------------

// Message SquidProtocol::receiveAndParseMessage()
// {
//     std::string receivedMessage = receiveMessageWithLength();
//     std::cout << nodeType + ": Received message: " << receivedMessage << std::endl;
//     return this->formatter.parseMessage(receivedMessage);
// }

// void checkBytesRead(ssize_t bytesRead, std::string nodeType)
// {
//     if (bytesRead == 0)
//     {
//         std::cerr << nodeType + ": Connection closed by peer" << std::endl;
//         throw std::runtime_error("Connection closed by peer");
//     }
//     else if (bytesRead < 0)
//     {
//         std::string msg = std::string(nodeType) + ": Failed to receive message";
//         perror(msg.c_str());
//         throw std::runtime_error("Failed to receive message");
//     }
// }

// std::string SquidProtocol::receiveMessageWithLength()
// {
//     // Read the length of the message
//     uint32_t messageLength;
//     ssize_t bytesRead = recv(socket_fd, &messageLength, sizeof(messageLength), 0);
//     checkBytesRead(bytesRead, nodeType);

//     messageLength = ntohl(messageLength);
//     std::cout << nodeType + "[INFO]: Expecting message of length: " << messageLength << std::endl;

//     // Read the actual message
//     char *buffer = new char[messageLength + 1];
//     bytesRead = recv(socket_fd, buffer, messageLength, 0);
//     checkBytesRead(bytesRead, nodeType);

//     buffer[messageLength] = '\0';
//     std::string message(buffer);
//     delete[] buffer;

//     // std::cout << "[INFO]: Received message: " << message << std::endl;
//     return message;
// }

// -----------------------------
// --------- RESPONSES ---------
// -----------------------------

// void SquidProtocol::response(std::string ack)
// {
//     std::cout << "Sending response: " << ack << std::endl;
//     this->sendMessage(this->formatter.responseFormat(ack));
// }

// void SquidProtocol::response(std::string nodeType, std::string processName)
// {
//     this->sendMessage(this->formatter.responseFormat(nodeType, processName));
// }

// void SquidProtocol::response(std::map<std::string, fs::file_time_type> filesLastWrite)
// {
//     this->sendMessage(this->formatter.responseFormat(filesLastWrite));
// }

// void SquidProtocol::response(bool lock)
// {
//     std::cout << "Sending response: " << lock << std::endl;
//     this->sendMessage(this->formatter.responseFormat(lock));
// }

// --------------------------------
// --------- SEND MESSAGE ---------
// --------------------------------

// void SquidProtocol::sendMessage(std::string message)
// {
//     // std::cout << "[DEBUG " + processName + "]: socket_fd = " << socket_fd << " in " << __FUNCTION__ << std::endl;
//     sendMessageWithLength(message);
//     // send(this->socket_fd, message.c_str(), message.length(), 0);
// }

// void SquidProtocol::sendMessageWithLength(std::string &message)
// {
//     uint32_t messageLength = htonl(message.size());

//     // Send the length of the message
//     if (send(socket_fd, &messageLength, sizeof(messageLength), 0) < 0)
//     {
//         std::cerr << nodeType + "[ERROR]: Failed to send message length" << std::endl;
//         return;
//     }

//     // Send the actual message
//     if (send(socket_fd, message.c_str(), message.size(), 0) < 0)
//     {
//         std::cerr << nodeType + "[ERROR]: Failed to send message" << std::endl;
//         return;
//     }

//     std::cout << nodeType + "[INFO]: Sent message with length: " << message.size() << std::endl;
// }

// -------------------------------
// --------- DISPATCHERS ---------
// -------------------------------

// void SquidProtocol::requestDispatcher(Message message)
// {
//     switch (message.keyword)
//     {
//     case CREATE_FILE:
//         std::cout << nodeType + ": Dispatcher: create file\n";
//         this->response(std::string("ACK"));
//         std::cout << "calling file transfer.receiveFile" << std::endl;
//         this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
//         this->response(std::string("ACK"));
//         break;
//     case TRANSFER_FILE:
//         this->response(std::string("ACK"));
//         this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
//         this->response(std::string("ACK"));
//         break;
//     case READ_FILE:
//         this->response(std::string("ACK"));
//         this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
//         this->response(std::string("ACK"));
//         break;
//     case UPDATE_FILE:
//         this->response(std::string("ACK"));
//         this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
//         this->response(std::string("ACK"));
//         break;
//     case DELETE_FILE:
//         FileManager::getInstance().deleteFile(message.args["filePath"]);
//         this->response(std::string("ACK"));
//         break;
//     case ACQUIRE_LOCK:
//         this->response(FileManager::getInstance().acquireLock(message.args["filePath"]));
//         break;
//     case RELEASE_LOCK:
//         FileManager::getInstance().releaseLock(message.args["filePath"]);
//         this->response(std::string("ACK"));
//         break;
//     case HEARTBEAT:
//         this->response(std::string("ACK"));
//         break;
//     case SYNC_STATUS:
//         this->response(FileManager::getInstance().getFilesLastWrite(DEFAULT_FOLDER_PATH));
//         break;
//     case IDENTIFY:
//         this->response(this->nodeType, this->processName);
//         break;
//     case CLOSE:
//         this->response(std::string("ACK"));
//         close(this->socket_fd);
//         socket_fd = -1;
//         std::cout << nodeType + ": Connection closed" << std::endl;
//         break;
//     default:
//         break;
//     }
// }

// void SquidProtocol::responseDispatcher(Message response)
// {
//     switch (response.keyword)
//     {
//     case CREATE_FILE:
//         if (response.args["ACK"] != "ACK")
//             perror(std::string(nodeType + ": Error while creating file: " + response.args["filePath"]).c_str());
//         else
//             std::cout << nodeType + ": Created file successfully on server" << std::endl;
//         break;
//     case TRANSFER_FILE:
//         if (response.args["ACK"] != "ACK")
//             perror(std::string(nodeType + ": Error while transfering file: " + response.args["filePath"]).c_str());
//         else
//             std::cout << nodeType + ": Transfered file successfully on server" << std::endl;
//         break;
//     case READ_FILE:
//         if (response.args["ACK"] != "ACK")
//             perror(std::string(nodeType + ": Error while reading file: " + response.args["filePath"]).c_str());
//         else
//             std::cout << nodeType + ": Read file successfully on server" << std::endl;
//         break;
//     case UPDATE_FILE:
//         if (response.args["ACK"] != "ACK")
//             perror(std::string(nodeType + ": Error while updating file: " + response.args["filePath"]).c_str());
//         else
//             std::cout << nodeType + ": Updated file successfully on server" << std::endl;
//         break;
//     case DELETE_FILE:
//         if (response.args["ACK"] != "ACK")
//             perror(std::string(nodeType + ": Error while deleting file: " + response.args["filePath"]).c_str());
//         else
//             std::cout << nodeType + ": Deleted file successfully on server" << std::endl;
//         break;
//     case ACQUIRE_LOCK:
//         if (response.args["LOCK"] != "true")
//             perror(std::string(nodeType + ": Lock refused for file: " + response.args["filePath"]).c_str());
//         else
//             std::cout << nodeType + ": Acquired lock successfully on server" << std::endl;
//         break;
//     case RELEASE_LOCK:
//         if (response.args["ACK"] != "ACK")
//             perror(std::string(nodeType + ": Error while releasing lock for file: " + response.args["filePath"]).c_str());
//         else
//             std::cout << nodeType + ": Released lock successfully on server" << std::endl;
//         break;
//     case HEARTBEAT:
//         if (response.args["ACK"] != "ACK")
//             perror(std::string(nodeType + ": Heartbeat error").c_str());
//         else
//             std::cout << nodeType + ": Received heartbeat successfully from server" << std::endl;
//         break;
//     case SYNC_STATUS:
//         if (response.args["ACK"] != "ACK")
//             perror(std::string(nodeType + ": Error while synchronizing state").c_str());
//         else
//             std::cout << nodeType + ": Synchronization with server successful" << std::endl;
//         break;
//     case IDENTIFY:
//         if (response.args["ACK"] != "ACK")
//             perror(std::string(nodeType + ": Error while identifying").c_str());
//         else
//             std::cout << nodeType + ": Identified successfully on server" << std::endl;
//         break;
//     case CLOSE:
//         if (response.args["ACK"] != "ACK")
//             perror(std::string(nodeType + ": Error while closing connection").c_str());
//         else
//         {
//             close(this->socket_fd);
//             socket_fd = -1;
//             std::cout << nodeType + ": Connection closed successfully" << std::endl;
//         }
//         break;
//     default:
//         break;
//     }
// }