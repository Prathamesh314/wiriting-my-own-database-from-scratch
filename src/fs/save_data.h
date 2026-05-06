#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <fstream>
#include <stdexcept>
#include <random>

#include "../util/defer.h"

namespace FileStorage {
    inline void saveDataToFile(const std::string& filepath, const std::byte* content, std::size_t size) {
        std::ofstream file(filepath, std::ios::binary);

        if (!file) {
            throw std::runtime_error("Failed to open file");
        }

        file.write(reinterpret_cast<const char*>(content), size);

        if (!file) {
            throw std::runtime_error("Failed to write data");
        }
    }

    inline void saveDataToFileAtomically(const std::string& filepath, const std::byte* content, std::size_t size) {
        // to save data atomically, we need to follow few steps
        // 1. write data to a temporary file
        // 2. flush the data to disk before renaming it
        // 3. rename the temporary file to the original file
        // 4. remove temporary file


        static std::mt19937_64 gen(std::random_device{}());
        static std::uniform_int_distribution<std::uint64_t> dist;

        std::string temporary_filepath = filepath + ".tmp." + std::to_string(dist(gen));

        saveDataToFile(temporary_filepath, content, size);

        // flush the data to disk before renaming it
        std::ofstream file(temporary_filepath, std::ios::binary);
        file.flush();

        // if anything below throws or fails before we cancel, remove the temp file
        auto cleanup = util::make_defer([&]{ std::remove(temporary_filepath.c_str()); });

        if (std::rename(temporary_filepath.c_str(), filepath.c_str()) != 0) {
            throw std::runtime_error("Failed to rename temporary file");
        }

        cleanup.cancel();
    }
}
