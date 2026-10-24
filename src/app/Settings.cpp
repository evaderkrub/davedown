#include "app/Settings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace dd {

const char* viewModeName(ViewMode mode)
{
    switch (mode) {
    case ViewMode::Source: return "source";
    case ViewMode::Edit:   return "edit";
    default:               return "preview";
    }
}

bool viewModeFromName(std::string_view name, ViewMode& out)
{
    if (name == "preview") { out = ViewMode::Preview; return true; }
    if (name == "source")  { out = ViewMode::Source;  return true; }
    if (name == "edit")    { out = ViewMode::Edit;    return true; }
    return false;
}

float clampUiScale(float scale)
{
    if (!std::isfinite(scale) || scale <= 0.0f) return 1.0f;
    return std::clamp(scale, 0.7f, 2.0f);
}

std::string serializeSettings(const Settings& s)
{
    std::string out;
    out += "# davedown settings\n";
    out += "theme=" + s.themeName + "\n";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f", (double)s.uiScale);
    out += std::string("ui_scale=") + buf + "\n";
    out += std::string("default_mode=") + viewModeName(s.defaultMode) + "\n";
    out += std::string("show_outline=") + (s.showOutline ? "1" : "0") + "\n";
    out += "window_width=" + std::to_string(s.windowWidth) + "\n";
    out += "window_height=" + std::to_string(s.windowHeight) + "\n";
    out += std::string("window_maximized=") + (s.windowMaximized ? "1" : "0") + "\n";
    for (const std::string& r : s.recentFiles) out += "recent=" + r + "\n";
    return out;
}

Settings parseSettings(std::string_view text)
{
    Settings s;
    size_t start = 0;
    while (start < text.size()) {
        size_t nl = text.find('\n', start);
        std::string_view line = text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        start = nl == std::string_view::npos ? text.size() : nl + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view key = line.substr(0, eq);
        const std::string value(line.substr(eq + 1));

        auto toInt = [&](int fallback) {
            char* end = nullptr;
            const long v = std::strtol(value.c_str(), &end, 10);
            return (end == value.c_str() || *end != '\0') ? fallback : (int)v;
        };

        if (key == "theme") { if (!value.empty()) s.themeName = value; }
        else if (key == "ui_scale") {
            char* end = nullptr;
            const float v = std::strtof(value.c_str(), &end);
            if (end != value.c_str() && *end == '\0') s.uiScale = clampUiScale(v);
        }
        else if (key == "default_mode") { ViewMode m; if (viewModeFromName(value, m)) s.defaultMode = m; }
        else if (key == "show_outline") s.showOutline = toInt(1) != 0;
        else if (key == "window_width") s.windowWidth = std::max(400, toInt(s.windowWidth));
        else if (key == "window_height") s.windowHeight = std::max(300, toInt(s.windowHeight));
        else if (key == "window_maximized") s.windowMaximized = toInt(0) != 0;
        else if (key == "recent") {
            if (!value.empty() && s.recentFiles.size() < Settings::kMaxRecent) s.recentFiles.push_back(value);
        }
    }
    return s;
}

void addRecentFile(Settings& s, const std::string& path)
{
    if (path.empty()) return;
    removeRecentFile(s, path);
    s.recentFiles.insert(s.recentFiles.begin(), path);
    if (s.recentFiles.size() > Settings::kMaxRecent) s.recentFiles.resize(Settings::kMaxRecent);
}

void removeRecentFile(Settings& s, const std::string& path)
{
    s.recentFiles.erase(std::remove(s.recentFiles.begin(), s.recentFiles.end(), path),
                        s.recentFiles.end());
}

} // namespace dd
