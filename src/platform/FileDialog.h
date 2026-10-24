// Native open/save dialogs through SDL3. The dialogs are asynchronous: SDL
// may deliver the result from another thread, so results are queued and
// handed out on the main thread by poll().
#pragma once

#include <string>

struct SDL_Window;

namespace dd::platform {

enum class DialogKind { Open, Save };

struct DialogResult {
    DialogKind  kind = DialogKind::Open;
    bool        accepted = false;   // false = cancelled or failed
    std::string path;               // chosen file when accepted
    std::string error;              // SDL's message when it failed
    int         token = 0;          // the token passed to show*()
};

// `token` lets the caller tell which request a result belongs to (which
// document asked for Save As, for instance).
void showOpenDialog(SDL_Window* window, int token, const std::string& defaultDir);
void showSaveDialog(SDL_Window* window, int token, const std::string& defaultPath);

// Returns true and fills `out` when a result is waiting. Call every frame.
bool pollDialogResult(DialogResult& out);

} // namespace dd::platform
