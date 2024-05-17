#include "BasePlatform.h"
#include "Model.h"

#include "GfxDevice.h"
#include "FileLoader.h"

#include "assimp/scene.h"
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"

#include "assimp/IOSystem.hpp"
#include "assimp/IOStream.hpp"

#include "assimp/GltfMaterial.h" // for alpha mode,...

namespace
{
  // assimp から Vulkan用に変換するための関数.
  glm::mat4 ConvertMatrix(const aiMatrix4x4& from)
  {
    glm::mat4 to;

    to[0][0] = from.a1; to[1][0] = from.a2;
    to[2][0] = from.a3; to[3][0] = from.a4;
    to[0][1] = from.b1; to[1][1] = from.b2;
    to[2][1] = from.b3; to[3][1] = from.b4;
    to[0][2] = from.c1; to[1][2] = from.c2;
    to[2][2] = from.c3; to[3][2] = from.c4;
    to[0][3] = from.d1; to[1][3] = from.d2;
    to[2][3] = from.d3; to[3][3] = from.d4;

    return to;
  }

  glm::vec2 Convert(const aiVector2D& v) { return glm::vec2(v.x, v.y); }
  glm::vec3 Convert(const aiVector3D& v) { return glm::vec3(v.x, v.y, v.z); }
  glm::vec3 Convert(const aiColor3D& v) { return glm::vec3(v.r, v.g, v.b); }

  VkSamplerAddressMode ConvertAddressMode(aiTextureMapMode mode) {
    switch (mode)
    {
    default:
    case aiTextureMapMode_Wrap:
      return VK_SAMPLER_ADDRESS_MODE_REPEAT;

    case aiTextureMapMode_Clamp:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    case aiTextureMapMode_Mirror:
      return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    }
  };
}

class MemoryIOStream : public Assimp::IOStream
{
private:
  std::vector<char> m_data;
  size_t m_offset;
public:
  MemoryIOStream(std::vector<char>&& fileData) : m_data(std::move(fileData)), m_offset(0)
  {
  }

  size_t Read(void* pvBuffer, size_t pSize, size_t pCount) override
  {
    auto remainBytes = m_data.size() - m_offset;
    auto bytesToRead = pSize * pCount;
    if (bytesToRead > remainBytes)
    {
      bytesToRead = remainBytes;
    }
    memcpy(pvBuffer, m_data.data() + m_offset, bytesToRead);
    m_offset += bytesToRead;
    return bytesToRead / pSize;
  }

  size_t Write(const void* pvBuffer, size_t pSize, size_t pCount) override
  {
    return 0; // サポートしない.
  }

  aiReturn Seek(size_t pOffset, aiOrigin pOrigin) override
  {
    if (pOrigin == aiOrigin_SET)
    {
      m_offset = pOffset;
    }
    if (pOrigin == aiOrigin_CUR)
    {
      m_offset += pOffset;
    }
    if (pOrigin == aiOrigin_END)
    {
      m_offset = m_data.size() - pOffset;
    }

    return aiReturn_SUCCESS;
  }

  size_t Tell() const override
  {
    return m_offset;
  }

  size_t FileSize() const override
  {
    return m_data.size();
  }

  void Flush() override { }
};


class MemoryIOSystem : public Assimp::IOSystem
{
private:
  std::filesystem::path m_basePath;

public:
  MemoryIOSystem(std::filesystem::path basePath)
  {
    m_basePath = basePath;
  }

  bool Exists(const char* file) const override
  {
    return true;
  }

  char getOsSeparator() const override
  {
    return '/';
  }

  Assimp::IOStream* Open(const char* file, const char* mode = "rb") override
  {
    auto filePath = m_basePath / std::string(file);

    std::vector<char> fileData;
    if (!GetFileLoader()->Load(filePath, fileData))
    {
      return nullptr;
    }

    return new MemoryIOStream(std::move(fileData));
  }
  void Close(Assimp::IOStream* fileStream) override
  {
    delete fileStream;
  }
};

