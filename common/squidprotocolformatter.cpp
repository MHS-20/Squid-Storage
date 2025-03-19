#include "squidprotocolformatter.hpp"

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
    message.pop_back();
    message += ">";
    return message;
}

std::string SquidProtocolFormatter::createFileFormat(std::string filePath)
{
    return this->createMessage(CREATE_FILE, {"filePath:" + filePath});
}

std::string SquidProtocolFormatter::transferFileFormat(std::string fileContent)
{
    return this->createMessage(TRANSFER_FILE, {"fileContent:" + fileContent});
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

std::string SquidProtocolFormatter::responseFormat()
{
    return this->createMessage(RESPONSE, {});
}
