#include "filelock.hpp"

class IClient 
{
    public:
        virtual bool connectToServer(int ip);
        virtual bool createFile(int fileId);
        virtual bool updateFile(int fileId);
        virtual bool deleteFile(int fileId);

    private: 
        FileLock file_lock; 
        virtual bool requireLock(int fileId);
}; 