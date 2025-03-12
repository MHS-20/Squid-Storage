#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>


#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

class DataNode{

    private: 
        int socket_fd = 0;
        struct sockaddr_in server_addr;
        char buffer[BUFFER_SIZE] = {0};


    public: 
        DataNode();
        DataNode(const char* server_ip, int port);
        ~DataNode();
        virtual void connectToServer();
        virtual void sendMessage(const char* message);
        virtual void receiveMessage();
        // virtual bool sendFileToServer(int fileId);
        // virtual bool receiveFileFromServer(int fileId);
}; 