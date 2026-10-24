// A word-wrapping, syntax-coloured multi-line text editing widget.
//
// Dear ImGui's InputTextMultiline neither wraps nor colours, and prose is
// unreadable as one long scrolling line, so both the source view and the
// WYSIWYG block editor draw with this instead. It keeps editing state (caret,
// selection, layout cache) in a caller-owned struct and reports what the
// caller may want to act on (Enter in block mode, Backspace at the start,
// arrow keys leaving the text) as events rather than deciding itself.
#pragma once

#include "imgui.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace dd::ui {

struct TextStyleRun {
    int     start = 0;   // byte range within the logical line
    int     end = 0;
    ImU32   color = 0;   // 0 = default text colour
    ImFont* font = nullptr;   // nullptr = the area's font
};

// Called per logical line; appends runs covering any part of the line that
// should not use the default style. Runs may not overlap.
using HighlightFn = std::function<void(std::string_view line, std::vector<TextStyleRun>& runs)>;

struct TextAreaOptions {
    ImFont*     font = nullptr;        // nullptr = current font
    float       fontSize = 0.0f;       // 0 = current size
    float       lineHeightFactor = 1.5f;
    bool        wordWrap = true;
    bool        showLineNumbers = false;
    bool        readOnly = false;
    // Block mode: Enter is reported instead of inserting a newline (Shift+Enter
    // still inserts one), Backspace/Delete at the edges and Up/Down past the
    // first/last line are reported so the host can move between blocks.
    bool        blockMode = false;
    // Keeps the widget sized to its text instead of filling the given size.
    bool        autoHeight = false;
    ImU32       textColor = 0;         // 0 = ImGuiCol_Text
    HighlightFn highlight;
    const char* placeholder = nullptr; // shown faint when the text is empty
    float       paddingX = 0.0f;
};

struct TextAreaState {
    int    cursor = 0;        // byte offset
    int    anchor = 0;        // selection anchor; == cursor means none
    float  preferredX = -1.0f; // column to keep when moving up/down
    double lastEditTime = 0.0;
    double blinkStart = 0.0;
    bool   focused = false;

    // Requests the host sets for the next frame.
    bool   requestFocus = false;
    int    requestCursor = -1;      // -1 = leave; -2 = end of text
    int    requestCursorLine = -1;  // scroll so this logical line is visible
    bool   scrollToCursor = false;
    bool   dragging = false;

    // Layout cache.
    struct VisualLine {
        int   start = 0;      // byte range into the text, end excludes '\n'
        int   end = 0;
        int   logical = 0;    // logical line index
        bool  hardEnd = true; // terminated by '\n' or end of text (not a wrap)
        float width = 0.0f;
    };
    std::vector<VisualLine> lines;
    size_t cachedHash = 0;
    size_t cachedLength = 0;
    float  cachedWidth = -1.0f;
    float  cachedFontSize = 0.0f;
    int    logicalLineCount = 1;

    bool hasSelection() const { return cursor != anchor; }
    int  selectionStart() const { return cursor < anchor ? cursor : anchor; }
    int  selectionEnd() const { return cursor < anchor ? anchor : cursor; }
    void clearSelection() { anchor = cursor; }
};

struct TextAreaEvents {
    bool changed = false;
    bool enterPressed = false;      // block mode only
    bool backspaceAtStart = false;  // block mode: Backspace with caret at 0 and no selection
    bool deleteAtEnd = false;       // block mode: Delete with caret at end
    bool upAtFirstLine = false;     // block mode
    bool downAtLastLine = false;    // block mode
    bool escapePressed = false;
    bool tabPressed = false;        // block mode: Tab/Shift+Tab not consumed
    bool shiftHeld = false;
    bool clicked = false;           // the area was clicked this frame
    bool focused = false;
    int  cursorLine = 0;            // logical line / column of the caret (bytes)
    int  cursorColumn = 0;
    float contentHeight = 0.0f;     // height the text occupies
    int  ctrlShortcut = 0;          // an unhandled Ctrl+<letter> while focused ('b', 'i', ...), else 0
};

// Draws the editor inside a child region of `size` (0 = fill available;
// autoHeight ignores y). Returns what happened.
TextAreaEvents textArea(const char* id, std::string& text, TextAreaState& state,
                        const TextAreaOptions& options, ImVec2 size);

// Markdown syntax colouring for the source view.
void markdownHighlight(std::string_view line, std::vector<TextStyleRun>& runs);

// Byte offset helpers (UTF-8 aware).
int utf8Prev(std::string_view text, int offset);
int utf8Next(std::string_view text, int offset);

} // namespace dd::ui
