#include <string>
#include <iostream>
#include <filesystem>
namespace fs = std::__fs::filesystem;

int main() {
    std::string path = "./";
    for (const auto & entry : fs::directory_iterator(path))
        std::cout << entry.path() << std::endl;
    return 0;
}