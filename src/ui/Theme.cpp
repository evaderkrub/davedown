#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dd::ui {

namespace {

constexpr ImU32 rgb(int r, int g, int b)        { return IM_COL32(r, g, b, 255); }
constexpr ImU32 rgba(int r, int g, int b, int a) { return IM_COL32(r, g, b, a); }

// Wili Dark note: accent is red, so `danger` cannot rely on hue alone to
// distinguish itself. Accent is the deeper red; danger is brighter and more
// saturated. Destructive controls additionally differ in treatment (filled
// vs outlined) -- see Widgets.
const ThemeTokens kThemes[] = {
    { "Wili Dark", true,
      rgb( 22,  23,  27), rgb( 25,  26,  31), rgb( 39,  41,  49),
      rgb( 30,  32,  38), rgb( 52,  56,  66),
      rgb(228, 230, 235), rgb(150, 156, 168), rgb(108, 114, 126),
      rgb(200,  48,  48), rgb(255, 120, 120),
      rgb( 18,  19,  23), rgb( 48,  52,  64), rgb(236, 196, 141),
      rgba(200, 48,  48,  90),
      rgb(102, 187, 122), rgb(226, 173,  88), rgb(255,  92,  92) },

    { "Slate Dark", true,
      rgb( 22,  23,  27), rgb( 25,  26,  31), rgb( 39,  41,  49),
      rgb( 30,  32,  38), rgb( 52,  56,  66),
      rgb(228, 230, 235), rgb(150, 156, 168), rgb(108, 114, 126),
      rgb( 88, 140, 236), rgb(110, 168, 254),
      rgb( 18,  19,  23), rgb( 48,  52,  64), rgb(236, 196, 141),
      rgba( 88, 140, 236, 90),
      rgb(102, 187, 122), rgb(226, 173,  88), rgb(226, 106, 106) },

    { "Light", false,
      rgb(247, 248, 250), rgb(255, 255, 255), rgb(236, 238, 242),
      rgb(255, 255, 255), rgb(213, 217, 224),
      rgb( 26,  28,  34), rgb( 82,  89, 102), rgb(138, 145, 158),
      rgb( 36, 100, 216), rgb( 26,  88, 198),
      rgb(243, 244, 248), rgb(232, 234, 240), rgb(166,  62,  22),
      rgba( 36, 100, 216, 70),
      rgb( 24, 132,  68), rgb(168, 106,   8), rgb(198,  48,  48) },

    { "Midnight", true,
      rgb(  8,   9,  12), rgb( 13,  14,  18), rgb( 25,  27,  33),
      rgb( 18,  20,  25), rgb( 38,  41,  50),
      rgb(226, 230, 238), rgb(140, 148, 162), rgb( 96, 103, 116),
      rgb( 96, 150, 250), rgb(118, 172, 255),
      rgb(  4,   5,   7), rgb( 34,  38,  48), rgb(240, 190, 130),
      rgba( 96, 150, 250, 90),
      rgb( 94, 190, 118), rgb(230, 176,  84), rgb(232, 100, 100) },

    { "Nord", true,
      rgb( 46,  52,  64), rgb( 52,  58,  72), rgb( 59,  66,  82),
      rgb( 59,  66,  82), rgb( 67,  76,  94),
      rgb(216, 222, 233), rgb(166, 178, 196), rgb(126, 138, 158),
      rgb(136, 192, 208), rgb(143, 188, 187),
      rgb( 38,  43,  54), rgb( 67,  76,  94), rgb(235, 203, 139),
      rgba(136, 192, 208, 90),
      rgb(163, 190, 140), rgb(235, 203, 139), rgb(191,  97, 106) },

    { "Paper", false,
      rgb(248, 244, 235), rgb(255, 252, 245), rgb(237, 230, 216),
      rgb(255, 252, 245), rgb(205, 194, 175),
      rgb( 48,  43,  37), rgb(102,  91,  77), rgb(109,  98,  83),
      rgb(149,  81,  29), rgb(135,  70,  23),
      rgb(241, 235, 221), rgb(233, 223, 205), rgb(133,  60,  36),
      rgba(149,  81,  29, 70),
      rgb( 40,  98,  62), rgb(134,  87,  10), rgb(174,  52,  52) },

    { "Sage", false,
      rgb(241, 246, 241), rgb(250, 253, 250), rgb(225, 235, 226),
      rgb(250, 253, 250), rgb(190, 206, 191),
      rgb( 36,  53,  44), rgb( 76,  98,  84), rgb( 89, 108,  94),
      rgb( 40, 103,  73), rgb( 32,  94,  67),
      rgb(234, 241, 233), rgb(222, 233, 221), rgb( 75,  86,  48),
      rgba( 40, 103,  73, 70),
      rgb( 40,  97,  63), rgb(134,  87,   8), rgb(173,  54,  64) },

    { "Lavender", false,
      rgb(246, 243, 250), rgb(253, 251, 255), rgb(234, 227, 242),
      rgb(253, 251, 255), rgb(206, 194, 221),
      rgb( 48,  40,  62), rgb( 96,  83, 111), rgb(108,  93, 121),
      rgb(113,  70, 153), rgb(100,  58, 143),
      rgb(240, 235, 246), rgb(230, 221, 239), rgb(123,  54,  95),
      rgba(113,  70, 153, 70),
      rgb( 40, 100,  68), rgb(135,  85,   9), rgb(172,  53,  76) },
};

constexpr int kThemeCount = (int)(sizeof(kThemes) / sizeof(kThemes[0]));

Palette  gPalette;
int      gThemeIndex = 0;
int      gPendingTheme = -1;
float    gAppliedSizeScale = 1.0f;
unsigned gApplyCount = 0;

inline float chan(ImU32 c, int shift) { return (float)((c >> shift) & 0xFF) / 255.0f; }

float srgbToLinear(float c)
{
    return (c <= 0.03928f) ? (c / 12.92f) : powf((c + 0.055f) / 1.055f, 2.4f);
}

float relativeLuminance(ImU32 c)
{
    const float r = srgbToLinear(chan(c, IM_COL32_R_SHIFT));
    const float g = srgbToLinear(chan(c, IM_COL32_G_SHIFT));
    const float b = srgbToLinear(chan(c, IM_COL32_B_SHIFT));
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

} // namespace

int themeCount() { return kThemeCount; }

const ThemeTokens& themeAt(int index)
{
    return kThemes[std::clamp(index, 0, kThemeCount - 1)];
}

int themeIndexByName(const char* name)
{
    if (!name) return 0;
    for (int i = 0; i < kThemeCount; ++i)
        if (std::strcmp(name, kThemes[i].name) == 0) return i;
    return 0;
}

int currentThemeIndex() { return gThemeIndex; }
const Palette& palette() { return gPalette; }

ImU32 withAlpha(ImU32 color, int alpha)
{
    return (color & ~IM_COL32_A_MASK) | ((ImU32)(alpha & 0xFF) << IM_COL32_A_SHIFT);
}

ImU32 lift(ImU32 color, float amount, bool isDark)
{
    const float target = isDark ? 1.0f : 0.0f;
    int c[3];
    const int shifts[3] = { IM_COL32_R_SHIFT, IM_COL32_G_SHIFT, IM_COL32_B_SHIFT };
    for (int i = 0; i < 3; ++i) {
        float v = chan(color, shifts[i]);
        v += (target - v) * amount;
        c[i] = std::clamp((int)(v * 255.0f + 0.5f), 0, 255);
    }
    const int a = (int)((color >> IM_COL32_A_SHIFT) & 0xFF);
    return IM_COL32(c[0], c[1], c[2], a);
}

float contrastRatio(ImU32 a, ImU32 b)
{
    const float la = relativeLuminance(a);
    const float lb = relativeLuminance(b);
    const float hi = std::max(la, lb);
    const float lo = std::min(la, lb);
    return (hi + 0.05f) / (lo + 0.05f);
}

Palette buildPalette(const ThemeTokens& t)
{
    Palette p;
    const bool d = t.isDark;

    p.text      = t.text;
    p.textMuted = t.textMuted;
    p.textFaint = t.textFaint;
    // Headings sit above body text in contrast on dark themes and below it on
    // light ones -- which is the direction each needs.
    p.heading   = lift(t.text, 0.18f, d);
    p.rule      = t.border;
    p.quoteBar  = t.textFaint;
    p.link      = t.link;

    p.surface        = t.surface;
    p.surfaceHovered = lift(t.surfaceInput, 0.12f, d);
    p.surfaceActive  = lift(t.surfaceInput, 0.20f, d);
    p.border         = t.border;
    p.borderHovered  = lift(t.border, 0.25f, d);

    p.codeBackground       = t.codeBackground;
    p.inlineCodeBackground = t.inlineCodeBackground;
    p.inlineCodeText       = t.inlineCodeText;

    p.accent        = t.accent;
    p.accentHovered = lift(t.accent, 0.15f, d);
    p.accentActive  = lift(t.accent, 0.28f, d);
    // Text drawn on top of an accent fill: pick whichever of the theme's own
    // extremes has more contrast against the accent, rather than assuming
    // white reads on every accent color.
    p.accentText = (contrastRatio(t.accent, t.text) >= contrastRatio(t.accent, t.background))
                 ? t.text : t.background;

    p.success = t.success;
    p.warning = t.warning;
    p.danger  = t.danger;
    p.neutral = t.textFaint;

    p.successFill = withAlpha(t.success, 56);
    p.warningFill = withAlpha(t.warning, 56);
    p.dangerFill  = withAlpha(t.danger,  56);
    p.neutralFill = withAlpha(t.textFaint, 40);
    p.accentFill  = withAlpha(t.accent,  56);
    return p;
}

void applyThemeNow(int index, float sizeScale)
{
    ++gApplyCount;
    if (!std::isfinite(sizeScale) || sizeScale <= 0.0f) sizeScale = 1.0f;
    gAppliedSizeScale = sizeScale;
    gThemeIndex = std::clamp(index, 0, kThemeCount - 1);

    const ThemeTokens& t = kThemes[gThemeIndex];
    const bool d = t.isDark;
    gPalette = buildPalette(t);

    // Start from a matching base so the handful of ImGui colors not named
    // below stay legible even if ImGui adds new ones in a future update.
    if (d) ImGui::StyleColorsDark(); else ImGui::StyleColorsLight();

    ImGuiStyle& s = ImGui::GetStyle();

    // --- Metrics. Assigned from constants (never read-modify-write), so a
    // --- re-apply lands on exactly the same numbers.
    s.WindowPadding     = ImVec2(12, 12);
    s.FramePadding      = ImVec2(10,  6);
    s.CellPadding       = ImVec2( 6,  4);
    s.ItemSpacing       = ImVec2( 8,  7);
    s.ItemInnerSpacing  = ImVec2( 6,  5);
    s.IndentSpacing     = 21.0f;
    s.ScrollbarSize     = 12.0f;
    s.GrabMinSize       = 10.0f;

    s.WindowBorderSize  = 0.0f;
    s.ChildBorderSize   = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.PopupBorderSize   = 1.0f;
    s.TabBorderSize     = 0.0f;

    s.WindowRounding    = 6.0f;
    s.ChildRounding     = 8.0f;
    s.FrameRounding     = 6.0f;
    s.PopupRounding     = 6.0f;
    s.ScrollbarRounding = 6.0f;
    s.GrabRounding      = 6.0f;
    s.TabRounding       = 6.0f;

    s.WindowTitleAlign  = ImVec2(0.0f, 0.5f);

    // ScaleAllSizes also scales the fields below. They MUST be assigned from
    // constants here too, or they carry the previous apply's already-scaled
    // value into ScaleAllSizes and compound. Values are ImGuiStyle's own
    // defaults (imgui.cpp, ImGuiStyle::ImGuiStyle()).
    s.WindowMinSize                    = ImVec2(32, 32);
    s.WindowBorderHoverPadding         = 4.0f;
    s.TouchExtraPadding                = ImVec2(0, 0);
    s.ColumnsMinSpacing                = 6.0f;
    s.LogSliderDeadzone                = 4.0f;
    s.ImageBorderSize                  = 0.0f;
    s.TabCloseButtonMinWidthSelected   = -1.0f;
    s.TabCloseButtonMinWidthUnselected = 0.0f;
    s.TabBarOverlineSize               = 1.0f;
    s.TabMinWidthBase                  = 1.0f;
    s.TabMinWidthShrink                = 80.0f;
    s.SeparatorTextPadding             = ImVec2(20.0f, 3.0f);
    s.DockingSeparatorSize             = 2.0f;
    s.DisplayWindowPadding             = ImVec2(19, 19);
    s.DisplaySafeAreaPadding           = ImVec2(3, 3);
    s.MouseCursorScale                 = 1.0f;
    s.ScrollbarPadding                 = 2.0f;
    s.ImageRounding                    = 0.0f;
    s.TabBarBorderSize                 = 1.0f;
    s.TreeLinesSize                    = 1.0f;
    s.TreeLinesRounding                = 0.0f;
    s.DragDropTargetRounding           = 0.0f;
    s.DragDropTargetBorderSize         = 2.0f;
    s.DragDropTargetPadding            = 3.0f;
    s.ColorMarkerSize                  = 3.0f;
    s.SeparatorSize                    = 1.0f;
    s.SeparatorTextBorderSize          = 3.0f;

    // --- Colors.
    auto V = [](ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); };
    auto L = [&](ImU32 c, float amt) { return V(lift(c, amt, d)); };
    auto A = [&](ImU32 c, float alpha) { ImVec4 v = V(c); v.w = alpha; return v; };

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                   = V(t.text);
    c[ImGuiCol_TextDisabled]           = V(t.textFaint);
    c[ImGuiCol_TextSelectedBg]         = V(t.selection);
    c[ImGuiCol_TextLink]               = V(t.link);

