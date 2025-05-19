#include "filemanager.hpp"

FileManager::FileManager()
{
    std::vector<std::string> entries = getFiles(DEFAULT_PATH);
    for (auto entry : entries)
    {
        fileMap[entry] = FileLock(entry);
    }
    std::cout << "[FILEMANAGER]: File map initialized" << std::endl;

    // creating file version file if it doesn't exist
    createFileVersionFile();
}

void FileManager::setFileMap(std::map<std::string, FileLock> fileMap)
{
    this->fileMap = fileMap;
}

std::map<std::string, FileLock> FileManager::getFileMap()
{
    return fileMap;
}

std::vector<std::string> FileManager::getFiles(std::string path)
{
    std::vector<std::string> files;

    for (const auto &entry : fs::directory_iterator(path))
        files.push_back(entry.path().filename().string());

    return files;
}

std::vector<fs::directory_entry> FileManager::getFileEntries(std::string path)
{
    std::vector<fs::directory_entry> entries;
    for (const auto &entry : fs::directory_iterator(path))
        entries.push_back(entry);

    return entries;
}

std::map<std::string, fs::file_time_type> FileManager::getFilesLastWrite(std::string path)
{
    auto files = this->getFiles(path);
    std::map<std::string, fs::file_time_type> filesLastWrite;
    for (auto file : files)
    {
        if (file == ".DS_Store" || file == "SquidStorage" || file == "SquidStorageServer" || file == "imgui.ini" || file == "DataNode" || file == ".fileVersion.txt")
            continue;
        filesLastWrite[file] = fs::last_write_time(file);
    }
    return filesLastWrite;
}

std::map<std::string, int> FileManager::getFileVersionMap(std::string path)
{
    std::map<std::string, int> savedFilesVersion;
    std::ifstream versionFile(FILE_VERSION_PATH);
    std::string line;
    while (std::getline(versionFile, line))
    {
        std::istringstream iss(line);
        std::string filePath;
        int version;
        if (iss >> filePath >> version)
        {
            savedFilesVersion[filePath] = version;
        }
    }
    versionFile.close();

    std::map<std::string, int> filesVersion;
    auto files = this->getFiles(path);
    for (auto file : files)
    {
        if (file == ".DS_Store" || file == "SquidStorage" || file == "SquidStorageServer" || file == "imgui.ini" || file == "DataNode" || file == ".fileVersion.txt")
            continue;
        if (savedFilesVersion.find(file) != savedFilesVersion.end())
        {
            filesVersion[file] = savedFilesVersion[file];
        }
        else
        {
            filesVersion[file] = 0;
        }
    }

    return filesVersion;
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

bool FileManager::createFile(std::string path, int version)
{
    if (createFile(path))
    {
        if (setFileVersion(path, version))
        {
            return true;
        }
    }
    return false;
}

bool FileManager::deleteFile(std::string path)
{
    return fs::remove(path);
}

bool FileManager::deleteFileAndVersion(std::string path)
{
    if (deleteFile(path))
    {
        std::ifstream versionFile(FILE_VERSION_PATH);
        std::string line;
        std::vector<std::string> lines;
        while (std::getline(versionFile, line))
        {
            std::istringstream iss(line);
            std::string filePath;
            int version;
            if (iss >> filePath >> version)
            {
                if (filePath != path)
                {
                    lines.push_back(line);
                }
            }
        }
        versionFile.close();
        std::ofstream newVersionFile(FILE_VERSION_PATH);
        for (auto line : lines)
        {
            newVersionFile << line << std::endl;
        }
        newVersionFile.close();
        return true;
    }
    return false;
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

bool FileManager::updateFile(std::string path, std::string content, int version)
{
    if (updateFile(path, content))
    {
        if (setFileVersion(path, version))
        {
            return true;
        }
    }
    return false;
}

bool FileManager::updateFileAndVersion(std::string path, std::string content)
{
    int version = getFileVersion(path);
    if (version == -1)
    {
        return false; // file version not found
    }
    version++;
    if (updateFile(path, content))
    {
        if (setFileVersion(path, version))
        {
            return true;
        }
    }
    return false;
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

std::string FileManager::formatFileList(std::vector<std::string> files)
{
    std::string fileList = "";
    for (auto file : files)
    {
        fileList += file + ";";
    }
    fileList.pop_back();
    return fileList;
}

int FileManager::getFileVersion(std::string path)
{
    std::ifstream versionFile(FILE_VERSION_PATH);
    std::string line;
    while (std::getline(versionFile, line))
    {
        std::istringstream iss(line);
        std::string filePath;
        int version;
        if (iss >> filePath >> version)
        {
            if (filePath == path)
            {
                versionFile.close();
                return version;
            }
        }
    }
    return -1; // file not found
}

bool FileManager::setFileVersion(std::string path, int version)
{
    std::ifstream versionFile(FILE_VERSION_PATH);
    std::string line;
    std::vector<std::string> lines;
    bool found = false;
    while (std::getline(versionFile, line))
    {
        std::istringstream iss(line);
        std::string filePath;
        int ver;
        if (iss >> filePath >> ver)
        {
            if (filePath == path)
            {
                lines.push_back(filePath + " " + std::to_string(version));
                found = true;
            }
            else
            {
                lines.push_back(line);
            }
        }
    }
    versionFile.close();
    if (!found)
    {
        lines.push_back(path + " " + std::to_string(version));
    }
    std::ofstream newVersionFile(FILE_VERSION_PATH);
    for (auto line : lines)
    {
        newVersionFile << line << std::endl;
    }
    newVersionFile.close();
    
    return true;
}

// used by client 
void FileManager::setFileLock(FileLock fileLock)
{
    this->fileLock = fileLock;
}

FileLock& FileManager::getFileLock()
{
    return this->fileLock;
}

void FileManager::updateFileMap()
{
    std::vector<std::string> entries = getFiles(DEFAULT_PATH);
    for (auto entry : entries)
    {
        if (fileMap.find(entry) == fileMap.end())
            fileMap[entry] = FileLock(entry);
    }
}

void FileManager::createFileVersionFile()
{
    std::ifstream versionFile(FILE_VERSION_PATH);
    if (!versionFile)
    {
        std::ofstream newFile(FILE_VERSION_PATH);
        newFile.close();
    }
    versionFile.close();
}
