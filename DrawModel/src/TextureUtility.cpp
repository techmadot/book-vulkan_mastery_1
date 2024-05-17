#include "TextureUtility.h"
#include "GfxDevice.h"
#include "FileLoader.h"

#include "stb_image.h"
#include "stb_image_resize.h"

#include <cstring>
#include <cassert>
#include <cmath>

#ifdef max
#undef max
#endif

bool CreateTextureFromFile(GpuImage& outImage, std::filesystem::path filePath)
{
  std::vector<char> fileData;

  if (!GetFileLoader()->Load(filePath, fileData))
  {
    return false;
  }
  return CreateTextureFromMemory(outImage, fileData.data(), fileData.size());
}

bool CreateTextureFromMemory(GpuImage& outImage, const void* srcBuffer, size_t bufferSize)
{
  using namespace std;

  auto buffer = reinterpret_cast<const stbi_uc*>(srcBuffer);
  int imageWidth, imageHeight, comp;
  int result = stbi_info_from_memory(buffer, int(bufferSize), &imageWidth, &imageHeight, &comp);

  if (result == 0)
  {
    // 失敗.
    return false;
  }
  assert(imageWidth != 0 && imageHeight != 0);

  auto& gfxDevice = GetGfxDevice();
  // 完全なミップマップを生成したときの段数を求める.
  auto mipmapCount = uint32_t(floor(log2(max(imageWidth, imageHeight))) + 1);

  // テクスチャを生成する.
  outImage = gfxDevice->CreateImage2D(
    uint32_t(imageWidth), uint32_t(imageHeight), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mipmapCount);

  // RGBA各8bitのフォーマットをここでは対象とする.
  int channels = 4;
  auto srcImage = stbi_load_from_memory(buffer, int(bufferSize), &imageWidth, &imageHeight, nullptr, channels);

  const int pixelBytes = sizeof(uint32_t);
  int width = imageWidth, height = imageHeight;
  unsigned char* resizeSrc = srcImage;
  int mipLevel = 0;
  size_t totalBufferSize = imageWidth * imageHeight * pixelBytes;
  std::vector<unsigned char*> workImages;
  while (width > 1 && height > 1)
  {
    int mipWidth = std::max(1, width / 2);
    int mipHeight = std::max(1, height / 2);
    int surfaceByteSize = mipWidth * mipHeight * pixelBytes;
    unsigned char* dstImage = new unsigned char[surfaceByteSize];
    workImages.push_back(dstImage);

    stbir_resize_uint8(resizeSrc, width, height, 0, dstImage, mipWidth, mipHeight, 0, channels);

    resizeSrc = dstImage;
    width = mipWidth;
    height = mipHeight;
    totalBufferSize += surfaceByteSize;
  }

  // GPU転送元のステージングバッファを用意する.
  auto stagingBuffer = gfxDevice->CreateBuffer(
    totalBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, nullptr);
  auto writePtr = reinterpret_cast<unsigned char*>(stagingBuffer.mapped);
  uint64_t offset = 0;
  std::vector<VkBufferImageCopy> imageCopyInfos;
  for (uint32_t mipmap = 0; mipmap < mipmapCount; ++mipmap)
  {
    width = std::max(1, imageWidth >> mipmap );
    height = std::max(1, imageHeight >> mipmap );

    // 転送コマンド発行用に情報を記録しておく.
    auto& info = imageCopyInfos.emplace_back();
    info.bufferOffset = offset;
    info.bufferRowLength = width;
    info.bufferImageHeight = height;
    info.imageSubresource = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .mipLevel = mipmap,
      .baseArrayLayer = 0,
      .layerCount = 1,
    };
    info.imageOffset = { .x = 0, .y = 0, .z = 0 };
    info.imageExtent = {
      .width = uint32_t(width),
      .height = uint32_t(height),
      .depth  = 1,
    };
    
    // ステージングバッファ内へ画像情報を記録.
    int surfaceByteSize = width * height * 4;
    if (mipmap == 0)
    {
      memcpy(writePtr + offset, srcImage, surfaceByteSize);
    } else
    {
      memcpy(writePtr + offset, workImages[mipmap-1], surfaceByteSize);
    }
    totalBufferSize -= surfaceByteSize;
    offset += surfaceByteSize;
  }
  VkMappedMemoryRange memRange{
    .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
    .memory = stagingBuffer.memory,
    .offset = 0,
    .size = VK_WHOLE_SIZE,
  };
  vkFlushMappedMemoryRanges(gfxDevice->GetVkDevice(), 1, &memRange);

  // GPUへ転送処理.
  auto commandBuffer = gfxDevice->AllocateCommandBuffer();

  // 転送先となるテクスチャのバリア設定.
  VkImageMemoryBarrier2 barrierInfo{
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
    .pNext = nullptr,
    .srcStageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
    .srcAccessMask = VK_ACCESS_NONE,
    .dstStageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
    .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    .image = outImage.image,
    .subresourceRange = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0, .levelCount = uint32_t(mipmapCount),
      .baseArrayLayer = 0, .layerCount = 1,
    }
  };
  VkDependencyInfo dependencyInfo{
    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    .imageMemoryBarrierCount = 1,
    .pImageMemoryBarriers = &barrierInfo,
  };

  if (gfxDevice->IsSupportVulkan13())
  {
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
  }
  else
  {
    vkCmdPipelineBarrier2KHR(commandBuffer, &dependencyInfo);
  }

  vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.buffer, outImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
    uint32_t(imageCopyInfos.size()), imageCopyInfos.data());

  // テクスチャとして使えるように後バリアを設定.
  barrierInfo.oldLayout = barrierInfo.newLayout;
  barrierInfo.srcAccessMask = barrierInfo.dstAccessMask;
  barrierInfo.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrierInfo.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

  if (gfxDevice->IsSupportVulkan13())
  {
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
  }
  else
  {
    vkCmdPipelineBarrier2KHR(commandBuffer, &dependencyInfo);
  }

  gfxDevice->SubmitOneShot(commandBuffer);

  outImage.accessFlags = barrierInfo.dstAccessMask;
  outImage.layout = barrierInfo.newLayout;

  // ホスト側メモリの後始末.
  stbi_image_free(srcImage);
  for (auto& v : workImages)
  {
    delete[] v;
  }

  gfxDevice->DestroyBuffer(stagingBuffer);

  return true;
}