    c[ImGuiCol_WindowBg]               = V(t.background);
    c[ImGuiCol_ChildBg]                = V(t.surface);
    c[ImGuiCol_PopupBg]                = V(t.surfacePopup);
    c[ImGuiCol_Border]                 = V(t.border);
    c[ImGuiCol_BorderShadow]           = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]                = V(t.surfaceInput);
    c[ImGuiCol_FrameBgHovered]         = L(t.surfaceInput, 0.08f);
    c[ImGuiCol_FrameBgActive]          = L(t.surfaceInput, 0.14f);

    c[ImGuiCol_TitleBg]                = V(t.background);
    c[ImGuiCol_TitleBgActive]          = V(t.surface);
    c[ImGuiCol_TitleBgCollapsed]       = V(t.background);
    c[ImGuiCol_MenuBarBg]              = V(t.surface);

    c[ImGuiCol_ScrollbarBg]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]          = L(t.surfaceInput, 0.10f);
    c[ImGuiCol_ScrollbarGrabHovered]   = L(t.surfaceInput, 0.20f);
    c[ImGuiCol_ScrollbarGrabActive]    = L(t.surfaceInput, 0.30f);

    c[ImGuiCol_CheckMark]              = V(t.accent);
    c[ImGuiCol_SliderGrab]             = V(t.accent);
    c[ImGuiCol_SliderGrabActive]       = L(t.accent, 0.20f);

    c[ImGuiCol_Button]                 = V(t.surfaceInput);
    c[ImGuiCol_ButtonHovered]          = L(t.surfaceInput, 0.12f);
    c[ImGuiCol_ButtonActive]           = L(t.surfaceInput, 0.20f);

    c[ImGuiCol_Header]                 = V(t.surfaceInput);
    c[ImGuiCol_HeaderHovered]          = L(t.surfaceInput, 0.12f);
    c[ImGuiCol_HeaderActive]           = L(t.surfaceInput, 0.20f);

    c[ImGuiCol_Separator]              = V(t.border);
    c[ImGuiCol_SeparatorHovered]       = V(t.accent);
    c[ImGuiCol_SeparatorActive]        = V(t.accent);

    c[ImGuiCol_ResizeGrip]             = A(t.border, 0.60f);
    c[ImGuiCol_ResizeGripHovered]      = V(t.accent);
    c[ImGuiCol_ResizeGripActive]       = V(t.accent);

    c[ImGuiCol_InputTextCursor]        = V(t.text);

    c[ImGuiCol_Tab]                    = V(t.background);
    c[ImGuiCol_TabHovered]             = L(t.surfaceInput, 0.14f);
    c[ImGuiCol_TabSelected]            = V(t.surfaceInput);
    c[ImGuiCol_TabSelectedOverline]    = V(t.accent);
    c[ImGuiCol_TabDimmed]              = V(t.background);
    c[ImGuiCol_TabDimmedSelected]      = V(t.surface);
    c[ImGuiCol_TabDimmedSelectedOverline] = A(t.accent, 0.0f);

    c[ImGuiCol_DockingPreview]         = A(t.accent, 0.55f);
    c[ImGuiCol_DockingEmptyBg]         = V(t.background);

    c[ImGuiCol_PlotLines]              = V(t.textMuted);
    c[ImGuiCol_PlotLinesHovered]       = V(t.accent);
    c[ImGuiCol_PlotHistogram]          = V(t.warning);
    c[ImGuiCol_PlotHistogramHovered]   = L(t.warning, 0.20f);

    c[ImGuiCol_TableHeaderBg]          = V(t.surfaceInput);
    c[ImGuiCol_TableBorderStrong]      = V(t.border);
    c[ImGuiCol_TableBorderLight]       = A(t.border, 0.55f);
    c[ImGuiCol_TableRowBg]             = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]          = d ? ImVec4(1, 1, 1, 0.035f) : ImVec4(0, 0, 0, 0.025f);

    c[ImGuiCol_DragDropTarget]         = V(t.accent);
    c[ImGuiCol_NavCursor]              = V(t.accent);
    c[ImGuiCol_NavWindowingHighlight]  = A(t.text, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]      = d ? ImVec4(0.20f, 0.20f, 0.20f, 0.35f)
                                           : ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    c[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.04f, 0.04f, 0.05f, 0.65f);

    // Viewports are enabled. A translucent WindowBg or PopupBg renders
    // incorrectly in a platform window, so force them opaque here rather
    // than relying on every theme author remembering.
    c[ImGuiCol_WindowBg].w = 1.0f;
    c[ImGuiCol_PopupBg].w  = 1.0f;

    // ScaleAllSizes accumulates its factor into the style's _MainScale.
    // Every apply rebuilds the metrics from constants, so the reference
    // scale must start from 1 as well, or it compounds across theme changes.
    s._MainScale = 1.0f;
    if (sizeScale != 1.0f) s.ScaleAllSizes(sizeScale);
}

