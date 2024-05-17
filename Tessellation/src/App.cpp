#include "App.h"
#include "Window.h"
#include "GfxDevice.h"

#include "imgui.h"
#include "FileLoader.h"

#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
#include "GLFW/glfw3.h"
#include "backends/imgui_impl_glfw.h"
#endif

#if defined(PLATFORM_ANDROID)
extern "C"
{
#include <game-activity/native_app_glue/android_native_app_glue.h>
}
#include "backends/imgui_impl_android.h"
#endif

#define IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
#include "backends/imgui_impl_vulkan.h"

void Application::Initialize()
{
  InitializeWindow();
  InitializeGfxDevice();

  m_isInitialized = true;

  // ImGui 初期化.
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  auto& window = GetAppWindow();
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
  GLFWwindow* glfwWindow = window->GetPlatformHandle()->window;
  ImGui_ImplGlfw_InitForVulkan(glfwWindow, true);
#endif

#if defined(PLATFORM_ANDROID)
  auto* app = reinterpret_cast<android_app*>(m_androidApp);
  ImGui_ImplAndroid_Init(app->window);

  {
    auto& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.SizePixels = 40.0f;
    io.Fonts->AddFontDefault(&cfg);
  }
#endif

  auto& gfxDevice = GetGfxDevice();

  int width, height;
  window->GetWindowSize(width, height);
  m_depthBuffer.format = VK_FORMAT_D32_SFLOAT;
  m_depthBuffer.depth = gfxDevice->CreateImage2D(width, height, m_depthBuffer.format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1);

  ImGui_ImplVulkan_LoadFunctions(
    [](const char* functionName, void* userArgs) {
      auto& dev = GetGfxDevice();
      auto vkDevice = dev->GetVkDevice();
      auto vkInstance = dev->GetVkInstance();
      auto devFuncAddr = vkGetDeviceProcAddr(vkDevice, functionName);
      if (devFuncAddr != nullptr)
      {
        return devFuncAddr;
      }
      auto instanceFuncAddr = vkGetInstanceProcAddr(vkInstance, functionName);
      return instanceFuncAddr;
    });
  ImGui_ImplVulkan_InitInfo vulkanInfo{
    .Instance = gfxDevice->GetVkInstance(),
    .PhysicalDevice = gfxDevice->GetVkPhysicalDevice(),
    .Device = gfxDevice->GetVkDevice(),
    .QueueFamily = gfxDevice->GetGraphicsQueueFamily(),
    .Queue = gfxDevice->GetGraphicsQueue(),
    .DescriptorPool = gfxDevice->GetDescriptorPool(),
    .RenderPass = VK_NULL_HANDLE,
    .MinImageCount = uint32_t(gfxDevice->InflightFrames),
    .ImageCount = gfxDevice->GetSwapchainImageCount(),
    .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
  };
  VkFormat colorFormat = gfxDevice->GetSwapchainFormat().format;
  VkFormat depthFormat = m_depthBuffer.format;
  vulkanInfo.UseDynamicRendering = true;
  vulkanInfo.PipelineRenderingCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &colorFormat,
    .depthAttachmentFormat = depthFormat,
  };

  ImGui_ImplVulkan_Init(&vulkanInfo);
  ImGui_ImplVulkan_CreateFontsTexture();

  // アプリケーションコード初期化.
  PreparePipeline();

  PrepareSceneUniformBuffer();
  
  PrepareTessellationPlane();
}

