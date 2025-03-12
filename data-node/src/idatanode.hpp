class IDataNode{

    public: 
        virtual bool connectToServer(int ip);
        virtual bool sendFileToServer(int fileId);
        virtual bool receiveFileFromServer(int fileId);
}; 