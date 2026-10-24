// Token-based theming, carried over from fwcom.
//
// A theme is 17 semantic colors. Everything else -- the ~50 ImGui style
// colors, the style metrics, the markdown palette -- is derived from them,
// so adding a theme means picking 17 colors rather than maintaining a
// 50-entry list by hand and hoping nothing was missed.
#pragma once

#include "imgui.h"

namespace dd::ui {

struct ThemeTokens {
    const char* name;
    bool        isDark;           // chooses the ImGui base style to layer over

    ImU32 background;             // window bg   -- MUST be alpha 255 (viewports)
    ImU32 surface;                // panels, child regions
    ImU32 surfaceInput;           // text fields, buttons
    ImU32 surfacePopup;           // menus, combos, tooltips -- MUST be alpha 255
    ImU32 border;

    ImU32 text;
    ImU32 textMuted;
    ImU32 textFaint;

    ImU32 accent;
    ImU32 link;

    ImU32 codeBackground;
    ImU32 inlineCodeBackground;
    ImU32 inlineCodeText;

    ImU32 selection;              // carries its own alpha; drawn over content

    ImU32 success;
    ImU32 warning;
    ImU32 danger;
};

// Colors the widget helpers and the markdown renderer draw with. Rebuilt
// whenever a theme is applied.
struct Palette {
    ImU32 text, textMuted, textFaint;
    ImU32 heading;
    ImU32 rule, quoteBar;
    ImU32 link;

    ImU32 surface, surfaceHovered, surfaceActive;
    ImU32 border, borderHovered;

    ImU32 codeBackground;
    ImU32 inlineCodeBackground, inlineCodeText;

    ImU32 accent, accentHovered, accentActive, accentText;

    ImU32 success, warning, danger, neutral;

    // Pill/status fills: the state color at low alpha, for filled backgrounds.
    ImU32 successFill, warningFill, dangerFill, neutralFill, accentFill;
};

int                 themeCount();
const ThemeTokens&  themeAt(int index);                 // clamps to range
int                 themeIndexByName(const char* name); // 0 if unknown
int                 currentThemeIndex();
const Palette&      palette();
Palette             buildPalette(const ThemeTokens& t);

// Blend toward white on dark themes, toward black on light ones, so one
// "slightly brighter on hover" rule reads correctly in both directions.
ImU32 lift(ImU32 color, float amount, bool isDark);
ImU32 withAlpha(ImU32 color, int alpha);

// WCAG relative-luminance contrast ratio, 1.0 .. 21.0. Alpha is ignored.
float contrastRatio(ImU32 a, ImU32 b);

// Rebuilds the entire ImGui style -- colors AND metrics -- from the theme's
// tokens, then scales sizes by `sizeScale`. Sizes are reset from constants
// on every call, so this is idempotent (ScaleAllSizes alone is not).
// Call between frames only.
void applyThemeNow(int index, float sizeScale);

// Appearance = theme + user scale + monitor DPI. The user scale also scales
// fonts; DPI scales fonts separately so a 1.0 user scale is crisp anywhere.
void applyAppearanceNow(int theme, float uiScale, float dpiScale);

// Applies any pending change. Call immediately before ImGui::NewFrame();
// cheap when nothing changed.
void pumpAppearance(float uiScale, float dpiScale);
void requestTheme(int index);
bool hasPendingTheme();

// Number of times applyThemeNow has run, so tests can prove a pump did (or
// did not) do work.
unsigned themeApplyCountForTests();

} // namespace dd::ui
