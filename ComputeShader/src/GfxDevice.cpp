#include "GfxDevice.h"
#include <algorithm>
#include <cstring>
#include <cassert>

#include "Window.h"

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3.h"
#include "GLFW/glfw3native.h"
#endif

#if defined(PLATFORM_LINUX)
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include "GLFW/glfw3.h"
#include "GLFW/glfw3native.h"
#endif

#if defined(__ANDROID__)
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include "AndroidOut.h"
#endif

static std::unique_ptr<GfxDevice> gGfxDevice = nullptr;
static bool gUseValidation = false;

void CheckVkResult(VkResult res)
{
  assert(res == VK_SUCCESS);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessageUtilsCallback(
  VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  VkDebugUtilsMessageTypeFlagsEXT messageType,
  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
  void* pUserData)
{
    char buf[2048] = { 0 };
#if defined(PLATFORM_WINDOWS)
    sprintf_s(buf, "%s\n", pCallbackData->pMessage);
    OutputDebugStringA(buf);
#elif defined(PLATFORM_LINUX)
   

#endif

  return VK_FALSE;
}

std::unique_ptr<GfxDevice>& GetGfxDevice()
{
  if (gGfxDevice == nullptr)
  {
    gGfxDevice = std::make_unique<GfxDevice>();
#ifdef _DEBUG
    gUseValidation = true;
#endif
  }
  return gGfxDevice;
}

void GfxDevice::Initialize(const DeviceInitParams& initParams)
{
  // Vulkan APIを使用する前にVolkを初期化.
  volkInitialize();

  // VkInstance の初期化.
  InitVkInstance();

  // 物理デバイス(GPU) の初期化.
  InitPhysicalDevice();

  // VkDevice (論理デバイス) の初期化.
  InitVkDevice();

  // 描画出力先となるサーフェースの初期化.
  InitWindowSurface(initParams);

  // スワップチェインの初期化.
  RecreateSwapchain(m_width, m_height);

  // コマンドプールを作成.
  InitCommandPool();

  // ディスクリプタプールを作成.
  InitDescriptorPool();

  // 描画の際に必要になる同期プリミティブの初期化.
  InitSemaphores();

  // 描画のためのコマンドバッファやフェンスの初期化.
  InitCommandBuffers();
}

void GfxDevice::Shutdown()
{
  // デバイスがアイドルになるまで待機.
  WaitForIdle();

  if (m_vkDevice != VK_NULL_HANDLE)
  {
    // コマンドバッファやフェンスの破棄.
    DestroyCommandBuffers();

    // 同期プリミティブの破棄.
    DestroySemaphores();

    // ディスクリプタプールの破棄.
    DestroyDescriptorPool();

    // コマンドプールの破棄.
    DestroyCommandPool();

    // スワップチェインの破棄.
    DestroySwapchain();

    // VkDevice (論理デバイス) の終了・破棄.
    DestroyVkDevice();

  }
  // サーフェースの破棄.
  DestroyWindowSurface();

  if (m_debugMessenger != VK_NULL_HANDLE)
  {
    vkDestroyDebugUtilsMessengerEXT(m_vkInstance, m_debugMessenger, nullptr);
    m_debugMessenger = VK_NULL_HANDLE;
  }

  // VkInstance の終了・破棄.
  DestroyVkInstance();
}

void GfxDevice::NewFrame()
{
  auto& frameInfo = m_frameCommandInfos[m_currentFrameIndex];
  auto fence = frameInfo.commandFence;
  vkWaitForFences(m_vkDevice, 1, &fence, VK_TRUE, UINT64_MAX);
  auto res = vkAcquireNextImageKHR(m_vkDevice, m_swapchain, UINT64_MAX, frameInfo.presentCompleted, VK_NULL_HANDLE, &m_swapchainImageIndex);
  if (res == VK_ERROR_OUT_OF_DATE_KHR)
  {
    return;
  }
  vkResetFences(m_vkDevice, 1, &fence);

  // コマンドバッファを開始.
  vkResetCommandBuffer(frameInfo.commandBuffer, 0);
  VkCommandBufferBeginInfo commandBeginInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };
  vkBeginCommandBuffer(frameInfo.commandBuffer, &commandBeginInfo);
}

VkCommandBuffer GfxDevice::GetCurrentCommandBuffer()
{
  return m_frameCommandInfos[m_currentFrameIndex].commandBuffer;
}

