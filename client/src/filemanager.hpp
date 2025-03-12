#include <vector>
#include <string>

#include <filesystem>
namespace fs = std::filesystem;

class FileManager
{
public:
    FileManager();
    std::vector<std::string> getFiles(std::string path);
};