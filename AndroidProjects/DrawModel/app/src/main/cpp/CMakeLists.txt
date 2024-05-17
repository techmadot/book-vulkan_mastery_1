# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

cmake_minimum_required(VERSION 3.22.1)

set(APP_NAME "drawmodel")
project(${APP_NAME})

set(COMMON_INCLUDE_DIR ${PROJECT_SOURCE_DIR}//../../../../../../Common/include)
set(APPLICATION_SRC_DIR ${PROJECT_SOURCE_DIR}/../../../../../../DrawModel)
set(COMMON_SRC_DIR ${PROJECT_SOURCE_DIR}//../../../../../../Common)

# C++標準を設定
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

#set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_ANDROID")

# stb
set(STB_INCLUDE_DIR ${COMMON_SRC_DIR}/stb)

# glm
set(GLM_INCLUDE_DIR ${COMMON_SRC_DIR}/glm)

# assimp
#   assimpのビルドオプションを設定
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
set(CMAKE_DEBUG_POSTFIX "" CACHE STRING "" FORCE)
set(LIBRARY_SUFFIX "" CACHE STRING "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
add_subdirectory(
        ${COMMON_SRC_DIR}/assimp    ## ソースディレクトリ
        ${CMAKE_BINARY_DIR}/assimp  ## バイナリ配置ディレクトリ
        )

# imgui
set(IMGUI_INCLUDES
        ${COMMON_SRC_DIR}/imgui ${COMMON_SRC_DIR}/imgui/backends)
set(IMGUI_SRCS
        ${COMMON_SRC_DIR}/imgui/imgui.cpp
        ${COMMON_SRC_DIR}/imgui/imgui_draw.cpp
        ${COMMON_SRC_DIR}/imgui/imgui_tables.cpp
        ${COMMON_SRC_DIR}/imgui/imgui_widgets.cpp
        ${COMMON_SRC_DIR}/imgui/backends/imgui_impl_android.cpp
        )

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions( -D_DEBUG -DDEBUG)
endif ()

# ソースファイルを指定
file(GLOB SOURCES "${APPLICATION_SRC_DIR}/src/*.cpp")
# ヘッダファイルを指定
file(GLOB HEADERS "${APPLICATION_SRC_DIR}/src/*.h")

#
## ImGui をスタティックライブラリとして先にビルドしてしまう ##
# imgui_impl_vulkan.cpp にのみ VK_NO_PROTOTYPES マクロを定義
#
add_library(imgui_impl_vulkan OBJECT ${COMMON_SRC_DIR}/imgui/backends/imgui_impl_vulkan.cpp )
target_compile_definitions(imgui_impl_vulkan PRIVATE VK_NO_PROTOTYPES VK_USE_PLATFORM_ANDROID)
target_include_directories(imgui_impl_vulkan PUBLIC ${IMGUI_INCLUDES} )

add_library(imgui STATIC
        ${IMGUI_SRCS}
        $<TARGET_OBJECTS:imgui_impl_vulkan> # オブジェクトライブラリを追加
        )
target_include_directories(imgui PUBLIC ${IMGUI_INCLUDES})
target_link_libraries(imgui)

# Creates your game shared library. The name must be the same as the
# one used for loading in your Kotlin/Java or AndroidManifest.txt files.
add_library(${APP_NAME} SHARED
        AndroidOut.cpp
        ${SOURCES}
        ${HEADERS}
        main.cpp
    )

# Searches for a package provided by the game activity dependency
find_package(game-activity REQUIRED CONFIG)

target_compile_definitions(${APP_NAME} PRIVATE VK_USE_PLATFORM_ANDROID)
target_include_directories(${APP_NAME} PUBLIC
        ${IMGUI_INCLUDES}
        ${STB_INCLUDE_DIR}
        ${APPLICATION_SRC_DIR}/src
        ${COMMON_INCLUDE_DIR}
        ${GLM_INCLUDE_DIR}
        ${ASSIMP_INCLUDE_DIRS}
        ${PROJECT_SOURCE_DIR}/ )

# Configure libraries CMake uses to link your target library.
target_link_libraries(${APP_NAME}
        assimp::assimp
        # The game activity
        game-activity::game-activity
        imgui
        android
        log)