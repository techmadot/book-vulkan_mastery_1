#include "App.h"
#include "Window.h"
#include "GfxDevice.h"

#include "imgui.h"

#include "FileLoader.h"
#include "Model.h"

#include "TextureUtility.h"

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

  if (!gfxDevice->IsSupportVulkan13())
  {
    // Vulkan 1.3 をサポートしていない状況では RenderPass を使った実装にする.
    PrepareRenderPass();
  }

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

  if (!gfxDevice->IsSupportVulkan13())
  {
    vulkanInfo.UseDynamicRendering = false;
    vulkanInfo.RenderPass = m_renderPass;
  }

  ImGui_ImplVulkan_Init(&vulkanInfo);
  ImGui_ImplVulkan_CreateFontsTexture();

  // アプリケーションコード初期化.
  PrepareModelDrawPipelines();

  m_lightDir = glm::vec3(0.0f,-1.0f,-0.2f);


  PrepareSceneUniformBuffer();
  
  PrepareModelData();
}

void Application::Shutdown()
{
  auto& gfxDevice = GetGfxDevice();
  gfxDevice->WaitForIdle();

  DestroyModelData();
  DestroySceneUniformBuffer();

  auto vkDevice = gfxDevice->GetVkDevice();

  //vkDestroyPipeline(vkDevice, m_drawOpaquePipeline, nullptr);
  vkDestroyPipeline(vkDevice, m_drawMaskPipeline, nullptr);
  vkDestroyPipeline(vkDevice, m_drawBlendPipeline, nullptr);
  m_drawOpaquePipeline = VK_NULL_HANDLE;
  m_drawMaskPipeline = VK_NULL_HANDLE;
  m_drawBlendPipeline = VK_NULL_HANDLE;
  vkDestroyPipelineLayout(vkDevice, m_pipelineLayout, nullptr);
  m_pipelineLayout = VK_NULL_HANDLE;

  vkDestroyRenderPass(vkDevice, m_renderPass, nullptr);
  m_renderPass = VK_NULL_HANDLE;

  for (auto& f : m_framebuffers)
  {
    vkDestroyFramebuffer(vkDevice, f, nullptr);
  }
  m_framebuffers.clear();

  vkDestroyDescriptorSetLayout(vkDevice, m_modelDescriptorSetLayout, nullptr);
  m_modelDescriptorSetLayout = VK_NULL_HANDLE;

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
  initParams.title = "DrawModel";
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

  if (useDynamicRendering)
  {
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
  }
  else
  {
    VkClearValue clearValues[] = {
      clearValue, clearDepth,
    };
    VkRenderPassBeginInfo renderPassBegin{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = m_renderPass,
      .framebuffer = m_framebuffers[gfxDevice->GetSwapchainImageIndex()],
      .renderArea = {
        .extent = { uint32_t(width), uint32_t(height) },
      },
      .clearValueCount = 2,
      .pClearValues = clearValues,
    };
    vkCmdBeginRenderPass(commandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
  }

  VkViewport viewport{
    .x = 0, .y = 0,
    .width = float(width), .height = float(height),
    .minDepth = 0.0f, .maxDepth = 1.0f,
  };
  VkRect2D scissor{
    .offset = { 0, 0 },
    .extent = {
      .width = uint32_t(width),
      .height = uint32_t(height)
    },
  };
  // 上下を反転する.
  viewport.y = float(height);
  viewport.height = -float(height);

  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

  // モデルを描画する前に、シーン共通のパラメータを更新.
  SceneParameters sceneParams;
  //sceneParams.matView = glm::lookAtRH(glm::vec3(0, 1.0f,5.0f), glm::vec3(0, 0.5f,0), glm::vec3(0,1,0));
  //sceneParams.matView = glm::lookAtRH(glm::vec3(4, 2.5f, 0.0f), glm::vec3(0, 2.0f, 0), glm::vec3(0, 1, 0));
  //sceneParams.matView = glm::lookAtRH(glm::vec3(4, 1.0f,0.0f), glm::vec3(0, 0.5f,0), glm::vec3(0,1,0));
  sceneParams.matView = glm::lookAtRH(glm::vec3(2, 1.0f, 0.0f), glm::vec3(0, 1.0f, 0), glm::vec3(0, 1, 0));
 
  sceneParams.matProj = glm::perspectiveFovRH(glm::radians(45.0f), float(width), float(height), 0.1f, 500.0f);
  sceneParams.lightDir = glm::vec4(m_lightDir, 0);
  memcpy(
    m_sceneUniformBuffers[gfxDevice->GetFrameIndex()].mapped,
    &sceneParams, sizeof(sceneParams));

  DrawModel();

  // ImGui によるGui構築

  ImGui::Begin("Information");
  ImGui::Text("FPS: %.2f", ImGui::GetIO().Framerate);
  ImGui::Text(useDynamicRendering ? "USE Dynamic Rendering" : "USE RenderPass");
  {
    float* v = reinterpret_cast<float*>(&m_lightDir);
    ImGui::InputFloat3("LightDir", v);
  }
  ImGui::End();

  // ImGui の描画処理.
  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

  if (useDynamicRendering)
  {
    vkCmdEndRendering(commandBuffer);
  }
  else
  {
    vkCmdEndRenderPass(commandBuffer);
  }
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

void Application::PrepareRenderPass()
{
  auto& device = GetGfxDevice();
  auto vkDevice = device->GetVkDevice();

  VkFormat format = device->GetSwapchainFormat().format;
  VkAttachmentDescription colorAttachment{
    .format = format,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };
  VkAttachmentDescription depthAttachment{
    .format = m_depthBuffer.format,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
  };
  VkAttachmentReference colorRefs{
    .attachment = 0,
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };
  VkAttachmentReference depthRefs{
    .attachment = 1,
    .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
  };

  VkSubpassDescription subpass{
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = 1,
    .pColorAttachments = &colorRefs,
    .pDepthStencilAttachment = &depthRefs,
  };

  std::vector<VkAttachmentDescription> attachmentDescriptions = {
    colorAttachment, depthAttachment
  };

  VkSubpassDependency subpassDependency{
    .srcSubpass = VK_SUBPASS_EXTERNAL,
    .dstSubpass = 0,
    .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .srcAccessMask = 0,
    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
  };

  VkRenderPassCreateInfo renderPassCI{
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = uint32_t(attachmentDescriptions.size()),
    .pAttachments = attachmentDescriptions.data(),
    .subpassCount = 1,
    .pSubpasses = &subpass,
  };
  vkCreateRenderPass(vkDevice, &renderPassCI, nullptr, &m_renderPass);

  int viewCount = device->GetSwapchainImageCount();
  int width, height;
  device->GetSwapchainResolution(width, height);
  m_framebuffers.resize(viewCount);
  for (int i = 0; i < viewCount; ++i)
  {
    std::vector<VkImageView> views = {
      device->GetSwapchainImageView(i),
      m_depthBuffer.depth.view,
    };

    VkFramebufferCreateInfo fbCI{
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass = m_renderPass,
      .attachmentCount = uint32_t(views.size()),
      .pAttachments = views.data(),
      .width = uint32_t(width),
      .height = uint32_t(height),
      .layers = 1,
    };
    vkCreateFramebuffer(vkDevice, &fbCI, nullptr, &m_framebuffers[i]);
  }
}

void Application::PrepareModelDrawPipelines()
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
    // マテリアルで使用するユニフォームバッファ.
    {
      .binding = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS
    },
    // ベース(ディフューズテクスチャ).
    {
      .binding = 2,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS
    },
  };
  VkDescriptorSetLayoutCreateInfo dsLayoutCI{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = uint32_t(layoutBindings.size()),
    .pBindings = layoutBindings.data(),
  };
  vkCreateDescriptorSetLayout(gfxDevice->GetVkDevice(), &dsLayoutCI, nullptr, &m_modelDescriptorSetLayout);


  VkPipelineLayoutCreateInfo layoutCI{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1,
    .pSetLayouts = &m_modelDescriptorSetLayout
  };
  vkCreatePipelineLayout(vkDevice, &layoutCI, nullptr, &m_pipelineLayout);

  std::array<VkVertexInputBindingDescription, 3> vertexBindingDescs = { {
    { // POSITION
      .binding = 0, .stride = sizeof(glm::vec3), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    },
    { // NORMAL
      .binding = 1, .stride = sizeof(glm::vec3), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    },
    { // UV
      .binding = 2, .stride = sizeof(glm::vec2), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    },
  } };
  std::array<VkVertexInputAttributeDescription, 3> vertInputAttribs = { {
    { // POSITION
      .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0,
    },
    { // NORMAL
      .location = 1, .binding = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0,
    },
    { // UV
      .location = 2, .binding = 2, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0,
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
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
  };
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

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

  VkPipelineRasterizationStateCreateInfo rasterizeState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .cullMode = VK_CULL_MODE_BACK_BIT,
    .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    .lineWidth = 1.0f,
  };

  VkPipelineColorBlendAttachmentState blendAttachment{
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  };

  VkPipelineColorBlendStateCreateInfo blendState{
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

  std::vector<char> vertexSpv, fragmentSpv;
  GetFileLoader()->Load("res/shader.vert.spv", vertexSpv);
  GetFileLoader()->Load("res/shader.frag.spv", fragmentSpv);

  std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{ {
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
    }
  } };

  VkGraphicsPipelineCreateInfo pipelineCreateInfo{
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount = uint32_t(shaderStages.size()),
    .pStages = shaderStages.data(),
    .pVertexInputState = &vertexInput,
    .pInputAssemblyState = &inputAssembly,
    .pViewportState = &viewportState,
    .pRasterizationState = &rasterizeState,
    .pMultisampleState = &multisample,
    .pDepthStencilState = &depthStencil,
    .pColorBlendState = &blendState,
    .layout = m_pipelineLayout,
    .renderPass = VK_NULL_HANDLE,
  };

  if (gfxDevice->IsSupportVulkan13())
  {
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
  }
  else
  {
    pipelineCreateInfo.renderPass = m_renderPass;

  }

  // 不透明描画パイプラインを作る.
  blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  auto res = vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_drawOpaquePipeline);
  assert(res == VK_SUCCESS);
  gfxDevice->SetObjectName(uint64_t(m_drawOpaquePipeline), "名前を付けてみたよ", VK_OBJECT_TYPE_PIPELINE);

  // アルファ抜き描画パイプラインを作る.
  blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;;
 
  res = vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_drawMaskPipeline);
  assert(res == VK_SUCCESS);

  // アルファブレンド有効描画パイプラインを作る.
  blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;;

  depthStencil.depthWriteEnable = VK_FALSE;
  res = vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_drawBlendPipeline);
  assert(res == VK_SUCCESS);

  for (auto& m : shaderStages)
  {
    gfxDevice->DestroyShaderModule(m.module);
  }

}

