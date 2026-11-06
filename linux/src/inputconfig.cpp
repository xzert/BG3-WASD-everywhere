#include "inputconfig.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "log.hpp"

namespace wasd::inputconfig {
namespace {

// One `"key": value` of the top-level object. Arrays of strings are parsed; anything else is kept
// exactly as it was in the file.
struct Entry {
    std::string key;
    bool string_array = false;
    std::vector<std::string> values;
    std::string raw;

    bool operator==(const Entry& other) const
    {
        return key == other.key && string_array == other.string_array &&
               (string_array ? values == other.values : raw == other.raw);
    }
};

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    bool ParseObject(std::vector<Entry>& entries)
    {
        SkipSpace();
        if (!Eat('{')) {
            return false;
        }
        SkipSpace();
        if (Eat('}')) {
            return AtEnd();
        }
        for (;;) {
            Entry entry;
            SkipSpace();
            if (!ParseString(entry.key)) {
                return false;
            }
            SkipSpace();
            if (!Eat(':')) {
                return false;
            }
            SkipSpace();
            const size_t start = i_;
            if (!SkipValue()) {
                return false;
            }
            entry.raw = s_.substr(start, i_ - start);
            entry.string_array = ParseStringArray(entry.raw, entry.values);
            entries.push_back(std::move(entry));
            SkipSpace();
            if (Eat(',')) {
                continue;
            }
            return Eat('}') && AtEnd();
        }
    }

    static bool ParseStringArray(const std::string& raw, std::vector<std::string>& out)
    {
        Parser p(raw);
        p.SkipSpace();
        if (!p.Eat('[')) {
            return false;
        }
        p.SkipSpace();
        if (p.Eat(']')) {
            return p.AtEnd();
        }
        for (;;) {
            std::string value;
            p.SkipSpace();
            if (!p.ParseString(value)) {
                out.clear();
                return false;
            }
            out.push_back(std::move(value));
            p.SkipSpace();
            if (p.Eat(',')) {
                continue;
            }
            if (p.Eat(']') && p.AtEnd()) {
                return true;
            }
            out.clear();
            return false;
        }
    }

private:
    void SkipSpace()
    {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) {
            ++i_;
        }
    }

    bool AtEnd()
    {
        SkipSpace();
        return i_ == s_.size();
    }

    bool Eat(char c)
    {
        if (i_ < s_.size() && s_[i_] == c) {
            ++i_;
            return true;
        }
        return false;
    }

