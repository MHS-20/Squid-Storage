#include "filemanager.hpp"
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>

namespace {
bool isRuntimeFile(const fs::path &path)
{
  static const std::vector<std::string> ignoredFiles = {
  ".squid"};

  const auto name = path.filename().string();
  return std::find(ignoredFiles.begin(), ignoredFiles.end(), name) ==
         ignoredFiles.end();
}
} // namespace

FileManager::FileManager() {
  fs::create_directories(storageRoot());
  std::vector<std::string> entries = getFiles(storageRoot().string());
  for (auto entry : entries) {
    fileMap[entry] = FileLock(entry);
  }
  std::cout << "[FILEMANAGER]: File map initialized" << std::endl;

  // creating file version if it doesn't exist
  createFileVersionFile();
}

void FileManager::setFileMap(std::map<std::string, FileLock> fileMap) {
  std::lock_guard<std::mutex> lk(mutex_);
  this->fileMap = fileMap;
}

std::map<std::string, FileLock> FileManager::getFileMap() {
  std::lock_guard<std::mutex> lk(mutex_);
  return fileMap;
}

std::vector<std::string> FileManager::getFiles(std::string path) {
  std::vector<std::string> files;
  fs::path root(path);
  if (!fs::exists(root))
    return files;

  for (const auto &entry : fs::directory_iterator(root)) {
    if (!entry.is_regular_file())
      continue;
    if (!isRuntimeFile(entry.path()))
      continue;
    files.push_back(relativePath(entry.path()));
  }

  return files;
}

std::vector<fs::directory_entry> FileManager::getFileEntries(std::string path) {
  std::vector<fs::directory_entry> entries;
  fs::path root(path);
  if (!fs::exists(root))
    return entries;

  for (const auto &entry : fs::directory_iterator(root)) {
    if (!entry.is_regular_file())
      continue;
    if (!isRuntimeFile(entry.path()))
      continue;
    entries.push_back(entry);
  }

  return entries;
}

std::map<std::string, fs::file_time_type>
FileManager::getFilesLastWrite(std::string path) {
  std::map<std::string, fs::file_time_type> filesLastWrite;
  for (const auto &entry : getFileEntries(path)) {
    auto rel = relativePath(entry.path());
    filesLastWrite[rel] = entry.last_write_time();
  }
  return filesLastWrite;
}

std::map<std::string, int> FileManager::getFileVersionMap(std::string path) {
  std::map<std::string, int> savedFilesVersion;
  std::ifstream versionFile(versionFilePath());
  std::string line;
  while (std::getline(versionFile, line)) {
    std::istringstream iss(line);
    std::string filePath;
    int version;
    if (iss >> filePath >> version) {
      savedFilesVersion[filePath] = version;
    }
  }
  versionFile.close();

  std::map<std::string, int> filesVersion;
  auto files = this->getFiles(path);
  for (auto file : files) {
    if (savedFilesVersion.find(file) != savedFilesVersion.end()) {
      filesVersion[file] = savedFilesVersion[file];
    } else {
      filesVersion[file] = 0;
    }
  }

  return filesVersion;
}

bool FileManager::createFile(std::string path) {
  fs::path fullPath = resolvePath(path);
  fs::create_directories(fullPath.parent_path());
  std::ofstream newFile(fullPath);
  newFile.close();
  return true;
}

bool FileManager::createFile(std::string path, int version) {
  if (createFile(path)) {
    if (setFileVersion(path, version)) {
      return true;
    }
  }
  return false;
}

bool FileManager::deleteFile(std::string path) { return fs::remove(resolvePath(path)); }

bool FileManager::deleteFileAndVersion(std::string path) {
  if (deleteFile(path)) {
    fs::create_directories(versionFilePath().parent_path());
    std::ifstream versionFile(versionFilePath());
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(versionFile, line)) {
      std::istringstream iss(line);
      std::string filePath;
      int version;
      if (iss >> filePath >> version) {
        if (filePath != path) {
          lines.push_back(line);
        }
      }
    }
    versionFile.close();
    std::ofstream newVersionFile(versionFilePath());
    for (auto line : lines) {
      newVersionFile << line << std::endl;
    }
    newVersionFile.close();
    int fd = ::open(versionFilePath().c_str(), O_RDONLY);
    if (fd >= 0) { ::fsync(fd); ::close(fd); }
    return true;
  }
  return false;
}

bool FileManager::updateFile(std::string path, std::string content) {
  fs::path fullPath = resolvePath(path);
  fs::create_directories(fullPath.parent_path());
  std::ofstream file(fullPath, std::ios::trunc);
  if (!file.is_open()) {
    return false;
  }
  file << content;
  file.close();
  return true;
}

bool FileManager::updateFile(std::string path, std::string content,
                             int version) {
  if (updateFile(path, content)) {
    if (setFileVersion(path, version)) {
      return true;
    }
  }
  return false;
}

bool FileManager::updateFileAndVersion(std::string path, std::string content) {
  int version = getFileVersion(path);
  if (version == -1) {
    return false; // file version not found
  }
  version++;
  if (updateFile(path, content)) {
    if (setFileVersion(path, version)) {
      return true;
    }
  }
  return false;
}

std::string FileManager::formatFileList(std::vector<std::string> files) {
  std::string fileList = "";
  for (auto file : files) {
    fileList += file + ";";
  }
  fileList.pop_back();
  return fileList;
}

int FileManager::getFileVersion(std::string path) {
  std::ifstream versionFile(versionFilePath());
  std::string line;
  while (std::getline(versionFile, line)) {
    std::istringstream iss(line);
    std::string filePath;
    int version;
    if (iss >> filePath >> version) {
      if (filePath == path) {
        versionFile.close();
        return version;
      }
    }
  }
  return -1; // file not found
}

bool FileManager::setFileVersion(std::string path, int version) {
  fs::create_directories(versionFilePath().parent_path());
  std::ifstream versionFile(versionFilePath());
  std::string line;
  std::vector<std::string> lines;
  bool found = false;
  while (std::getline(versionFile, line)) {
    std::istringstream iss(line);
    std::string filePath;
    int ver;
    if (iss >> filePath >> ver) {
      if (filePath == path) {
        lines.push_back(filePath + " " + std::to_string(version));
        found = true;
      } else {
        lines.push_back(line);
      }
    }
  }
  versionFile.close();
  if (!found) {
    lines.push_back(path + " " + std::to_string(version));
  }
  std::ofstream newVersionFile(versionFilePath());
  for (auto line : lines) {
    newVersionFile << line << std::endl;
  }
  newVersionFile.close();
  int fd = ::open(versionFilePath().c_str(), O_RDONLY);
  if (fd >= 0) { ::fsync(fd); ::close(fd); }

  return true;
}

void FileManager::setFileLock(FileLock fileLock) {
  std::lock_guard<std::mutex> lk(mutex_);
  this->fileLock = fileLock;
}

FileLock &FileManager::getFileLock() {
  std::lock_guard<std::mutex> lk(mutex_);
  return this->fileLock;
}

void FileManager::updateFileMap() {
  std::lock_guard<std::mutex> lk(mutex_);
  std::vector<std::string> entries = getFiles(storageRoot().string());
  for (auto entry : entries) {
    if (fileMap.find(entry) == fileMap.end())
      fileMap[entry] = FileLock(entry);
  }
}

void FileManager::createFileVersionFile() {
  fs::create_directories(versionFilePath().parent_path());
  std::ifstream versionFile(versionFilePath());
  if (!versionFile) {
    std::ofstream newFile(versionFilePath());
    newFile.close();
  }
  versionFile.close();
}