void Application::PrepareModelData()
{
  ModelLoader loader;
  std::vector<ModelMesh> modelMeshes;
  std::vector<ModelMaterial> modelMaterials;
  std::vector<ModelEmbeddedTextureData> modelEmbeddedTextures;
  //const char* modelFile = "res/model/BoxTextured.glb";
  //const char* modelFile = "res/model/teapot.glb";
  //const char* modelFile = "res/model/sponza/Sponza.gltf";
  const char* modelFile = "res/model/alicia-solid.vrm.glb";

  if (!loader.Load(modelFile, modelMeshes, modelMaterials, modelEmbeddedTextures))
  {
    //OutputDebugStringA("failed.\n");
    fprintf(stderr,"ERROR\n");
  }

  auto& gfxDevice = GetGfxDevice();
  auto vkDevice = gfxDevice->GetVkDevice();

  for (const auto& embeddedInfo : modelEmbeddedTextures)
  {
    auto& texture = m_model.embeddedTextures.emplace_back();
    auto success = CreateTextureFromMemory(texture.textureImage, embeddedInfo.data.data(), embeddedInfo.data.size());
    assert(success);
  }

  for (const auto& material : modelMaterials)
  {
    auto& dstMaterial = m_model.materials.emplace_back();
    dstMaterial = material;

    const auto& texDiffuse = dstMaterial.texDiffuse;
    auto texItr = FindModelTexture(texDiffuse.filePath, m_model);
    if (texItr == m_model.textureList.end())
    {

      bool success;
      
      if (texDiffuse.embeddedIndex == -1)
      {
        // 新規追加.
        auto& info = m_model.textureList.emplace_back();
        info.filePath = texDiffuse.filePath;

        std::vector<char> fileData;
        success = GetFileLoader()->Load(texDiffuse.filePath, fileData);
        assert(success);

        success = CreateTextureFromMemory(info.textureImage, fileData.data(), fileData.size());
        assert(success);

        auto modeU = texDiffuse.addressModeU;
        auto modeV = texDiffuse.addressModeV;
        VkSamplerCreateInfo samplerCI{
          .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
          .magFilter = VK_FILTER_LINEAR,
          .minFilter = VK_FILTER_LINEAR,
          .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
          .addressModeU = modeU,
          .addressModeV = modeV,
          .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
          .minLod = 0.0f,
          .maxLod = VK_LOD_CLAMP_NONE,
        };

        auto res = vkCreateSampler(vkDevice, &samplerCI, nullptr, &info.sampler);
        assert(res == VK_SUCCESS);
        info.descriptorInfo = {
          .sampler = info.sampler,
          .imageView = info.textureImage.view,
          .imageLayout = info.textureImage.layout,
        };
      }
      else
      {
        // 埋め込みテクスチャを参照.
        auto& embTexture = m_model.embeddedTextures[texDiffuse.embeddedIndex];
        
        auto modeU = texDiffuse.addressModeU;
        auto modeV = texDiffuse.addressModeV;
        VkSamplerCreateInfo samplerCI{
          .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
          .magFilter = VK_FILTER_LINEAR,
          .minFilter = VK_FILTER_LINEAR,
          .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
          .addressModeU = modeU,
          .addressModeV = modeV,
          .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        };

        auto res = vkCreateSampler(vkDevice, &samplerCI, nullptr, &embTexture.sampler);
        assert(res == VK_SUCCESS);
        embTexture.descriptorInfo = {
          .sampler = embTexture.sampler,
          .imageView = embTexture.textureImage.view,
          .imageLayout = embTexture.textureImage.layout,
        };
      }
    }

  }

  std::vector<VkDescriptorSetLayout> setLayouts(gfxDevice->InflightFrames, m_modelDescriptorSetLayout);

  for (const auto& mesh : modelMeshes)
  {
    VkMemoryPropertyFlags memFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    auto& dstMesh = m_model.meshes.emplace_back();
    size_t bufferSize = 0;
    bufferSize = mesh.positions.size() * sizeof(glm::vec3);
    dstMesh.position = gfxDevice->CreateBuffer(bufferSize, usage, memFlags, mesh.positions.data());

    bufferSize = mesh.normals.size() * sizeof(glm::vec3);
    dstMesh.normal = gfxDevice->CreateBuffer(bufferSize, usage, memFlags, mesh.normals.data());

    bufferSize = mesh.texcoords.size() * sizeof(glm::vec2);
    dstMesh.texcoord0 = gfxDevice->CreateBuffer(bufferSize, usage, memFlags, mesh.texcoords.data());

    bufferSize = mesh.indices.size() * sizeof(uint32_t);
    dstMesh.indices = gfxDevice->CreateBuffer(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, memFlags, mesh.indices.data());

    dstMesh.vertexCount = uint32_t(mesh.positions.size());
    dstMesh.indexCount = uint32_t(mesh.indices.size());
    dstMesh.materialIndex = mesh.materialIndex;
  }


  for (uint32_t i = 0; i < m_model.meshes.size(); ++i)
  {
    auto& mesh = m_model.meshes[i];
    auto& material = m_model.materials[mesh.materialIndex];

    auto& info = m_model.drawInfos.emplace_back();
    info.descriptorSets.resize(gfxDevice->InflightFrames);

    // マテリアルとの関連付けしてディスクリプタセットを構築.
    VkDescriptorSetAllocateInfo allocInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = gfxDevice->GetDescriptorPool(),
      .descriptorSetCount = uint32_t(setLayouts.size()),
      .pSetLayouts = setLayouts.data(),
    };
    auto res = vkAllocateDescriptorSets(vkDevice, &allocInfo, info.descriptorSets.data());
    assert(res == VK_SUCCESS);

    for (int j = 0; j < gfxDevice->InflightFrames; ++j)
    {
      auto bufferSize = sizeof(DrawParameters);
      auto& ubo = info.modelMeshUniforms.emplace_back();
      ubo = gfxDevice->CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
  }

  // ディスクリプタセット書込み.
  for (uint32_t i = 0; i < m_model.drawInfos.size(); ++i)
  {
    auto& info = m_model.drawInfos[i];
    auto& targetMesh = m_model.meshes[i];
    auto& material = m_model.materials[targetMesh.materialIndex];

    for (uint32_t frameIndex = 0; frameIndex < info.descriptorSets.size(); ++frameIndex)
    {
      auto descriptorSet = info.descriptorSets[frameIndex];

      std::vector<VkWriteDescriptorSet> writeDescs;
      VkDescriptorBufferInfo sceneUniformBuffer{
        .buffer = m_sceneUniformBuffers[frameIndex].buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE
      };
      auto& dsSceneUBO = writeDescs.emplace_back();
      dsSceneUBO = VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &sceneUniformBuffer
      };

      auto& dsMeshUBO = writeDescs.emplace_back();
      VkDescriptorBufferInfo meshUniformBuffer{
        .buffer = info.modelMeshUniforms[frameIndex].buffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE
      };
      dsMeshUBO = VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &meshUniformBuffer
      };


      const auto& texDiffuse = material.texDiffuse;
      auto& dsDiffuseTex = writeDescs.emplace_back();
      if (texDiffuse.embeddedIndex == -1)
      {
        auto itrTexDiffuse = FindModelTexture(material.texDiffuse.filePath, m_model);
        dsDiffuseTex = VkWriteDescriptorSet{
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = descriptorSet,
          .dstBinding = 2,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .pImageInfo = &itrTexDiffuse->descriptorInfo,
        };
      }
      else
      {
        auto& embeddedTex = m_model.embeddedTextures[material.texDiffuse.embeddedIndex];
        dsDiffuseTex = VkWriteDescriptorSet{
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = descriptorSet,
          .dstBinding = 2,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .pImageInfo = &embeddedTex.descriptorInfo,
        };
      }

      vkUpdateDescriptorSets(vkDevice, uint32_t(writeDescs.size()), writeDescs.data(), 0, nullptr);
    }
  }
}

