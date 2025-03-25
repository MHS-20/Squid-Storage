#include "filemanager.hpp"

FileManager::FileManager()
{
}

std::vector<std::string> FileManager::getFiles(std::string path)
{
    std::vector<std::string> files;

    for (const auto &entry : fs::directory_iterator(path))
    {
        files.push_back(entry.path().string());
    }
    return files;
}

std::map<std::string, fs::file_time_type> FileManager::getFilesLastWrite(std::string path)
{
    auto files = this->getFiles(path);
    std::map<std::string, fs::file_time_type> filesLastWrite;
    for (auto file : files)
    {
        filesLastWrite[file] = fs::last_write_time(file);
    }
    return filesLastWrite;
}

char *FileManager::stringToChar(std::string str)
{
    char *res = new char[str.length() + 1];
    for (int i = 0; i <= str.length(); i++)
    {
        res[i] = str[i];
    }
    res[str.length()] = '\0';

    return res;
}

bool FileManager::createFile(std::string path)
{
    std::ofstream newFile(path);
    newFile.close();
    return true;
}

bool FileManager::deleteFile(std::string path)
{
    return fs::remove(path);
}

bool FileManager::updateFile(std::string path, std::string content)
{
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open())
    {
        return false;
    }
    file << content;
    file.close();
    return true;
}

std::string FileManager::readFile(std::string path)
{
    std::ifstream file(path);
    std::string fileContent;
    std::string line;
    while (std::getline(file, line))
    {
        fileContent += line + "\n";
    }
    file.close();
    return fileContent;
}

bool FileManager::acquireLock(std::string path)
{
    return false;
}

bool FileManager::releaseLock(std::string path)
{
    return false;
}

std::string formatFileList(std::vector<std::string> files)
{
    std::string fileList = "";
    for (auto file : files)
    {
        fileList += file + ";";
    }
    fileList.pop_back();
    return fileList;
}
