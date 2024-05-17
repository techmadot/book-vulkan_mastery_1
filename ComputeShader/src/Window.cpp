#include "BasePlatform.h"
#include "Window.h"
#include <cassert>
#include <algorithm>

#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
#include "GLFW/glfw3.h"
#elif defined(PLATFORM_ANDROID)
#include <game-activity/native_app_glue/android_native_app_glue.h>
#endif


static std::unique_ptr<Window> gWindow = nullptr;

std::unique_ptr<Window>& GetAppWindow()
{
  if (gWindow == nullptr)
  {
    gWindow = std::make_unique<Window>();
  }
  return gWindow;

}

void error_callback(int error, const char* description)
{
  fprintf(stderr, "Error: %s\n", description);
}


void Window::Initialize(const WindowInitParams& initParams)
{
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
  glfwSetErrorCallback(error_callback);

  auto result = glfwInit();
  assert(result == GLFW_TRUE);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  m_platformHandle.window = glfwCreateWindow(initParams.width, initParams.height, initParams.title, nullptr, nullptr);
  glfwSetWindowUserPointer(m_platformHandle.window, this);
#endif

#if defined(PLATFORM_ANDROID)
  m_platformHandle.androidApp = initParams.android_app;
#endif
}

void Window::Shutdown()
{
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
  if (m_platformHandle.window != nullptr)
  {
    glfwDestroyWindow(m_platformHandle.window);
  }
  glfwTerminate();
#endif

#if defined(PLATFORM_ANDROID)
  m_platformHandle.androidApp = nullptr;
#endif
}

void Window::ProcessMessages()
{
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
  m_isExitRequested = glfwWindowShouldClose(m_platformHandle.window) == GLFW_TRUE;
  if (m_isExitRequested)
  {
    return;
  }
  glfwPollEvents();
#endif

#if defined(PLATFORM_ANDROID)
  auto* app = reinterpret_cast<android_app*>(m_platformHandle.androidApp);
  m_isExitRequested = app->destroyRequested != 0;
#endif
}

void Window::GetWindowSize(int& width, int& height)
{
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
  auto window = m_platformHandle.window;
  glfwGetWindowSize(window, &width, &height);
#endif

#if defined(PLATFORM_ANDROID)
  auto* app = reinterpret_cast<android_app*>(m_platformHandle.androidApp);
  width = ANativeWindow_getWidth(app->window);
  height = ANativeWindow_getHeight(app->window);
#endif
}