void GfxDevice::Submit()
{
  auto& frameInfo = m_frameCommandInfos[m_currentFrameIndex];
  vkEndCommandBuffer(frameInfo.commandBuffer);

  // コマンドを発行する.
  VkPipelineStageFlags waitStage{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  VkSubmitInfo submitInfo{
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &frameInfo.presentCompleted,
    .pWaitDstStageMask = &waitStage,
    .commandBufferCount = 1,
    .pCommandBuffers = &frameInfo.commandBuffer,
    .signalSemaphoreCount = 1,
    .pSignalSemaphores = &frameInfo.renderCompleted,
  };
  vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, frameInfo.commandFence);

  m_currentFrameIndex = (++m_currentFrameIndex) % InflightFrames;

  // プレゼンテーションの実行.
  VkPresentInfoKHR presentInfo{
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &frameInfo.renderCompleted,
    .swapchainCount = 1,
    .pSwapchains = &m_swapchain,
    .pImageIndices = &m_swapchainImageIndex
  };
  vkQueuePresentKHR(m_graphicsQueue, &presentInfo);

}

void GfxDevice::WaitForIdle()
{
  if (m_vkDevice != VK_NULL_HANDLE)
  {
    vkDeviceWaitIdle(m_vkDevice);
  }
}

void GfxDevice::GetSwapchainResolution(int& width, int& height) const
{
  width = m_width;
  height = m_height;
}

VkImage GfxDevice::GetCurrentSwapchainImage()
{
  return m_swapchainState[m_swapchainImageIndex].image;
}

VkImageView GfxDevice::GetCurrentSwapchainImageView()
{
  return m_swapchainState[m_swapchainImageIndex].view;
}

VkSurfaceFormatKHR GfxDevice::GetSwapchainFormat() const
{
  return m_surfaceFormat;
}

uint32_t GfxDevice::GetSwapchainImageCount() const
{
  return uint32_t(m_swapchainState.size());
}

VkImageView GfxDevice::GetSwapchainImageView(int i) const
{
  return m_swapchainState[i].view;
}

void GfxDevice::TransitionLayoutSwapchainImage(VkCommandBuffer commandBuffer, VkImageLayout newLayout, VkAccessFlags2 newAccessFlags)
{
  auto& swapchainState = m_swapchainState[m_swapchainImageIndex];
  VkImageMemoryBarrier2 barrierToRT{
  .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
  .pNext = nullptr,
  .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
  .srcAccessMask = swapchainState.accessFlags,
  .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
  .dstAccessMask = newAccessFlags,
  .oldLayout = swapchainState.layout,
  .newLayout = newLayout,
  .image = swapchainState.image,
  .subresourceRange = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0, .levelCount = 1,
      .baseArrayLayer = 0, .layerCount = 1,
    }
  };

  VkDependencyInfo info{
    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
    .imageMemoryBarrierCount = 1,
    .pImageMemoryBarriers = &barrierToRT,
  };

  if (vkCmdPipelineBarrier2)
  {
    vkCmdPipelineBarrier2(commandBuffer, &info);
  }
  else
  {
    vkCmdPipelineBarrier2KHR(commandBuffer, &info);
  }
  swapchainState.layout = newLayout;
  swapchainState.accessFlags = newAccessFlags;
}