void Application::Shutdown()
{
  auto& gfxDevice = GetGfxDevice();
  gfxDevice->WaitForIdle();
  auto vkDevice = gfxDevice->GetVkDevice();

  gfxDevice->DestroyBuffer(m_vertexBuffer);
  gfxDevice->DestroyBuffer(m_indexBuffer);

  DestroySceneUniformBuffer();
  vkDestroyPipeline(vkDevice, m_tessellationPipeline, nullptr);
  vkDestroyPipeline(vkDevice, m_tessellationPipeline2, nullptr);
  m_tessellationPipeline = VK_NULL_HANDLE;
  m_tessellationPipeline2 = VK_NULL_HANDLE;


  vkDestroyPipelineLayout(vkDevice, m_pipelineLayout, nullptr);
  m_pipelineLayout = VK_NULL_HANDLE;

  vkDestroyDescriptorSetLayout(vkDevice, m_descriptorSetLayout, nullptr);
  m_descriptorSetLayout = VK_NULL_HANDLE;

  gfxDevice->DestroyImage(m_depthBuffer.depth);

  // ImGui 終了の処理.
  ImGui_ImplVulkan_Shutdown();

#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
  ImGui_ImplGlfw_Shutdown();
#endif
  ImGui::DestroyContext();

  gfxDevice->Shutdown();

  auto& window = GetAppWindow();
  window->Shutdown();

  m_isInitialized = false;
}

void Application::SurfaceSizeChanged()
{
  auto& window = GetAppWindow();
  int newWidth = 0, newHeight = 0;
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
  GLFWwindow* glfwWindow = window->GetPlatformHandle()->window;
  glfwGetWindowSize(glfwWindow, &newWidth, &newHeight);
#endif
#if defined(PLATFORM_ANDROID)
  auto app = reinterpret_cast<android_app*>(m_androidApp);
  newWidth = ANativeWindow_getWidth(app->window);
  newHeight = ANativeWindow_getHeight(app->window);
#endif
  assert(newWidth != 0 && newHeight != 0);

  int width = 0, height = 0;
  auto& gfxDevice = GetGfxDevice();
  gfxDevice->GetSwapchainResolution(width, height);

  if (width != newWidth || height != newHeight)
  {
    gfxDevice->RecreateSwapchain(uint32_t(newWidth), uint32_t(newHeight));
  }
}

void Application::InitializeWindow()
{
  auto& window = GetAppWindow();
  Window::WindowInitParams initParams{};
  initParams.title = "Tessellation";
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
  initParams.width = 1280;
  initParams.height = 720;
#elif defined(PLATFORM_ANDROID)
  initParams.android_app = m_androidApp;
#endif
  window->Initialize(initParams);
}

void Application::InitializeGfxDevice()
{
  GfxDevice::DeviceInitParams devInitParams{};
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
  auto& window = GetAppWindow();
  devInitParams.glfwWindow = window->GetPlatformHandle()->window;
#endif

#if defined(PLATFORM_ANDROID)
  auto* app = reinterpret_cast<android_app*>(m_androidApp);
  devInitParams.window = app->window;
#endif
  auto& gfxDevice = GetGfxDevice();
  gfxDevice->Initialize(devInitParams);
}


