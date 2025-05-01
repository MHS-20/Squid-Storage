#pragma once
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <fstream>

using namespace std;
#define BUFFER_SIZE 1024

class FileTransfer
{
public:
    FileTransfer();
    ~FileTransfer();
    bool handleErrors(ssize_t bytes);
    void sendFile(int socket, string rolename, string filepath);
    void receiveFile(int socket, string rolename, string outputpath);
};