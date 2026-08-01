#pragma once

#ifndef LLP_VERSION_MAJOR
#define LLP_VERSION_MAJOR 1
#endif

#ifndef LLP_VERSION_MINOR
#define LLP_VERSION_MINOR 11
#endif

#ifndef LLP_VERSION_PATCH
#define LLP_VERSION_PATCH 0
#endif

#ifndef LLP_VERSION_BUILD
#define LLP_VERSION_BUILD 0
#endif

#ifndef LLP_FILE_VERSION_COMMA
#define LLP_FILE_VERSION_COMMA LLP_VERSION_MAJOR, LLP_VERSION_MINOR, LLP_VERSION_PATCH, LLP_VERSION_BUILD
#endif

#ifndef LLP_FILE_VERSION_STRING
#define LLP_FILE_VERSION_STRING "1.11.0.0"
#endif

#ifndef LLP_BUILD_VERSION_STRING
#define LLP_BUILD_VERSION_STRING "1.11.0-dev+unknown"
#endif

#ifndef LLP_BUILD_VERSION_WIDE
#define LLP_BUILD_VERSION_WIDE L"1.11.0-dev+unknown"
#endif

#ifndef LLP_BUILD_COMMIT_STRING
#define LLP_BUILD_COMMIT_STRING "unknown"
#endif

#ifndef LLP_BUILD_COMMIT_WIDE
#define LLP_BUILD_COMMIT_WIDE L"unknown"
#endif

#ifndef LLP_BUILD_CHANNEL_STRING
#define LLP_BUILD_CHANNEL_STRING "development"
#endif

#ifndef LLP_BUILD_CHANNEL_WIDE
#define LLP_BUILD_CHANNEL_WIDE L"development"
#endif

#ifndef LLP_SINGLETON_NAME_WIDE
#define LLP_SINGLETON_NAME_WIDE L"Local\\LightLaunchpad.NativeUi.SingleInstance.development.unknown"
#endif
