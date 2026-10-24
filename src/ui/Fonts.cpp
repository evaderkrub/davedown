#include "ui/Fonts.h"

#include "IconsMaterialDesign.h"

#include <cstdio>

namespace dd::ui::fonts {

namespace {

ImFont* g_ui = nullptr;
ImFont* g_uiBold = nullptr;
ImFont* g_uiItalic = nullptr;
ImFont* g_code = nullptr;
ImFont* g_icons = nullptr;
float   g_baseSize = 18.0f;

// ImGui keeps pointers to these arrays, so they must outlive initialize().
const ImWchar kIconRanges[] = { ICON_MIN_MD, ICON_MAX_16_MD, 0 };

// Basic Latin + Latin Supplement, General Punctuation (bullets, dashes,
// smart quotes, ellipsis), arrows and math operators -- the glyphs prose
// routinely contains that ImGui's default range leaves as '?'.
const ImWchar kTextRanges[] = {
    0x0020, 0x00FF,
    0x0100, 0x017F,   // Latin Extended-A
    0x2000, 0x206F,
    0x2190, 0x21FF,
    0x2200, 0x22FF,
    0x2500, 0x257F,   // box drawing, common in ASCII-art diagrams
    0x25A0, 0x25FF,   // geometric shapes (■ ▶ ●)
    0,
};

// Open Sans has no arrow glyphs at all, but Material Icons does. Each arrow
// is drawn from there and mapped onto the real Unicode codepoint so text
// can use the proper character.
struct Remap { ImWchar unicode; ImWchar icon; };
const Remap kArrowRemaps[] = {
    { 0x2190, 0xE5C4 },   // ← ICON_MD_ARROW_BACK
    { 0x2191, 0xE5D8 },   // ↑ ICON_MD_ARROW_UPWARD
    { 0x2192, 0xE5C8 },   // → ICON_MD_ARROW_FORWARD
    { 0x2193, 0xE5DB },   // ↓ ICON_MD_ARROW_DOWNWARD
};

bool exists(const std::string& path)
{
    if (FILE* f = std::fopen(path.c_str(), "rb")) { std::fclose(f); return true; }
    return false;
}

ImFont* addText(ImGuiIO& io, const std::string& path, float size)
{
    ImFontConfig cfg;
    return io.Fonts->AddFontFromFileTTF(path.c_str(), size, &cfg, kTextRanges);
}

void mergeIcons(ImGuiIO& io, const std::string& path, float size)
{
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.PixelSnapH = true;
    cfg.GlyphMinAdvanceX = size;          // monospace-align icons with UI text
    // Material glyphs sit high on the line; nudge them down so they centre
    // inside framed buttons.
    cfg.GlyphOffset.y = size * 0.18f;
    io.Fonts->AddFontFromFileTTF(path.c_str(), size, &cfg, kIconRanges);
}

void remapArrows(ImFont* font)
{
    if (!font) return;
    for (const Remap& r : kArrowRemaps) font->AddRemapChar(r.unicode, r.icon);
}

} // namespace

bool initialize(ImGuiIO& io, const std::string& assetDir, float size, std::string& error)
{
    std::string dir = assetDir;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir.push_back('/');
    const std::string regular  = dir + "OpenSans-Regular.ttf";
    const std::string semibold = dir + "OpenSans-SemiBold.ttf";
    const std::string italic   = dir + "OpenSans-Italic.ttf";
    const std::string mono     = dir + "FiraCode-Regular.ttf";
    const std::string icons    = dir + "MaterialIcons-Regular.ttf";
    for (const std::string* p : { &regular, &semibold, &italic, &mono, &icons }) {
        if (!exists(*p)) {
            error = "Font file missing: " + *p;
            return false;
        }
    }
    g_baseSize = size;

    g_ui = addText(io, regular, size);
    mergeIcons(io, icons, size);
    g_uiBold = addText(io, semibold, size);
    mergeIcons(io, icons, size);
    g_uiItalic = addText(io, italic, size);
    mergeIcons(io, icons, size);
    g_code = addText(io, mono, size);
    mergeIcons(io, icons, size);

    {
        ImFontConfig cfg;
        cfg.PixelSnapH = true;
        cfg.GlyphMinAdvanceX = size;
        g_icons = io.Fonts->AddFontFromFileTTF(icons.c_str(), size, &cfg, kIconRanges);
    }

    remapArrows(g_ui);
    remapArrows(g_uiBold);
    remapArrows(g_uiItalic);
    remapArrows(g_code);

    if (!g_ui || !g_uiBold || !g_uiItalic || !g_code || !g_icons) {
        error = "A font file could not be parsed.";
        return false;
    }
    io.FontDefault = g_ui;
    error.clear();
    return true;
}

ImFont* ui()       { return g_ui; }
ImFont* uiBold()   { return g_uiBold ? g_uiBold : g_ui; }
ImFont* uiItalic() { return g_uiItalic ? g_uiItalic : g_ui; }
ImFont* code()     { return g_code ? g_code : g_ui; }
ImFont* icons()    { return g_icons ? g_icons : g_ui; }
float   baseSize() { return g_baseSize; }

} // namespace dd::ui::fonts
