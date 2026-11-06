#include "log.hpp"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

namespace wasd::log {
namespace {

std::FILE* g_file = nullptr;
std::mutex g_mutex;

}  // namespace

void Open(const char* path, bool append)
{
    std::lock_guard lock(g_mutex);
    g_file = std::fopen(path, append ? "a" : "w");
    if (!g_file) {
        std::fprintf(stderr, "BG3 WASD Everywhere: cannot open log %s: %s\n", path, std::strerror(errno));
    }
}

void Write(const char* level, const char* format, ...)
{
    std::lock_guard lock(g_mutex);
    std::FILE* out = g_file ? g_file : stderr;

    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char stamp[16];
    std::strftime(stamp, sizeof stamp, "%H:%M:%S", &local);
    std::fprintf(out, "[%s] [%s] ", stamp, level);

    va_list args;
    va_start(args, format);
    std::vfprintf(out, format, args);
    va_end(args);

    std::fputc('\n', out);
    std::fflush(out);
}

}  // namespace wasd::log
