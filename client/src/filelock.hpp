#pragma once
#include <string>

class FileLock
{
public:
    FileLock();
    FileLock(std::string filePath);
    bool isLocked();
    std::string getFilePath();
    void setIsLocked(bool locked);

private:
    bool locked;
    std::string filePath;
};