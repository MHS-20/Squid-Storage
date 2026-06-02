#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <map>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include "filelock.hpp"

namespace fs = std::filesystem;

// Singleton class: private constructor and prevent copying

class FileManager
{
public:
    static FileManager &getInstance()
    {
        static FileManager instance;
        return instance;
    }

    static fs::path storageRoot()
    {
        if (const char *overrideRoot = std::getenv("SQUID_STORAGE_ROOT"); overrideRoot && *overrideRoot)
            return fs::path(overrideRoot).lexically_normal();

        if (const char *home = std::getenv("HOME"); home && *home)
            return (fs::path(home) / "SquidStorage").lexically_normal();

        return fs::current_path().lexically_normal();
    }

    static fs::path versionFilePath()
    {
        return storageRoot() / ".squid" / "fileVersions.txt";
    }

    static fs::path resolvePath(const std::string &path)
    {
        fs::path p(path);
        if (p.is_absolute())
            return p.lexically_normal();
        return (storageRoot() / p).lexically_normal();
    }

    static std::string relativePath(const fs::path &path)
    {
        try
        {
            return fs::relative(path.lexically_normal(), storageRoot()).generic_string();
        }
        catch (...)
        {
            return path.lexically_normal().generic_string();
        }
    }

    std::map<std::string, FileLock> getFileMap();
    void setFileMap(std::map<std::string, FileLock> fileMap);

    std::vector<std::string> getFiles(std::string path);
    std::vector<fs::directory_entry> getFileEntries(std::string path);
    std::map<std::string, fs::file_time_type> getFilesLastWrite(std::string path);
    std::map<std::string, int> getFileVersionMap(std::string path);

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
    
    void setFileLock(FileLock fileLock);
    FileLock& getFileLock();

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
    void createFileVersionFile();
    FileLock fileLock;
};
