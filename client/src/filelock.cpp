#include "filelock.hpp"

FileLock::FileLock(){}

FileLock::FileLock(std::string filePath)
{
    this->filePath = filePath;
    this->isLocked = false;
}

std::string FileLock::getFilePath()
{
    return this->filePath;
}

bool FileLock::getIsLocked()
{
    return this->isLocked;
}

void FileLock::setIsLocked(bool isLocked)
{
    this->isLocked = isLocked;
}