bool ModelLoader::Load(
  std::filesystem::path filePath, 
  std::vector<ModelMesh>& meshes,
  std::vector<ModelMaterial>& materials,
  std::vector<ModelEmbeddedTextureData>& embeddedData)
{
  Assimp::Importer importer;
  uint32_t flags = 0;
  flags |= aiProcess_Triangulate;   // 3角形化する.
  flags |= aiProcess_RemoveRedundantMaterials;  // 冗長なマテリアルを削除.
  flags |= aiProcess_FlipUVs;         // テクスチャ座標系:左上を原点とする.
  flags |= aiProcess_GenUVCoords;     // UVを生成.
  flags |= aiProcess_PreTransformVertices;    // モデルデータ内の頂点を変換済みにする.
  flags |= aiProcess_GenSmoothNormals;
  flags |= aiProcess_OptimizeMeshes;

  std::vector<char> fileData;
  if (GetFileLoader()->Load(filePath, fileData) == false)
  {
    return false;
  }

  m_basePath = filePath.parent_path();

  // メモリからのロードのために、カスタムのハンドラを設定しておく.
  // このハンドラは importer 破棄の時に解放される.
  importer.SetIOHandler(new MemoryIOSystem(m_basePath));
  const auto scene = importer.ReadFileFromMemory(fileData.data(), fileData.size(), flags);
  if (scene == nullptr)
  {
    return false;
  }
  if (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
  {
    //OutputDebugStringA(importer.GetErrorString());
    return false;
  }

  for (uint32_t i = 0; i < scene->mNumMaterials; ++i)
  {
    auto& material = materials.emplace_back();
    if (!ReadMaterial(material, scene->mMaterials[i]))
    {
      return false;
    }
  }

  for (uint32_t i = 0; i < scene->mNumMeshes; ++i)
  {
    auto& mesh = meshes.emplace_back();
    if (!ReadMeshes(mesh, scene->mMeshes[i]))
    {
      return false;
    }
  }

  for (uint32_t i = 0; i < scene->mNumTextures; ++i)
  {
    auto& tex = embeddedData.emplace_back();
    if (!ReadEmbeddedTexture(tex, scene->mTextures[i]))
    {
      return false;
    }
  }
  
  importer.FreeScene();
  return true;
}

bool ModelLoader::ReadMaterial(ModelMaterial& dstMaterial, const aiMaterial* srcMaterial)
{
  glm::vec3 diffuse(1.0f), specular(1.0f), ambient(0.0f);
  if (aiColor3D c; srcMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, c) == AI_SUCCESS)
  {
    diffuse = Convert(c);
  }
  if (aiColor3D c; srcMaterial->Get(AI_MATKEY_COLOR_SPECULAR, c) == AI_SUCCESS)
  {
    specular = Convert(c);
  }
  if (aiColor3D c; srcMaterial->Get(AI_MATKEY_COLOR_AMBIENT, c) == AI_SUCCESS)
  {
    ambient = Convert(c);
  }

  dstMaterial.diffuse = diffuse;
  dstMaterial.specular = specular;
  dstMaterial.ambient = ambient;
  
  float alpha = 1.0f;
  if (srcMaterial->Get(AI_MATKEY_OPACITY, alpha) == AI_SUCCESS)
  {
  }
  else if (srcMaterial->Get(AI_MATKEY_TRANSPARENCYFACTOR, alpha) == AI_SUCCESS)
  {
    alpha = 1.0f - alpha;
  }
  dstMaterial.alpha = alpha;

  if (aiString alphaMode; srcMaterial->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS)
  {
    std::string mode = alphaMode.C_Str();
    if (mode == "OPAQUE")
    {
      dstMaterial.alphaMode = ModelMaterial::ALPHA_MODE_OPAQUE;
    }
    if (mode == "MASK")
    {
      dstMaterial.alphaMode = ModelMaterial::ALPHA_MODE_MASK;
    }
    if (mode == "ALPHA")
    {
      dstMaterial.alphaMode = ModelMaterial::ALPHA_MODE_BLEND;
    }
  }
  if (dstMaterial.alpha < 1.0f && dstMaterial.alphaMode == ModelMaterial::ALPHA_MODE_OPAQUE)
  {
    dstMaterial.alphaMode = ModelMaterial::ALPHA_MODE_BLEND;
  }


  if (aiString texPath; srcMaterial->Get(AI_MATKEY_TEXTURE_DIFFUSE(0), texPath) == AI_SUCCESS)
  {
    auto fileName = std::string(texPath.C_Str());
    dstMaterial.texDiffuse.filePath = (m_basePath / fileName).string();
    if (fileName[0] == '*')
    {
      dstMaterial.texDiffuse.embeddedIndex = std::atoi(&fileName[1]);
    }

    aiTextureMapMode mapU = aiTextureMapMode_Wrap;
    aiTextureMapMode mapV = aiTextureMapMode_Wrap;
    srcMaterial->Get(AI_MATKEY_MAPPINGMODE_U_DIFFUSE(0), mapU);
    srcMaterial->Get(AI_MATKEY_MAPPINGMODE_V_DIFFUSE(0), mapV);

    dstMaterial.texDiffuse.addressModeU = ConvertAddressMode(mapU);
    dstMaterial.texDiffuse.addressModeV = ConvertAddressMode(mapV);
  }
  if (aiString texPath; srcMaterial->Get(AI_MATKEY_TEXTURE_SPECULAR(0), texPath) == AI_SUCCESS)
  {
    dstMaterial.texSpecular.filePath = (m_basePath / std::string(texPath.C_Str())).string();

    aiTextureMapMode mapU = aiTextureMapMode_Wrap;
    aiTextureMapMode mapV = aiTextureMapMode_Wrap;
    srcMaterial->Get(AI_MATKEY_MAPPINGMODE_U_SPECULAR(0), mapU);
    srcMaterial->Get(AI_MATKEY_MAPPINGMODE_V_SPECULAR(0), mapV);

    dstMaterial.texSpecular.addressModeU = ConvertAddressMode(mapU);
    dstMaterial.texSpecular.addressModeV = ConvertAddressMode(mapV);
  }

  return true;
}

