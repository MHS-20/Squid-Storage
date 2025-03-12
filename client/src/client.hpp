#include <iostream>
#include <unistd.h>
#include <string.h>
#include <fstream>

#include "filelock.hpp"
#include "filetransfer.hpp"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

class Client
{
public:
    Client();
    Client(const char *server_ip, int port);
    ~Client();
    virtual void connectToServer();

    /* Messages for Testing */
    virtual void sendMessage(const char *message);
    virtual void receiveMessage();

    /* File Transfer API */
    void sendFile(const char *filepath);
    void retriveFile(const char *outputpath);

    // virtual bool createFile(int fileId);
    // virtual bool updateFile(int fileId);
    // virtual bool deleteFile(int fileId);

private:
    int socket_fd = 0;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE] = {0};
    FileLock file_lock;
    FileTransfer fileTransfer;
    // virtual bool requireLock(int fileId);
};