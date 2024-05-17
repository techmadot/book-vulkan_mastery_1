#include "App.h"
#include "Window.h"
#include "GfxDevice.h"

#include "imgui.h"

#include "FileLoader.h"
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
  vulkanInfo.UseDynamicRendering = true;
  vulkanInfo.PipelineRenderingCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &colorFormat,
    .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
  };

  if (!gfxDevice->IsSupportVulkan13())
  {
    vulkanInfo.UseDynamicRendering = false;
    vulkanInfo.RenderPass = m_renderPass;
  }

  ImGui_ImplVulkan_Init(&vulkanInfo);
  ImGui_ImplVulkan_CreateFontsTexture();

  // アプリケーションコード初期化.
  PreparePipelines();

  PrepareSceneUniformBuffer();
  
  PrepareImageFilterResources();
}

void Application::Shutdown()
{
  auto& gfxDevice = GetGfxDevice();
  gfxDevice->WaitForIdle();

  DestroyImageFilterResources();
  DestroySceneUniformBuffer();

  auto vkDevice = gfxDevice->GetVkDevice();

  vkDestroyPipeline(vkDevice, m_computePipeline, nullptr);
  vkDestroyPipeline(vkDevice, m_graphicsPipeline, nullptr);
  m_computePipeline = VK_NULL_HANDLE;
  m_graphicsPipeline = VK_NULL_HANDLE;

  vkDestroyPipelineLayout(vkDevice, m_pipelineLayouts.compute, nullptr);
  vkDestroyPipelineLayout(vkDevice, m_pipelineLayouts.graphics, nullptr);
  m_pipelineLayouts.compute = VK_NULL_HANDLE;
  m_pipelineLayouts.graphics = VK_NULL_HANDLE;

  vkDestroyRenderPass(vkDevice, m_renderPass, nullptr);
  m_renderPass = VK_NULL_HANDLE;

  for (auto& f : m_framebuffers)
  {
    vkDestroyFramebuffer(vkDevice, f, nullptr);
  }
  m_framebuffers.clear();

  vkDestroyDescriptorSetLayout(vkDevice, m_descriptorSetLayouts.compute, nullptr);
  vkDestroyDescriptorSetLayout(vkDevice, m_descriptorSetLayouts.graphics, nullptr);
  m_descriptorSetLayouts.compute = VK_NULL_HANDLE;
  m_descriptorSetLayouts.graphics = VK_NULL_HANDLE;

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
  initParams.title = "ComputeShader";
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

  // テクスチャのレイアウト状態をGENERALに変更する.
  std::vector<VkImageMemoryBarrier2> barriers;
  barriers = {
    {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .srcAccessMask = m_sourceImage.accessFlags,
      .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
      .oldLayout = m_sourceImage.layout,
      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .image = m_sourceImage.image,
      .subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0, .levelCount = 1,
        .baseArrayLayer = 0, .layerCount = 1,
      }
    },
    {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .srcAccessMask = m_destinationImage.accessFlags,
      .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
      .oldLayout = m_destinationImage.layout,
      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .image = m_destinationImage.image,
      .subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0, .levelCount = 1,
        .baseArrayLayer = 0, .layerCount = 1,
      }
    },
  };
  VkDependencyInfo barrierInfo{
    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
    .imageMemoryBarrierCount = uint32_t(barriers.size()),
    .pImageMemoryBarriers = barriers.data(),
  };
  vkCmdPipelineBarrier2(commandBuffer, &barrierInfo);
  m_sourceImage.layout = VK_IMAGE_LAYOUT_GENERAL;
  m_sourceImage.accessFlags = VK_ACCESS_2_SHADER_READ_BIT;
  m_destinationImage.layout = VK_IMAGE_LAYOUT_GENERAL;
  m_destinationImage.accessFlags = VK_ACCESS_2_SHADER_WRITE_BIT;


  // コンピュートパイプラインを実行する.
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
  auto dsCompute = m_descriptorSets[gfxDevice->GetFrameIndex()].compute;
  vkCmdBindDescriptorSets(commandBuffer, 
    VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayouts.compute, 0, 1, &dsCompute, 0, nullptr);
  auto groupX = m_sourceImage.extent.width;
  auto groupY = m_sourceImage.extent.height;
  vkCmdDispatch(commandBuffer, groupX, groupY, 1);

  // 描画で使用するためテクスチャの状態を更新(SHADER_READ_ONLY_OPTIMAL)する.
  barriers = {
    {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .srcAccessMask = m_sourceImage.accessFlags,
      .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
      .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
      .oldLayout = m_sourceImage.layout,
      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .image = m_sourceImage.image,
      .subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0, .levelCount = 1,
        .baseArrayLayer = 0, .layerCount = 1,
      }
    },
    {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .srcAccessMask = m_destinationImage.accessFlags,
      .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
      .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
      .oldLayout = m_destinationImage.layout,
      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .image = m_destinationImage.image,
      .subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0, .levelCount = 1,
        .baseArrayLayer = 0, .layerCount = 1,
      }
    },
  };
  barrierInfo.imageMemoryBarrierCount = uint32_t(barriers.size());
  barrierInfo.pImageMemoryBarriers = barriers.data();
  vkCmdPipelineBarrier2(commandBuffer, &barrierInfo);
  m_sourceImage.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  m_sourceImage.accessFlags = VK_ACCESS_2_SHADER_READ_BIT;
  m_destinationImage.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  m_destinationImage.accessFlags = VK_ACCESS_2_SHADER_READ_BIT;

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

  // 描画する前に、シーン共通のパラメータを更新.
  SceneParameters sceneParams;
  sceneParams.matView = glm::mat4(1.0f);
  sceneParams.matProj = glm::ortho(-640.0f, 640.0f, -360.0f, 360.0f, -100.0f, 100.0f);
  sceneParams.modeParams = glm::vec4(float(m_filterMode), m_hueShift, 0.0f, 0.0f);

  memcpy(
    m_sceneUniformBuffers[gfxDevice->GetFrameIndex()].mapped,
    &sceneParams, sizeof(sceneParams));


  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertexBuffer.buffer, offsets);
  // 元画像を描画.
  vkCmdBindDescriptorSets(commandBuffer,
    VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayouts.graphics, 0, 
    1, &m_descriptorSets[gfxDevice->GetFrameIndex()].drawSrc,
    0, nullptr);
  vkCmdDraw(commandBuffer, 4, 1, 0, 0);

  // 結果画像を描画.
  vkCmdBindDescriptorSets(commandBuffer,
    VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayouts.graphics, 0,
    1, &m_descriptorSets[gfxDevice->GetFrameIndex()].drawDst,
    0, nullptr);
  vkCmdDraw(commandBuffer, 4, 1, 4, 0);

  // ImGui によるGui構築

  ImGui::Begin("Information");
  ImGui::Text("FPS: %.2f", ImGui::GetIO().Framerate);
  ImGui::Text(useDynamicRendering ? "USE Dynamic Rendering" : "USE RenderPass");

  ImGui::Combo("Mode", &m_filterMode, "Sepia\0Hue Shift\0\0");
  ImGui::SliderFloat("Offset", &m_hueShift, 0.0f, 1.0);

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

  VkAttachmentReference colorRefs{
    .attachment = 0,
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };

  VkSubpassDescription subpass{
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = 1,
    .pColorAttachments = &colorRefs,
  };

  std::vector<VkAttachmentDescription> attachmentDescriptions = { colorAttachment };

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

