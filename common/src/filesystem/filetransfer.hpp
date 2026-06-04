#pragma once
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <vector>
#include <fstream>
#include <cstdint>

#include "INetworkChannel.hpp"

using namespace std;
#define BUFFER_SIZE 1024
// Maximum allowed file size to receive (1 GiB)
#define FILETRANSFER_MAX_SIZE ((uint64_t)1 << 30)

class FileTransfer
{
public:
    FileTransfer();
    ~FileTransfer();
    bool handleErrors(ssize_t bytes);
    bool sendFile(INetworkChannel &channel, const string &rolename, const string &filepath);
    bool sendFile(INetworkChannel &channel, const string &rolename, const std::vector<uint8_t> &data);
    bool receiveFile(INetworkChannel &channel, const string &rolename, const string &outputpath);
    bool receiveFile(INetworkChannel &channel, const string &rolename, std::vector<uint8_t> &data);
};
