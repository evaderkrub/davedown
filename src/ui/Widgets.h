// Shared UI helpers built on the theme palette (ported from fwcom).
#pragma once

#include "imgui.h"

namespace dd::ui {

// RAII: pushes the monospace code font for the enclosing scope.
struct MonoScope {
    MonoScope();
    ~MonoScope();
    MonoScope(const MonoScope&) = delete;
    MonoScope& operator=(const MonoScope&) = delete;
private:
    bool pushed_ = false;
};

enum Kind { KindNeutral = 0, KindOk, KindWarn, KindError, KindAccent };

// Rounded surface region with a border. Pair with endCard() only when this
// returns true, exactly like ImGui::BeginChild.
bool beginCard(const char* id, ImVec2 size = ImVec2(0, 0), ImGuiWindowFlags flags = 0);
void endCard();

// Bold heading followed by a rule.
void sectionHeader(const char* text);

// Small filled, rounded status label.
void statusPill(const char* label, Kind kind);

// The only accent-filled button. Using it anywhere else is what erodes
// "restrained accent" back into "everything is the accent color".
bool accentButton(const char* label, ImVec2 size = ImVec2(0, 0));

// Destructive action. Outlined rather than filled, because in a red-accented
// theme hue alone cannot distinguish it from accentButton.
bool dangerButton(const char* label, ImVec2 size = ImVec2(0, 0));

// Flat toolbar button: an icon glyph on a transparent background with a
// hover highlight and a delayed tooltip. `id` keeps the item identity stable
// if the icon changes.
bool iconButton(const char* id, const char* icon, const char* tooltip = nullptr,
                bool enabled = true);

void mutedText(const char* fmt, ...) IM_FMTARGS(1);
void faintText(const char* fmt, ...) IM_FMTARGS(1);

// A row of buttons sharing one tinted, rounded background, with a bright
// outline on the selected one -- a segmented control. Each entry is an icon
// plus optional label. Returns true if the selection changed.
bool segmentedControl(const char* id, const char* const* icons, const char* const* labels,
                      const char* const* tooltips, int count, int* selected,
                      Kind kind = KindNeutral);

// Draws `text` vertically centred in the current line (for toolbars that mix
// buttons and labels).
void centeredLabel(const char* text);

// Thin horizontal rule in the palette's border colour spanning the content width.
void hairline();

} // namespace dd::ui