void Application::Process()
{
  auto& gfxDevice = GetGfxDevice();
  auto vkDevice = gfxDevice->GetVkDevice();

  gfxDevice->NewFrame();
  auto commandBuffer = gfxDevice->GetCurrentCommandBuffer();

  static bool isFirstFrame = true;
  if (isFirstFrame)
  {
    isFirstFrame = false;
  }
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
  ImGui_ImplGlfw_NewFrame();
#endif
#if defined(__ANDROID__)
  ImGui_ImplAndroid_NewFrame();
#endif

  ImGui_ImplVulkan_NewFrame();
  ImGui::NewFrame();

  int width, height;
  gfxDevice->GetSwapchainResolution(width, height);
  VkClearValue clearValue = {
    VkClearColorValue{ 0.85f, 0.5f, 0.7f, 0.0f },
  };
  VkClearValue clearDepth = {
    .depthStencil { .depth = 1.0f }
  };

  bool useDynamicRendering = gfxDevice->IsSupportVulkan13();

  BeginRender();

  VkRenderingAttachmentInfo colorAttachmentInfo{
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .imageView = gfxDevice->GetCurrentSwapchainImageView(),
    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .clearValue = clearValue,
  };

  VkRenderingAttachmentInfo depthAttachmentInfo{
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .imageView = m_depthBuffer.depth.view,
    .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .clearValue = clearDepth,
  };

  VkRenderingInfo renderingInfo{
    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
    .renderArea = {
      .extent = { uint32_t(width), uint32_t(height) },
    },
    .layerCount = 1,
    .colorAttachmentCount = 1,
    .pColorAttachments = &colorAttachmentInfo,
    .pDepthAttachment = &depthAttachmentInfo,
  };

  vkCmdBeginRendering(commandBuffer, &renderingInfo);

  // モデルを描画する前に、シーン共通のパラメータを更新.
  static float frameDeltaAccum = 0.0f;
  SceneParameters sceneParams;
  sceneParams.matView = glm::lookAtRH(glm::vec3(0, 5.0f,10.0f), glm::vec3(0, 0.0f,0), glm::vec3(0,1,0));
  sceneParams.matProj = glm::perspectiveFovRH(glm::radians(45.0f), float(width), float(height), 0.1f, 500.0f);
  sceneParams.tessParams = glm::vec4(m_tessLevelInner, m_tessLevelOuter, 0.0f, 0.0f);
  if (m_useFillColor)
  {
    sceneParams.tessParams.z = 1;
  }

  frameDeltaAccum += std::min(ImGui::GetIO().DeltaTime, 1.0f);
  sceneParams.time = frameDeltaAccum; // 累積値を時間情報として送る.
  memcpy(
    m_sceneUniformBuffers[gfxDevice->GetFrameIndex()].mapped,
    &sceneParams, sizeof(sceneParams));

  VkBuffer vertexBuffers[] = { m_vertexBuffer.buffer };
  VkDeviceSize offsets[] = { 0 };
  VkDescriptorSet descriptorSet = m_descriptorSets[gfxDevice->GetFrameIndex()];
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tessellationPipeline);
  if (m_useFillColor)
  {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tessellationPipeline2);
  }
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
  vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
  vkCmdDrawIndexed(commandBuffer, 4, 1, 0, 0, 0);

  // ImGui によるGui構築
  auto& io = ImGui::GetIO();
  ImGui::Begin("Information", 0, ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::Text("FPS: %.2f", ImGui::GetIO().Framerate);

  ImGui::SliderFloat("TessInner", &m_tessLevelInner, 1.0f, 64.f);
  ImGui::SliderFloat("TessOuter", &m_tessLevelOuter, 1.0f, 64.f);

  if (!m_isSupportWireframe)
  {
    ImColor msgColor(255, 64, 0);
    ImGui::TextColored(msgColor, "Not Supported Wireframe");
  }
  else
  {
    ImColor msgColor(0, 255, 0);
    ImGui::TextColored(msgColor, "Supported Wireframe");
  }
  ImGui::Checkbox("FILL", &m_useFillColor);

  ImGui::End();

  // ImGui の描画処理.
  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

  vkCmdEndRendering(commandBuffer);
  EndRender();

  gfxDevice->Submit();
  m_frameCount++;
}

