#pragma once

namespace wasd::log {

// Opens the log file, truncating it unless `append` is set. Before this is called, messages go to
// stderr.
void Open(const char* path, bool append = false);

void Write(const char* level, const char* format, ...) __attribute__((format(printf, 2, 3)));

}  // namespace wasd::log

#define LOG_INFO(...) ::wasd::log::Write("INFO", __VA_ARGS__)
#define LOG_WARN(...) ::wasd::log::Write("WARN", __VA_ARGS__)
#define LOG_ERROR(...) ::wasd::log::Write("ERROR", __VA_ARGS__)
