#pragma once
#include <memory>
#include <vector>
#include <cstdint>
#include <filesystem>

class FileLoader
{
public:
  bool Load(std::filesystem::path filePath, std::vector<char>& fileData);
};

std::unique_ptr<FileLoader>& GetFileLoader();
