﻿#pragma once
#include <memory>
#include <vector>
#include <string>

#include "BasePlatform.h"

#if defined(PLATFORM_WINDOWS)
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(PLATFORM_LINUX)
#define VK_USE_PLATFORM_WAYLAND_KHR
#elif defined(PLATFORM_ANDROID)
# define VK_USE_PLATFORM_ANDROID_KHR
#endif

#include <Volk/volk.h>
#include <vulkan/vulkan.h>

#if defined(__ANDROID__)
# include <vulkan/vulkan_android.h>
#endif

struct GpuBuffer
{
  VkBuffer buffer;
  VkDeviceMemory memory;

  void* mapped = nullptr;
};

struct GpuImage
{
  VkImage image;
  VkDeviceMemory memory;
  VkImageView view;
  VkFormat format;
  int mipmapCount;

  VkExtent3D extent;
  VkAccessFlags2 accessFlags = VK_ACCESS_2_NONE;
  VkImageLayout  layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

class GfxDevice
{
public:
  struct DeviceInitParams
  {
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
    void* glfwWindow;
#elif defined(PLATFORM_ANDROID)
    void* window; // ANativeWindow*
#endif
  };

  void Initialize(const DeviceInitParams& initParams);
  void Shutdown();

  VkInstance GetVkInstance() const { return m_vkInstance; }
  VkPhysicalDevice GetVkPhysicalDevice() const { return m_vkPhysicalDevice; }
  VkDevice GetVkDevice() const { return m_vkDevice; }

  uint32_t GetFrameIndex() const { return m_currentFrameIndex; }

  void NewFrame();
  VkCommandBuffer GetCurrentCommandBuffer();

  void Submit();
  void WaitForIdle();

  void GetSwapchainResolution(int& width, int& height) const;

  VkImage GetCurrentSwapchainImage();
  VkImageView GetCurrentSwapchainImageView();
  VkSurfaceFormatKHR GetSwapchainFormat() const;
  uint32_t GetSwapchainImageCount() const;
  VkImageView GetSwapchainImageView(int i) const;
  uint32_t GetSwapchainImageIndex() const { return m_swapchainImageIndex; }

  void TransitionLayoutSwapchainImage(VkCommandBuffer commandBuffer, VkImageLayout newLayout, VkAccessFlags2 newAccessFlag);
  void RecreateSwapchain(uint32_t width, uint32_t height);

  VkShaderModule CreateShaderModule(const void* code, size_t length);
  void DestroyShaderModule(VkShaderModule shaderModule);

  static const int InflightFrames = 2;

  // GPU上にバッファを確保する.
  //  srcData に元データのポインタが設定される場合その内容をバッファメモリに書き込む.
  //  DeviceLocal なメモリを要求する場合、ステージングバッファに書き込んだあと、転送も行う.
  GpuBuffer CreateBuffer(VkDeviceSize byteSize, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags, const void* srcData = nullptr);
  void DestroyBuffer(GpuBuffer& buffer);

  // GPU上にイメージ(テクスチャ)を確保する.
  GpuImage CreateImage2D(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkMemoryPropertyFlags flags, uint32_t mipmapCount);
  void DestroyImage(GpuImage image);

  uint32_t GetGraphicsQueueFamily() const;
  VkQueue GetGraphicsQueue() const;
  VkDescriptorPool GetDescriptorPool() const;

  // コマンドバッファを新規に確保する.
  VkCommandBuffer AllocateCommandBuffer();

  // コマンドバッファを実行する. (ワンショット実行用)
  void SubmitOneShot(VkCommandBuffer commandBuffer);

  uint32_t GetMemoryTypeIndex(VkMemoryRequirements reqs, VkMemoryPropertyFlags memoryPropFlags);
  bool IsSupportVulkan13();
  
  void SetObjectName(uint64_t handle, const char* name, VkObjectType type);
private:
  void InitVkInstance();
  void InitPhysicalDevice();
  void InitVkDevice();
  void InitWindowSurface(const DeviceInitParams& initParams);
  void InitCommandPool();
  void InitSemaphores();
  void InitCommandBuffers();
  void InitDescriptorPool();

  void DestroyVkInstance();
  void DestroyVkDevice();
  void DestroyWindowSurface();
  void DestroySwapchain();
  void DestroyCommandPool();
  void DestroySemaphores();
  void DestroyCommandBuffers();
  void DestroyDescriptorPool();

  VkInstance m_vkInstance = VK_NULL_HANDLE;
  VkPhysicalDevice m_vkPhysicalDevice = VK_NULL_HANDLE;
  VkDevice m_vkDevice = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties m_physDevMemoryProps;

  VkSurfaceKHR m_windowSurface = VK_NULL_HANDLE;
  VkSurfaceFormatKHR m_surfaceFormat{};

  int32_t m_width, m_height;
  struct SwapchainState
  {
    VkImage image = VK_NULL_HANDLE;
    VkImageView  view = VK_NULL_HANDLE;
    VkAccessFlags2 accessFlags = VK_ACCESS_2_NONE;
    VkImageLayout  layout = VK_IMAGE_LAYOUT_UNDEFINED;
  };
  VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
  std::vector<SwapchainState> m_swapchainState;

  // キューインデックス.
  uint32_t m_graphicsQueueIndex;
  VkQueue  m_graphicsQueue;

  VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;

  VkCommandPool m_commandPool = VK_NULL_HANDLE;
  VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
  uint32_t m_currentFrameIndex = 0;
  uint32_t m_swapchainImageIndex = 0;

  struct FrameInfo
  {
    VkFence commandFence = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    // 描画完了・Present完了待機のためのセマフォ.
    VkSemaphore renderCompleted = VK_NULL_HANDLE;
    VkSemaphore presentCompleted = VK_NULL_HANDLE;
  };
  FrameInfo  m_frameCommandInfos[InflightFrames];
};

std::unique_ptr<GfxDevice>& GetGfxDevice();
