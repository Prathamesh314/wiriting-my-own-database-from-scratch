#include <cstddef>
#include <string>
#include <fstream>
#include <stdexcept>

namespace FileStorage {
    void saveDataToFile(std::string filepath, const std::byte* content, std::size_t size) {
        std::ofstream file(filepath, std::ios::binary);

        if (!file) {
            throw std::runtime_error("Failed to open file");
        }

        file.write(reinterpret_cast<const char*>(content), size);

        if (!file) {
            throw std::runtime_error("Failed to write data");
        }

        file.close();
    }
}