void requestTheme(int index) { gPendingTheme = index; }
bool hasPendingTheme() { return gPendingTheme >= 0; }
unsigned themeApplyCountForTests() { return gApplyCount; }

void applyAppearanceNow(int theme, float uiScale, float dpiScale)
{
    if (!std::isfinite(uiScale) || uiScale <= 0.0f) uiScale = 1.0f;
    if (!std::isfinite(dpiScale) || dpiScale <= 0.0f) dpiScale = 1.0f;
    gPendingTheme = -1;
    applyThemeNow(theme, uiScale * dpiScale);
    ImGui::GetStyle().FontScaleMain = uiScale;
    ImGui::GetStyle().FontScaleDpi = dpiScale;
}

void pumpAppearance(float uiScale, float dpiScale)
{
    if (!std::isfinite(uiScale) || uiScale <= 0.0f) uiScale = 1.0f;
    if (!std::isfinite(dpiScale) || dpiScale <= 0.0f) dpiScale = 1.0f;
    if (gPendingTheme < 0 && ImGui::GetStyle().FontScaleMain == uiScale &&
        gAppliedSizeScale == uiScale * dpiScale)
        return;
    const int theme = gPendingTheme < 0 ? gThemeIndex : gPendingTheme;
    applyAppearanceNow(theme, uiScale, dpiScale);
}

} // namespace dd::ui
