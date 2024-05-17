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

  void PrepareRenderPass();

  void PreparePipelines();
  void PrepareImageFilterResources();
  void DestroyImageFilterResources();

  void PrepareSceneUniformBuffer();
  void DestroySceneUniformBuffer();

  bool m_isInitialized = false;
#if defined(PLATFORM_ANDROID)
  void* m_androidApp = nullptr;
#endif

  // それぞれのパイプラインで使用するレイアウト情報.
  struct PipelineLayouts {
    VkPipelineLayout graphics = VK_NULL_HANDLE;
    VkPipelineLayout compute = VK_NULL_HANDLE;
  } m_pipelineLayouts;

  VkPipeline m_computePipeline = VK_NULL_HANDLE;
  VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;

  std::vector<VkFramebuffer> m_framebuffers;
  VkRenderPass m_renderPass;

  uint64_t m_frameCount = 0;

  // それぞれのパイプラインで使用するディスクリプタセットレイアウト情報.
  struct DescriptorSetLayouts {
    VkDescriptorSetLayout graphics = VK_NULL_HANDLE;
    VkDescriptorSetLayout compute = VK_NULL_HANDLE;
  } m_descriptorSetLayouts;

  // ユニフォームバッファーに送るために1要素16バイトアライメントとった状態にしておく.
  struct SceneParameters
  {
    glm::mat4 matView;
    glm::mat4 matProj;
    glm::vec4 modeParams; // x:mode, t:カラーシフト量.
  };

  struct Vertex
  {
    glm::vec3 positions;
    glm::vec2 uv0;
  };

  // ソース・デスティネーションの画像.
  GpuImage m_sourceImage;
  GpuImage m_destinationImage;

  std::vector<GpuBuffer> m_sceneUniformBuffers;
  GpuBuffer m_vertexBuffer;

  struct DescriptorSet
  {
    VkDescriptorSet compute = VK_NULL_HANDLE;  // 画像処理用.
    VkDescriptorSet drawSrc = VK_NULL_HANDLE;  // 表示用(元画像).
    VkDescriptorSet drawDst = VK_NULL_HANDLE;  // 表示用(結果).
  };
  std::vector<DescriptorSet> m_descriptorSets;
  VkSampler m_sampler = VK_NULL_HANDLE;

  int m_filterMode = 0;
  float m_hueShift = 0.0f;
};