bool ModelLoader::ReadMeshes(ModelMesh& dstMesh, const aiMesh* srcMesh)
{
  dstMesh.materialIndex = srcMesh->mMaterialIndex;
  
  auto vertexCount = srcMesh->mNumVertices;
  dstMesh.positions.resize(vertexCount);
  dstMesh.normals.resize(vertexCount);
  dstMesh.texcoords.resize(vertexCount);
  for (uint32_t i = 0; i < vertexCount; ++i)
  {
    dstMesh.positions[i] = Convert(srcMesh->mVertices[i]);
    dstMesh.normals[i] = Convert(srcMesh->mNormals[i]);
    dstMesh.texcoords[i] = Convert(srcMesh->mTextureCoords[0][i]);
  }

  auto indexCount = srcMesh->mNumFaces * 3;
  dstMesh.indices.resize(indexCount);
  for (uint32_t i = 0; i < srcMesh->mNumFaces; ++i)
  {
    auto& face = srcMesh->mFaces[i];
    dstMesh.indices.push_back(face.mIndices[0]);
    dstMesh.indices.push_back(face.mIndices[1]);
    dstMesh.indices.push_back(face.mIndices[2]);
  }
  return true;
}

bool ModelLoader::ReadEmbeddedTexture(ModelEmbeddedTextureData& dstEmbedded, const aiTexture* srcTexture)
{
  // バイナリ埋め込みテクスチャのみを対象とする.
  assert(srcTexture->mHeight == 0 && srcTexture->mWidth > 0);
  auto head = reinterpret_cast<const char*>(srcTexture->pcData);
  auto last = head + srcTexture->mWidth;
  dstEmbedded.data.assign(head, last);
  dstEmbedded.name = srcTexture->mFilename.C_Str();
  return true;
}


