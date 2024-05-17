#include "App.h"
#include "Window.h"
#include "GfxDevice.h"
#include "FileLoader.h"

#if defined(PLATFORM_ANDROID)
extern "C"
{
#include <game-activity/native_app_glue/android_native_app_glue.h>
}
#endif

#include "imgui.h"
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
#include "GLFW/glfw3.h"
#include "backends/imgui_impl_glfw.h"
#elif defined(PLATFORM_ANDROID)
#include "backends/imgui_impl_android.h"
#endif

#define IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
#include "backends/imgui_impl_vulkan.h"

//#define USE_RENDERPASS (1)

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

#ifdef USE_RENDERPASS
  // Dynamic Renderingを使わない場合、VkRenderPass の準備が必要.
  PrepareRenderPass();
#endif

  auto& gfxDevice = GetGfxDevice();

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
  VkFormat depthFormat = VK_FORMAT_UNDEFINED;
  vulkanInfo.UseDynamicRendering = true;
  vulkanInfo.PipelineRenderingCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &colorFormat,
    .depthAttachmentFormat = depthFormat,
  };
#ifdef USE_RENDERPASS
  vulkanInfo.UseDynamicRendering = false;
  vulkanInfo.RenderPass = m_renderPass;
#endif

  ImGui_ImplVulkan_Init(&vulkanInfo);
  ImGui_ImplVulkan_CreateFontsTexture();

  // アプリケーションコード初期化.
  PrepareTriangle();
}

