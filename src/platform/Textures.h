// Image files -> GPU textures for the markdown views. Decoding is stb_image;
// upload is SDL_Renderer. The cache is keyed by resolved path so a picture
// referenced from several places is loaded once; a failed load is cached
// too (with its reason) so a missing file is not retried every frame.
#pragma once

#include <cstdint>
#include <string>

struct SDL_Renderer;

namespace dd::platform {

struct TextureInfo {
    std::uintptr_t id = 0;   // SDL_Texture* as an opaque handle for ImGui
    int            width = 0;
    int            height = 0;
    std::string    error;    // why there is no texture, when id == 0
};

// Set once after the renderer exists; nullptr in the headless test build,
// where every lookup reports "no renderer" and the views draw fallbacks.
void setTextureRenderer(SDL_Renderer* renderer);

// Returns the cached texture for `path`, loading it on first use.
const TextureInfo& textureForFile(const std::string& path);

void clearTextureCache();

} // namespace dd::platform
