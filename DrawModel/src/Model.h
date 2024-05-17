#pragma once
#include <memory>
#include <vector>
#include <cstdint>
#include <filesystem>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"

#include "GfxDevice.h"

#include "assimp/scene.h"

struct ModelMesh
{
  std::vector<glm::vec3> positions;
  std::vector<glm::vec3> normals;
  std::vector<glm::vec2> texcoords;
  std::vector<uint32_t>  indices;

  uint32_t materialIndex;
};

struct ModelTexture
{
  std::string filePath;
  VkSamplerAddressMode addressModeU;
  VkSamplerAddressMode addressModeV;

  GpuImage texture;
  VkSampler sampler;
  int  embeddedIndex = -1;
};

struct ModelEmbeddedTextureData
{
  std::string name;
  std::vector<char> data;
};

struct ModelMaterial
{
  glm::vec3 diffuse;
  glm::vec3 specular;
  glm::vec3 ambient;

  float shininess = 0.0f;
  float alpha = 1.0f;

  enum AlphaMode {
    ALPHA_MODE_OPAQUE = 0,
    ALPHA_MODE_MASK,
    ALPHA_MODE_BLEND,
  };
  AlphaMode alphaMode = ALPHA_MODE_OPAQUE;

  ModelTexture texDiffuse;
  ModelTexture texSpecular; 
};

class ModelLoader
{
public:
  bool Load(std::filesystem::path filePath, std::vector<ModelMesh>& meshes, std::vector<ModelMaterial>& materials, std::vector<ModelEmbeddedTextureData>& embeddedData);

private:
  bool ReadMaterial(ModelMaterial& dstMaterial, const aiMaterial* srcMaterial);
  bool ReadMeshes(ModelMesh& dstMesh, const aiMesh* srcMesh);
  bool ReadEmbeddedTexture(ModelEmbeddedTextureData& dstEmbeddedTex, const aiTexture* srcTexture);

  std::filesystem::path m_basePath;
};
