#include "ui/TextArea.h"

#include "ui/Fonts.h"
#include "ui/Theme.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace dd::ui {

namespace {

bool isCont(unsigned char c) { return (c & 0xC0) == 0x80; }

bool isWordChar(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

float measure(ImFont* font, float size, std::string_view s)
{
    if (s.empty()) return 0.0f;
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, s.data(), s.data() + s.size()).x;
}

size_t hashText(std::string_view s)
{
    // FNV-1a; cheap enough per frame for a document and detects edits.
    size_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

void relayout(TextAreaState& st, std::string_view text, ImFont* font, float fontSize, float width, bool wrap)
{
    const size_t h = hashText(text);
    if (h == st.cachedHash && text.size() == st.cachedLength && width == st.cachedWidth &&
        fontSize == st.cachedFontSize && !st.lines.empty())
        return;
    st.cachedHash = h;
    st.cachedLength = text.size();
    st.cachedWidth = width;
    st.cachedFontSize = fontSize;
    st.lines.clear();

    const float spaceW = measure(font, fontSize, " ");
    int logical = 0;
    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        size_t nl = text.find('\n', lineStart);
        const size_t lineEnd = nl == std::string_view::npos ? text.size() : nl;
        std::string_view line = text.substr(lineStart, lineEnd - lineStart);

        if (!wrap || line.empty()) {
            TextAreaState::VisualLine vl;
            vl.start = (int)lineStart;
            vl.end = (int)lineEnd;
            vl.logical = logical;
            vl.hardEnd = true;
            vl.width = measure(font, fontSize, line);
            st.lines.push_back(vl);
        } else {
            size_t i = 0;
            size_t vstart = 0;
            float x = 0.0f;
            while (i < line.size()) {
                size_t e = i;
                while (e < line.size() && line[e] != ' ') ++e;
                const size_t wordEnd = e;
                while (e < line.size() && line[e] == ' ') ++e;
                const float wordW = measure(font, fontSize, line.substr(i, wordEnd - i));
                const float trailing = spaceW * (float)(e - wordEnd);
                if (x > 0.0f && x + wordW > width) {
                    TextAreaState::VisualLine vl;
                    vl.start = (int)(lineStart + vstart);
                    vl.end = (int)(lineStart + i);
                    vl.logical = logical;
                    vl.hardEnd = false;
                    vl.width = x;
                    st.lines.push_back(vl);
                    vstart = i;
                    x = 0.0f;
                }
                // A single word wider than the line: break it by character.
                if (wordW > width && x == 0.0f) {
                    size_t c = i;
                    float cx = 0.0f;
                    while (c < wordEnd) {
                        const size_t n = (size_t)utf8Next(line, (int)c);
                        const float cw = measure(font, fontSize, line.substr(c, n - c));
                        if (cx > 0.0f && cx + cw > width) {
                            TextAreaState::VisualLine vl;
                            vl.start = (int)(lineStart + vstart);
                            vl.end = (int)(lineStart + c);
                            vl.logical = logical;
                            vl.hardEnd = false;
                            vl.width = cx;
                            st.lines.push_back(vl);
                            vstart = c;
                            cx = 0.0f;
                        }
                        cx += cw;
                        c = n;
                    }
                    x = cx + trailing;
                } else {
                    x += wordW + trailing;
                }
                i = e;
            }
            TextAreaState::VisualLine vl;
            vl.start = (int)(lineStart + vstart);
            vl.end = (int)lineEnd;
            vl.logical = logical;
            vl.hardEnd = true;
            vl.width = x;
            st.lines.push_back(vl);
        }
        if (nl == std::string_view::npos) break;
        lineStart = nl + 1;
        ++logical;
    }
    st.logicalLineCount = logical + 1;
    if (st.lines.empty()) {
        TextAreaState::VisualLine vl;
        st.lines.push_back(vl);
    }
}

int visualLineOf(const TextAreaState& st, int offset)
{
    for (size_t i = 0; i < st.lines.size(); ++i) {
        const auto& vl = st.lines[i];
        if (offset < vl.end) return (int)i;
        if (offset == vl.end && (vl.hardEnd || i + 1 == st.lines.size())) return (int)i;
    }
    return (int)st.lines.size() - 1;
}

float xOfOffset(const TextAreaState& st, std::string_view text, int vline, int offset,
                ImFont* font, float fontSize)
{
    const auto& vl = st.lines[(size_t)vline];
    const int s = std::clamp(offset, vl.start, vl.end);
    return measure(font, fontSize, text.substr((size_t)vl.start, (size_t)(s - vl.start)));
}

int offsetAtX(const TextAreaState& st, std::string_view text, int vline, float x,
              ImFont* font, float fontSize)
{
    const auto& vl = st.lines[(size_t)vline];
    int off = vl.start;
    float acc = 0.0f;
    while (off < vl.end) {
        const int n = utf8Next(text, off);
        const float w = measure(font, fontSize, text.substr((size_t)off, (size_t)(n - off)));
        if (x < acc + w * 0.5f) return off;
        acc += w;
        off = n;
    }
    return vl.end;
}

struct EditOps {
    std::string& text;
    TextAreaState& st;
    bool changed = false;

