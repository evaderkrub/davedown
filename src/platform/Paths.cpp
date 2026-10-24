#include "platform/Paths.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace dd::platform {

namespace {

std::string normalizeSeparators(std::string path)
{
    for (char& c : path)
        if (c == '\\') c = '/';
    return path;
}

} // namespace

std::string exeDirectory()
{
    static std::string cached;
    if (!cached.empty()) return cached;
    const char* base = SDL_GetBasePath();   // owned by SDL, valid until SDL_Quit
    cached = base ? normalizeSeparators(base) : std::string("./");
    if (cached.empty() || cached.back() != '/') cached.push_back('/');
    return cached;
}

std::string resolve(const std::string& relative)
{
    return exeDirectory() + normalizeSeparators(relative);
}

namespace {
std::string g_settingsOverride;
}

void overrideSettingsPath(const std::string& path) { g_settingsOverride = path; }

std::string settingsPath()
{
    if (!g_settingsOverride.empty()) return g_settingsOverride;
    const std::string beside = resolve("davedown.settings");
    // Probe writability once: open for append creates the file if missing
    // without touching existing contents.
    {
        std::ofstream probe(beside, std::ios::app);
        if (probe) return beside;
    }
    char* pref = SDL_GetPrefPath("davedown", "davedown");
    std::string dir = pref ? normalizeSeparators(pref) : exeDirectory();
    if (pref) SDL_free(pref);
    return dir + "davedown.settings";
}

bool readTextFile(const std::string& path, std::string& out, std::string& error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Could not open \"" + path + "\".";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    error.clear();
    return true;
}

bool writeTextFile(const std::string& path, const std::string& text, std::string& error)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Could not write \"" + path + "\".";
        return false;
    }
    out.write(text.data(), (std::streamsize)text.size());
    if (!out) {
        error = "Write failed for \"" + path + "\".";
        return false;
    }
    error.clear();
    return true;
}

bool fileExists(const std::string& path)
{
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(path), ec);
}

bool isAbsoluteOrUrl(const std::string& path)
{
    if (path.empty()) return false;
    if (path[0] == '/' || path[0] == '\\') return true;
    if (path.size() >= 2 && std::isalpha((unsigned char)path[0]) && path[1] == ':') return true;
    return path.find("://") != std::string::npos;
}

std::string joinPath(const std::string& directory, const std::string& relative)
{
    if (isAbsoluteOrUrl(relative) || directory.empty()) return relative;
    std::string dir = normalizeSeparators(directory);
    if (dir.back() != '/') dir.push_back('/');
    std::string rel = normalizeSeparators(relative);
    if (rel.rfind("./", 0) == 0) rel.erase(0, 2);
    return dir + rel;
}

} // namespace dd::platform
