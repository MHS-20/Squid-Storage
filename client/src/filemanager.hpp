#include <vector>
#include <string>
#include <fstream>

#include <filesystem>
namespace fs = std::filesystem;

class FileManager
{
public:
    FileManager();
    std::vector<std::string> getFiles(std::string path);
    char *stringToChar(std::string str);
    bool createFile(std::string path);
    bool deleteFile(std::string path);
    bool updateFile(std::string path, std::string content);
    std::string readFile(std::string path);
    bool acquireLock(std::string path);
    bool releaseLock(std::string path);
};