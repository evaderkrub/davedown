#include "platform/FileDialog.h"

#include <SDL3/SDL.h>

#include <deque>
#include <mutex>

namespace dd::platform {

namespace {

std::mutex                g_mutex;
std::deque<DialogResult>  g_results;

struct Request {
    DialogKind kind;
    int        token;
};

const SDL_DialogFileFilter kFilters[] = {
    { "Markdown files", "md;markdown;mdown;mkd;txt" },
    { "All files", "*" },
};

void SDLCALL onDialogDone(void* userdata, const char* const* filelist, int /*filter*/)
{
    Request* req = static_cast<Request*>(userdata);
    DialogResult r;
    r.kind = req->kind;
    r.token = req->token;
    if (!filelist) {
        r.accepted = false;
        r.error = SDL_GetError();
    } else if (!filelist[0]) {
        r.accepted = false;   // cancelled
    } else {
        r.accepted = true;
        r.path = filelist[0];
    }
    delete req;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_results.push_back(std::move(r));
}

} // namespace

void showOpenDialog(SDL_Window* window, int token, const std::string& defaultDir)
{
    auto* req = new Request{DialogKind::Open, token};
    SDL_ShowOpenFileDialog(onDialogDone, req, window, kFilters, (int)SDL_arraysize(kFilters),
                           defaultDir.empty() ? nullptr : defaultDir.c_str(), false);
}

void showSaveDialog(SDL_Window* window, int token, const std::string& defaultPath)
{
    auto* req = new Request{DialogKind::Save, token};
    SDL_ShowSaveFileDialog(onDialogDone, req, window, kFilters, (int)SDL_arraysize(kFilters),
                           defaultPath.empty() ? nullptr : defaultPath.c_str());
}

bool pollDialogResult(DialogResult& out)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_results.empty()) return false;
    out = std::move(g_results.front());
    g_results.pop_front();
    return true;
}

} // namespace dd::platform
