#pragma once

namespace wasd::log {

// Opens the log file, truncating it unless `append` is set. Before this is called, messages go to
// stderr.
void Open(const char* path, bool append = false);

void Write(const char* level, const char* format, ...) __attribute__((format(printf, 2, 3)));

// DebugLog in the config: the mod's per-action decisions (each click, rotation, cursor move) go to
// the log only while it is on. Startup, settings and state changes are always logged.
void SetDebug(bool on);
bool DebugEnabled();

}  // namespace wasd::log

#define LOG_INFO(...) ::wasd::log::Write("INFO", __VA_ARGS__)
#define LOG_WARN(...) ::wasd::log::Write("WARN", __VA_ARGS__)
#define LOG_ERROR(...) ::wasd::log::Write("ERROR", __VA_ARGS__)
#define LOG_DEBUG(...)                                \
    do {                                              \
        if (::wasd::log::DebugEnabled()) {            \
            ::wasd::log::Write("DEBUG", __VA_ARGS__); \
        }                                             \
    } while (0)
