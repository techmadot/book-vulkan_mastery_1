#ifdef __ANDROID__
#include <jni.h>
#endif

#include <memory>
#include "App.h"
#include "Window.h"
#include "imgui.h"

#include <game-activity/GameActivity.cpp>
#include <game-text-input/gametextinput.cpp>

extern "C"
{
#include <game-activity/native_app_glue/android_native_app_glue.c>
};

std::unique_ptr<Application> theApp;

void handle_cmd(android_app *pApp, int32_t cmd)
{
  auto& window = GetAppWindow();

  switch (cmd) {
    case APP_CMD_INIT_WINDOW:
      if(theApp) {
        if (theApp->IsInitialized())
        {
          theApp->Shutdown();
        }
        theApp->Initialize();
      }
      break;

    case APP_CMD_TERM_WINDOW:
      theApp->Shutdown();
      break;

    case APP_CMD_WINDOW_RESIZED:
      if (theApp)
      {
        // auto width = ANativeWindow_getWidth(pApp->window);
        // auto height = ANativeWindow_getHeight(pApp->window);
        theApp->SurfaceSizeChanged();
      }
      break;
    default:
      break;
  }
}
int32_t handle_input(struct android_app* app)
{
  auto inputBuffer = android_app_swap_input_buffers(app);
  auto& io = ImGui::GetIO();
  if ( inputBuffer == nullptr )
  {
    return 0;
  }

  int32_t processed = 0;
  for (int i = 0; i < inputBuffer->motionEventsCount; ++i)
  {
    auto& event = inputBuffer->motionEvents[i];
    auto action = event.action;
    auto pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)>> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    auto& pointer = event.pointers[pointerIndex];
    auto x= GameActivityPointerAxes_getX(&pointer);
    auto y = GameActivityPointerAxes_getY(&pointer);
    auto scrollH = GameActivityPointerAxes_getAxisValue(&pointer, AMOTION_EVENT_AXIS_HSCROLL);
    auto scrollV = GameActivityPointerAxes_getAxisValue(&pointer, AMOTION_EVENT_AXIS_VSCROLL);

    switch(action & AMOTION_EVENT_ACTION_MASK)
    {
      case AMOTION_EVENT_ACTION_DOWN:
      case AMOTION_EVENT_ACTION_UP:
        io.AddMousePosEvent(x, y);
        io.AddMouseButtonEvent(0, action == AMOTION_EVENT_ACTION_DOWN);
        processed = 1;
        break;

      case AMOTION_EVENT_ACTION_HOVER_MOVE:
      case AMOTION_EVENT_ACTION_MOVE:
        io.AddMousePosEvent(x, y);
        processed = 1;
        break;

      default:
        break;
    }
  }
  android_app_clear_motion_events(inputBuffer);
  return processed;
}

bool motion_event_filter_func(const GameActivityMotionEvent *motionEvent) {
  auto sourceClass = motionEvent->source & AINPUT_SOURCE_CLASS_MASK;
  return (sourceClass == AINPUT_SOURCE_CLASS_POINTER ||
          sourceClass == AINPUT_SOURCE_CLASS_JOYSTICK);
}

extern "C"
{
void android_main(struct android_app *pApp)
{
  theApp = std::make_unique<Application>();
  theApp->SetAndroidApp(pApp);

  pApp->onAppCmd = handle_cmd;
  android_app_set_motion_event_filter(pApp, motion_event_filter_func);

  int events;
  android_poll_source *pSource;

  auto& window = GetAppWindow();
  while(!pApp->destroyRequested)
  {
    if (ALooper_pollAll(0, nullptr, &events, (void **) &pSource) >= 0)
    {
      if (pSource) {
        pSource->process(pApp, pSource);
      }
    }
    if(theApp->IsInitialized())
    {
      handle_input(pApp);
      theApp->Process();
    }
  }
}
} // extern "C"

