#include <vector>
#include <netinet/in.h>
class IServer {
    public: 
        void start();
        // virtual bool listenForClients();
        // virtual bool listenForDataNodes();
        // virtual bool lock(int filedId);
        // virtual bool unlock(int fileId);
        // virtual bool updateFile(int fileId);
        // virtual bool deleteFile(int fileId);
        // virtual bool createFile(int fileId);

    private: 
        void handleClient(int client_socket);
        // virtual bool replicateFileToDataNodes(int fileId, std::vector<int> ip);
        // virtual bool receiveFileFromDataNode(int fileId, int ip);
        // virtual bool sendFileToClient(int fileId, int ip);
        // virtual bool receiveFileFromClient(int fileId, int ip);
};