void GfxDevice::RecreateSwapchain(uint32_t width, uint32_t height)
{
#if defined(PLATFORM_WINDOWS)
  VkBool32 supported = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(m_vkPhysicalDevice, m_graphicsQueueIndex, m_windowSurface, &supported);
  assert(supported == VK_TRUE);
#endif
  m_width = width;
  m_height = height;

  // 能力情報の取得.
  uint32_t count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(m_vkPhysicalDevice, m_windowSurface, &count, nullptr);
  std::vector<VkPresentModeKHR> modes(count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(m_vkPhysicalDevice, m_windowSurface, &count, modes.data());

  VkSurfaceCapabilitiesKHR surfaceCaps{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_vkPhysicalDevice, m_windowSurface, &surfaceCaps);

  auto desirePresentMode = VK_PRESENT_MODE_FIFO_KHR;
  bool presentModeSupported = std::any_of(modes.begin(), modes.end(), [=](const auto& v) { return v == desirePresentMode; });
  assert(presentModeSupported);

  VkExtent2D extent = surfaceCaps.currentExtent;
  if (surfaceCaps.currentExtent.width == UINT32_MAX)
  {
    extent.width = width;
    extent.height = height;
  }

  // ダブルバッファリングを想定して2枚.
  uint32_t swapchainImageCount = 2;
#if defined(PLATFORM_ANDROID)
  // Android は一部の端末で SurfaceFlinger の処理で余計な時間が掛かるようなので 3枚.
  swapchainImageCount = 3;
#endif
  if (swapchainImageCount < surfaceCaps.minImageCount)
  {
    swapchainImageCount = surfaceCaps.minImageCount;
  }

  // デバイス状態の待機.
  WaitForIdle();

  VkSwapchainKHR oldSwapchain = m_swapchain;
  // スワップチェインの初期化.
  VkSwapchainCreateInfoKHR swapchainCI{
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = m_windowSurface,
    .minImageCount = swapchainImageCount,
    .imageFormat = m_surfaceFormat.format,
    .imageColorSpace = m_surfaceFormat.colorSpace,
    .imageExtent = extent,
    .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = desirePresentMode,
    .clipped = VK_TRUE,
    .oldSwapchain = oldSwapchain,
  };
#if defined(PLATFORM_ANDROID)
  swapchainCI.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
#endif

  CheckVkResult(vkCreateSwapchainKHR(m_vkDevice, &swapchainCI, nullptr, &m_swapchain));

  // スワップチェインのイメージ取得.
  count = 0;
  vkGetSwapchainImagesKHR(m_vkDevice, m_swapchain, &count, nullptr);
  std::vector<VkImage> swapchainImages(count);
  vkGetSwapchainImagesKHR(m_vkDevice, m_swapchain, &count, swapchainImages.data());

  // スワップチェインのイメージビューの準備.
  std::vector<SwapchainState> swapchainState(count);

  for (auto i = 0; auto & state : swapchainState)
  {
    auto image = swapchainImages[i];
    VkImageViewCreateInfo imageViewCI{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = m_surfaceFormat.format,
      .components = {
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
      },
      .subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
      }
    };
    vkCreateImageView(m_vkDevice, &imageViewCI, nullptr, &state.view);
    state.image = image;
    state.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    state.accessFlags = VK_ACCESS_2_NONE;
    ++i;
  }
  m_swapchainState.swap(swapchainState);

  // 古い方を破棄.
  if (oldSwapchain != VK_NULL_HANDLE)
  {
    for (auto& state : swapchainState)
    {
      vkDestroyImageView(m_vkDevice, state.view, nullptr);
    }
    vkDestroySwapchainKHR(m_vkDevice, oldSwapchain, nullptr);
  }

  // Present で使用できるデバイスキューの準備.
  count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysicalDevice, &count, nullptr);
  std::vector<VkBool32> supportPresent(count);
  for (uint32_t i = 0; i < count; ++i)
  {
    vkGetPhysicalDeviceSurfaceSupportKHR(m_vkPhysicalDevice, i, m_windowSurface, &supportPresent[i]);
  }
  // 今回はグラフィックスキューでPresentを実行するため確認.
  assert(supportPresent[m_graphicsQueueIndex] == VK_TRUE);

}

VkShaderModule GfxDevice::CreateShaderModule(const void* code, size_t length)
{
  VkShaderModuleCreateInfo ci{
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = length,
    .pCode = reinterpret_cast<const uint32_t*>(code),
  };
  VkShaderModule shaderModule = VK_NULL_HANDLE;
  vkCreateShaderModule(m_vkDevice, &ci, nullptr, &shaderModule);
  return shaderModule;
}

void GfxDevice::DestroyShaderModule(VkShaderModule shaderModule)
{
  vkDestroyShaderModule(m_vkDevice, shaderModule, nullptr);
}

