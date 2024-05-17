// 本ヘッダファイルはプラットフォーム判定のために使用するものです.
// プラットフォーム判定分岐が必要なソースコードでは本ファイルを使用してください.
#pragma once

#if defined(_WIN32)
#define PLATFORM_WINDOWS 1
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#define PLATFORM_LINUX 1
#endif

#if defined(__linux__) && defined(__ANDROID__)
#define PLATFORM_ANDROID 1
#endif

