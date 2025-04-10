#pragma once
#include <string>

class FileLock
{
public:
    FileLock();
    FileLock(std::string filePath);

private:
    bool isLocked;
    std::string filePath;

    bool getIsLocked();
    std::string getFilePath();
    void setIsLocked(bool isLocked);
};