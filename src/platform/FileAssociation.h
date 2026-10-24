// Registering davedown as a handler for markdown files.
//
// Windows: per-user registry entries (no elevation) that make davedown
// appear in "Open with" and in Settings > Default apps, plus the shell's own
// "How do you want to open this file?" dialog, which is the only supported
// way for a user to pick the default on Windows 10/11 (the UserChoice key
// is hash-protected and not meant to be written by applications).
#pragma once

#include <string>

namespace dd::platform {

// Writes the registry entries for this executable. Safe to repeat.
bool registerMarkdownHandler(std::string& error);

// Opens the system dialog where the person confirms davedown as the default
// for `.md`. Returns false with a message if the dialog could not be shown.
bool promptMakeDefault(std::string& error);

// True when `.md` currently resolves to this executable.
bool isDefaultMarkdownHandler();

// False on platforms where none of this is implemented.
bool fileAssociationSupported();

} // namespace dd::platform
