// Starts the game with the library preloaded, from Steam. Steam for macOS can't set environment
// variables from launch options, but it does run a program named there. Set the game's launch
// options to
//
//   /absolute/path/to/bg3-wasd-everywhere-launch %command%
//
// Steam replaces %command% with the game's .app. This program starts the executable inside it with
// libbg3-wasd-everywhere.dylib (from its own folder) added to DYLD_INSERT_LIBRARIES, keeping what
// Steam put there, and replaces itself with the game, so Steam keeps tracking the same process.
//
// It is a compiled program on purpose: macOS clears DYLD_* variables when it starts a system shell,
// so a shell script would lose the ones Steam sets.
//
// macOS counts the game as Steam's, the process that started it, when it asks about privacy
// permissions. So the first start asks once whether Steam may access the Documents folder, where
// the game keeps its profiles and saves; allow it. Making the game responsible for itself
// (responsibility_spawnattrs_setdisclaim) avoided that question, but in the one test with it, on
// 2026-11-07, the game hung while loading. So the launcher doesn't do it.

#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {

constexpr const char* kLibrary = "libbg3-wasd-everywhere.dylib";

std::string Directory(const std::string& path)
{
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

std::string OwnDirectory()
{
    char path[PATH_MAX];
    uint32_t size = sizeof path;
    char resolved[PATH_MAX];
    if (_NSGetExecutablePath(path, &size) != 0 || !realpath(path, resolved)) {
        return ".";
    }
    return Directory(resolved);
}

// The executable of an .app bundle, or the path itself if it isn't a bundle.
std::string Executable(const std::string& path)
{
    struct stat info {};
    if (stat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
        return path;
    }
    std::string executable = path;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr,
        reinterpret_cast<const UInt8*>(path.c_str()), CFIndex(path.size()), true);
    CFBundleRef bundle = url ? CFBundleCreate(nullptr, url) : nullptr;
    CFURLRef executable_url = bundle ? CFBundleCopyExecutableURL(bundle) : nullptr;
    char buffer[PATH_MAX];
    if (executable_url && CFURLGetFileSystemRepresentation(executable_url, true,
                              reinterpret_cast<UInt8*>(buffer), sizeof buffer)) {
        executable = buffer;
    }
    if (executable_url) {
        CFRelease(executable_url);
    }
    if (bundle) {
        CFRelease(bundle);
    }
    if (url) {
        CFRelease(url);
    }
    return executable;
}

}  // namespace

int main(int argc, char** argv)
{
    const char* home = std::getenv("HOME");
    const std::string logs = std::string(home && *home ? home : "/tmp") + "/Library/Logs/bg3-wasd-everywhere";
    mkdir(logs.c_str(), 0755);
    // A record of how Steam started us, next to the library's own log.
    std::FILE* log = std::fopen((logs + "/launch.log").c_str(), "w");
    auto note = [&](const char* format, const char* value) {
        if (log) {
            std::fprintf(log, format, value);
            std::fputc('\n', log);
            std::fflush(log);
        }
    };
    const std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    note("started %s", stamp);
    for (int i = 0; i < argc; ++i) {
        note("argument: %s", argv[i]);
    }
    if (argc < 2) {
        note("%s", "usage: bg3-wasd-everywhere-launch <game .app or executable> [arguments...]");
        std::fprintf(stderr, "usage: %s <game .app or executable> [arguments...]\n", argv[0]);
        return 2;
    }

    const std::string library = OwnDirectory() + "/" + kLibrary;
    const char* inherited = std::getenv("DYLD_INSERT_LIBRARIES");
    note("DYLD_INSERT_LIBRARIES from Steam: %s", inherited && *inherited ? inherited : "(none)");
    const std::string insert = inherited && *inherited ? std::string(inherited) + ":" + library : library;
    setenv("DYLD_INSERT_LIBRARIES", insert.c_str(), 1);

    std::string executable = Executable(argv[1]);
    std::vector<char*> arguments;
    arguments.push_back(executable.data());
    for (int i = 2; i < argc; ++i) {
        arguments.push_back(argv[i]);
    }
    arguments.push_back(nullptr);

    // The library appends to its log, so start it fresh, as run-game.sh does.
    unlink((logs + "/bg3-wasd-everywhere.log").c_str());
    note("starting %s", executable.c_str());
    note("DYLD_INSERT_LIBRARIES=%s", insert.c_str());
    // Replaces this process, so Steam keeps tracking the same pid.
    execv(executable.c_str(), arguments.data());
    note("could not start it: %s", std::strerror(errno));
    std::fprintf(stderr, "%s: cannot start %s: %s\n", argv[0], executable.c_str(), std::strerror(errno));
    return 127;
}
