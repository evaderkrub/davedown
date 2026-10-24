#include "platform/Shell.h"

#include <SDL3/SDL.h>

namespace dd::platform {

bool openExternal(const std::string& target, std::string& error)
{
    if (target.empty()) {
        error = "Nothing to open.";
        return false;
    }
    if (!SDL_OpenURL(target.c_str())) {
        error = SDL_GetError();
        return false;
    }
    error.clear();
    return true;
}

double nowSeconds()
{
    return (double)SDL_GetTicksNS() / 1.0e9;
}

} // namespace dd::platform
