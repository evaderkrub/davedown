// Persisted preferences. A flat key=value text file; parse and serialize are
// pure functions so the format is covered by the console tests.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace dd {

enum class ViewMode { Preview = 0, Source = 1, Edit = 2 };

const char* viewModeName(ViewMode mode);             // "preview" / "source" / "edit"
bool viewModeFromName(std::string_view name, ViewMode& out);

struct Settings {
    std::string themeName = "Wili Dark";
    float       uiScale = 1.0f;
    ViewMode    defaultMode = ViewMode::Preview;
    bool        showOutline = true;
    int         windowWidth = 1280;
    int         windowHeight = 820;
    bool        windowMaximized = false;
    std::vector<std::string> recentFiles;   // most recent first

    static constexpr size_t kMaxRecent = 10;
};

std::string serializeSettings(const Settings& settings);
// Unknown keys are ignored; malformed values keep the default. Always
// succeeds so a damaged file degrades to defaults rather than blocking startup.
Settings parseSettings(std::string_view text);

void addRecentFile(Settings& settings, const std::string& path);
void removeRecentFile(Settings& settings, const std::string& path);

float clampUiScale(float scale);   // 0.7 .. 2.0, 1.0 for garbage

} // namespace dd