void Application::PreparePipelines()
{
  auto& gfxDevice = GetGfxDevice();
  auto vkDevice = gfxDevice->GetVkDevice();

  std::vector<VkDescriptorSetLayoutBinding> layoutBindings{
    // シーン全体で使用するユニフォームバッファ.
    {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    },
    // ソース画像
    {
      .binding = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    },
    // ディスティネーション画像
    {
      .binding = 2,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    },
  };
  VkDescriptorSetLayoutCreateInfo dsLayoutCI{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = uint32_t(layoutBindings.size()),
    .pBindings = layoutBindings.data(),
  };
  vkCreateDescriptorSetLayout(gfxDevice->GetVkDevice(), &dsLayoutCI, nullptr, &m_descriptorSetLayouts.compute);

  VkPipelineLayoutCreateInfo layoutCI{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1,
  };
  layoutCI.pSetLayouts = &m_descriptorSetLayouts.compute;
  vkCreatePipelineLayout(vkDevice, &layoutCI, nullptr, &m_pipelineLayouts.compute);

  // コンピュートパイプラインの作成.
  std::vector<char> computeSpv;
  GetFileLoader()->Load("res/shader.comp.spv", computeSpv);
  VkPipelineShaderStageCreateInfo computeStage{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
    .module = gfxDevice->CreateShaderModule(computeSpv.data(), computeSpv.size()),
    .pName = "main",
  };
  VkComputePipelineCreateInfo computePipelineCI{
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage = computeStage,
    .layout= m_pipelineLayouts.compute,
  };
  auto res = vkCreateComputePipelines(vkDevice, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr, &m_computePipeline);
  assert(res == VK_SUCCESS);


  // 結果描画用のディスクリプタセットレイアウトを作成.
  layoutBindings = {
    // シーン全体で使用するユニフォームバッファ.
    {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS
    },
    // フィルタ適用前・後のイメージセットを想定
    {
      .binding = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    },
  };
  dsLayoutCI.bindingCount = uint32_t(layoutBindings.size());
  dsLayoutCI.pBindings = layoutBindings.data();
  vkCreateDescriptorSetLayout(gfxDevice->GetVkDevice(), &dsLayoutCI, nullptr, &m_descriptorSetLayouts.graphics);

  layoutCI.pSetLayouts = &m_descriptorSetLayouts.graphics;
  vkCreatePipelineLayout(vkDevice, &layoutCI, nullptr, &m_pipelineLayouts.graphics);

  std::array<VkVertexInputBindingDescription, 1> vertexBindingDescs = { {
    { // POSITION & UV
      .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    },
  } };
  std::array<VkVertexInputAttributeDescription, 2> vertInputAttribs = { {
    { // POSITION
      .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0,
    },
    { // UV
      .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = sizeof(glm::vec3),
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
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

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

  // 上下を反転する.
  viewport.y = float(height);
  viewport.height = -float(height);

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
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  };

  VkPipelineColorBlendStateCreateInfo blendState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &blendAttachment
  };


  VkPipelineDepthStencilStateCreateInfo depthStencilState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    .depthTestEnable = VK_FALSE,
    .depthWriteEnable = VK_FALSE,
    .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
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
    .pMultisampleState = &multisampleState,
    .pDepthStencilState = &depthStencilState,
    .pColorBlendState = &blendState,
    .layout = m_pipelineLayouts.graphics,
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
  res = vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_graphicsPipeline);
  assert(res == VK_SUCCESS);

  for (auto& m : shaderStages)
  {
    gfxDevice->DestroyShaderModule(m.module);
  }
  gfxDevice->DestroyShaderModule(computeStage.module);

}

