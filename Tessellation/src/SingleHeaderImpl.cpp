#include "BasePlatform.h"

#if defined(PLATFORM_WINDOWS)
# define VK_USE_PLATFORM_WIN32_KHR
#endif

#if defined(PLATFORM_LINUX)
# ifndef VK_USE_PLATFORM_WAYLAND_KHR
#  define VK_USE_PLATFORM_WAYLAND_KHR
# endif
#endif

#if defined(PLATFORM_ANDROID)
# define VK_USE_PLATFORM_ANDROID_KHR
# define VK_KHR_surface
#endif

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#ifdef PLATFORM_LINUX
# include <vulkan/vulkan_wayland.h>
#endif
#ifdef PLATFORM_ANDROID
# include <vulkan/vulkan_android.h>
#endif

#define VOLK_IMPLEMENTATION
#include "Volk/volk.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"
