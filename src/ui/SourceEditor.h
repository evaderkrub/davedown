// The raw-markdown view: a monospace, syntax-coloured, word-wrapping editor
// with line numbers.
#pragma once

#include "ui/TextArea.h"

#include <string>

namespace dd::ui {

struct SourceEditorState {
    TextAreaState area;
};

struct SourceEditorResult {
    bool changed = false;
    int  cursorLine = 0;
    int  cursorColumn = 0;
    bool focused = false;
};

// `scrollToLine` >= 0 scrolls that logical line into view this frame.
SourceEditorResult drawSourceEditor(const char* id, std::string& text, SourceEditorState& state,
                                    int scrollToLine, bool requestFocus);

// Formatting commands applied to the editor's current selection.
enum class InlineCommand { Bold, Italic, Strike, Code, Link };
bool applyInlineCommand(std::string& text, SourceEditorState& state, InlineCommand cmd);

} // namespace dd::ui
