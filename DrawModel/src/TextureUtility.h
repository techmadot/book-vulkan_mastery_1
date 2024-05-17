#pragma once

#include "GfxDevice.h"
#include <filesystem>

// ファイルからテクスチャを生成.
// テクスチャは GPU 転送済み、ミップマップ作成ありで生成される.
bool CreateTextureFromFile(GpuImage& outImage, std::filesystem::path filePath);

// メモリからテクスチャを生成.
// テクスチャは GPU 転送済み、ミップマップ作成ありで生成される.
bool CreateTextureFromMemory(GpuImage& outImage, const void* srcBuffer, size_t bufferSize);