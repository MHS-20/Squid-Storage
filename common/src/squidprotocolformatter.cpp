#include "squidprotocolformatter.hpp"
#include <iostream>

SquidProtocolFormatter::SquidProtocolFormatter() {};

SquidProtocolFormatter::SquidProtocolFormatter(std::string nodeType)
{
    this->nodeType = nodeType;
}

std::string SquidProtocolFormatter::createMessage(ProtocolKeyWord keyword, std::vector<std::string> args)
{
    std::string keywordValue;

    switch (keyword)
    {
    case CREATE_FILE:
        keywordValue = "CREATE_FILE";
        break;
    case TRANSFER_FILE:
        keywordValue = "TRANSFER_FILE";
        break;
    case READ_FILE:
        keywordValue = "READ_FILE";
        break;
    case UPDATE_FILE:
        keywordValue = "UPDATE_FILE";
        break;
    case DELETE_FILE:
        keywordValue = "DELETE_FILE";
        break;
    case ACQUIRE_LOCK:
        keywordValue = "ACQUIRE_LOCK";
        break;
    case RELEASE_LOCK:
        keywordValue = "RELEASE_LOCK";
        break;
    case HEARTBEAT:
        keywordValue = "HEARTBEAT";
        break;
    case SYNC_STATUS:
        keywordValue = "SYNC_STATUS";
        break;
    case IDENTIFY:
        keywordValue = "IDENTIFY";
        break;
    case RESPONSE:
        keywordValue = "RESPONSE";
        break;
    default:
        break;
    }

    std::string message = keywordValue + "<";
    for (auto arg : args)
    {
        message += arg + ",";
    }

    if (args.size() != 0)
        message.pop_back();
    message += ">";

    //std::cout <<  "FORMATTER: " + message << std::endl;
    return message;
}

ProtocolKeyWord valueOf(const std::string &keyword)
{
    if (keyword == "CREATE_FILE")
        return CREATE_FILE;
    if (keyword == "TRANSFER_FILE")
        return TRANSFER_FILE;
    if (keyword == "READ_FILE")
        return READ_FILE;
    if (keyword == "UPDATE_FILE")
        return UPDATE_FILE;
    if (keyword == "DELETE_FILE")
        return DELETE_FILE;
    if (keyword == "ACQUIRE_LOCK")
        return ACQUIRE_LOCK;
    if (keyword == "RELEASE_LOCK")
        return RELEASE_LOCK;
    if (keyword == "HEARTBEAT")
        return HEARTBEAT;
    if (keyword == "SYNC_STATUS")
        return SYNC_STATUS;
    if (keyword == "IDENTIFY")
        return IDENTIFY;
    if (keyword == "RESPONSE")
        return RESPONSE;

    throw std::invalid_argument("Invalid keyword: " + keyword);
}

std::string SquidProtocolFormatter::createFileFormat(std::string filePath)
{
    return this->createMessage(CREATE_FILE, {"filePath:" + filePath});
}

std::string SquidProtocolFormatter::transferFileFormat(std::string filePath)
{
    return this->createMessage(TRANSFER_FILE, {"filePath:" + filePath});
}

std::string SquidProtocolFormatter::readFileFormat(std::string filePath)
{
    return this->createMessage(READ_FILE, {"filePath:" + filePath});
}

std::string SquidProtocolFormatter::updateFileFormat(std::string filePath)
{
    return this->createMessage(UPDATE_FILE, {"filePath:" + filePath});
}

std::string SquidProtocolFormatter::deleteFileFormat(std::string filePath)
{
    return this->createMessage(DELETE_FILE, {"filePath:" + filePath});
}

std::string SquidProtocolFormatter::acquireLockFormat(std::string filePath)
{
    return this->createMessage(ACQUIRE_LOCK, {"filePath:" + filePath});
}

std::string SquidProtocolFormatter::releaseLockFormat(std::string filePath)
{
    return this->createMessage(RELEASE_LOCK, {"filePath:" + filePath});
}

std::string SquidProtocolFormatter::heartbeatFormat()
{
    return this->createMessage(HEARTBEAT, {});
}

std::string SquidProtocolFormatter::syncStatusFormat()
{
    return this->createMessage(SYNC_STATUS, {});
}

std::string SquidProtocolFormatter::identifyFormat()
{
    return this->createMessage(IDENTIFY, {});
}

std::string SquidProtocolFormatter::responseFormat(std::string ack)
{
    return this->createMessage(RESPONSE, {"ACK:" + ack});
}

std::string SquidProtocolFormatter::responseFormat(std::string nodeType, std::string processName)
{
    return this->createMessage(RESPONSE, {"nodeType:" + nodeType, "processName:" + processName});
}

std::string SquidProtocolFormatter::responseFormat(bool lock)
{
    return this->createMessage(RESPONSE, {"lock:" + std::to_string(lock)});
}

std::string SquidProtocolFormatter::responseFormat(std::map<std::string, fs::file_time_type> filesLastWrite)
{
    std::vector<std::string> arguments;
    for (auto arg : filesLastWrite)
    {
        arguments.push_back(arg.first + ":" + std::to_string(static_cast<long long>(arg.second.time_since_epoch().count())));
    }
    return this->createMessage(RESPONSE, arguments);
}

Message SquidProtocolFormatter::parseMessage(std::string message)
{
    std::string keyword = message.substr(0, message.find("<"));
    std::string args = message.substr(message.find("<") + 1, message.find(">") - message.find("<") - 1);
    std::map<std::string, std::string> argMap;
    std::string arg;

    // std::cout << nodeType + ": Keyword: " << keyword << std::endl;
    // std::cout << nodeType + ": Args: " << args << std::endl;
    while (args.length() > 0)
    {
        size_t commaPos = args.find(",");
        if (commaPos == std::string::npos)
        {
            // Handle the last key-value pair
            std::string arg = args;
            argMap[arg.substr(0, arg.find(":"))] = arg.substr(arg.find(":") + 1);
            break; // Exit the loop after processing the last pair
        }
        else
        {
            // Handle the current key-value pair
            std::string arg = args.substr(0, commaPos);
            argMap[arg.substr(0, arg.find(":"))] = arg.substr(arg.find(":") + 1);
            args = args.substr(commaPos + 1); // Update args to exclude the processed pair
        }
    }

    // std::cout << nodeType + ": Message Parsed: " << std::endl;
    // for (auto arg : argMap)
    // {
    //     std::cout << arg.first << " => " << arg.second << std::endl;
    // }

    return Message(valueOf(keyword), argMap);
}
