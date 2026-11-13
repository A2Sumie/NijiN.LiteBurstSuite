// File: vk_platform.h
// Copyright 2014-2026 The Khronos Group Inc.
// SPDX-License-Identifier: Apache-2.0 OR MIT

#ifndef VK_PLATFORM_H_
#define VK_PLATFORM_H_

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
    #define VKAPI_ATTR
    #define VKAPI_CALL __stdcall
    #define VKAPI_PTR  VKAPI_CALL
#else
    #define VKAPI_ATTR
    #define VKAPI_CALL
    #define VKAPI_PTR
#endif

#if !defined(VK_NO_STDDEF_H)
    #include <stddef.h>
#endif

#if !defined(VK_NO_STDINT_H)
    #include <stdint.h>
#endif

#ifdef __cplusplus
}
#endif

#endif
