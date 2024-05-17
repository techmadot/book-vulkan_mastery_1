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

  void PreparePipeline();
  void PrepareTessellationPlane();

  void PrepareSceneUniformBuffer();
  void DestroySceneUniformBuffer();

  bool m_isInitialized = false;
#if defined(PLATFORM_ANDROID)
  void* m_androidApp = nullptr;
#endif

  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_tessellationPipeline = VK_NULL_HANDLE;
  VkPipeline m_tessellationPipeline2 = VK_NULL_HANDLE;

  uint64_t m_frameCount = 0;

  // 描画する時に使用するディスクリプタセットレイアウト.
  VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;

  // ユニフォームバッファーに送るために1要素16バイトアライメントとった状態にしておく.
  struct SceneParameters
  {
    glm::mat4 matView;
    glm::mat4 matProj;
    glm::vec4 tessParams; // x: inner, y: outer
    float     time;
  };
 
  // テッセレーション分割レベル.
  float m_tessLevelInner = 32.0f;
  float m_tessLevelOuter = 16.0f;
  bool  m_useFillColor = false;
  bool  m_isSupportWireframe = true;

  std::vector<GpuBuffer> m_sceneUniformBuffers;
  struct DepthBuffer
  {
    VkFormat format;
    GpuImage depth;
  } m_depthBuffer;

  // テッセレーション平面用.
  GpuBuffer m_vertexBuffer;
  GpuBuffer m_indexBuffer;
  std::vector<VkDescriptorSet> m_descriptorSets;
};