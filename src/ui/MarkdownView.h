// Markdown (GFM dialect, via md4c) -> Dear ImGui rendering.
//
// Descended from fwcom's Documentation view renderer, rebuilt around direct
// draw-list text so inline runs can mix the real bold, italic and code
// faces, wrap as one paragraph, and carry exact search highlights. Blocks
// still become ImGui items (one Dummy per paragraph, a child per code
// block, a real ImGui table for tables), so scrolling, clipping and the
// test engine all see ordinary items.
#pragma once

#include "imgui.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace dd::ui::md {

struct ImageInfo {
    ImTextureID tex = 0;
    int         w = 0;
    int         h = 0;
    std::string note;   // why there is no picture, shown in the fallback box
};

// Resolves `![alt](src)` to a texture. Returning false draws an alt-text box.
using ImageProvider = std::function<bool(std::string_view src, ImageInfo& out)>;

// Mouse selection over the rendered text, owned by the caller and kept
// between frames. Offsets index the plain text the renderer assembles in
// reading order: one line per block, table cells separated by tabs. They
// stay valid across resizes and scrolling because they follow the text, not
// the layout.
struct Selection {
    int  anchor = -1;
    int  cursor = -1;
    bool dragging = false;
    bool active() const { return anchor >= 0 && cursor >= 0 && anchor != cursor; }
    int  start() const { return anchor < cursor ? anchor : cursor; }
    int  end() const { return anchor < cursor ? cursor : anchor; }
    void clear() { anchor = cursor = -1; dragging = false; }
};

struct Options {
    std::string_view     search;              // case-insensitive; matches get highlighted
    int                  currentMatch = -1;   // 0-based match to scroll into view and emphasise
    std::string_view     scrollToHeading;     // heading text to bring to the top this frame
    int                  scrollToLine = -1;   // or a source line (headings carry their line)
    const ImageProvider* images = nullptr;
    bool                 interactiveTasks = true;   // click a task checkbox to toggle it
    // Enables click-drag selection (double-click selects a word, Ctrl+A all,
    // Escape clears) and reports Ctrl+C through Result::copiedSelection.
    Selection*           selection = nullptr;
};

struct HeadingHit {
    int         level = 1;
    std::string text;
    float       screenY = 0.0f;
    int         sourceLine = 0;
};

struct Result {
    int         matchCount = 0;
    std::string clickedLink;        // href clicked this frame, empty if none
    std::string hoveredLink;        // href under the mouse, for a status bar
    bool        headingFound = false;
    int         taskToggleOffset = -1;   // byte offset of a clicked "[ ]"/"[x]" mark, -1 if none
    int         imagesDrawn = 0;
    int         imagesFallback = 0;
    int         codeBlocksDrawn = 0;
    int         tablesDrawn = 0;
    std::string copiedCode;          // a code block's copy button was pressed: its text
    std::string copiedSelection;     // Ctrl+C with a selection: the selected plain text
    std::vector<HeadingHit> headings;
    std::vector<float> lastTableColumnWidths;   // for tests
};

// Renders `text` into the current window at the cursor, advancing it.
Result render(std::string_view text, const Options& options);

// Decodes HTML entities md4c passes through verbatim ("&amp;", "&#169;").
std::string decodeEntity(std::string_view entity);

} // namespace dd::ui::md
