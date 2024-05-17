cmake_minimum_required(VERSION 3.10)

# プロジェクト名と使用する言語を設定
set(APPNAME DrawModel)
project(${APPNAME} VERSION 1.0 LANGUAGES CXX)

set(COMMON_SRC_DIR ${PROJECT_SOURCE_DIR}/../Common)

# C++標準を設定
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# stb
set(STB_INCLUDE_DIR ${COMMON_SRC_DIR}/stb)

# glm
set(GLM_INCLUDE_DIR ${COMMON_SRC_DIR}/glm)

# デフォルトのビルドタイプをデバッグに設定
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# GLFW を探し、Wayland を使用するオプションを設定
find_package(glfw3 REQUIRED)
add_definitions(-DGLFW_USE_WAYLAND)

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
        ${COMMON_SRC_DIR}/assimp
        ${CMAKE_BINARY_DIR}/assimp)

# imgui
set(IMGUI_INCLUDES
        ${COMMON_SRC_DIR}/imgui ${COMMON_SRC_DIR}/imgui/backends)
set(IMGUI_SRCS
        ${COMMON_SRC_DIR}/imgui/imgui.cpp
        ${COMMON_SRC_DIR}/imgui/imgui_draw.cpp
        ${COMMON_SRC_DIR}/imgui/imgui_tables.cpp
        ${COMMON_SRC_DIR}/imgui/imgui_widgets.cpp
        ${COMMON_SRC_DIR}/imgui/backends/imgui_impl_glfw.cpp
        )

# ソースファイルを指定
file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/src/*.cpp")
# ヘッダーファイルを指定（プロジェクト内で参照するため）
file(GLOB HEADERS "${PROJECT_SOURCE_DIR}/src/*.h")

#
## ImGui をスタティックライブラリとして先にビルドしてしまう ##
# imgui_impl_vulkan.cpp にのみ VK_NO_PROTOTYPES マクロを定義
#
add_library(imgui_impl_vulkan OBJECT ${COMMON_SRC_DIR}/imgui/backends/imgui_impl_vulkan.cpp )
target_compile_definitions(imgui_impl_vulkan PRIVATE VK_NO_PROTOTYPES)
target_include_directories(imgui_impl_vulkan PUBLIC ${IMGUI_INCLUDES} )

add_library(imgui STATIC
        ${IMGUI_SRCS}
        $<TARGET_OBJECTS:imgui_impl_vulkan> # オブジェクトライブラリを追加
        )
target_include_directories(imgui PUBLIC ${IMGUI_INCLUDES})
target_link_libraries(imgui)


# 実行可能ファイルを指定したソースから作成
add_executable(${APPNAME} ${SOURCES} ${HEADERS})

target_compile_definitions(${APPNAME} PRIVATE
  $<$<CONFIG:Debug>:DEBUG>
  $<$<CONFIG:Debug>:_DEBUG>
)

# インクルードの設定
target_include_directories(${APPNAME} PUBLIC 
        ${COMMON_SRC_DIR}/include
        ${STB_INCLUDE_DIR}
        ${GLM_INCLUDE_DIR}
        ${IMGUI_INCLUDES}
        ${ASSIMP_INCLUDE_DIRS}
        ${PROJECT_SOURCE_DIR}/
        )

# リンクの設定
target_link_libraries(${APPNAME} glfw imgui assimp::assimp)
