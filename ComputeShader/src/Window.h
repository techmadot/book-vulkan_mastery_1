#pragma once
#include <memory>
#include <vector>
#include <cstdint>

#include "BasePlatform.h"

#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
struct GLFWwindow;
#endif

class Window
{
public:
  struct WindowInitParams
  {
    int width = 1280;
    int height = 720;
    const char* title = "SampleApp";

#if defined(PLATFORM_ANDROID)
    void* android_app = nullptr;
#endif
  };
  struct PlatformHandle
  {
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
    GLFWwindow* window;
#endif
#if defined(PLATFORM_ANDROID)
    void* androidApp;
#endif
  };

  void Initialize(const WindowInitParams& initParams);
  void Shutdown();

  bool IsExitRequired() const { return m_isExitRequested; }
  void ProcessMessages();

  const PlatformHandle* GetPlatformHandle() { return &m_platformHandle; }

  void GetWindowSize(int& width, int& height);
private:

  bool m_isExitRequested;
  PlatformHandle m_platformHandle;
};


std::unique_ptr<Window>& GetAppWindow();