void Application::Shutdown()
{
  auto& gfxDevice = GetGfxDevice();
  gfxDevice->WaitForIdle();
  
  auto vkDevice = gfxDevice->GetVkDevice();

  // 頂点バッファ破棄.
  vkDestroyBuffer(vkDevice, m_vertexBuffer.buffer, nullptr);
  vkFreeMemory(vkDevice, m_vertexBuffer.memory, nullptr);
  m_vertexBuffer.buffer = VK_NULL_HANDLE;
  m_vertexBuffer.memory = VK_NULL_HANDLE;

  vkDestroyPipeline(vkDevice, m_pipeline, nullptr);
  vkDestroyPipelineLayout(vkDevice, m_pipelineLayout, nullptr);
  m_pipeline = VK_NULL_HANDLE;
  m_pipelineLayout = VK_NULL_HANDLE;

#ifdef USE_RENDERPASS
  vkDestroyRenderPass(vkDevice, m_renderPass, nullptr);
  m_renderPass = VK_NULL_HANDLE;

  for (auto& f : m_framebuffers)
  {
    vkDestroyFramebuffer(vkDevice, f, nullptr);
  }
  m_framebuffers.clear();
#endif

  // ImGui 終了の処理.
  ImGui_ImplVulkan_Shutdown();

#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
  ImGui_ImplGlfw_Shutdown();
#elif defined(PLATFORM_ANDROID)
  ImGui_ImplAndroid_Shutdown();
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
  initParams.title = "HelloTriangle";
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

#if !defined(USE_RENDERPASS)
  BeginRender();

  VkRenderingAttachmentInfo colorAttachmentInfo{
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .imageView = gfxDevice->GetCurrentSwapchainImageView(),
    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .clearValue = clearValue,
  };
  VkRenderingInfo renderingInfo{
    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
    .renderArea = {
      .extent = { uint32_t(width), uint32_t(height) },
    },
    .layerCount = 1,
    .colorAttachmentCount = 1,
    .pColorAttachments = &colorAttachmentInfo,
  };

  vkCmdBeginRendering(commandBuffer, &renderingInfo);
#else

  VkRenderPassBeginInfo renderPassBegin{
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass = m_renderPass,
    .framebuffer = m_framebuffers[gfxDevice->GetSwapchainImageIndex()],
    .renderArea = {
      .extent = { uint32_t(width), uint32_t(height) },
    },
    .clearValueCount = 1,
    .pClearValues = &clearValue,
  };
  vkCmdBeginRenderPass(commandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
#endif

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
  VkDeviceSize vertexOffsets[] = { 0 };
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertexBuffer.buffer, vertexOffsets);
  vkCmdDraw(commandBuffer, 3, 1, 0, 0);

  // ImGui によるGui構築

  ImGui::Begin("Information");
  ImGui::Text("Hello Triangle");
  ImGui::Text("FPS: %.2f", ImGui::GetIO().Framerate);
#if defined(USE_RENDERPASS)
  ImGui::Text("USE RenderPass");
#else
  ImGui::Text("USE Dynamic Rendering");
#endif
  ImGui::End();

  // ImGui の描画処理.
  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

#if !defined(USE_RENDERPASS)
  vkCmdEndRendering(commandBuffer);
#else
  vkCmdEndRenderPass(commandBuffer);
#endif

  EndRender();

  gfxDevice->Submit();
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

void Application::PrepareTriangle()
{
  auto& gfxDevice = GetGfxDevice();
  auto vkDevice = gfxDevice->GetVkDevice();

  // 頂点バッファの作成.
  Vertex triangleVerts[] = {
    Vertex{ glm::vec3( 0.5f, 0.5f,0.5f), glm::vec3(1,0,0) },
    Vertex{ glm::vec3( 0.0f,-0.5f,0.5f), glm::vec3(0,1,0) },
    Vertex{ glm::vec3(-0.5f, 0.5f,0.5f), glm::vec3(0,0,1) },
  };
  VkBufferCreateInfo bufferCI{
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = uint32_t(sizeof(triangleVerts)),
    .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
  };
  vkCreateBuffer(vkDevice, &bufferCI, nullptr, &m_vertexBuffer.buffer);
  // メモリ要件を求める.
  VkMemoryRequirements reqs;
  vkGetBufferMemoryRequirements(vkDevice, m_vertexBuffer.buffer, &reqs);
  // メモリを確保する.
  VkMemoryAllocateInfo memoryAllocateInfo{
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = reqs.size,
    .memoryTypeIndex = gfxDevice->GetMemoryTypeIndex(reqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
  };
  vkAllocateMemory(vkDevice, &memoryAllocateInfo, nullptr, &m_vertexBuffer.memory);
  vkBindBufferMemory(vkDevice, m_vertexBuffer.buffer, m_vertexBuffer.memory, 0);

  // 頂点データを書き込む.
  void* mapped;
  vkMapMemory(vkDevice, m_vertexBuffer.memory, 0, VK_WHOLE_SIZE, 0, &mapped);
  memcpy(mapped, triangleVerts, sizeof(triangleVerts));
  vkUnmapMemory(vkDevice, m_vertexBuffer.memory);


  VkPipelineLayoutCreateInfo layoutCI{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
  };
  vkCreatePipelineLayout(vkDevice, &layoutCI, nullptr, &m_pipelineLayout);

  VkVertexInputBindingDescription vertexBindingDesc{
    .binding = 0,
    .stride = uint32_t(sizeof(Vertex)),
    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
  };
  VkVertexInputAttributeDescription vertexAttribs[] = {
    {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0 },
    {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = sizeof(glm::vec3) },
  };
  VkPipelineVertexInputStateCreateInfo vertexInput{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = 1,
    .pVertexBindingDescriptions = &vertexBindingDesc,
    .vertexAttributeDescriptionCount = 2,
    .pVertexAttributeDescriptions = vertexAttribs,
  };

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
  };

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
      .width = uint32_t(width),
      .height = uint32_t(height)
    },
  };

  VkPipelineViewportStateCreateInfo viewportState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1,
    .pViewports = &viewport,
    .scissorCount = 1,
    .pScissors = &scissor,
  };


  VkPipelineRasterizationStateCreateInfo rasterizeState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .cullMode = VK_CULL_MODE_BACK_BIT,
    .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    .lineWidth = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisampleState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
  };

  VkPipelineColorBlendAttachmentState blendAttachment{
    .blendEnable = VK_TRUE,
    .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
    .colorBlendOp = VK_BLEND_OP_ADD,
    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    .alphaBlendOp = VK_BLEND_OP_ADD,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  };

  VkPipelineColorBlendStateCreateInfo blendState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &blendAttachment
  };


  VkPipelineDepthStencilStateCreateInfo depthStencilState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
  };


  // シェーダー(SPIR-V) 読み込み.
  std::vector<char> vertexShaderSpv, fragmentShaderSpv;
  GetFileLoader()->Load("res/shader.vert.spv", vertexShaderSpv);
  GetFileLoader()->Load("res/shader.frag.spv", fragmentShaderSpv);

  std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{ {
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = gfxDevice->CreateShaderModule(vertexShaderSpv.data(), vertexShaderSpv.size()),
      .pName = "main",
    },
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = gfxDevice->CreateShaderModule(fragmentShaderSpv.data(), fragmentShaderSpv.size()),
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
    .pMultisampleState = &multisampleState,
    .pDepthStencilState = &depthStencilState,
    .pColorBlendState = &blendState,
    .layout = m_pipelineLayout,
    .renderPass = VK_NULL_HANDLE,
  };
#ifndef USE_RENDERPASS
  // Dynamic Rendering を使う際にはこちらの構造体も必要.
  VkFormat colorFormats[] = {
    gfxDevice->GetSwapchainFormat().format,
  };
  VkPipelineRenderingCreateInfo renderingCI{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = colorFormats,
  };
  pipelineCreateInfo.pNext = &renderingCI; // ここから参照させる.
#else
  pipelineCreateInfo.renderPass = m_renderPass;
#endif

  auto res = vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_pipeline);
  if (res != VK_SUCCESS)
  {

  }
  for (auto& m : shaderStages)
  {
    gfxDevice->DestroyShaderModule(m.module);
  }

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
  VkAttachmentReference colorRefs{
    .attachment = 0,
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };

  VkSubpassDescription subpass{
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = 1,
    .pColorAttachments = &colorRefs,
  };

  VkRenderPassCreateInfo renderPassCI{
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &colorAttachment,
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
    auto view = device->GetSwapchainImageView(i);

    VkFramebufferCreateInfo fbCI{
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass = m_renderPass,
      .attachmentCount = 1,
      .pAttachments = &view,
      .width = uint32_t(width),
      .height = uint32_t(height),
      .layers = 1,
    };
    vkCreateFramebuffer(vkDevice, &fbCI, nullptr, &m_framebuffers[i]);
  }
}