GpuBuffer GfxDevice::CreateBuffer(VkDeviceSize byteSize, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags, const void* srcData)
{
  GpuBuffer retBuffer;
  bool useStaging = false;
  if (srcData != nullptr && (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
  {
    useStaging = true;
  }
  VkBufferCreateInfo bufferCI{
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = byteSize,
    .usage = usage,
  };
  if (useStaging)
  {
    bufferCI.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }
  vkCreateBuffer(m_vkDevice, &bufferCI, nullptr, &retBuffer.buffer);

  // メモリの要件
  VkMemoryRequirements reqs{};
  vkGetBufferMemoryRequirements(m_vkDevice, retBuffer.buffer, &reqs);

  // メモリプロパティを指定して、確保用パラメータを決定.
  VkMemoryAllocateInfo memoryAI{
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = reqs.size,
    .memoryTypeIndex = GetMemoryTypeIndex(reqs, flags),
  };
  vkAllocateMemory(m_vkDevice, &memoryAI, nullptr, &retBuffer.memory);
  vkBindBufferMemory(m_vkDevice, retBuffer.buffer, retBuffer.memory, 0);

  if (srcData != nullptr)
  {
    if (!useStaging)
    {
      // 直接書込み可.
      vkMapMemory(m_vkDevice, retBuffer.memory, 0, VK_WHOLE_SIZE, 0, &retBuffer.mapped);
      memcpy(retBuffer.mapped, srcData, byteSize);
    }
    else
    {
      // ステージングバッファ経由での書込み、転送.
      bufferCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      VkBuffer srcBuffer;
      vkCreateBuffer(m_vkDevice, &bufferCI, nullptr, &srcBuffer);

      vkGetBufferMemoryRequirements(m_vkDevice, srcBuffer, &reqs);

      VkDeviceMemory srcMemory;
      memoryAI.memoryTypeIndex = GetMemoryTypeIndex(reqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
      vkAllocateMemory(m_vkDevice, &memoryAI, nullptr, &srcMemory);
      vkBindBufferMemory(m_vkDevice, srcBuffer, srcMemory, 0);

      void* p;
      vkMapMemory(m_vkDevice, srcMemory, 0, VK_WHOLE_SIZE, 0, &p);
      if (p)
      {
        memcpy(p, srcData, byteSize);
        VkMappedMemoryRange memRange{
          .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
          .memory = srcMemory,
          .offset = 0,
          .size = memoryAI.allocationSize,
        };
        vkFlushMappedMemoryRanges(m_vkDevice, 1, &memRange);
      }

      // Staging -> GPU への転送.
      VkBufferCopy copyRegion{
        .srcOffset = 0,
        .dstOffset = 0,
        .size = byteSize
      };
      auto commandBuffer = AllocateCommandBuffer();
      vkCmdCopyBuffer(commandBuffer, srcBuffer, retBuffer.buffer, 1, &copyRegion);
      SubmitOneShot(commandBuffer);
      
      // ステージングバッファの廃棄.
      vkDestroyBuffer(m_vkDevice, srcBuffer, nullptr);
      vkFreeMemory(m_vkDevice, srcMemory, nullptr);
    }
  }

  // ホストで見えるメモリが指定されているとき、マップしておく.
  if (flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
  {
    vkMapMemory(m_vkDevice, retBuffer.memory, 0, VK_WHOLE_SIZE, 0, &retBuffer.mapped);
  }
  return retBuffer;
}

void GfxDevice::DestroyBuffer(GpuBuffer& buffer)
{
  vkDestroyBuffer(m_vkDevice, buffer.buffer, nullptr);
  vkFreeMemory(m_vkDevice, buffer.memory, nullptr);
  buffer.buffer = VK_NULL_HANDLE;
  buffer.memory = VK_NULL_HANDLE;
  buffer.mapped = nullptr;
}

GpuImage GfxDevice::CreateImage2D(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkMemoryPropertyFlags flags, uint32_t mipmapCount)
{
  GpuImage retImage;
  VkImageCreateInfo imageCI{
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = format,
    .extent = {.width = uint32_t(width), .height = uint32_t(height), .depth = 1, },
    .mipLevels = mipmapCount,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = usage,
  };
  if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  {
    imageCI.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }

  auto res = vkCreateImage(m_vkDevice, &imageCI, nullptr, &retImage.image);
  assert(res == VK_SUCCESS);

  // メモリの要件を取得.
  VkMemoryRequirements reqs{};
  vkGetImageMemoryRequirements(m_vkDevice, retImage.image, &reqs);

  // メモリプロパティを指定して、確保用パラメータを決定.
  VkMemoryAllocateInfo memoryAI{
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext = nullptr,
    .allocationSize = reqs.size,
    .memoryTypeIndex = GetMemoryTypeIndex(reqs, flags),
  };
  // メモリの確保.
  vkAllocateMemory(m_vkDevice, &memoryAI, nullptr, &retImage.memory);

  // メモリのバインド.
  vkBindImageMemory(m_vkDevice, retImage.image, retImage.memory, 0);

  // ビューの用意.
  VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
  switch (format)
  {
    default:
      aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
      break;
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
      aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
      break;
  }
  VkImageViewCreateInfo viewCI{
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .image = retImage.image,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = imageCI.format,
    .components = {
      VK_COMPONENT_SWIZZLE_IDENTITY,
      VK_COMPONENT_SWIZZLE_IDENTITY,
      VK_COMPONENT_SWIZZLE_IDENTITY,
      VK_COMPONENT_SWIZZLE_IDENTITY,
    },
    .subresourceRange = {
       .aspectMask = aspectFlags,
       .baseMipLevel = 0,
       .levelCount = imageCI.mipLevels,
       .baseArrayLayer = 0,
       .layerCount = 1,
    },
  };
  res = vkCreateImageView(m_vkDevice, &viewCI, nullptr, &retImage.view);
  assert(res == VK_SUCCESS);

  retImage.format = format;
  retImage.mipmapCount = mipmapCount;
  retImage.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  retImage.accessFlags = VK_ACCESS_2_NONE;
  retImage.extent = imageCI.extent;

  return retImage;
}

void GfxDevice::DestroyImage(GpuImage image)
{
  vkDestroyImage(m_vkDevice, image.image, nullptr);
  vkDestroyImageView(m_vkDevice, image.view, nullptr);
  vkFreeMemory(m_vkDevice, image.memory, nullptr);
}

uint32_t GfxDevice::GetGraphicsQueueFamily() const
{
  return m_graphicsQueueIndex;
}

VkQueue GfxDevice::GetGraphicsQueue() const
{
  return m_graphicsQueue;
}

VkDescriptorPool GfxDevice::GetDescriptorPool() const
{
  return m_descriptorPool;
}

void GfxDevice::SubmitOneShot(VkCommandBuffer commandBuffer)
{
  vkEndCommandBuffer(commandBuffer);

  VkFenceCreateInfo fenceCI{
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
  };
  VkFence waitFence;
  vkCreateFence(m_vkDevice, &fenceCI, nullptr, &waitFence);

  VkSubmitInfo submitInfo{
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .commandBufferCount = 1,
    .pCommandBuffers = &commandBuffer,
  };
  vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, waitFence);

  // 実行完了を待機して、廃棄処理.
  vkWaitForFences(m_vkDevice, 1, &waitFence, VK_TRUE, UINT64_MAX);
  vkDestroyFence(m_vkDevice, waitFence, nullptr);
  vkFreeCommandBuffers(m_vkDevice, m_commandPool, 1, &commandBuffer);
}

uint32_t GfxDevice::GetMemoryTypeIndex(VkMemoryRequirements reqs, VkMemoryPropertyFlags memoryPropFlags)
{
  auto requestBits = reqs.memoryTypeBits;
  for (uint32_t i = 0; i < m_physDevMemoryProps.memoryTypeCount; ++i)
  {
    if (requestBits & 1)
    {
      // 要求されたメモリプロパティと一致するものを見つける.
      const auto types = m_physDevMemoryProps.memoryTypes[i];
      if ((types.propertyFlags & memoryPropFlags) == memoryPropFlags)
      {
        return i;
      }
    }
    requestBits >>= 1;
  }
  return UINT32_MAX;

}

bool GfxDevice::IsSupportVulkan13()
{
  VkPhysicalDeviceProperties props{};
  vkGetPhysicalDeviceProperties(m_vkPhysicalDevice, &props);

  if (props.apiVersion < VK_API_VERSION_1_3)
  {
    return false;
  }
  return true;
}

void GfxDevice::SetObjectName(uint64_t handle, const char* name, VkObjectType type)
{
  VkDebugUtilsObjectNameInfoEXT nameInfo{
    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
    .objectType = type,
    .objectHandle = uint64_t(handle),
    .pObjectName = name,
  };
  vkSetDebugUtilsObjectNameEXT(m_vkDevice, &nameInfo);
}

void GfxDevice::InitVkInstance()
{
  const char* appName = nullptr;
  VkApplicationInfo appInfo{
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName = appName,
    .pEngineName = appName,
    .engineVersion = VK_MAKE_VERSION(1, 0, 0),
    .apiVersion = VK_API_VERSION_1_3,
  };
#if 0
  // Raspberry Pi 4で動かす場合にはこちらに設定する.
  appInfo.apiVersion = VK_API_VERSION_1_1;
#endif

  VkInstanceCreateInfo ci{
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &appInfo
  };

  // Instance レベルの拡張機能やレイヤーを設定.
  std::vector<const char*> layers;
  std::vector<const char*> extensions;

#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
  //   VK_KHR_SURFACE_EXTENSION_NAME
  //   VK_KHR_WIN32_SURFACE_EXTENSION_NAME / VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME
  uint32_t glfwRequiredCount = 0;
  const char** glfwExtensionNames = glfwGetRequiredInstanceExtensions(&glfwRequiredCount);
  std::for_each_n(
    glfwExtensionNames,
    glfwRequiredCount,
    [&](auto v) { extensions.push_back(v); });
#elif defined(PLATFORM_ANDROID)
  // Android の場合には glfw を使わないので直接追加.
  extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
  extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);

  gUseValidation = false;
#endif
  // Vulkan 1.3 使えない環境向け.
  extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

  // 検証レイヤーの有効化 (バリデーションレイヤー),デバッグUtil
  if (gUseValidation)
  {
    layers.push_back("VK_LAYER_KHRONOS_validation");
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  ci.enabledExtensionCount = uint32_t(extensions.size());
  ci.ppEnabledExtensionNames = extensions.data();
  ci.enabledLayerCount = uint32_t(layers.size());
  ci.ppEnabledLayerNames = layers.data();
  CheckVkResult(vkCreateInstance(&ci, nullptr, &m_vkInstance));

  // Volkに生成した VkInstanceを渡して初期化.
  volkLoadInstance(m_vkInstance);


  if (gUseValidation)
  {
    VkDebugUtilsMessengerCreateInfoEXT utilMsgCreateInfo{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
      .pfnUserCallback = DebugMessageUtilsCallback,
      .pUserData = nullptr,
    };
    vkCreateDebugUtilsMessengerEXT(m_vkInstance, &utilMsgCreateInfo, nullptr, &m_debugMessenger);
  }
}


void GfxDevice::InitPhysicalDevice()
{
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(m_vkInstance, &count, nullptr);
  std::vector<VkPhysicalDevice> physDevs(count);
  vkEnumeratePhysicalDevices(m_vkInstance, &count, physDevs.data());

  // 最初に見つかったものを使用する.
  m_vkPhysicalDevice = physDevs[0];

  // メモリ情報を取得しておく.
  vkGetPhysicalDeviceMemoryProperties(m_vkPhysicalDevice, &m_physDevMemoryProps);

  // グラフィックス用のキューインデックスを調査.
  vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysicalDevice, &count, nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilyProps(count);
  vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysicalDevice, &count, queueFamilyProps.data());

  uint32_t gfxQueueIndex = ~0u;
  for (uint32_t i = 0; const auto & props : queueFamilyProps)
  {
    if (props.queueFlags & VK_QUEUE_GRAPHICS_BIT)
    {
      gfxQueueIndex = i;
      break;
    }
    ++i;
  }
  assert(gfxQueueIndex != ~0u);
  m_graphicsQueueIndex = gfxQueueIndex;
}

void GfxDevice::InitVkDevice()
{
  std::vector<const char*> extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,  // スワップチェインは常に使用.
    // ---- 以下 Vulkan 1.3 使えない環境向け ---
    VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
  };

  // 対象のGPUが使える機能を取得し、使えるものは有効状態でデバイスを生成.
  // ここでは各バージョンレベルの機能について取得するため各構造体をリンク.
  VkPhysicalDeviceFeatures2 physFeatures2{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
  VkPhysicalDeviceVulkan11Features vulkan11Features{
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES
  };
  VkPhysicalDeviceVulkan12Features vulkan12Features{
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
  };
  VkPhysicalDeviceVulkan13Features vulkan13Features{
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
  };
  physFeatures2.pNext = &vulkan11Features;
  vulkan11Features.pNext = &vulkan12Features;
  vulkan12Features.pNext = &vulkan13Features;
  vkGetPhysicalDeviceFeatures2(m_vkPhysicalDevice, &physFeatures2);

  // 有効にする機能を明示的にセット.
  vulkan13Features.dynamicRendering = VK_TRUE;
  vulkan13Features.synchronization2 = VK_TRUE;
  vulkan13Features.maintenance4 = VK_TRUE;

  vulkan12Features.descriptorIndexing = VK_FALSE;

  if (!IsSupportVulkan13())
  {
    // 下記を諦める.
    vulkan13Features.dynamicRendering = VK_FALSE;
  }
  // VkDeviceの生成.
  const float queuePriorities[] = { 1.0f };
  VkDeviceQueueCreateInfo deviceQueueCI{
    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueFamilyIndex = m_graphicsQueueIndex,
    .queueCount = 1,
    .pQueuePriorities = queuePriorities,
  };
  VkDeviceCreateInfo deviceCI{
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &deviceQueueCI,
    .enabledExtensionCount = uint32_t(extensions.size()),
    .ppEnabledExtensionNames = extensions.data(),
  };
  // VkPhysicalDeviceFeatures2に有効にする機能情報を入れているので、pNextに設定.
  deviceCI.pNext = &physFeatures2;

  CheckVkResult(vkCreateDevice(m_vkPhysicalDevice, &deviceCI, nullptr, &m_vkDevice));

  // Volk に VkDevice を渡して初期化.
  volkLoadDevice(m_vkDevice);

  // デバイスキューを取得.
  vkGetDeviceQueue(m_vkDevice, m_graphicsQueueIndex, 0, &m_graphicsQueue);
}

void GfxDevice::InitWindowSurface(const DeviceInitParams& initParams)
{
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
  GLFWwindow* window = reinterpret_cast<GLFWwindow*>(initParams.glfwWindow);
#endif

#if defined(PLATFORM_WINDOWS)
  HINSTANCE hInstance = GetModuleHandleW(NULL);
  VkWin32SurfaceCreateInfoKHR surfaceCI{
    .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
    .hinstance = hInstance,
    .hwnd = glfwGetWin32Window(window),
  };
  CheckVkResult(vkCreateWin32SurfaceKHR(m_vkInstance, &surfaceCI, nullptr, &m_windowSurface));

  int width, height;
  glfwGetWindowSize(window, &width, &height);
  m_width = width;
  m_height = height;
#endif

#if defined(PLATFORM_LINUX)
  VkWaylandSurfaceCreateInfoKHR  surfaceCI{
    .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
    .display = glfwGetWaylandDisplay(),
    .surface = glfwGetWaylandWindow(window),
  };
  CheckVkResult(vkCreateWaylandSurfaceKHR(m_vkInstance, &surfaceCI, nullptr, &m_windowSurface));

  int width, height;
  glfwGetWindowSize(window, &width, &height);
  m_width = width;
  m_height = height;
#endif


#if defined(PLATFORM_ANDROID)
  ANativeWindow* window = reinterpret_cast<ANativeWindow*>(initParams.window);
  VkAndroidSurfaceCreateInfoKHR asurfaceCI{
    .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
    .window = window,
  };
  vkCreateAndroidSurfaceKHR(m_vkInstance, &asurfaceCI, nullptr, &m_windowSurface);
#endif
  GetAppWindow()->GetWindowSize(m_width, m_height);

  // フォーマットの選択.
  uint32_t count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(m_vkPhysicalDevice, m_windowSurface, &count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(m_vkPhysicalDevice, m_windowSurface, &count, formats.data());

  const VkFormat desireFormats[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
  m_surfaceFormat.format = VK_FORMAT_UNDEFINED;
  bool found = false;
  for (int i = 0; i < std::size(desireFormats) && !found; ++i)
  {
    auto format = desireFormats[i];
    for (const auto& f : formats)
    {
      if (f.format == format && f.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR)
      {
        m_surfaceFormat = f;
        found = true;
        break;
      }
    }
  }
  assert(found);
}

void GfxDevice::InitCommandPool()
{
  VkCommandPoolCreateInfo commandPoolCI{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = m_graphicsQueueIndex,
  };
  CheckVkResult(vkCreateCommandPool(m_vkDevice, &commandPoolCI, nullptr, &m_commandPool));
}

void GfxDevice::InitSemaphores()
{
  VkSemaphoreCreateInfo semCI{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };
  for (auto& frame : m_frameCommandInfos)
  {
    vkCreateSemaphore(m_vkDevice, &semCI, nullptr, &frame.renderCompleted);
    vkCreateSemaphore(m_vkDevice, &semCI, nullptr, &frame.presentCompleted);
  }
}

void GfxDevice::InitCommandBuffers()
{
  VkFenceCreateInfo fenceCI{
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    .flags = VK_FENCE_CREATE_SIGNALED_BIT,
  };
  for (auto& frame : m_frameCommandInfos)
  {
    vkCreateFence(m_vkDevice, &fenceCI, nullptr, &frame.commandFence);
  }

  VkCommandBufferAllocateInfo commandAI{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = m_commandPool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1,
  };
  for (auto& frame : m_frameCommandInfos)
  {
    vkAllocateCommandBuffers(m_vkDevice, &commandAI, &frame.commandBuffer);
  }
}

void GfxDevice::InitDescriptorPool()
{
  const uint32_t count = 10000;
  std::vector<VkDescriptorPoolSize> poolSizes = { {
    {
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = count,
    },
    {
      .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .descriptorCount = count,
    },
    {
      .type = VK_DESCRIPTOR_TYPE_SAMPLER,
      .descriptorCount = count,
    },
    {
      .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = count,
    },
    {
      .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .descriptorCount = count,
    },
  } };
  VkDescriptorPoolCreateInfo descriptorPoolCI{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
    .maxSets = count,
    .poolSizeCount = uint32_t(poolSizes.size()),
    .pPoolSizes = poolSizes.data(),
  };
  vkCreateDescriptorPool(m_vkDevice, &descriptorPoolCI, nullptr, &m_descriptorPool);
}

void GfxDevice::DestroyVkDevice()
{
  vkDestroyDevice(m_vkDevice, nullptr);
  m_vkDevice = VK_NULL_HANDLE;
}

void GfxDevice::DestroyWindowSurface()
{
  if (m_vkInstance != VK_NULL_HANDLE)
  {
    vkDestroySurfaceKHR(m_vkInstance, m_windowSurface, nullptr);
  }
  m_windowSurface = VK_NULL_HANDLE;
}

void GfxDevice::DestroySwapchain()
{
  for (auto& state : m_swapchainState)
  {
    vkDestroyImageView(m_vkDevice, state.view, nullptr);
  }
  // スワップチェインから取得したイメージについては廃棄処理は不要.
  m_swapchainState.clear();
  vkDestroySwapchainKHR(m_vkDevice, m_swapchain, nullptr);
  m_swapchain = VK_NULL_HANDLE;
}

void GfxDevice::DestroyCommandPool()
{
  vkDestroyCommandPool(m_vkDevice, m_commandPool, nullptr);
  m_commandPool = VK_NULL_HANDLE;
}


void GfxDevice::DestroySemaphores()
{
  for (auto& frame : m_frameCommandInfos)
  {
    vkDestroySemaphore(m_vkDevice, frame.renderCompleted, nullptr);
    vkDestroySemaphore(m_vkDevice, frame.presentCompleted, nullptr);
    frame.renderCompleted = VK_NULL_HANDLE;
    frame.presentCompleted = VK_NULL_HANDLE;
  }
}

void GfxDevice::DestroyCommandBuffers()
{
  for (auto& f : m_frameCommandInfos)
  {
    vkDestroyFence(m_vkDevice, f.commandFence, nullptr);
    vkFreeCommandBuffers(m_vkDevice, m_commandPool, 1, &f.commandBuffer);
    f.commandFence = VK_NULL_HANDLE;
    f.commandBuffer = VK_NULL_HANDLE;
  }

}

void GfxDevice::DestroyDescriptorPool()
{
  vkDestroyDescriptorPool(m_vkDevice, m_descriptorPool, nullptr);
  m_descriptorPool = VK_NULL_HANDLE;
}

VkCommandBuffer GfxDevice::AllocateCommandBuffer()
{
  VkCommandBufferAllocateInfo allocInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = m_commandPool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1,
  };
  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(m_vkDevice, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };
  vkBeginCommandBuffer(commandBuffer, &beginInfo);
  return commandBuffer;
}

void GfxDevice::DestroyVkInstance()
{
  vkDestroyInstance(m_vkInstance, nullptr);
  m_vkInstance = VK_NULL_HANDLE;
}
