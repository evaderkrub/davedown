// Where things live on disk. Every runtime path is resolved against the
// executable's own directory, never the working directory: a shortcut or a
// launch from another drive gives a different cwd and is how a portable
// build stops being portable.
#pragma once

#include <string>

namespace dd::platform {

// Directory holding the executable, with a trailing separator. Falls back to
// "./" if SDL cannot tell (it always can on the supported platforms).
std::string exeDirectory();

// exeDirectory() + relative, separators normalised to '/'.
std::string resolve(const std::string& relative);

// Settings file beside the executable. If that directory is not writable
// (an install under Program Files), the per-user preferences folder is used
// instead so the app still remembers its state.
std::string settingsPath();

// Test harnesses point the settings file elsewhere so a test run never
// touches the preferences of the application staged beside it.
void overrideSettingsPath(const std::string& path);

bool readTextFile(const std::string& path, std::string& out, std::string& error);
bool writeTextFile(const std::string& path, const std::string& text, std::string& error);
bool fileExists(const std::string& path);

// Joins a directory and a relative path; returns `relative` unchanged when
// it is already absolute or a URL.
std::string joinPath(const std::string& directory, const std::string& relative);
bool isAbsoluteOrUrl(const std::string& path);

} // namespace dd::platform
