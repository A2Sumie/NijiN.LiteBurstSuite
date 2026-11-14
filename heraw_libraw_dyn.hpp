// heraw_libraw_dyn.hpp -- Runtime custom LibRaw shared library loader (.dll / .dylib / .so)
// Falls back seamlessly to built-in static LibRaw 0.23.0-Snapshot202611 when unconfigured.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "../libraw-heraw/libraw/libraw.h"

namespace heraw_lr {

struct DynLibRaw {
    bool active = false;
    std::string lib_path;
    std::string ver_str;
    int ver_num = 0;
#ifdef _WIN32
    HMODULE handle = nullptr;
#else
    void* handle = nullptr;
#endif

    libraw_data_t* (*p_init)(unsigned int) = nullptr;
    const char*    (*p_version)(void) = nullptr;
    int            (*p_versionNumber)(void) = nullptr;
    int            (*p_open_file)(libraw_data_t*, const char*) = nullptr;
#ifdef _WIN32
    int            (*p_open_wfile)(libraw_data_t*, const wchar_t*) = nullptr;
#endif
    int            (*p_unpack)(libraw_data_t*) = nullptr;
    int            (*p_raw2image)(libraw_data_t*) = nullptr;
    int            (*p_COLOR)(libraw_data_t*, int, int) = nullptr;
    int            (*p_get_raw_width)(libraw_data_t*) = nullptr;
    int            (*p_get_raw_height)(libraw_data_t*) = nullptr;
    int            (*p_get_iwidth)(libraw_data_t*) = nullptr;
    int            (*p_get_iheight)(libraw_data_t*) = nullptr;
    float          (*p_get_cam_mul)(libraw_data_t*, int) = nullptr;
    float          (*p_get_pre_mul)(libraw_data_t*, int) = nullptr;
    float          (*p_get_rgb_cam)(libraw_data_t*, int, int) = nullptr;
    int            (*p_get_color_maximum)(libraw_data_t*) = nullptr;
    libraw_iparams_t*  (*p_get_iparams)(libraw_data_t*) = nullptr;
    libraw_imgother_t* (*p_get_imgother)(libraw_data_t*) = nullptr;
    void           (*p_subtract_black)(libraw_data_t*) = nullptr;
    const char*    (*p_strerror)(int) = nullptr;
    void           (*p_close)(libraw_data_t*) = nullptr;

    bool load(const std::string& path, std::string& err_out) {
        unload();
        if (path.empty()) return true;
#ifdef _WIN32
        int wn = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
        std::wstring wp(wn > 0 ? wn : 0, L'\0');
        if (wn > 0) MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), &wp[0], wn);
        handle = LoadLibraryW(wp.c_str());
        if (!handle) {
            err_out = "LoadLibraryW failed on: " + path;
            return false;
        }
        auto sym = [&](const char* name) -> void* {
            return reinterpret_cast<void*>(GetProcAddress(handle, name));
        };
#else
        handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            const char* e = dlerror();
            err_out = std::string("dlopen failed: ") + (e ? e : path.c_str());
            return false;
        }
        auto sym = [&](const char* name) -> void* { return dlsym(handle, name); };
#endif

        p_init              = reinterpret_cast<decltype(p_init)>(sym("libraw_init"));
        p_version           = reinterpret_cast<decltype(p_version)>(sym("libraw_version"));
        p_versionNumber     = reinterpret_cast<decltype(p_versionNumber)>(sym("libraw_versionNumber"));
        p_open_file         = reinterpret_cast<decltype(p_open_file)>(sym("libraw_open_file"));
#ifdef _WIN32
        p_open_wfile        = reinterpret_cast<decltype(p_open_wfile)>(sym("libraw_open_wfile"));
#endif
        p_unpack            = reinterpret_cast<decltype(p_unpack)>(sym("libraw_unpack"));
        p_raw2image         = reinterpret_cast<decltype(p_raw2image)>(sym("libraw_raw2image"));
        p_COLOR             = reinterpret_cast<decltype(p_COLOR)>(sym("libraw_COLOR"));
        p_get_raw_width     = reinterpret_cast<decltype(p_get_raw_width)>(sym("libraw_get_raw_width"));
        p_get_raw_height    = reinterpret_cast<decltype(p_get_raw_height)>(sym("libraw_get_raw_height"));
        p_get_iwidth        = reinterpret_cast<decltype(p_get_iwidth)>(sym("libraw_get_iwidth"));
        p_get_iheight       = reinterpret_cast<decltype(p_get_iheight)>(sym("libraw_get_iheight"));
        p_get_cam_mul       = reinterpret_cast<decltype(p_get_cam_mul)>(sym("libraw_get_cam_mul"));
        p_get_pre_mul       = reinterpret_cast<decltype(p_get_pre_mul)>(sym("libraw_get_pre_mul"));
        p_get_rgb_cam       = reinterpret_cast<decltype(p_get_rgb_cam)>(sym("libraw_get_rgb_cam"));
        p_get_color_maximum = reinterpret_cast<decltype(p_get_color_maximum)>(sym("libraw_get_color_maximum"));
        p_get_iparams       = reinterpret_cast<decltype(p_get_iparams)>(sym("libraw_get_iparams"));
        p_get_imgother      = reinterpret_cast<decltype(p_get_imgother)>(sym("libraw_get_imgother"));
        p_subtract_black    = reinterpret_cast<decltype(p_subtract_black)>(sym("libraw_subtract_black"));
        p_strerror          = reinterpret_cast<decltype(p_strerror)>(sym("libraw_strerror"));
        p_close             = reinterpret_cast<decltype(p_close)>(sym("libraw_close"));

        if (!p_init || !p_open_file || !p_unpack || !p_raw2image || !p_COLOR ||
            !p_get_iwidth || !p_get_iheight || !p_get_iparams || !p_get_imgother || !p_close) {
            err_out = "Missing required LibRaw C API exports in: " + path;
            unload();
            return false;
        }
        ver_str = p_version ? p_version() : "custom";
        ver_num = p_versionNumber ? p_versionNumber() : 0;
        lib_path = path;
        active = true;
        return true;
    }

    void unload() {
        if (handle) {
#ifdef _WIN32
            FreeLibrary(handle);
#else
            dlclose(handle);
#endif
            handle = nullptr;
        }
        active = false;
    }

    int open_path(libraw_data_t* lr, const std::string& path) const {
#ifdef _WIN32
        if (p_open_wfile) {
            int wn = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
            std::wstring wp(wn > 0 ? wn : 0, L'\0');
            if (wn > 0) MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), &wp[0], wn);
            return p_open_wfile(lr, wp.c_str());
        }
#endif
        return p_open_file(lr, path.c_str());
    }
};

inline DynLibRaw& global_dyn_libraw() {
    static DynLibRaw g;
    return g;
}

} // namespace heraw_lr
