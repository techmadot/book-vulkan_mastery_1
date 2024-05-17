#include "BasePlatform.h"
#include "App.h"
#include "Window.h"

#if defined(PLATFORM_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int __stdcall wWinMain(_In_ HINSTANCE hInstance,
  _In_opt_ HINSTANCE hPrevInstance,
  _In_ LPWSTR lpCmdLine,
  _In_ int nCmdShow)
{
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);

  auto theApp = std::make_unique<Application>();
  theApp->Initialize();

  auto& window = GetAppWindow();
  while (!window->IsExitRequired())
  {
    window->ProcessMessages();

    theApp->Process();
  }

  theApp->Shutdown();

  return 0;

}
#endif

#if defined(PLATFORM_LINUX)
int main(int argc, char* argv[])
{
  auto theApp = std::make_unique<Application>();
  theApp->Initialize();

  auto& window = GetAppWindow();
  while (!window->IsExitRequired())
  {
    window->ProcessMessages();

    theApp->Process();
  }

  theApp->Shutdown();
  return 0;
}
#endif
