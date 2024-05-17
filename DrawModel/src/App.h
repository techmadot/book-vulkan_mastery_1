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

#include "Model.h"

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

  void PrepareModelDrawPipelines();
  void PrepareModelData();
  void DestroyModelData();

  void PrepareSceneUniformBuffer();
  void DestroySceneUniformBuffer();

  void DrawModel();

  bool m_isInitialized = false;
#if defined(PLATFORM_ANDROID)
  void* m_androidApp = nullptr;
#endif

  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

  VkPipeline m_drawOpaquePipeline = VK_NULL_HANDLE;
  VkPipeline m_drawBlendPipeline = VK_NULL_HANDLE;
  VkPipeline m_drawMaskPipeline = VK_NULL_HANDLE;

  std::vector<VkFramebuffer> m_framebuffers;
  VkRenderPass m_renderPass;

  uint64_t m_frameCount = 0;

  // モデルを描画する時に使用するディスクリプタセットレイアウト.
  VkDescriptorSetLayout m_modelDescriptorSetLayout = VK_NULL_HANDLE;

  // ユニフォームバッファーに送るために1要素16バイトアライメントとった状態にしておく.
  struct SceneParameters
  {
    glm::mat4 matView;
    glm::mat4 matProj;
    glm::vec4 lightDir;
  };
  // 平行光源.
  //  光がすすむ方向を設定.
  glm::vec3 m_lightDir; 

  std::vector<GpuBuffer> m_sceneUniformBuffers;
  struct DepthBuffer
  {
    VkFormat format;
    GpuImage depth;
  } m_depthBuffer;

  struct PolygonMesh {
    GpuBuffer position;
    GpuBuffer normal;
    GpuBuffer texcoord0;
    GpuBuffer indices;

    uint32_t  indexCount;
    uint32_t  vertexCount;
    uint32_t  materialIndex;
  };

  // UniformBufferに書き込むための構造体.
  // アライメントに注意.
  // - モデルのワールド行列
  // - 対象メッシュを描画するのに必要となるマテリアル情報.
  struct DrawParameters {
    glm::mat4 matWorld;
    //----
    glm::vec4 baseColor; // diffuse + alpha
    glm::vec4 specular;  // specular + shininess
    glm::vec4 ambient;
    uint32_t  mode;
  };
  struct DrawInfo
  {
    std::vector<GpuBuffer> modelMeshUniforms;
    std::vector<VkDescriptorSet> descriptorSets;
  };

  struct TextureInfo {
    std::string filePath;
    GpuImage    textureImage;
    VkSampler   sampler;

    VkDescriptorImageInfo descriptorInfo;
  };

  struct ModelData
  {
    std::vector<PolygonMesh> meshes;
    std::vector<ModelMaterial> materials;
    std::vector<DrawInfo> drawInfos;
    std::vector<TextureInfo> textureList;
    std::vector<TextureInfo> embeddedTextures;

    glm::mat4 matWorld = glm::mat4(1.0f);
  } m_model;

  std::vector<TextureInfo>::const_iterator FindModelTexture(const std::string& filePath, const ModelData& model);
};