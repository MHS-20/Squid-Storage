#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <map>
#include <filesystem>
namespace fs = std::filesystem;

class FileManager
{
public:
    FileManager();
    std::vector<std::string> getFiles(std::string path);
	std::vector<fs::directory_entry> getFileEntries(std::string path);
    std::map<std::string, fs::file_time_type> getFilesLastWrite(std::string path);
    char *stringToChar(std::string str);
    bool createFile(std::string path);
    bool deleteFile(std::string path);
    bool updateFile(std::string path, std::string content);
    std::string readFile(std::string path);
    bool acquireLock(std::string path);
    bool releaseLock(std::string path);
    std::string formatFileList(std::vector<std::string> files);
};