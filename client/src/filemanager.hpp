#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <map>
#include <filesystem>
#include "filelock.hpp"

#define DEFAULT_PATH "./test_txt"
namespace fs = std::filesystem;

//Singleton class: private constructor and prevent copying

class FileManager
{
public:
    static FileManager &getInstance()
    {
        static FileManager instance; // viene creata una sola volta, la prima volta che viene chiamata
        return instance;
    }

    std::map<std::string, FileLock> getFileMap();
    std::vector<std::string> getFiles(std::string path);
    std::vector<fs::directory_entry> getFileEntries(std::string path);
    std::map<std::string, fs::file_time_type> getFilesLastWrite(std::string path);

    char *stringToChar(std::string str);
    bool createFile(std::string path);
    bool deleteFile(std::string path);
    bool updateFile(std::string path, std::string content);
    std::string readFile(std::string path);
    std::string formatFileList(std::vector<std::string> files);

    bool acquireLock(std::string path);
    bool releaseLock(std::string path);

private:
    FileManager();
    ~FileManager() {};

    // Prevent copying and assignment
    FileManager(const FileManager&) = delete;
    FileManager& operator=(const FileManager&) = delete;

    std::map<std::string, FileLock> fileMap;
};