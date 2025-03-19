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
    return std::string();
}