# Vulkan Mastery Vol.1

本リポジトリは 「Vulkan Mastery Vol.1」書籍のサンプルプログラムを提供しています。


# ビルドについて

本リポジトリのコードは、Windows, Linux, Androidそれぞれを対象としたビルドをサポートしています。


## Build (Windows)

### 前提条件

開発環境として、以下のものをシステムにインストールしてください

- Vulkan SDK 1.3.280
- Visual Studio 2022

### 手順

Developer Command Prompt for VS 2022を開き、Commonフォルダ内のPrepareAssimp.batを実行してください。
その後、各サンプルの sln を開いてビルド・実行してください。


## Build (Linux)

### 前提条件

WaylandとGLFW, cmakeを使用します。
以下のようなコマンドでシステムにインストールしてください。

```
sudo apt install build-essential cmake git
sudo apt install -y libglfw3-dev libglfw3-wayland
```

Vulkan SDKのインストールなどは、以下のページを参考にしてください。
https://vulkan.lunarg.com/doc/view/latest/linux/getting_started_ubuntu.html

### 手順

各サンプルのフォルダでcmakeによるビルドを実行してください。
手順の例を次に示します。

```
$ cd DrawModel
$ mkdir build; cd build; cmake -D CMAKE_BUILD_TYPE=Debug ..
$ make
```

### 注意事項

Ubuntu 22.04 + Wayland の環境にて、描画はうまくいくもののウィンドウ処理に不備があるようで、これは調査中となっています。

#### Raspberry Pi 向け

Raspberry Pi 4 + Raspberry Pi OS (64bit) の環境では、Dynamic Rendering を無効にしてコンパイル・実行してください。
たとえば、HelloTriangleにおいて、以下の変更を行います。

- `IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING` を無効
- `USE_RENDERPASS` を有効


## Android

Android Studio を使用し、AndroidProjects フォルダ以下のプロジェクトを使用してください。


# モデルデータについて

モデルデータは配布元の条件に従ってください。

- sponza, boxTextured
    - https://github.com/KhronosGroup/glTF-Sample-Assets
- alicia-solid
    - https://3d.nicovideo.jp/alicia/

# 免責事項

本リポジトリはサンプルコードの提供であり、ビルドや動作を保証するものではありません。
利用する場合には、利用者の責任において使用してください。
