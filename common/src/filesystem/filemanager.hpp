#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <map>
#include <filesystem>
#include "filelock.hpp"

#define DEFAULT_PATH fs::current_path().string() // current directory
#define FILE_VERSION_PATH DEFAULT_PATH + "/.fileVersion.txt"

namespace fs = std::filesystem;

// Singleton class: private constructor and prevent copying

class FileManager
{
public:
    static FileManager &getInstance()
    {
        static FileManager instance; // viene creata una sola volta, la prima volta che viene chiamata
        return instance;
    }

    std::map<std::string, FileLock> getFileMap();
    void setFileMap(std::map<std::string, FileLock> fileMap);

    std::vector<std::string> getFiles(std::string path);
    std::vector<fs::directory_entry> getFileEntries(std::string path);
    std::map<std::string, fs::file_time_type> getFilesLastWrite(std::string path);
    std::map<std::string, int> getFilesVersion(std::string path);

    char *stringToChar(std::string str);
    bool createFile(std::string path);
    bool createFile(std::string path, int version);
    bool deleteFile(std::string path);
    bool deleteFileAndVersion(std::string path);
    bool updateFile(std::string path, std::string content);
    bool updateFile(std::string path, std::string content, int version);
    bool updateFileAndVersion(std::string path, std::string content);
    std::string readFile(std::string path);
    std::string formatFileList(std::vector<std::string> files);
    int getFileVersion(std::string path);
    bool setFileVersion(std::string path, int version);
    

    bool acquireLock(std::string path);
    bool releaseLock(std::string path);

private:
    FileManager();
    ~FileManager() {};

    // Prevent copying and assignment
    void updateFileMap();
    FileManager(const FileManager &) = delete;
    FileManager &operator=(const FileManager &) = delete;
    std::map<std::string, FileLock> fileMap;
    std::map<std::string, int> filesVersion;
    void createFileVersionFile();
};