void Application::DestroyModelData()
{
  auto& gfxDevice = GetGfxDevice();
  auto vkDevice = gfxDevice->GetVkDevice();
  for (auto& m : m_model.drawInfos)
  {
    for (auto& ubo : m.modelMeshUniforms)
    {
      gfxDevice->DestroyBuffer(ubo);
    }
    auto count = uint32_t(m.descriptorSets.size());
    vkFreeDescriptorSets(vkDevice, gfxDevice->GetDescriptorPool(), count, m.descriptorSets.data());
  }
  for (auto& m : m_model.meshes)
  {
    gfxDevice->DestroyBuffer(m.position);
    gfxDevice->DestroyBuffer(m.normal);
    gfxDevice->DestroyBuffer(m.texcoord0);
    gfxDevice->DestroyBuffer(m.indices);
  }
  for (auto& t : m_model.textureList)
  {
    gfxDevice->DestroyImage(t.textureImage);
    vkDestroySampler(vkDevice, t.sampler, nullptr);
  }
  m_model.textureList.clear();

  for (auto& t : m_model.embeddedTextures)
  {
    gfxDevice->DestroyImage(t.textureImage);
    vkDestroySampler(vkDevice, t.sampler, nullptr);
  }
  m_model.embeddedTextures.clear();
}

