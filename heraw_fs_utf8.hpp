// heraw_fs_utf8.hpp -- C++20 UTF-8 filesystem path bridge & RAII thread helpers (pure 7-bit ASCII)
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace heraw_fs {

inline std::filesystem::path from_utf8(std::string_view sv) {
    if (sv.empty()) {
        return std::filesystem::path();
    }
    return std::filesystem::path(std::u8string_view(
        reinterpret_cast<const char8_t*>(sv.data()), sv.size()));
}

inline std::string to_utf8(const std::filesystem::path& p) {
    const std::u8string u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

inline std::string join_utf8(std::string_view dir, std::string_view leaf) {
    return to_utf8(from_utf8(dir) / from_utf8(leaf));
}

inline std::string replace_ext_utf8(std::string_view utf8_path, std::string_view new_ext = {}) {
    auto p = from_utf8(utf8_path);
    p.replace_extension(from_utf8(new_ext));
    return to_utf8(p);
}

} // namespace heraw_fs

namespace heraw_sync {

#ifdef __APPLE__
using NativeWaitInt = int64_t;
#else
using NativeWaitInt = int32_t;
#endif

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
using JoiningThread = std::jthread;
#else
class JoiningThread {
public:
    JoiningThread() noexcept = default;

    template <class Fn, class... Args>
    explicit JoiningThread(Fn&& fn, Args&&... args)
        : t_(std::forward<Fn>(fn), std::forward<Args>(args)...) {}

    ~JoiningThread() {
        if (t_.joinable()) {
            t_.join();
        }
    }

    JoiningThread(const JoiningThread&) = delete;
    JoiningThread& operator=(const JoiningThread&) = delete;

    JoiningThread(JoiningThread&& other) noexcept = default;
    JoiningThread& operator=(JoiningThread&& other) noexcept {
        if (this != &other) {
            if (t_.joinable()) {
                t_.join();
            }
            t_ = std::move(other.t_);
        }
        return *this;
    }

    bool joinable() const noexcept { return t_.joinable(); }
    void join() {
        if (t_.joinable()) {
            t_.join();
        }
    }

private:
    std::thread t_;
};
#endif

} // namespace heraw_sync
