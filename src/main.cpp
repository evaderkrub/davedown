// Entry point: SDL3 window + SDL_Renderer, Dear ImGui, and the frame loop.
// Everything that is not plumbing lives in src/ui and src/app.
#include "app/Workspace.h"
#include "platform/Paths.h"
#include "platform/Textures.h"
#include "ui/Fonts.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

struct App {
    SDL_Window*   window = nullptr;
    SDL_Renderer* renderer = nullptr;
    dd::Workspace workspace;
    std::unique_ptr<dd::ui::MainWindow> ui;
    std::vector<std::string> pendingFiles;
    std::string fontError;
};

// A procedurally drawn window icon: a rounded tile with a down-arrow, the
// "down" in davedown. Avoids shipping or decoding an image for one glyph.
void setWindowIcon(SDL_Window* window)
{
    const int n = 64;
    std::vector<Uint32> px((size_t)n * n, 0);
    auto put = [&](int x, int y, Uint32 c) { if (x >= 0 && y >= 0 && x < n && y < n) px[(size_t)y * n + x] = c; };
    const Uint32 bg = 0xFF3030C8;      // ABGR little-endian for RGBA32: R=0xC8 G=0x30 B=0x30
    const Uint32 fg = 0xFFFFFFFF;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            // Rounded rectangle.
            const int r = 12;
            int dx = 0, dy = 0;
            if (x < r) dx = r - x; else if (x >= n - r) dx = x - (n - r - 1);
            if (y < r) dy = r - y; else if (y >= n - r) dy = y - (n - r - 1);
            if (dx * dx + dy * dy <= r * r) put(x, y, bg);
        }
    // Arrow shaft and head.
    for (int y = 14; y < 38; ++y) for (int x = 28; x < 36; ++x) put(x, y, fg);
    for (int i = 0; i < 14; ++i) for (int x = 18 + i; x < 46 - i; ++x) put(x, 36 + i, fg);
    SDL_Surface* icon = SDL_CreateSurfaceFrom(n, n, SDL_PIXELFORMAT_RGBA32, px.data(), n * 4);
    if (icon) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }
}

float displayScale(SDL_Window* window)
{
    const float s = SDL_GetWindowDisplayScale(window);
    return (s > 0.1f && std::isfinite(s)) ? s : 1.0f;
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    auto* app = new App();
    *appstate = app;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");

    // Settings are read before the window exists so it opens at its last size.
    {
        std::string text, err;
        if (dd::platform::readTextFile(dd::platform::settingsPath(), text, err))
            app->workspace.settings = dd::parseSettings(text);
    }
    const dd::Settings& s = app->workspace.settings;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    if (s.windowMaximized) flags |= SDL_WINDOW_MAXIMIZED;
    app->window = SDL_CreateWindow("davedown", s.windowWidth, s.windowHeight, flags);
    if (!app->window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    app->renderer = SDL_CreateRenderer(app->window, nullptr);
    if (!app->renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    setWindowIcon(app->window);
    dd::platform::setTextureRenderer(app->renderer);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // layout is rebuilt each run; no imgui.ini beside the exe
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;

    const float dpi = displayScale(app->window);
    if (!dd::ui::fonts::initialize(io, dd::platform::resolve("assets/fonts"), 18.0f, app->fontError)) {
        // Keep running with ImGui's built-in font so the problem is visible
        // on screen rather than a silent exit.
        std::fprintf(stderr, "%s\n", app->fontError.c_str());
        io.Fonts->AddFontDefault();
    }
    dd::ui::applyAppearanceNow(dd::ui::themeIndexByName(s.themeName.c_str()), s.uiScale, dpi);

    ImGui_ImplSDL3_InitForSDLRenderer(app->window, app->renderer);
    ImGui_ImplSDLRenderer3_Init(app->renderer);

    std::string welcome, err;
    dd::platform::readTextFile(dd::platform::resolve("assets/docs/welcome.md"), welcome, err);
    app->ui = std::make_unique<dd::ui::MainWindow>(app->workspace);
    app->ui->initialize(welcome);
    if (!app->fontError.empty())
        app->workspace.setStatus(app->fontError, 0.0);

    for (int i = 1; i < argc; ++i)
        if (argv[i] && argv[i][0] != '-') app->pendingFiles.push_back(argv[i]);

    SDL_ShowWindow(app->window);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    auto* app = static_cast<App*>(appstate);
    ImGui_ImplSDL3_ProcessEvent(event);
    switch (event->type) {
    case SDL_EVENT_QUIT:
        app->ui->requestQuit();
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if (event->window.windowID == SDL_GetWindowID(app->window)) app->ui->requestQuit();
        break;
    case SDL_EVENT_DROP_FILE:
        if (event->drop.data) app->pendingFiles.push_back(event->drop.data);
        break;
    default:
        break;
    }
    return app->ui->quitConfirmed() ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    auto* app = static_cast<App*>(appstate);
    if (app->ui->quitConfirmed()) return SDL_APP_SUCCESS;

    // Save the renderer a frame when minimised.
    if (SDL_GetWindowFlags(app->window) & SDL_WINDOW_MINIMIZED) {
        SDL_Delay(16);
        return SDL_APP_CONTINUE;
    }

    for (const std::string& f : app->pendingFiles) app->ui->openFileFromOutside(f);
    app->pendingFiles.clear();

    const float dpi = displayScale(app->window);
    dd::ui::pumpAppearance(app->workspace.settings.uiScale, dpi);

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    dd::ui::UiFrame frame;
    frame.nativeWindow = app->window;
    frame.dpiScale = dpi;
    app->ui->draw(frame);

    ImGui::Render();
    const ImVec4 clear = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    SDL_SetRenderScale(app->renderer, ImGui::GetIO().DisplayFramebufferScale.x, ImGui::GetIO().DisplayFramebufferScale.y);
    SDL_SetRenderDrawColorFloat(app->renderer, clear.x, clear.y, clear.z, 1.0f);
    SDL_RenderClear(app->renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), app->renderer);
    SDL_RenderPresent(app->renderer);

    // Window title mirrors the active document.
    if (dd::OpenDocument* od = app->workspace.active()) {
        std::string title = od->doc.title() + (od->doc.isDirty() ? " * - davedown" : " - davedown");
        SDL_SetWindowTitle(app->window, title.c_str());
    }
    return app->ui->quitConfirmed() ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/)
{
    auto* app = static_cast<App*>(appstate);
    if (!app) return;
    if (app->window && app->ui) {
        int w = 0, h = 0;
        SDL_GetWindowSize(app->window, &w, &h);
        const bool maximized = (SDL_GetWindowFlags(app->window) & SDL_WINDOW_MAXIMIZED) != 0;
        if (!maximized && w > 0 && h > 0) {
            app->workspace.settings.windowWidth = w;
            app->workspace.settings.windowHeight = h;
        }
        app->workspace.settings.windowMaximized = maximized;
        app->ui->saveSettings();
    }
    if (app->renderer) {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        dd::platform::clearTextureCache();
        SDL_DestroyRenderer(app->renderer);
    }
    if (app->window) SDL_DestroyWindow(app->window);
    delete app;
}
