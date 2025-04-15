#include "filelock.hpp"

FileLock::FileLock() {}

FileLock::FileLock(std::string filePath)
{
    this->filePath = filePath;
    this->locked = false;
}

std::string FileLock::getFilePath()
{
    return this->filePath;
}

bool FileLock::isLocked()
{
    return this->locked;
}

void FileLock::setIsLocked(bool locked)
{
    this->locked = locked;
}
