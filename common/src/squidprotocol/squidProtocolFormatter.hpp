#pragma once
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <iostream>
using namespace std;
namespace fs = std::filesystem;

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
    CLOSE,
    CONNECT
};

class Message
{
public:
    ProtocolKeyWord keyword;
    std::map<std::string, std::string> args;
    Message() {};
    Message(ProtocolKeyWord keyword, std::map<std::string, std::string> args) : keyword(keyword), args(args) {}

    std::string toString()
    {
        std::string result = "Message: \n";
        for (const auto &arg : args)
        {
            result += arg.first + ": " + arg.second + "\n";
        }
        return result;
    }
};

class SquidProtocolFormatter
{
public:
    SquidProtocolFormatter();
    SquidProtocolFormatter(std::string nodeType);
    Message parseMessage(std::string message);

    std::string identifyFormat();
    std::string closeFormat();
    std::string connectServerFormat();

    std::string createFileFormat(std::string filePath);
    std::string createFileFormat(std::string filePath, int version);
    
    std::string transferFileFormat(std::string fileContent);
    std::string readFileFormat(std::string filePath);
    std::string updateFileFormat(std::string filePath);
    std::string updateFileFormat(std::string filePath, int version);
    std::string deleteFileFormat(std::string filePath);

    std::string acquireLockFormat(std::string filePath);
    std::string releaseLockFormat(std::string filePath);

    std::string heartbeatFormat();
    std::string syncStatusFormat();

    std::string responseFormat(bool lock);
    std::string responseFormat(std::string ack);
    std::string responseFormat(int port);
    std::string responseFormat(std::string nodeType, std::string processName);
    std::string responseFormat(std::map<std::string, long long> fileTimeMap);
    std::string responseFormat(std::map<std::string, int> fileVersionMap);
    std::string responseFormat(std::map<std::string, fs::file_time_type> filesLastWrite);

private:
    std::string nodeType;
    std::string createMessage(ProtocolKeyWord keyword, std::vector<std::string> args);
};