    void deleteRange(int a, int b)
    {
        if (a > b) std::swap(a, b);
        a = std::clamp(a, 0, (int)text.size());
        b = std::clamp(b, 0, (int)text.size());
        if (a == b) return;
        text.erase((size_t)a, (size_t)(b - a));
        st.cursor = a;
        st.anchor = a;
        changed = true;
    }
    void deleteSelection()
    {
        if (st.hasSelection()) deleteRange(st.selectionStart(), st.selectionEnd());
    }
    void insert(std::string_view s)
    {
        deleteSelection();
        st.cursor = std::clamp(st.cursor, 0, (int)text.size());
        text.insert((size_t)st.cursor, s);
        st.cursor += (int)s.size();
        st.anchor = st.cursor;
        changed = true;
    }
};

} // namespace

int utf8Prev(std::string_view text, int offset)
{
    if (offset <= 0) return 0;
    int i = std::min(offset, (int)text.size()) - 1;
    while (i > 0 && isCont((unsigned char)text[(size_t)i])) --i;
    return i;
}

int utf8Next(std::string_view text, int offset)
{
    if (offset >= (int)text.size()) return (int)text.size();
    int i = offset + 1;
    while (i < (int)text.size() && isCont((unsigned char)text[(size_t)i])) ++i;
    return i;
}

