#include "BasePlatform.h"
#include "FileLoader.h"
#include "Window.h"

#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
#include <fstream>
#endif

#if defined(PLATFORM_ANDROID)
#	include <android/asset_manager.h>
# include <game-activity/native_app_glue/android_native_app_glue.h>
#endif

static std::unique_ptr<FileLoader> gFileLoader = nullptr;

std::unique_ptr<FileLoader>& GetFileLoader()
{
  if (gFileLoader == nullptr)
  {
    gFileLoader = std::make_unique<FileLoader>();
  }
  return gFileLoader;
}


#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
bool FileLoader::Load(std::filesystem::path filePath, std::vector<char>& fileData)
{
  if (std::filesystem::exists(filePath))
  {
    std::ifstream infile(filePath, std::ios::binary);
    if (infile)
    {
      auto size = infile.seekg(0, std::ios::end).tellg();
      fileData.resize(size);
      infile.seekg(0, std::ios::beg).read(fileData.data(), size);
      return true;
    }
  }
  filePath = std::filesystem::path("../") / filePath;
  if (std::filesystem::exists(filePath))
  {
    std::ifstream infile(filePath, std::ios::binary);
    if (infile)
    {
      auto size = infile.seekg(0, std::ios::end).tellg();
      fileData.resize(size);
      infile.seekg(0, std::ios::beg).read(fileData.data(), size);
      return true;
    }
  }
  return false;
}
#endif

#if defined(PLATFORM_ANDROID)
bool FileLoader::Load(std::filesystem::path filePath, std::vector<char>& fileData)
{
  auto& window = GetAppWindow();
  auto androidApp = reinterpret_cast<android_app*>(window->GetPlatformHandle()->androidApp);
  
  AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filePath.c_str(), AASSET_MODE_BUFFER);
  if (asset)
  {
    auto fileSize = AAsset_getLength(asset);
    fileData.resize(fileSize);
    AAsset_read(asset, fileData.data(), fileSize);
    AAsset_close(asset);
  }
  return asset != nullptr;
}
#endif





