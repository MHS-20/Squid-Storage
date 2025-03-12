#include <vector>
#include <string>
#if __cplusplus >= 201703L
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif

class FileManager
{
public:
    FileManager();
    std::vector<std::string> getFiles(std::string path);
};