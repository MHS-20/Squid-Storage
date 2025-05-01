#pragma once
#include <string>
#include <chrono>
using namespace std;

class FileLock
{
public:
    FileLock();
    FileLock(string filePath);

    bool isLocked();
    void setIsLocked(bool locked);

    string getFilePath();

    void setExpiration(chrono::system_clock::time_point exp);
    chrono::system_clock::time_point getExpiration();

    string getClientHolder();
    void setClientHolder(string clientHolder);

private:
    bool locked;
    string clientHolder;
    string filePath;
    chrono::system_clock::time_point expirationTime;
};