void Application::BeginRender()
{
  auto& gfxDevice = GetGfxDevice();
  auto commandBuffer = gfxDevice->GetCurrentCommandBuffer();

  // スワップチェインイメージのレイアウト変更.
  gfxDevice->TransitionLayoutSwapchainImage(commandBuffer,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

void Application::EndRender()
{
  auto& gfxDevice = GetGfxDevice();
  auto commandBuffer = gfxDevice->GetCurrentCommandBuffer();

  // スワップチェインイメージのレイアウト変更.
  gfxDevice->TransitionLayoutSwapchainImage(commandBuffer,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_2_NONE);

}

void Application::PreparePipeline()
{
  auto& gfxDevice = GetGfxDevice();
  auto vkDevice = gfxDevice->GetVkDevice();

  std::vector<VkDescriptorSetLayoutBinding> layoutBindings{
    // シーン全体で使用するユニフォームバッファ.
    {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS
    },
  };
  VkDescriptorSetLayoutCreateInfo dsLayoutCI{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = uint32_t(layoutBindings.size()),
    .pBindings = layoutBindings.data(),
  };
  vkCreateDescriptorSetLayout(gfxDevice->GetVkDevice(), &dsLayoutCI, nullptr, &m_descriptorSetLayout);


  VkPipelineLayoutCreateInfo layoutCI{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1,
    .pSetLayouts = &m_descriptorSetLayout
  };
  vkCreatePipelineLayout(vkDevice, &layoutCI, nullptr, &m_pipelineLayout);

  std::array<VkVertexInputBindingDescription, 1> vertexBindingDescs = { {
    { // POSITION
      .binding = 0, .stride = sizeof(glm::vec3), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    },
  } };
  std::array<VkVertexInputAttributeDescription, 1> vertInputAttribs = { {
    { // POSITION
      .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0,
    },
  } };

  VkPipelineVertexInputStateCreateInfo vertexInput{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = uint32_t(vertexBindingDescs.size()),
    .pVertexBindingDescriptions = vertexBindingDescs.data(),
    .vertexAttributeDescriptionCount = uint32_t(vertInputAttribs.size()),
    .pVertexAttributeDescriptions = vertInputAttribs.data(),
  };

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
  };
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;

  VkPipelineRasterizationStateCreateInfo raster{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .polygonMode = VK_POLYGON_MODE_LINE,
    .cullMode = VK_CULL_MODE_BACK_BIT,
    .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    .lineWidth = 1.0f,
  };

  {
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(gfxDevice->GetVkPhysicalDevice(), &features);
    if (features.fillModeNonSolid == VK_FALSE)
    {
      // ワイヤーフレーム描画をサポートしていない.
      raster.polygonMode = VK_POLYGON_MODE_FILL;
      m_isSupportWireframe = false;
      m_useFillColor = true;
    }
  }

  // ビューポート/シザーの指定
  int width, height;
  gfxDevice->GetSwapchainResolution(width, height);
  VkViewport viewport{
    .x = 0, .y = 0,
    .width = float(width), .height = float(height),
    .minDepth = 0.0f, .maxDepth = 1.0f,
  };
  VkRect2D scissor{
    .offset = { 0, 0 },
    .extent = {
      .width = uint32_t(width), .height = uint32_t(height)
    },
  };
  VkPipelineViewportStateCreateInfo viewportState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1, .pViewports = &viewport,
    .scissorCount = 1, .pScissors = &scissor,
  };
  // 上下を反転する.
  viewport.y = float(height);
  viewport.height = -float(height);

  VkPipelineColorBlendAttachmentState blendAttachment{
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  };

  VkPipelineColorBlendStateCreateInfo blend{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &blendAttachment
  };


  VkPipelineDepthStencilStateCreateInfo depthStencil{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    .depthTestEnable = VK_TRUE,
    .depthWriteEnable = VK_TRUE,
    .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
  };

  VkPipelineMultisampleStateCreateInfo multisample{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
  };

  std::vector<char> vertexSpv, fragmentSpv, tessCtrlSpv, tessEvalSpv;
  GetFileLoader()->Load("res/shader.vert.spv", vertexSpv);
  GetFileLoader()->Load("res/shader.frag.spv", fragmentSpv);
  GetFileLoader()->Load("res/shader.tesc.spv", tessCtrlSpv);
  GetFileLoader()->Load("res/shader.tese.spv", tessEvalSpv);


  std::array<VkPipelineShaderStageCreateInfo, 4> shaderStages{ {
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = gfxDevice->CreateShaderModule(vertexSpv.data(), vertexSpv.size()),
      .pName = "main",
    },
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = gfxDevice->CreateShaderModule(fragmentSpv.data(), fragmentSpv.size()),
      .pName = "main",
    },
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
      .module = gfxDevice->CreateShaderModule(tessCtrlSpv.data(), tessCtrlSpv.size()),
      .pName = "main",
    },
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
      .module = gfxDevice->CreateShaderModule(tessEvalSpv.data(), tessEvalSpv.size()),
      .pName = "main",
    },
  } };

  VkPipelineTessellationStateCreateInfo tessellationState = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
    .patchControlPoints = 4, // コントロールポイント4点
  };

  VkGraphicsPipelineCreateInfo pipelineCreateInfo{
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount = uint32_t(shaderStages.size()),
    .pStages = shaderStages.data(),
    .pVertexInputState = &vertexInput,
    .pInputAssemblyState = &inputAssembly,
    .pTessellationState = &tessellationState,
    .pViewportState = &viewportState,
    .pRasterizationState = &raster,
    .pMultisampleState = &multisample,
    .pDepthStencilState = &depthStencil,
    .pColorBlendState = &blend,
    .layout = m_pipelineLayout,
    .renderPass = VK_NULL_HANDLE,
  };

  // Dynamic Rendering を使う際にはこちらの構造体も必要.
  VkFormat colorFormats[] = {
    gfxDevice->GetSwapchainFormat().format,
  };
  VkPipelineRenderingCreateInfo renderingCI{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = colorFormats,
    .depthAttachmentFormat = m_depthBuffer.format,
  };
  pipelineCreateInfo.pNext = &renderingCI; // ここから参照させる.

  // テッセレーション描画用のパイプラインを作る.
  blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  
  auto res = vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_tessellationPipeline);
  assert(res == VK_SUCCESS);

  raster.polygonMode = VK_POLYGON_MODE_FILL;
  res = vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_tessellationPipeline2);
  assert(res == VK_SUCCESS);

  for (auto& m : shaderStages)
  {
    gfxDevice->DestroyShaderModule(m.module);
  }
}

