#include "platform/Textures.h"

#include <SDL3/SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#include <stb_image.h>

#include <map>
#include <vector>

#include "platform/Paths.h"

namespace dd::platform {

namespace {

SDL_Renderer*                      g_renderer = nullptr;
std::map<std::string, TextureInfo> g_cache;

TextureInfo load(const std::string& path)
{
    TextureInfo info;
    if (!g_renderer) {
        info.error = "no renderer";
        return info;
    }
    std::string bytes, err;
    if (!readTextFile(path, bytes, err)) {
        info.error = "file not found";
        return info;
    }
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                            (int)bytes.size(), &w, &h, &channels, 4);
    if (!pixels) {
        info.error = stbi_failure_reason() ? stbi_failure_reason() : "undecodable image";
        return info;
    }
    SDL_Texture* tex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex) {
        info.error = SDL_GetError();
        stbi_image_free(pixels);
        return info;
    }
    SDL_UpdateTexture(tex, nullptr, pixels, w * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    stbi_image_free(pixels);
    info.id = reinterpret_cast<std::uintptr_t>(tex);
    info.width = w;
    info.height = h;
    return info;
}

} // namespace

void setTextureRenderer(SDL_Renderer* renderer)
{
    if (renderer != g_renderer) clearTextureCache();
    g_renderer = renderer;
}

const TextureInfo& textureForFile(const std::string& path)
{
    auto it = g_cache.find(path);
    if (it != g_cache.end()) return it->second;
    return g_cache.emplace(path, load(path)).first->second;
}

void clearTextureCache()
{
    for (auto& [path, info] : g_cache)
        if (info.id) SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(info.id));
    g_cache.clear();
}

} // namespace dd::platform