void Application::PrepareImageFilterResources()
{
  using namespace glm;
  std::filesystem::path filePath = "res/image.png";
  VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
  CreateTextureFromFile(m_sourceImage, filePath, imageUsage, 1);
  CreateTextureFromFile(m_destinationImage, filePath, imageUsage, 1);

  auto& gfxDevice = GetGfxDevice();
  auto vkDevice = gfxDevice->GetVkDevice();

  // サンプラー作成.
  VkSamplerCreateInfo samplerCI{
    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    .magFilter = VK_FILTER_LINEAR,
    .minFilter = VK_FILTER_LINEAR,
    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
  };
  vkCreateSampler(vkDevice, &samplerCI, nullptr, &m_sampler);

  {
    float offset = 10.0f;
    Vertex verts[] = {
      { vec3(-480.0f - offset, -135.0f, 0.0f), vec2(0.0f, 1.0f),},
      { vec3(   0.0f - offset, -135.0f, 0.0f), vec2(1.0f, 1.0f),},
      { vec3(-480.0f - offset,  135.0f, 0.0f), vec2(0.0f, 0.0f),},
      { vec3(   0.0f - offset,  135.0f, 0.0f), vec2(1.0f, 0.0f),},
      //----
      { vec3(   0.0f + offset, -135.0f, 0.0f), vec2(0.0f, 1.0f),},
      { vec3(+480.0f + offset, -135.0f, 0.0f), vec2(1.0f, 1.0f),},
      { vec3(   0.0f + offset,  135.0f, 0.0f), vec2(0.0f, 0.0f),},
      { vec3(+480.0f + offset,  135.0f, 0.0f), vec2(1.0f, 0.0f),},
    };
    m_vertexBuffer = gfxDevice->CreateBuffer(sizeof(verts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, verts);
  }

  // ディスクリプタセットの確保と書込み.
  m_descriptorSets.resize(GfxDevice::InflightFrames);
  for (uint32_t i = 0; i < GfxDevice::InflightFrames; ++i)
  {
    std::vector<VkDescriptorSetLayout> dsLayouts{
      m_descriptorSetLayouts.compute, 
      m_descriptorSetLayouts.graphics,
      m_descriptorSetLayouts.graphics,
    };
    VkDescriptorSetAllocateInfo dsAllocInfoComp = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = gfxDevice->GetDescriptorPool(),
      .descriptorSetCount = uint32_t(dsLayouts.size()),
      .pSetLayouts = dsLayouts.data(),
    };
    std::vector<VkDescriptorSet> descriptorSets(dsLayouts.size());
    auto res = vkAllocateDescriptorSets(vkDevice, &dsAllocInfoComp, descriptorSets.data());
    assert(res == VK_SUCCESS);

    VkDescriptorBufferInfo sceneUniformBuffer{
      .buffer = m_sceneUniformBuffers[i].buffer,
      .offset = 0, .range = VK_WHOLE_SIZE
    };
    VkWriteDescriptorSet writeSceneUbo{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptorSets[0], .dstBinding = 0, .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .pBufferInfo = &sceneUniformBuffer
    };
    VkDescriptorImageInfo descImageSource{
      .sampler = VK_NULL_HANDLE,
      .imageView = m_sourceImage.view,
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    VkWriteDescriptorSet writeSrcImage{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSets[0],
        .dstBinding = 1, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &descImageSource
    };

    VkDescriptorImageInfo descImageDestination{
      .sampler = VK_NULL_HANDLE,
      .imageView = m_destinationImage.view,
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    VkWriteDescriptorSet writeDestImage{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptorSets[0],
      .dstBinding = 2, .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .pImageInfo = &descImageDestination
    };

    // コンピュートパイプライン用.
    std::vector<VkWriteDescriptorSet> writeDescs;
    writeDescs = { writeSceneUbo, writeSrcImage, writeDestImage };
    vkUpdateDescriptorSets(vkDevice, uint32_t(writeDescs.size()), writeDescs.data(), 0, nullptr);
    m_descriptorSets[i].compute = descriptorSets[0];

    // グラフィックスパイプライン用.
    // 
    writeSrcImage.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descImageSource.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImageSource.sampler = m_sampler;

    writeDestImage.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descImageDestination.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImageDestination.sampler = m_sampler;

    writeSceneUbo.dstSet = descriptorSets[1];
    writeSrcImage.dstSet = descriptorSets[1];
    writeSrcImage.dstBinding = 1;
    writeDescs = { writeSceneUbo, writeSrcImage };
    vkUpdateDescriptorSets(vkDevice, uint32_t(writeDescs.size()), writeDescs.data(), 0, nullptr);
    m_descriptorSets[i].drawSrc = descriptorSets[1];

    writeSceneUbo.dstSet = descriptorSets[2];
    writeDestImage.dstSet = descriptorSets[2];
    writeDestImage.dstBinding = 1;
    writeDescs = { writeSceneUbo, writeDestImage };
    vkUpdateDescriptorSets(vkDevice, uint32_t(writeDescs.size()), writeDescs.data(), 0, nullptr);
    m_descriptorSets[i].drawDst = descriptorSets[2];
  }
}

void Application::DestroyImageFilterResources()
{
  auto& gfxDevice = GetGfxDevice();
  auto vkDevice = gfxDevice->GetVkDevice();

  vkDestroySampler(vkDevice, m_sampler, nullptr);
  gfxDevice->DestroyImage(m_sourceImage);
  gfxDevice->DestroyImage(m_destinationImage);
  gfxDevice->DestroyBuffer(m_vertexBuffer);
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

