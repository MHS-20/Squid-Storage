#pragma once

#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <fstream>

#define BUFFER_SIZE 1024

class FileTransfer
{
public:
    FileTransfer();
    ~FileTransfer();
    void sendFile(int socket, const char *rolename, const char *filepath);
    void receiveFile(int socket, const char *rolename, const char *outputpath);
};