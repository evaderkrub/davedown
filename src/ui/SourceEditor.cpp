#include "ui/SourceEditor.h"

#include "app/MarkdownModel.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"

namespace dd::ui {

SourceEditorResult drawSourceEditor(const char* id, std::string& text, SourceEditorState& st,
                                    int scrollToLine, bool requestFocus)
{
    SourceEditorResult r;
    if (scrollToLine >= 0) st.area.requestCursorLine = scrollToLine;
    if (requestFocus) st.area.requestFocus = true;

    TextAreaOptions opt;
    opt.font = fonts::code();
    opt.fontSize = ImGui::GetFontSize() * 0.95f;
    opt.lineHeightFactor = 1.55f;
    opt.wordWrap = true;
    opt.showLineNumbers = true;
    opt.highlight = markdownHighlight;
    opt.placeholder = "Write markdown here...";
    opt.paddingX = ImGui::GetFontSize() * 0.5f;

    const TextAreaEvents ev = textArea(id, text, st.area, opt, ImVec2(0, 0));
    r.changed = ev.changed;
    r.cursorLine = ev.cursorLine;
    r.cursorColumn = ev.cursorColumn;
    r.focused = ev.focused;

    // Ctrl+B/I/E/K are bound by the main window's global shortcuts, which
    // also cover the WYSIWYG mode, so they are deliberately not handled here.
    return r;
}

bool applyInlineCommand(std::string& text, SourceEditorState& st, InlineCommand cmd)
{
    int a = st.area.selectionStart();
    int b = st.area.selectionEnd();
    std::string out;
    switch (cmd) {
    case InlineCommand::Bold:   out = toggleInlineMarker(text, a, b, "**"); break;
    case InlineCommand::Italic: out = toggleInlineMarker(text, a, b, "*"); break;
    case InlineCommand::Strike: out = toggleInlineMarker(text, a, b, "~~"); break;
    case InlineCommand::Code:   out = toggleInlineMarker(text, a, b, "`"); break;
    case InlineCommand::Link:   out = insertLink(text, a, b, ""); break;
    }
    if (out == text) return false;
    text = std::move(out);
    st.area.anchor = a;
    st.area.cursor = b;
    st.area.scrollToCursor = true;
    return true;
}

} // namespace dd::ui
