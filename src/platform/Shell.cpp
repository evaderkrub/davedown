#include "platform/Shell.h"

#include <SDL3/SDL.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#elif defined(__APPLE__)
#include <cstdlib>
#endif

namespace dd::platform {

namespace {

std::string parentDirectory(const std::string& path)
{
    const size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? std::string() : path.substr(0, cut);
}

} // namespace

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

bool showInFolder(const std::string& path, std::string& error)
{
    if (path.empty()) {
        error = "The document has not been saved yet.";
        return false;
    }
#ifdef _WIN32
    std::string native = path;
    for (char& c : native) if (c == '/') c = '\\';
    const int n = MultiByteToWideChar(CP_UTF8, 0, native.c_str(), (int)native.size(), nullptr, 0);
    std::wstring wide((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, native.c_str(), (int)native.size(), wide.data(), n);
    // COM may already be initialised by SDL or a dialog; any mode is fine here.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    PIDLIST_ABSOLUTE item = ILCreateFromPathW(wide.c_str());
    if (!item) {
        if (SUCCEEDED(init)) CoUninitialize();
        error = "The file could not be found: " + path;
        return false;
    }
    const HRESULT hr = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ILFree(item);
    if (SUCCEEDED(init)) CoUninitialize();
    if (FAILED(hr)) {
        error = "Explorer could not open the folder.";
        return false;
    }
    error.clear();
    return true;
#elif defined(__APPLE__)
    std::string cmd = "open -R \"";
    for (char c : path) { if (c == '"' || c == '\\' || c == '$' || c == '`') cmd += '\\'; cmd += c; }
    cmd += "\"";
    if (std::system(cmd.c_str()) != 0) {
        error = "Finder could not open the folder.";
        return false;
    }
    error.clear();
    return true;
#else
    const std::string dir = parentDirectory(path);
    return openExternal(dir.empty() ? "." : dir, error);
#endif
}

double nowSeconds()
{
    return (double)SDL_GetTicksNS() / 1.0e9;
}

} // namespace dd::platform