void Application::PrepareTessellationPlane()
{
  auto& gfxDevice = GetGfxDevice();
  auto vkDevice = gfxDevice->GetVkDevice();

  // XZ平面に平面ポリゴンを用意する.
  std::vector<glm::vec3> verts = {
    glm::vec3(-4.0f, 0.0f, -4.0f),
    glm::vec3(+4.0f, 0.0f, -4.0f),
    glm::vec3(-4.0f, 0.0f, +4.0f),
    glm::vec3(+4.0f, 0.0f, +4.0f),
  };
  std::vector<uint32_t> indices = { 0, 1, 2, 3};
  VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  VkBufferUsageFlagBits usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  auto bufferSize = sizeof(glm::vec3) * verts.size();
  m_vertexBuffer = gfxDevice->CreateBuffer(bufferSize, usage, memProps, verts.data());
  bufferSize = sizeof(uint32_t) * indices.size();
  usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  m_indexBuffer = gfxDevice->CreateBuffer(bufferSize, usage, memProps, indices.data());

  // ディスクリプタセットを作る.
  m_descriptorSets.resize(GfxDevice::InflightFrames);
  VkDescriptorSetAllocateInfo allocInfo{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool = gfxDevice->GetDescriptorPool(),
    .descriptorSetCount = 1,
    .pSetLayouts = &m_descriptorSetLayout,
  };
  for (uint32_t i = 0; i < GfxDevice::InflightFrames; ++i)
  {
    vkAllocateDescriptorSets(vkDevice, &allocInfo, &m_descriptorSets[i]);
  }

  // ディスクリプタセットに書込み.
  for (uint32_t i = 0; i < GfxDevice::InflightFrames; ++i)
  {
    auto ds = m_descriptorSets[i];

    std::vector<VkWriteDescriptorSet> writeDescs;
    VkDescriptorBufferInfo sceneUniformBuffer{
      .buffer = m_sceneUniformBuffers[i].buffer,
      .offset = 0,
      .range = VK_WHOLE_SIZE
    };
    auto& dsSceneUBO = writeDescs.emplace_back();
    dsSceneUBO = VkWriteDescriptorSet{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = ds,
      .dstBinding = 0,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .pBufferInfo = &sceneUniformBuffer
    };

    vkUpdateDescriptorSets(vkDevice, uint32_t(writeDescs.size()), writeDescs.data(), 0, nullptr);
  }
}

void Application::PrepareSceneUniformBuffer()
{
  auto& gfxDevice = GetGfxDevice();
  m_sceneUniformBuffers.resize(gfxDevice->InflightFrames);

  for (auto& buffer : m_sceneUniformBuffers)
  {
    buffer = gfxDevice->CreateBuffer(sizeof(SceneParameters), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  }
}

void Application::DestroySceneUniformBuffer()
{
  auto& gfxDevice = GetGfxDevice();
  for (auto& buffer : m_sceneUniformBuffers)
  {
    gfxDevice->DestroyBuffer(buffer);
  }
  m_sceneUniformBuffers.clear();
}
