#include <string>
#include <vector>

enum ProtocolKeyWord
{
    CREATE_FILE,
    TRANSFER_FILE,
    READ_FILE,
    UPDATE_FILE,
    DELETE_FILE,
    ACQUIRE_LOCK,
    RELEASE_LOCK,
    HEARTBEAT,
    SYNC_STATUS,
    IDENTIFY,
    RESPONSE,
};

class SquidProtocolFormatter
{
public:
    std::string createFileFormat(std::string filePath);
    std::string transferFileFormat(std::string fileContent);
    std::string readFileFormat(std::string filePath);
    std::string updateFileFormat(std::string filePath);
    std::string deleteFileFormat(std::string filePath);
    std::string acquireLockFormat(std::string filePath);
    std::string releaseLockFormat(std::string filePath);
    std::string heartbeatFormat();
    std::string syncStatusFormat();
    std::string identifyFormat();
    std::string responseFormat();

private:
    std::string createMessage(ProtocolKeyWord keyword, std::vector<std::string> args);
};