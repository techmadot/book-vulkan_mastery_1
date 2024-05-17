#pragma once

#include <vector>
#include <array>
#include <string>

#include "BasePlatform.h"
#include "Window.h"
#include "GfxDevice.h"

// GLMで設定する値単位をラジアンに.
#define GLM_FORCE_RADIANS
// Vulkan ではClip空間 Z:[0,1]のため定義が必要.
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"
#include "glm/ext.hpp"

class Application
{
public:

  void Initialize();
  void Shutdown();

  void Process();

  bool IsInitialized() { return m_isInitialized; }

  void SurfaceSizeChanged();

#if defined(PLATFORM_ANDROID)
  void SetAndroidApp(void* android_app) { m_androidApp = android_app; }
#endif
private:
  void InitializeWindow();
  void InitializeGfxDevice();

  void BeginRender();
  void EndRender();

  void PrepareTriangle();
  void PrepareRenderPass();

  bool m_isInitialized = false;
#if defined(PLATFORM_ANDROID)
  void* m_androidApp = nullptr;
#endif
  struct Vertex
  {
    glm::vec3 position;
    glm::vec3 color;
  };
  struct VertexBuffer
  {
    VkBuffer buffer;
    VkDeviceMemory memory;
  } m_vertexBuffer;

  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_pipeline = VK_NULL_HANDLE;

  std::vector<VkFramebuffer> m_framebuffers;
  VkRenderPass m_renderPass;
};