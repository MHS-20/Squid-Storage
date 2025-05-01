#include "filelock.hpp"

FileLock::FileLock() {}

FileLock::FileLock(std::string filePath)
{
    this->filePath = filePath;
    this->locked = false;
    this->expirationTime = std::chrono::system_clock::now();
    this->clientHolder = "";
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

void FileLock::setExpiration(std::chrono::system_clock::time_point exp)
{
    this->expirationTime = exp;
}
std::chrono::system_clock::time_point FileLock::getExpiration()
{
    return this->expirationTime;
}

std::string FileLock::getClientHolder()
{
    return this->clientHolder;
}
void FileLock::setClientHolder(std::string clientHolder)
{
    this->clientHolder = clientHolder;
}