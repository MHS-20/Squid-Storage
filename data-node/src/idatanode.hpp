class IDataNode{

    public: 
        virtual bool connectToServer(int ip);
        virtual void sendMessage(const char* message);
        virtual void receiveMessage();
        virtual bool sendFileToServer(int fileId);
        virtual bool receiveFileFromServer(int fileId);
}; 