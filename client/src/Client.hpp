class Client 
{
    public:
        virtual bool connectToServer(int ip);
        virtual bool createFile(int fileId);
        virtual bool updateFile(int fileId);
        virtual bool deleteFile(int fileId);
}; 