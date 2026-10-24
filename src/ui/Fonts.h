// Font loading. Faces come from assets/fonts beside the executable (Open
// Sans for UI and prose, Fira Code for code, Material Icons merged into the
// text faces for toolbar glyphs).
//
// Dear ImGui 1.92: a font is a face, not a face-at-a-size. The size passed
// at load is only the default a bare PushFont(font) uses; any size can be
// asked for with PushFont(font, size) and glyphs rasterise on demand. The
// markdown renderer uses that for heading sizes.
#pragma once

#include "imgui.h"

#include <string>

namespace dd::ui::fonts {

// Loads every face. `assetDir` is the fonts folder (with or without a
// trailing slash). Returns false with a message when a file is missing;
// the caller decides whether to continue with ImGui's built-in font.
bool initialize(ImGuiIO& io, const std::string& assetDir, float baseSize, std::string& error);

ImFont* ui();          // Open Sans Regular + Material Icons
ImFont* uiBold();      // Open Sans SemiBold + Material Icons
ImFont* uiItalic();    // Open Sans Italic
ImFont* code();        // Fira Code
ImFont* icons();       // Material Icons alone, for large toolbar glyphs

float baseSize();

} // namespace dd::ui::fonts
