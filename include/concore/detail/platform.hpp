#pragma once

// User can specify a "platform include" that can override everything that's in here
#ifdef CONCORE_PLATFORM_INCLUDE
#include CONCORE_PLATFORM_INCLUDE
#endif

// Detect the platform (if we were not given one)
#ifndef CONCORE_PLATFORM

#if __APPLE__
#define CONCORE_PLATFORM_APPLE 1
#elif __linux__ || __FreeBSD__ || __NetBSD__ || __OpenBSD__
#define CONCORE_PLATFORM_LINUX 1
#elif _MSC_VER && __MINGW64__ || __MINGW32__
#define CONCORE_PLATFORM_WINDOWS 1
#else
#define CONCORE_PLATFORM_UNKNOWN 1
#endif

#define CONCORE_PLATFORM(X) (CONCORE_PLATFORM_##X)
#endif

// Detect the C++ version
#ifndef CONCORE_CPP_VERSION

#if __cplusplus == 201103L
#define CONCORE_CPP_VERSION 11
#elif __cplusplus == 201402L
#define CONCORE_CPP_VERSION 14
#elif __cplusplus == 201703L
#define CONCORE_CPP_VERSION 17
#else
// Assume C++20
#define CONCORE_CPP_VERSION 20
#endif

// Detect use of pthreads; use it on Linux
#if !CONCORE_USE_PTHREADS && CONCORE_PLATFORM_LINUX
#define CONCORE_USE_PTHREADS 1
#endif

// Detect the use of libdispatch; use it on Apple
#if !CONCORE_USE_PTHREADS && CONCORE_PLATFORM_APPLE
#define CONCORE_USE_LIBDISPATCH 1
#endif

#endif