TextAreaEvents textArea(const char* id, std::string& text, TextAreaState& st,
                        const TextAreaOptions& opt, ImVec2 size)
{
    TextAreaEvents ev;
    ImGuiIO& io = ImGui::GetIO();
    const Palette& pal = palette();
    ImFont* font = opt.font ? opt.font : ImGui::GetFont();
    const float fontSize = opt.fontSize > 0.0f ? opt.fontSize : ImGui::GetFontSize();
    const float lineH = std::floor(fontSize * opt.lineHeightFactor);
    const ImU32 textCol = opt.textColor ? opt.textColor : ImGui::GetColorU32(ImGuiCol_Text);

    st.cursor = std::clamp(st.cursor, 0, (int)text.size());
    st.anchor = std::clamp(st.anchor, 0, (int)text.size());

    // Pending requests from the host.
    if (st.requestCursor == -2) { st.cursor = st.anchor = (int)text.size(); st.requestCursor = -1; st.scrollToCursor = true; }
    else if (st.requestCursor >= 0) { st.cursor = st.anchor = std::min(st.requestCursor, (int)text.size()); st.requestCursor = -1; st.scrollToCursor = true; }

    ImGui::PushID(id);

    // Gutter for line numbers.
    float gutterW = 0.0f;
    if (opt.showLineNumbers) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", std::max(st.logicalLineCount, 10));
        gutterW = measure(font, fontSize, buf) + fontSize * 1.2f;
    }

    ImVec2 childSize = size;
    const float availW = ImGui::GetContentRegionAvail().x;
    if (childSize.x <= 0.0f) childSize.x = availW;
    // Wrap width is known before the child exists; reserve room for a
    // scrollbar so wrapping does not flicker when one appears.
    const float scrollbarW = opt.autoHeight ? 0.0f : ImGui::GetStyle().ScrollbarSize;
    const float wrapW = std::max(32.0f, childSize.x - gutterW - opt.paddingX * 2.0f - scrollbarW - 4.0f);
    relayout(st, text, font, fontSize, wrapW, opt.wordWrap);

    const float contentH = (float)st.lines.size() * lineH;
    ev.contentHeight = contentH;
    if (opt.autoHeight) childSize.y = contentH + 4.0f;
    else if (childSize.y <= 0.0f) childSize.y = ImGui::GetContentRegionAvail().y;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const ImGuiWindowFlags childFlags = opt.autoHeight ? ImGuiWindowFlags_NoScrollbar : ImGuiWindowFlags_None;
    const bool open = ImGui::BeginChild("##area", childSize, ImGuiChildFlags_None, childFlags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (!open) {
        ImGui::EndChild();
        ImGui::PopID();
        return ev;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float textX = origin.x + gutterW + opt.paddingX;
    const ImVec2 itemSize(std::max(childSize.x - scrollbarW, 8.0f),
                          std::max(contentH + 4.0f, opt.autoHeight ? 0.0f : ImGui::GetContentRegionAvail().y));
    ImGui::InvisibleButton("##hit", itemSize, ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImGuiID itemId = ImGui::GetItemID();

    bool justFocused = false;
    if (st.requestFocus) {
        ImGui::SetFocusID(itemId, ImGui::GetCurrentWindow());
        ImGui::FocusWindow(ImGui::GetCurrentWindow());
        st.requestFocus = false;
        st.focused = true;
        justFocused = true;
        st.blinkStart = ImGui::GetTime();
    }
    const bool focused = ImGui::IsItemFocused() || st.focused;
    ev.focused = focused;
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

    const double now = ImGui::GetTime();
    const bool shift = io.KeyShift;
    const bool ctrl = io.KeyCtrl;
    ev.shiftHeld = shift;

    auto setCursor = [&](int off, bool keepAnchor) {
        st.cursor = std::clamp(off, 0, (int)text.size());
        if (!keepAnchor) st.anchor = st.cursor;
        st.blinkStart = now;
        st.scrollToCursor = true;
    };

    // --- mouse -----------------------------------------------------------
    auto offsetFromMouse = [&](ImVec2 m) {
        const float relY = m.y - origin.y;
        int vline = (int)std::floor(relY / lineH);
        vline = std::clamp(vline, 0, (int)st.lines.size() - 1);
        return offsetAtX(st, text, vline, m.x - textX, font, fontSize);
    };
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ev.clicked = true;
        st.focused = true;
        ImGui::SetFocusID(itemId, ImGui::GetCurrentWindow());
        const int off = offsetFromMouse(io.MousePos);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            int a = off, b = off;
            while (a > 0 && isWordChar((unsigned char)text[(size_t)a - 1])) --a;
            while (b < (int)text.size() && isWordChar((unsigned char)text[(size_t)b])) ++b;
            st.anchor = a;
            st.cursor = b;
            st.dragging = false;
        } else {
            setCursor(off, shift);
            st.dragging = true;
        }
        st.preferredX = -1.0f;
    }
    if (st.dragging) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
                st.cursor = offsetFromMouse(io.MousePos);
                st.blinkStart = now;
                // Auto-scroll while dragging past the edges.
                const float wy = ImGui::GetWindowPos().y;
                if (io.MousePos.y < wy) ImGui::SetScrollY(ImGui::GetScrollY() - lineH);
                else if (io.MousePos.y > wy + ImGui::GetWindowHeight()) ImGui::SetScrollY(ImGui::GetScrollY() + lineH);
            }
        } else {
            st.dragging = false;
        }
    }
    // Clicking elsewhere drops focus (a host that re-focused us this frame,
    // e.g. after a toolbar button, is exempt).
    if (st.focused && !justFocused && !hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !st.dragging)
        st.focused = false;
    (void)active;

    // --- keyboard --------------------------------------------------------
    EditOps ops{text, st};
    if (st.focused) {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        g.WantTextInputNextFrame = 1;
        ImGui::SetNextFrameWantCaptureKeyboard(true);
        // Keep ImGui's nav from eating the arrows/tab.
        ImGui::SetKeyOwner(ImGuiKey_LeftArrow, itemId);
        ImGui::SetKeyOwner(ImGuiKey_RightArrow, itemId);
        ImGui::SetKeyOwner(ImGuiKey_UpArrow, itemId);
        ImGui::SetKeyOwner(ImGuiKey_DownArrow, itemId);
        ImGui::SetKeyOwner(ImGuiKey_Tab, itemId);
        ImGui::SetKeyOwner(ImGuiKey_Home, itemId);
        ImGui::SetKeyOwner(ImGuiKey_End, itemId);
        ImGui::SetKeyOwner(ImGuiKey_PageUp, itemId);
        ImGui::SetKeyOwner(ImGuiKey_PageDown, itemId);
        ImGui::SetKeyOwner(ImGuiKey_Enter, itemId);
        ImGui::SetKeyOwner(ImGuiKey_Backspace, itemId);
        ImGui::SetKeyOwner(ImGuiKey_Delete, itemId);
        ImGui::SetKeyOwner(ImGuiKey_Escape, itemId);

        const int curV = visualLineOf(st, st.cursor);
        auto moveVertical = [&](int delta) {
            const int target = curV + delta;
            if (target < 0 || target >= (int)st.lines.size()) return false;
            if (st.preferredX < 0.0f) st.preferredX = xOfOffset(st, text, curV, st.cursor, font, fontSize);
            setCursor(offsetAtX(st, text, target, st.preferredX, font, fontSize), shift);
            return true;
        };

        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            if (ctrl) {
                int c = st.cursor;
                while (c > 0 && !isWordChar((unsigned char)text[(size_t)c - 1])) --c;
                while (c > 0 && isWordChar((unsigned char)text[(size_t)c - 1])) --c;
                setCursor(c, shift);
            } else if (st.hasSelection() && !shift) {
                setCursor(st.selectionStart(), false);
            } else {
                setCursor(utf8Prev(text, st.cursor), shift);
            }
            st.preferredX = -1.0f;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            if (ctrl) {
                int c = st.cursor;
                while (c < (int)text.size() && isWordChar((unsigned char)text[(size_t)c])) ++c;
                while (c < (int)text.size() && !isWordChar((unsigned char)text[(size_t)c])) ++c;
                setCursor(c, shift);
            } else if (st.hasSelection() && !shift) {
                setCursor(st.selectionEnd(), false);
            } else {
                setCursor(utf8Next(text, st.cursor), shift);
            }
            st.preferredX = -1.0f;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            if (!moveVertical(-1)) { if (opt.blockMode) ev.upAtFirstLine = true; else setCursor(0, shift); }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            if (!moveVertical(1)) { if (opt.blockMode) ev.downAtLastLine = true; else setCursor((int)text.size(), shift); }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
            setCursor(ctrl ? 0 : st.lines[(size_t)curV].start, shift);
            st.preferredX = -1.0f;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_End)) {
            setCursor(ctrl ? (int)text.size() : st.lines[(size_t)curV].end, shift);
            st.preferredX = -1.0f;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp) || ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
            const int page = std::max(1, (int)(ImGui::GetWindowHeight() / lineH) - 1);
            const int delta = ImGui::IsKeyPressed(ImGuiKey_PageUp) ? -page : page;
            const int target = std::clamp(curV + delta, 0, (int)st.lines.size() - 1);
            if (st.preferredX < 0.0f) st.preferredX = xOfOffset(st, text, curV, st.cursor, font, fontSize);
            setCursor(offsetAtX(st, text, target, st.preferredX, font, fontSize), shift);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ev.escapePressed = true;

        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) { st.anchor = 0; st.cursor = (int)text.size(); }
        if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_C, false) || ImGui::IsKeyPressed(ImGuiKey_Insert, false))) {
            if (st.hasSelection())
                ImGui::SetClipboardText(text.substr((size_t)st.selectionStart(),
                                                    (size_t)(st.selectionEnd() - st.selectionStart())).c_str());
        }
        if (!opt.readOnly) {
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X, false) && st.hasSelection()) {
                ImGui::SetClipboardText(text.substr((size_t)st.selectionStart(),
                                                    (size_t)(st.selectionEnd() - st.selectionStart())).c_str());
                ops.deleteSelection();
            }
            if ((ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) || (shift && ImGui::IsKeyPressed(ImGuiKey_Insert, false))) {
                if (const char* clip = ImGui::GetClipboardText()) {
                    std::string s(clip);
                    std::string norm;
                    norm.reserve(s.size());
                    for (size_t i = 0; i < s.size(); ++i) {
                        if (s[i] == '\r') { norm.push_back('\n'); if (i + 1 < s.size() && s[i + 1] == '\n') ++i; }
                        else norm.push_back(s[i]);
                    }
                    ops.insert(norm);
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                if (st.hasSelection()) ops.deleteSelection();
                else if (st.cursor == 0 && opt.blockMode) ev.backspaceAtStart = true;
                else if (st.cursor > 0) {
                    int a = ctrl ? st.cursor : utf8Prev(text, st.cursor);
                    if (ctrl) {
                        while (a > 0 && !isWordChar((unsigned char)text[(size_t)a - 1])) --a;
                        while (a > 0 && isWordChar((unsigned char)text[(size_t)a - 1])) --a;
                    }
                    ops.deleteRange(a, st.cursor);
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                if (st.hasSelection()) ops.deleteSelection();
                else if (st.cursor >= (int)text.size() && opt.blockMode) ev.deleteAtEnd = true;
                else if (st.cursor < (int)text.size()) {
                    int b = ctrl ? st.cursor : utf8Next(text, st.cursor);
                    if (ctrl) {
                        while (b < (int)text.size() && isWordChar((unsigned char)text[(size_t)b])) ++b;
                        while (b < (int)text.size() && !isWordChar((unsigned char)text[(size_t)b])) ++b;
                    }
                    ops.deleteRange(st.cursor, b);
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                if (opt.blockMode && !shift) ev.enterPressed = true;
                else {
                    // Keep the indentation / list prefix of the current line.
                    const int ls = st.lines[(size_t)curV].start;
                    int ind = ls;
                    // Find the logical line start.
                    int lls = st.cursor;
                    while (lls > 0 && text[(size_t)lls - 1] != '\n') --lls;
                    ind = lls;
                    while (ind < (int)text.size() && (text[(size_t)ind] == ' ' || text[(size_t)ind] == '\t')) ++ind;
                    std::string prefix = text.substr((size_t)lls, (size_t)(ind - lls));
                    if (!opt.blockMode) {
                        // Continue list markers in the source view.
                        std::string_view rest = std::string_view(text).substr((size_t)ind);
                        if (rest.rfind("- [ ] ", 0) == 0 || rest.rfind("- [x] ", 0) == 0) prefix += "- [ ] ";
                        else if (rest.rfind("- ", 0) == 0 || rest.rfind("* ", 0) == 0 || rest.rfind("+ ", 0) == 0) prefix += rest.substr(0, 2);
                        else if (rest.rfind("> ", 0) == 0) prefix += "> ";
                        else {
                            size_t n = 0;
                            while (n < rest.size() && std::isdigit((unsigned char)rest[n])) ++n;
                            if (n > 0 && n + 1 < rest.size() && rest[n] == '.' && rest[n + 1] == ' ')
                                prefix += std::to_string(std::atoi(std::string(rest.substr(0, n)).c_str()) + 1) + ". ";
                        }
                    }
                    ops.insert("\n" + prefix);
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
                if (opt.blockMode) ev.tabPressed = true;
                else if (shift) {
                    // Outdent the current logical line by up to two spaces.
                    int lls = st.cursor;
                    while (lls > 0 && text[(size_t)lls - 1] != '\n') --lls;
                    int n = 0;
                    while (n < 2 && lls + n < (int)text.size() && text[(size_t)(lls + n)] == ' ') ++n;
                    if (n > 0) { text.erase((size_t)lls, (size_t)n); st.cursor = std::max(lls, st.cursor - n); st.anchor = st.cursor; ops.changed = true; }
                } else {
                    ops.insert("  ");
                }
            }
            // Typed characters.
            for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
                const ImWchar c = io.InputQueueCharacters[i];
                if (c < 32 || c == 127) continue;
                if (ctrl && !io.KeyAlt) continue;   // shortcuts, not text
                std::string utf8;
                char buf[5] = {};
                ImTextCharToUtf8(buf, (unsigned)c);
                utf8 = buf;
                ops.insert(utf8);
            }
            if (io.InputQueueCharacters.Size > 0) io.InputQueueCharacters.resize(0);
        }
        // Report Ctrl+letter shortcuts the host might bind (bold, italic...).
        if (ctrl && !io.KeyAlt) {
            for (int k = ImGuiKey_A; k <= ImGuiKey_Z; ++k) {
                if (ImGui::IsKeyPressed((ImGuiKey)k, false)) {
                    const char letter = (char)('a' + (k - ImGuiKey_A));
                    if (letter != 'a' && letter != 'c' && letter != 'v' && letter != 'x') ev.ctrlShortcut = letter;
                }
            }
        }
        if (ops.changed) {
            st.lastEditTime = now;
            st.blinkStart = now;
            st.scrollToCursor = true;
            st.preferredX = -1.0f;
            ev.changed = true;
            relayout(st, text, font, fontSize, wrapW, opt.wordWrap);
        }
    }

    st.cursor = std::clamp(st.cursor, 0, (int)text.size());
    st.anchor = std::clamp(st.anchor, 0, (int)text.size());

    // --- scrolling -------------------------------------------------------
    const int cursorV = visualLineOf(st, st.cursor);
    if (st.requestCursorLine >= 0) {
        for (size_t i = 0; i < st.lines.size(); ++i) {
            if (st.lines[i].logical == st.requestCursorLine) {
                ImGui::SetScrollY((float)i * lineH - lineH);
                break;
            }
        }
        st.requestCursorLine = -1;
    }
    if (st.scrollToCursor && !opt.autoHeight) {
        const float cy = (float)cursorV * lineH;
        const float sy = ImGui::GetScrollY();
        const float wh = ImGui::GetWindowHeight();
        if (cy < sy) ImGui::SetScrollY(cy);
        else if (cy + lineH > sy + wh) ImGui::SetScrollY(cy + lineH - wh);
        st.scrollToCursor = false;
    }

    // --- drawing ---------------------------------------------------------
    const float scrollY = ImGui::GetScrollY();
    const float viewTop = scrollY;
    const float viewBottom = scrollY + ImGui::GetWindowHeight() + lineH;
    const int firstV = std::max(0, (int)std::floor(viewTop / lineH) - 1);
    const int lastV = std::min((int)st.lines.size() - 1, (int)std::ceil(viewBottom / lineH) + 1);

    const int selA = st.selectionStart(), selB = st.selectionEnd();
    const ImU32 selCol = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);

    std::vector<TextStyleRun> runs;
    int lastLogical = -1;
    for (int v = firstV; v <= lastV && v < (int)st.lines.size(); ++v) {
        const auto& vl = st.lines[(size_t)v];
        const float y = origin.y + (float)v * lineH;
        const float textY = y + (lineH - fontSize) * 0.5f;

        // Gutter number on the first visual line of each logical line.
        if (opt.showLineNumbers && (v == 0 || st.lines[(size_t)v - 1].logical != vl.logical)) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", vl.logical + 1);
            const float w = measure(font, fontSize, buf);
            const bool isCur = vl.logical == st.lines[(size_t)cursorV].logical;
            dl->AddText(font, fontSize, ImVec2(origin.x + gutterW - w - fontSize * 0.7f, textY),
                        isCur ? pal.textMuted : pal.textFaint, buf);
        }

        // Selection.
        if (st.hasSelection() && selA < vl.end + 1 && selB > vl.start) {
            const int a = std::max(selA, vl.start);
            const int b = std::min(selB, vl.end);
            const float x0 = textX + xOfOffset(st, text, v, a, font, fontSize);
            float x1 = textX + xOfOffset(st, text, v, b, font, fontSize);
            if (selB > vl.end && vl.hardEnd) x1 += fontSize * 0.4f;   // show the newline is selected
            dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + lineH), selCol, 2.0f);
        }

        // Text with highlight runs (computed per logical line).
        if (opt.highlight && vl.logical != lastLogical) {
            runs.clear();
            int lls = vl.start;
            while (lls > 0 && text[(size_t)lls - 1] != '\n') --lls;
            int lle = lls;
            while (lle < (int)text.size() && text[(size_t)lle] != '\n') ++lle;
            opt.highlight(std::string_view(text).substr((size_t)lls, (size_t)(lle - lls)), runs);
            for (TextStyleRun& r : runs) { r.start += lls; r.end += lls; }
            std::sort(runs.begin(), runs.end(), [](const TextStyleRun& a, const TextStyleRun& b) { return a.start < b.start; });
            lastLogical = vl.logical;
        }
        float x = textX;
        int pos = vl.start;
        auto drawSeg = [&](int a, int b, ImU32 col, ImFont* f) {
            if (b <= a) return;
            std::string_view seg = std::string_view(text).substr((size_t)a, (size_t)(b - a));
            dl->AddText(f, fontSize, ImVec2(x, textY), col, seg.data(), seg.data() + seg.size());
            x += measure(f, fontSize, seg);
        };
        if (opt.highlight) {
            for (const TextStyleRun& r : runs) {
                const int rs = std::max(r.start, vl.start), re = std::min(r.end, vl.end);
                if (re <= pos) continue;
                if (rs >= vl.end) break;
                drawSeg(pos, rs, textCol, font);
                drawSeg(std::max(rs, pos), re, r.color ? r.color : textCol, r.font ? r.font : font);
                pos = std::max(pos, re);
            }
        }
        drawSeg(pos, vl.end, textCol, font);
    }

    if (text.empty() && opt.placeholder && !st.focused)
        dl->AddText(font, fontSize, ImVec2(textX, origin.y + (lineH - fontSize) * 0.5f), pal.textFaint, opt.placeholder);

    // Caret.
    if (st.focused && !opt.readOnly) {
        const double phase = std::fmod(now - st.blinkStart, 1.1);
        if (phase < 0.6 || st.dragging) {
            const float cx = textX + xOfOffset(st, text, cursorV, st.cursor, font, fontSize);
            const float cy = origin.y + (float)cursorV * lineH;
            dl->AddRectFilled(ImVec2(cx, cy + (lineH - fontSize) * 0.5f - 1.0f),
                              ImVec2(cx + 1.5f, cy + (lineH + fontSize) * 0.5f + 1.0f),
                              ImGui::GetColorU32(ImGuiCol_InputTextCursor));
        }
    }

    // Report caret position.
    {
        const auto& vl = st.lines[(size_t)cursorV];
        int lls = vl.start;
        while (lls > 0 && text[(size_t)lls - 1] != '\n') --lls;
        ev.cursorLine = vl.logical;
        ev.cursorColumn = st.cursor - lls;
    }

    ImGui::EndChild();
    ImGui::PopID();
    return ev;
}

