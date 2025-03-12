#include "filemanager.hpp"

std::vector<std::string> FileManager::getFiles(std::string path)
{
    std::vector<std::string> files;

    for (const auto &entry : fs::directory_iterator(path))
    {
        files.push_back(entry.path().string());
    }
    return files;
}