class Server {
    public: 
        virtual bool listenForClients();
        virtual bool listenForDataNodes();
        virtual bool lock(int filedId);
        virtual bool unlock(int fileId);
        virtual bool updateFile(int fileId);
        virtual bool deleteFile(int fileId);
        virtual bool createFile(int fileId);

    private: 
        virtual bool sendFileToDataNode(int fileId);
        virtual bool receiveFileFromDataNode(int fileId);
        virtual bool sendFileToClient(int fileId);
        virtual bool receiveFileFromClient(int fileId);
};