void markdownHighlight(std::string_view line, std::vector<TextStyleRun>& runs)
{
    const Palette& pal = palette();
    auto add = [&](int a, int b, ImU32 col, ImFont* f = nullptr) {
        if (b > a) runs.push_back({a, b, col, f});
    };
    std::string_view s = line;
    size_t lead = 0;
    while (lead < s.size() && (s[lead] == ' ' || s[lead] == '\t')) ++lead;
    std::string_view body = s.substr(lead);

    // Fences, headings, quotes, rules, list markers.
    if (body.size() >= 3 && (body.rfind("```", 0) == 0 || body.rfind("~~~", 0) == 0)) {
        add(0, (int)s.size(), pal.textMuted);
        return;
    }
    {
        size_t n = 0;
        while (n < body.size() && body[n] == '#') ++n;
        if (n >= 1 && n <= 6 && (n == body.size() || body[n] == ' ')) {
            add((int)lead, (int)(lead + n), pal.accent);
            add((int)(lead + n), (int)s.size(), pal.heading);
            return;
        }
    }
    if (!body.empty() && body[0] == '>') {
        add((int)lead, (int)lead + 1, pal.accent);
        add((int)lead + 1, (int)s.size(), pal.textMuted);
        return;
    }
    {
        bool rule = body.size() >= 3;
        for (char c : body) if (c != '-' && c != '*' && c != '_' && c != ' ') { rule = false; break; }
        if (rule && body.find_first_of("-*_") != std::string_view::npos) { add(0, (int)s.size(), pal.textFaint); return; }
    }
    int inlineStart = (int)lead;
    if (body.size() >= 2 && (body[0] == '-' || body[0] == '*' || body[0] == '+') && body[1] == ' ') {
        add((int)lead, (int)lead + 1, pal.accent);
        inlineStart = (int)lead + 2;
        std::string_view rest = body.substr(2);
        if (rest.size() >= 3 && rest[0] == '[' && rest[2] == ']') {
            add(inlineStart, inlineStart + 3, rest[1] == ' ' ? pal.textMuted : pal.success);
            inlineStart += 3;
        }
    } else {
        size_t n = 0;
        while (n < body.size() && std::isdigit((unsigned char)body[n])) ++n;
        if (n > 0 && n + 1 < body.size() && (body[n] == '.' || body[n] == ')') && body[n + 1] == ' ') {
            add((int)lead, (int)(lead + n + 1), pal.accent);
            inlineStart = (int)(lead + n + 2);
        }
    }
    // Table rows: colour the pipes.
    if (!body.empty() && body[0] == '|') {
        for (size_t i = 0; i < s.size(); ++i)
            if (s[i] == '|') add((int)i, (int)i + 1, pal.textFaint);
        return;
    }

    // Inline: code spans, bold/italic, links, images, inline HTML.
    int i = inlineStart;
    const int n = (int)s.size();
    while (i < n) {
        const char c = s[(size_t)i];
        if (c == '`') {
            int e = i + 1;
            while (e < n && s[(size_t)e] != '`') ++e;
            if (e < n) { add(i, e + 1, pal.inlineCodeText); i = e + 1; continue; }
        }
        if (c == '*' || c == '_') {
            const bool dbl = i + 1 < n && s[(size_t)i + 1] == c;
            const int len = dbl ? 2 : 1;
            int e = i + len;
            // Find the closing marker on this line.
            int close = -1;
            while (e < n) {
                if (s[(size_t)e] == c && (!dbl || (e + 1 < n && s[(size_t)e + 1] == c))) { close = e; break; }
                ++e;
            }
            if (close > i + len) {
                add(i, i + len, pal.textFaint);
                add(i + len, close, dbl ? pal.heading : pal.textMuted);
                add(close, close + len, pal.textFaint);
                i = close + len;
                continue;
            }
        }
        if (c == '~' && i + 1 < n && s[(size_t)i + 1] == '~') {
            const size_t close = s.find("~~", (size_t)i + 2);
            if (close != std::string_view::npos) {
                add(i, i + 2, pal.textFaint);
                add(i + 2, (int)close, pal.textMuted);
                add((int)close, (int)close + 2, pal.textFaint);
                i = (int)close + 2;
                continue;
            }
        }
        if (c == '[' || (c == '!' && i + 1 < n && s[(size_t)i + 1] == '[')) {
            const int lb = c == '!' ? i + 1 : i;
            const size_t rb = s.find(']', (size_t)lb + 1);
            if (rb != std::string_view::npos && rb + 1 < (size_t)n && s[rb + 1] == '(') {
                const size_t rp = s.find(')', rb + 2);
                if (rp != std::string_view::npos) {
                    add(i, lb + 1, pal.textFaint);
                    add(lb + 1, (int)rb, c == '!' ? pal.warning : pal.link);
                    add((int)rb, (int)rb + 2, pal.textFaint);
                    add((int)rb + 2, (int)rp, pal.textMuted);
                    add((int)rp, (int)rp + 1, pal.textFaint);
                    i = (int)rp + 1;
                    continue;
                }
            }
        }
        if (c == '<') {
            const size_t gt = s.find('>', (size_t)i + 1);
            if (gt != std::string_view::npos && gt > (size_t)i + 1 &&
                (std::isalpha((unsigned char)s[(size_t)i + 1]) || s[(size_t)i + 1] == '/' || s[(size_t)i + 1] == '!')) {
                add(i, (int)gt + 1, pal.textFaint);
                i = (int)gt + 1;
                continue;
            }
        }
        ++i;
    }
}

} // namespace dd::ui