void Application::PrepareSceneUniformBuffer()
{
  auto& gfxDevice = GetGfxDevice();
  m_sceneUniformBuffers.resize(gfxDevice->InflightFrames);

  for (auto& buffer : m_sceneUniformBuffers)
  {
    buffer = gfxDevice->CreateBuffer(sizeof(SceneParameters), 
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
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

void Application::DrawModel()
{
  auto& gfxDevice = GetGfxDevice();
  auto commandBuffer = gfxDevice->GetCurrentCommandBuffer();
  auto frameIndex = gfxDevice->GetFrameIndex();

  auto deltaTime = std::min(ImGui::GetIO().DeltaTime, 1.0f);
  static float angle = 0.0;
  angle += deltaTime * 0.1f;
  if (angle > 360.0f) { angle -= 360.f; }
  if (angle < -360.0f) { angle += 360.f; }

  // モデルのワールド行列を更新.
  m_model.matWorld = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 1, 0));

  auto modeList = { ModelMaterial::ALPHA_MODE_OPAQUE, ModelMaterial::ALPHA_MODE_MASK, ModelMaterial::ALPHA_MODE_BLEND };
  for (auto mode : modeList)
  {
    VkPipeline usePipeline;
    switch (mode)
    {
    default:
    case ModelMaterial::ALPHA_MODE_OPAQUE:
      usePipeline = m_drawOpaquePipeline;
      break;
    
    case ModelMaterial::ALPHA_MODE_MASK:
      usePipeline = m_drawMaskPipeline;
      break;

    case ModelMaterial::ALPHA_MODE_BLEND:
      usePipeline = m_drawBlendPipeline;
      break;
    }
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, usePipeline);

    for (uint32_t i = 0; i < m_model.drawInfos.size(); ++i)
    {
      const auto& info = m_model.drawInfos[i];
      const auto& mesh = m_model.meshes[i];
      const auto& material = m_model.materials[mesh.materialIndex];
      if (material.alphaMode != mode)
      {
        continue;
      }

      // ワールド行列とマテリアル情報をユニフォームバッファに書き込む.
      DrawParameters params;
      params.matWorld = m_model.matWorld;
      params.baseColor = glm::vec4(material.diffuse, material.alpha);
      params.specular = glm::vec4(material.specular, material.shininess);
      params.ambient = glm::vec4(material.ambient, 0.0f);
      params.mode = material.alphaMode;
      auto writePtr = info.modelMeshUniforms[frameIndex].mapped;
      memcpy(writePtr, &params, sizeof(DrawParameters));

      VkBuffer vertexBuffers[] = {
        mesh.position.buffer,
        mesh.normal.buffer,
        mesh.texcoord0.buffer
      };
      VkDeviceSize offsets[] = { 0, 0, 0 };
      vkCmdBindVertexBuffers(commandBuffer, 0, 3, vertexBuffers, offsets);
      vkCmdBindIndexBuffer(commandBuffer, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

      auto descriptorSet = info.descriptorSets[frameIndex];
      vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
      vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);

    }
  }
}

std::vector<Application::TextureInfo>::const_iterator Application::FindModelTexture(const std::string& filePath, const ModelData& model)
{
  return std::find_if(model.textureList.begin(), model.textureList.end(), [&](const auto& v) { return v.filePath == filePath; });
}