    bool ParseString(std::string& out)
    {
        if (!Eat('"')) {
            return false;
        }
        while (i_ < s_.size()) {
            const char c = s_[i_++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i_ >= s_.size()) {
                return false;
            }
            const char escaped = s_[i_++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                out += escaped;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                if (i_ + 4 > s_.size()) {
                    return false;
                }
                char* end = nullptr;
                const std::string hex = s_.substr(i_, 4);
                const unsigned long code = std::strtoul(hex.c_str(), &end, 16);
                if (end != hex.c_str() + 4 || code >= 0x80) {
                    return false;  // key names are ASCII
                }
                out += static_cast<char>(code);
                i_ += 4;
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool SkipValue()
    {
        if (i_ >= s_.size()) {
            return false;
        }
        std::string ignored;
        if (s_[i_] == '"') {
            return ParseString(ignored);
        }
        if (s_[i_] == '{' || s_[i_] == '[') {
            int depth = 0;
            while (i_ < s_.size()) {
                const char c = s_[i_];
                if (c == '"') {
                    ignored.clear();
                    if (!ParseString(ignored)) {
                        return false;
                    }
                    continue;
                }
                ++i_;
                if (c == '{' || c == '[') {
                    ++depth;
                } else if ((c == '}' || c == ']') && --depth == 0) {
                    return true;
                }
            }
            return false;
        }
        const size_t start = i_;  // number, true, false, null
        while (i_ < s_.size() && !std::strchr(",}] \t\r\n", s_[i_])) {
            ++i_;
        }
        return i_ > start;
    }

    const std::string& s_;
    size_t i_ = 0;
};

std::string Quote(const std::string& value)
{
    std::string out = "\"";
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char escaped[8];
            std::snprintf(escaped, sizeof escaped, "\\u%04x", c);
            out += escaped;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

// Same layout as nlohmann's dump(4), which wrote the file on Windows: sorted keys, 4-space indent.
std::string Serialize(std::vector<Entry> entries, const char* eol)
{
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.key < b.key; });
    std::string out = std::string("{") + eol;
    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        out += "    " + Quote(entry.key) + ": ";
        if (!entry.string_array) {
            out += entry.raw;
        } else if (entry.values.empty()) {
            out += "[]";
        } else {
            out += std::string("[") + eol;
            for (size_t j = 0; j < entry.values.size(); ++j) {
                out += "        " + Quote(entry.values[j]) + (j + 1 < entry.values.size() ? "," : "") + eol;
            }
            out += "    ]";
        }
        out += (i + 1 < entries.size() ? "," : "") + std::string(eol);
    }
    return out + "}";
}

Entry* Find(std::vector<Entry>& entries, const std::string& key)
{
    for (Entry& entry : entries) {
        if (entry.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

Entry& FindOrAdd(std::vector<Entry>& entries, const std::string& key)
{
    if (Entry* entry = Find(entries, key)) {
        return *entry;
    }
    entries.push_back({ key, true, {}, "" });
    return entries.back();
}

bool Contains(const std::vector<std::string>& values, const std::string& value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

// FixInvalidBindings: keep at most two placeholder entries per command.
void FixInvalidBindings(std::vector<Entry>& entries)
{
    for (Entry& entry : entries) {
        if (!entry.string_array) {
            continue;
        }
        int unknown = 0;
        std::erase_if(entry.values, [&](const std::string& key) {
            return (key == "INVALID:unknown" || key == "key:unknown") && ++unknown > 2;
        });
    }
}

// UpdateData: character movement uses the camera movement keys; camera and character input have
// opposite y axes on the left stick.
void BindCharacterMovement(std::vector<Entry>& entries)
{
    struct Pair {
        const char* character;
        const char* camera;
        std::vector<std::string> camera_defaults;
    };
    static const Pair kPairs[] = {
        { "CharacterMoveBackward", "CameraBackward", { "c:leftstick_ypos", "key:s", "key:down" } },
        { "CharacterMoveForward", "CameraForward", { "c:leftstick_yneg", "key:w", "key:up" } },
        { "CharacterMoveLeft", "CameraLeft", { "c:leftstick_xneg", "key:a", "key:left" } },
        { "CharacterMoveRight", "CameraRight", { "c:leftstick_xpos", "key:d", "key:right" } },
    };
    for (const Pair& pair : kPairs) {
        std::vector<std::string> keys = pair.camera_defaults;
        if (const Entry* camera = Find(entries, pair.camera); camera && camera->string_array) {
            keys = camera->values;
        }
        for (std::string& key : keys) {
            if (key == "c:leftstick_yneg") {
                key = "c:leftstick_ypos";
            } else if (key == "c:leftstick_ypos") {
                key = "c:leftstick_yneg";
            }
        }
        Entry& character = FindOrAdd(entries, pair.character);
        character.string_array = true;
        character.values = keys;
    }
}

// FixRightclickBindings. When right click rotates the camera, its modifier variants must rotate too
// (they would otherwise trigger other commands mid-rotation), and Ping must not use right click.
// The Windows code adds the variants even when right click isn't bound to rotation, which its own
// comment says it shouldn't; this port follows the comment.
void FixRightclickBindings(std::vector<Entry>& entries)
{
    Entry* rotate = Find(entries, "CameraToggleMouseRotate");
    if (!rotate || !rotate->string_array) {
        return;
    }
    static const char* kVariants[] = { "alt+mouse:right", "shift+mouse:right", "ctrl+mouse:right" };
    if (!Contains(rotate->values, "mouse:right")) {
        std::erase_if(rotate->values, [](const std::string& key) {
            return key == "alt+mouse:right" || key == "shift+mouse:right" || key == "ctrl+mouse:right";
        });
        return;
    }
    for (const char* variant : kVariants) {
        if (!Contains(rotate->values, variant)) {
            rotate->values.push_back(variant);
        }
    }

    Entry& ping = FindOrAdd(entries, "Ping");
    if (!ping.string_array || (ping.values.empty() && ping.raw.empty())) {
        ping.string_array = true;
        ping.values = { "alt+mouse:right" };  // the game's default when Ping isn't in the file
    }
    int valid = 0;
    for (std::string& key : ping.values) {
        if (key.find("mouse:right") != std::string::npos) {
            key = "key:unknown";
        } else if (key.find("INVALID:unknown") == std::string::npos && key.find("key:unknown") == std::string::npos) {
            ++valid;
        }
    }
    if (valid == 0) {
        ping.values = { "ctrl+alt+key:f24" };
    }
}

std::vector<std::string> KeyboardAndMouseKeys(const Entry* entry)
{
    std::vector<std::string> keys;
    if (entry && entry->string_array) {
        for (const std::string& key : entry->values) {
            if (key.rfind("c:", 0) != 0 && key.find("unknown") == std::string::npos) {
                keys.push_back(key);
            }
        }
    }
    return keys;
}

std::string ReadFile(const std::string& path, bool& exists)
{
    std::ifstream file(path, std::ios::binary);
    exists = static_cast<bool>(file);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

bool WriteFileAtomically(const std::string& path, const std::string& text, mode_t mode, std::string& error)
{
    const std::string temp = path + ".bg3-wasd-everywhere.tmp";
    const int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) {
        error = std::strerror(errno);
        return false;
    }
    const bool written = write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
    const bool closed = close(fd) == 0;
    if (!written || !closed || rename(temp.c_str(), path.c_str()) != 0) {
        error = std::strerror(errno);
        unlink(temp.c_str());
        return false;
    }
    return true;
}

Result PatchFile(const std::string& path)
{
    bool exists = false;
    const std::string in = ReadFile(path, exists);
    std::string out;
    Result result = PatchText(in, out);
    if (!result.ok) {
        LOG_WARN("Key bindings in %s left alone: %s", path.c_str(), result.error.c_str());
        return result;
    }
    if (!result.changed) {
        LOG_INFO("Key bindings in %s are up to date", path.c_str());
        return result;
    }

    struct stat info {};
    const mode_t mode = exists && stat(path.c_str(), &info) == 0 ? (info.st_mode & 07777) : 0640;
    const std::string backup = path + ".bg3-wasd-everywhere.bak";
    if (exists && access(backup.c_str(), F_OK) != 0) {
        std::string ignored;
        WriteFileAtomically(backup, in, mode, ignored);
    }
    std::string error;
    if (!WriteFileAtomically(path, out, mode, error)) {
        LOG_WARN("Could not write key bindings to %s: %s", path.c_str(), error.c_str());
        result.ok = false;
        return result;
    }
    LOG_INFO("Bound character movement keys in %s%s", path.c_str(), exists ? "" : " (new file)");
    return result;
}

}  // namespace

Result PatchText(const std::string& in, std::string& out)
{
    Result result;
    std::vector<Entry> entries;
    const bool empty = std::all_of(in.begin(), in.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); });
    if (!empty && !Parser(in).ParseObject(entries)) {
        result.error = "not a JSON object the mod understands";
        return result;
    }
    const std::vector<Entry> original = entries;

    FixInvalidBindings(entries);
    BindCharacterMovement(entries);
    FixRightclickBindings(entries);

    auto sorted = [](std::vector<Entry> list) {
        std::sort(list.begin(), list.end(), [](const Entry& a, const Entry& b) { return a.key < b.key; });
        return list;
    };
    result.ok = true;
    result.changed = sorted(original) != sorted(entries);
    const char* eol = in.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    out = Serialize(entries, eol);
    if (empty || in.back() == '\n') {
        out += eol;
    }

    result.forward_keys = KeyboardAndMouseKeys(Find(entries, "CharacterMoveForward"));
    result.backward_keys = KeyboardAndMouseKeys(Find(entries, "CharacterMoveBackward"));
    if (const Entry* rotate = Find(entries, "CameraToggleMouseRotate"); rotate && rotate->string_array) {
        result.rotate_keys = rotate->values;
        for (const std::string& key : rotate->values) {
            result.rotate_includes_left_click |= key.find("mouse:left") != std::string::npos;
        }
    }
    return result;
}

Result PatchAllProfiles()
{
    std::string base;
    if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data) {
        base = data;
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        base = std::string(home) + "/.local/share";
    }
    const std::string profiles = base + "/Larian Studios/Baldur's Gate 3/PlayerProfiles";

    DIR* dir = opendir(profiles.c_str());
    if (!dir) {
        LOG_WARN("No player profiles in %s yet; bind the movement keys after starting the game once",
            profiles.c_str());
        return {};
    }
    Result chosen;
    bool have_public = false;
    while (const dirent* item = readdir(dir)) {
        const std::string name = item->d_name;
        struct stat info {};
        if (name == "." || name == ".." || stat((profiles + "/" + name).c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
            continue;
        }
        Result result = PatchFile(profiles + "/" + name + "/inputconfig_p1.json");
        if (result.ok && !have_public && (name == "Public" || !chosen.ok)) {
            chosen = result;
            have_public = name == "Public";
        }
    }
    closedir(dir);
    return chosen;
}

}  // namespace wasd::inputconfig
