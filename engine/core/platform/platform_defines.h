#pragma once

// Detect the compilation target here; callers must not select an OS using
// compiler macros or runtime resource configuration. All selectors are 0/1.
#if defined(_WIN32)
#define WITH_WIN 1
#define WITH_MAC 0
#define WITH_IOS 0
#define WITH_ANDROID 0
#define WITH_LINUX 0
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#define WITH_WIN 0
#define WITH_ANDROID 0
#define WITH_LINUX 0
#if TARGET_OS_OSX
#define WITH_MAC 1
#define WITH_IOS 0
#elif TARGET_OS_IOS && !TARGET_OS_MACCATALYST
#define WITH_MAC 0
#define WITH_IOS 1
#else
#error "Toy3d does not define platform selectors for this Apple target."
#endif
#elif defined(__ANDROID__)
// Android also defines __linux__; select it before desktop Linux.
#define WITH_WIN 0
#define WITH_MAC 0
#define WITH_IOS 0
#define WITH_ANDROID 1
#define WITH_LINUX 0
#elif defined(__linux__)
#define WITH_WIN 0
#define WITH_MAC 0
#define WITH_IOS 0
#define WITH_ANDROID 0
#define WITH_LINUX 1
#else
#error "Toy3d does not define platform selectors for this target OS."
#endif

// CPU architecture is independent of OS. Unknown architectures leave both
// selectors false so consumers such as toolchain discovery can reject them.
#if defined(_M_ARM64) || defined(__aarch64__)
#define TOY3D_ARCH_ARM64 1
#define TOY3D_ARCH_X64 0
#elif defined(_M_X64) || defined(__x86_64__)
#define TOY3D_ARCH_ARM64 0
#define TOY3D_ARCH_X64 1
#else
#define TOY3D_ARCH_ARM64 0
#define TOY3D_ARCH_X64 0
#endif
