#include "ui/MarkdownView.h"

#include "ui/Fonts.h"
#include "ui/Theme.h"
#include "IconsMaterialDesign.h"

#include "imgui_internal.h"   // ImGuiTable: cell padding/spacing for column widths
#include "md4c.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace dd::ui::md {
namespace {

constexpr unsigned kMd4cFlags =
    MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS |
    MD_FLAG_PERMISSIVEAUTOLINKS;

enum SpanFlag : int {
    F_Bold   = 1 << 0,
    F_Italic = 1 << 1,
    F_Strike = 1 << 2,
    F_Code   = 1 << 3,
    F_Link   = 1 << 4,
};

struct ImageRef {
    std::string src;
    std::string alt;
};

// One styled run of inline text. `hardBreak` runs carry no text and force a
// new line; `imageIndex` runs stand for a picture drawn as its own block.
struct Span {
    std::string text;
    int         flags = 0;
    int         linkIndex = -1;
    int         imageIndex = -1;
    bool        hardBreak = false;
};

struct InlineContent {
    std::vector<Span>        spans;
    std::vector<std::string> links;
    std::vector<ImageRef>    images;
    bool empty() const { return spans.empty(); }
    void clear() { spans.clear(); links.clear(); images.clear(); }
};

struct ListCtx {
    bool ordered = false;
    int  next = 1;
    int  depth = 0;
};

struct TableMeasure {
    std::vector<float> widest;
    std::vector<float> longestWord;
    std::vector<bool>  hasImage;
};

struct State {
    const Options* opt = nullptr;
    Result         out;

    InlineContent  cur;
    int            spanFlags = 0;
    int            linkDepth = 0;
    int            currentLink = -1;
    int            currentImage = -1;

    // Block context.
    bool inHeading = false;
    int  headingLevel = 0;
    int  headingSourceLine = 0;
    bool inCode = false;
    std::string codeBuf;
    std::string codeLang;
    int  codeId = 0;

    std::vector<ListCtx> lists;
    bool markerPending = false;
    bool itemIsTask = false;
    bool itemChecked = false;
    int  itemTaskOffset = -1;

    int   quoteDepth = 0;
    std::vector<float> quoteStartY;

    bool inTable = false;
    bool inTableHead = false;
    int  tableCols = 0;
    int  tableCol = 0;
    int  tableId = 0;
    std::vector<int> cellAlign;    // MD_ALIGN per column
    std::vector<TableMeasure> tableMeasures;

    bool firstBlock = true;
    std::string_view source;
};

// ---- small helpers ---------------------------------------------------------

bool containsCi(std::string_view hay, std::string_view needle, size_t from, size_t& at)
{
    if (needle.empty() || needle.size() > hay.size()) return false;
    auto lo = [](unsigned char c) { return (char)std::tolower(c); };
    for (size_t i = from; i + needle.size() <= hay.size(); ++i) {
        bool eq = true;
        for (size_t j = 0; j < needle.size(); ++j)
            if (lo((unsigned char)hay[i + j]) != lo((unsigned char)needle[j])) { eq = false; break; }
        if (eq) { at = i; return true; }
    }
    return false;
}

std::string_view trimmed(std::string_view s)
{
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.remove_prefix(1);
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.remove_suffix(1);
    return s;
}

bool sameTextCi(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

void appendUtf8(std::string& out, unsigned cp)
{
    if (cp < 0x80) out.push_back((char)cp);
    else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

float headingScale(int level)
{
    switch (level) {
    case 1: return 2.0f;
    case 2: return 1.55f;
    case 3: return 1.28f;
    case 4: return 1.12f;
    default: return 1.0f;
    }
}

int sourceLineOf(std::string_view source, size_t offset)
{
    int line = 0;
    const size_t end = std::min(offset, source.size());
    for (size_t i = 0; i < end; ++i)
        if (source[i] == '\n') ++line;
    return line;
}

// ---- inline layout ---------------------------------------------------------

struct Piece {
    const Span* span;
    std::string_view text;    // slice of span->text
    size_t      plainStart;   // offset into the paragraph's plain text
    ImVec2      pos;
    float       width;
    float       fontSize;
    ImFont*     font;
    int         line;
};

struct FontChoice {
    ImFont* font;
    float   size;
    ImU32   color;
};

FontChoice fontFor(const Span& s, const State& st, float baseSize)
{
    const Palette& p = palette();
    FontChoice fc{fonts::ui(), baseSize, st.inHeading ? p.heading : p.text};
    if (st.inHeading) {
        fc.font = fonts::uiBold();
        fc.size = baseSize * headingScale(st.headingLevel);
        if (st.headingLevel >= 6) fc.color = p.textMuted;
    } else if (s.flags & F_Bold) {
        fc.font = fonts::uiBold();
    } else if (s.flags & F_Italic) {
        fc.font = fonts::uiItalic();
    }
    if (s.flags & F_Code) {
        fc.font = fonts::code();
        fc.size = fc.size * 0.92f;
        fc.color = p.inlineCodeText;
    }
    if (s.flags & F_Link) fc.color = p.link;
    if (st.quoteDepth > 0 && !st.inHeading) fc.color = p.textMuted;
    return fc;
}

float measure(ImFont* font, float size, std::string_view text)
{
    if (text.empty()) return 0.0f;
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x;
}

// Lays out `content` into `avail` width starting at `origin`, draws it, and
// returns the total height used. `align` is MD_ALIGN_* for table cells.
float layoutAndDraw(State& st, const InlineContent& content, ImVec2 origin, float avail,
                    int align, float baseSize, std::string& plainOut, std::vector<Piece>& pieces)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Palette& pal = palette();

    plainOut.clear();
    pieces.clear();

    // Line height is driven by the largest font on the paragraph; headings
    // are the only place that is not the base size.
    float lineH = baseSize * 1.5f;
    if (st.inHeading) lineH = baseSize * headingScale(st.headingLevel) * 1.3f;

    float x = 0.0f;
    int   line = 0;
    std::vector<float> lineWidths;
    auto newline = [&]() {
        lineWidths.push_back(x);
        x = 0.0f;
        ++line;
    };

    for (const Span& s : content.spans) {
        if (s.hardBreak) { newline(); continue; }
        if (s.imageIndex >= 0) continue;   // images are drawn by the caller as blocks
        const FontChoice fc = fontFor(s, st, baseSize);
        const float spaceW = measure(fc.font, fc.size, " ");
        size_t i = 0;
        const std::string& t = s.text;
        while (i < t.size()) {
            // A "word" is a run of non-spaces followed by its trailing spaces.
            size_t e = i;
            while (e < t.size() && t[e] != ' ') ++e;
            const size_t wordEnd = e;
            while (e < t.size() && t[e] == ' ') ++e;
            std::string_view word = std::string_view(t).substr(i, wordEnd - i);
            const float wordW = measure(fc.font, fc.size, word);
            const float trailing = spaceW * (float)(e - wordEnd);
            if (x > 0.0f && x + wordW > avail + 0.5f) newline();
            Piece pc;
            pc.span = &s;
            pc.text = std::string_view(t).substr(i, e - i);
            pc.plainStart = plainOut.size();
            pc.pos = ImVec2(x, (float)line * lineH);
            pc.width = wordW + trailing;
            pc.fontSize = fc.size;
            pc.font = fc.font;
            pc.line = line;
            pieces.push_back(pc);
            plainOut.append(pc.text);
            x += wordW + trailing;
            i = e;
        }
    }
    lineWidths.push_back(x);
    const int lineCount = line + 1;
    const float totalH = (float)lineCount * lineH;

    // Alignment offsets per line (table cells only).
    auto lineOffset = [&](int ln) {
        if (align == MD_ALIGN_CENTER) return std::max(0.0f, (avail - lineWidths[(size_t)ln]) * 0.5f);
        if (align == MD_ALIGN_RIGHT) return std::max(0.0f, avail - lineWidths[(size_t)ln]);
        return 0.0f;
    };

    const bool mouseInWindow = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    for (Piece& pc : pieces) {
        const FontChoice fc = fontFor(*pc.span, st, baseSize);
        pc.pos.x += origin.x + lineOffset(pc.line);
        pc.pos.y += origin.y + (lineH - fc.size) * 0.5f;
        std::string_view visible = pc.text;
        while (!visible.empty() && visible.back() == ' ') visible.remove_suffix(1);
        const float visW = measure(fc.font, fc.size, visible);

        if (pc.span->flags & F_Code) {
            dl->AddRectFilled(ImVec2(pc.pos.x - 2.0f, pc.pos.y - 1.0f),
                              ImVec2(pc.pos.x + visW + 2.0f, pc.pos.y + fc.size + 1.0f),
                              pal.inlineCodeBackground, 3.0f);
        }
        ImU32 color = fc.color;
        bool hovered = false;
        if (pc.span->flags & F_Link) {
            const ImRect r(pc.pos, ImVec2(pc.pos.x + visW, pc.pos.y + fc.size));
            hovered = mouseInWindow && r.Contains(mouse);
            if (hovered) {
                color = lift(fc.color, 0.2f, themeAt(currentThemeIndex()).isDark);
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (pc.span->linkIndex >= 0 && pc.span->linkIndex < (int)content.links.size()) {
                    st.out.hoveredLink = content.links[(size_t)pc.span->linkIndex];
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        st.out.clickedLink = st.out.hoveredLink;
                }
            }
            const float uy = pc.pos.y + fc.size - 1.0f;
            dl->AddLine(ImVec2(pc.pos.x, uy), ImVec2(pc.pos.x + visW, uy), withAlpha(color, 140), 1.0f);
        }
        if (!visible.empty())
            dl->AddText(fc.font, fc.size, pc.pos, color, visible.data(), visible.data() + visible.size());
        if (pc.span->flags & F_Strike) {
            const float sy = pc.pos.y + fc.size * 0.55f;
            dl->AddLine(ImVec2(pc.pos.x, sy), ImVec2(pc.pos.x + visW, sy), color, 1.0f);
        }
    }

    // Search highlights: exact ranges mapped back onto the pieces.
    const std::string_view search = st.opt->search;
    if (!search.empty() && !plainOut.empty()) {
        size_t at = 0, from = 0;
        while (containsCi(plainOut, search, from, at)) {
            const bool current = (st.out.matchCount == st.opt->currentMatch);
            const ImU32 col = current ? withAlpha(pal.accent, 150) : withAlpha(pal.warning, 95);
            const size_t mEnd = at + search.size();
            for (const Piece& pc : pieces) {
                const size_t pStart = pc.plainStart, pEnd = pc.plainStart + pc.text.size();
                if (mEnd <= pStart || at >= pEnd) continue;
                const size_t s0 = std::max(at, pStart) - pStart;
                const size_t s1 = std::min(mEnd, pEnd) - pStart;
                const float x0 = pc.pos.x + measure(pc.font, pc.fontSize, pc.text.substr(0, s0));
                const float x1 = pc.pos.x + measure(pc.font, pc.fontSize, pc.text.substr(0, s1));
                dl->AddRectFilled(ImVec2(x0, pc.pos.y - 1.0f), ImVec2(x1, pc.pos.y + pc.fontSize + 1.0f), col, 2.0f);
                if (current && &pc == &pieces[0] + (&pc - &pieces[0]))
                    ImGui::SetScrollFromPosY(pc.pos.y - ImGui::GetWindowPos().y, 0.4f);
            }
            st.out.matchCount++;
            from = mEnd;
        }
    }
    return totalH;
}

// ---- block drawing ---------------------------------------------------------

float baseFontSize() { return ImGui::GetFontSize(); }

void blockGap(State& st, float factor = 1.0f)
{
    if (st.firstBlock) { st.firstBlock = false; return; }
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y * factor));
}

void drawListMarker(State& st, ImVec2 contentPos, float lineH)
{
    if (!st.markerPending || st.lists.empty()) return;
    st.markerPending = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Palette& pal = palette();
    const float fs = baseFontSize();
    const float indent = fs * 1.6f;
    const float cy = contentPos.y + lineH * 0.5f;
    ListCtx& ctx = st.lists.back();

    if (st.itemIsTask) {
        const float box = fs * 0.85f;
        const ImVec2 b0(contentPos.x - indent + (indent - box) * 0.35f, cy - box * 0.5f);
        const ImVec2 b1(b0.x + box, b0.y + box);
        const bool hovered = st.opt->interactiveTasks &&
                             ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                             ImRect(b0, b1).Contains(ImGui::GetIO().MousePos);
        if (st.itemChecked) {
            dl->AddRectFilled(b0, b1, hovered ? pal.accentHovered : pal.accent, 3.0f);
            const float t = box * 0.12f;
            const ImVec2 check[3] = { ImVec2(b0.x + box * 0.22f, cy),
                                      ImVec2(b0.x + box * 0.42f, cy + box * 0.22f),
                                      ImVec2(b0.x + box * 0.78f, cy - box * 0.22f) };
            dl->AddPolyline(check, 3, pal.accentText, 0, t);
        } else {
            dl->AddRectFilled(b0, b1, pal.surface, 3.0f);
            dl->AddRect(b0, b1, hovered ? pal.borderHovered : pal.textFaint, 3.0f, 0, 1.5f);
        }
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) st.out.taskToggleOffset = st.itemTaskOffset;
        }
        return;
    }
    if (ctx.ordered) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d.", ctx.next++);
        const float w = measure(fonts::ui(), fs, buf);
        dl->AddText(fonts::ui(), fs, ImVec2(contentPos.x - w - fs * 0.45f, cy - fs * 0.5f), pal.textMuted, buf);
        return;
    }
    const float cx = contentPos.x - indent * 0.6f;
    const float r = fs * 0.14f;
    switch (ctx.depth % 3) {
    case 0: dl->AddCircleFilled(ImVec2(cx, cy), r, pal.textMuted, 12); break;
    case 1: dl->AddCircle(ImVec2(cx, cy), r, pal.textMuted, 12, 1.5f); break;
    default: dl->AddRectFilled(ImVec2(cx - r, cy - r), ImVec2(cx + r, cy + r), pal.textMuted); break;
    }
}

void drawImageBlock(State& st, const ImageRef& img)
{
    const Palette& pal = palette();
    ImageInfo info;
    const bool have = st.opt->images && *st.opt->images && (*st.opt->images)(img.src, info) &&
                      info.tex && info.w > 0 && info.h > 0;
    const float avail = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (have) {
        float w = (float)info.w, h = (float)info.h;
        if (w > avail && avail > 8.0f) { h *= avail / w; w = avail; }
        // Keep a picture an illustration inside the prose rather than the page.
        const float maxH = ImGui::GetWindowHeight() * 0.6f;
        if (maxH > 32.0f && h > maxH) { w *= maxH / h; h = maxH; }
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::Image(info.tex, ImVec2(w, h));
        dl->AddRect(p0, ImVec2(p0.x + w, p0.y + h), withAlpha(pal.border, 160), 4.0f);
        if (ImGui::IsItemHovered() && !img.alt.empty()) ImGui::SetTooltip("%s", img.alt.c_str());
        st.out.imagesDrawn++;
    } else {
        const float fs = baseFontSize();
        const float h = fs * 4.0f;
        const float w = std::max(avail, 8.0f);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, h));
        dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), pal.codeBackground, 6.0f);
        dl->AddRect(p0, ImVec2(p0.x + w, p0.y + h), pal.border, 6.0f);
        const std::string label = std::string(ICON_MD_IMAGE) + "  " + (img.alt.empty() ? img.src : img.alt);
        const float lw = measure(fonts::ui(), fs, label);
        dl->AddText(fonts::ui(), fs, ImVec2(p0.x + (w - lw) * 0.5f, p0.y + h * 0.5f - fs - 2.0f), pal.text, label.c_str());
        const std::string note = info.note.empty() ? "(image unavailable)" : "(" + info.note + ")";
        const float nw = measure(fonts::ui(), fs, note);
        dl->AddText(fonts::ui(), fs, ImVec2(p0.x + (w - nw) * 0.5f, p0.y + h * 0.5f + 2.0f), pal.textFaint, note.c_str());
        st.out.imagesFallback++;
    }
}

// Flushes the accumulated inline content as one or more blocks.
void flushInline(State& st, int align = MD_ALIGN_DEFAULT)
{
    if (st.cur.empty()) {
        // An empty list item still needs its marker and a line of height.
        if (st.markerPending) {
            const float lineH = baseFontSize() * 1.5f;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            drawListMarker(st, origin, lineH);
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, lineH));
        }
        return;
    }
    const float base = baseFontSize();

    // Split around images: text run, image, text run...
    std::vector<InlineContent> parts;
    InlineContent run;
    run.links = st.cur.links;
    std::vector<int> imageAt;   // -1 for text part, else image index
    for (const Span& s : st.cur.spans) {
        if (s.imageIndex >= 0) {
            if (!run.spans.empty()) { parts.push_back(run); imageAt.push_back(-1); run.spans.clear(); }
            parts.push_back(InlineContent());
            imageAt.push_back(s.imageIndex);
        } else {
            run.spans.push_back(s);
        }
    }
    if (!run.spans.empty()) { parts.push_back(run); imageAt.push_back(-1); }

    std::string plain;
    std::vector<Piece> pieces;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (imageAt[i] >= 0) {
            if (st.markerPending) drawListMarker(st, ImGui::GetCursorScreenPos(), base * 1.5f);
            drawImageBlock(st, st.cur.images[(size_t)imageAt[i]]);
            continue;
        }
        const InlineContent& part = parts[i];
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float avail = std::max(ImGui::GetContentRegionAvail().x, 24.0f);
        const float h = layoutAndDraw(st, part, origin, avail, align, base, plain, pieces);
        const float lineH = st.inHeading ? base * headingScale(st.headingLevel) * 1.3f : base * 1.5f;
        drawListMarker(st, origin, lineH);

        if (st.inHeading) {
            // Headings are anchors: remember where they landed and honour a
            // pending scroll request.
            HeadingHit hit;
            hit.level = st.headingLevel;
            hit.text = std::string(trimmed(plain));
            hit.screenY = origin.y;
            hit.sourceLine = st.headingSourceLine;
            st.out.headings.push_back(hit);
            const bool byText = !st.opt->scrollToHeading.empty() &&
                                sameTextCi(trimmed(plain), trimmed(st.opt->scrollToHeading));
            const bool byLine = st.opt->scrollToLine >= 0 && st.opt->scrollToLine == st.headingSourceLine;
            if (!st.out.headingFound && (byText || byLine)) {
                st.out.headingFound = true;
                ImGui::SetScrollFromPosY(origin.y - ImGui::GetWindowPos().y - ImGui::GetStyle().ItemSpacing.y, 0.0f);
            }
        }
        ImGui::Dummy(ImVec2(avail, h));
        if (st.inHeading && st.headingLevel <= 2) {
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(p0, ImVec2(p0.x + avail, p0.y), palette().rule, 1.0f);
            ImGui::Dummy(ImVec2(avail, 1.0f));
        }
    }
    st.cur.clear();
}

void drawCodeBlock(State& st)
{
    const Palette& pal = palette();
    const float fs = baseFontSize();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.codeBackground);
    ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(pal.border, 180));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.8f, fs * 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    const std::string id = "##code" + std::to_string(st.codeId++);
    const bool open = ImGui::BeginChild(ImGui::GetID(id.c_str()), ImVec2(-FLT_MIN, 0.0f),
                                        ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders,
                                        ImGuiWindowFlags_HorizontalScrollbar);
    if (open) {
        // Language tag and copy button on the first line, right aligned.
        const ImVec2 start = ImGui::GetCursorPos();
        const float right = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
        float tagW = 0.0f;
        if (!st.codeLang.empty()) tagW = measure(fonts::ui(), fs * 0.85f, st.codeLang) + fs * 0.6f;
        const float btnW = fs * 1.6f;
        ImGui::SetCursorPos(ImVec2(right - btnW - tagW, start.y - ImGui::GetStyle().WindowPadding.y * 0.5f));
        if (!st.codeLang.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, pal.textFaint);
            ImGui::PushFont(fonts::ui(), fs * 0.85f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(st.codeLang.c_str());
            ImGui::PopFont();
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, fs * 0.3f);
        }
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.surfaceHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, pal.surfaceActive);
        ImGui::PushStyleColor(ImGuiCol_Text, pal.textMuted);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 1.0f));
        if (ImGui::SmallButton(ICON_MD_CONTENT_COPY "##copy")) st.out.copiedCode = st.codeBuf;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Copy code");
        ImGui::SetCursorPos(start);

        ImGui::PushFont(fonts::code(), fs * 0.92f);
        ImGui::PushStyleColor(ImGuiCol_Text, pal.text);
        // Trailing newline would add an empty last line.
        std::string_view body = st.codeBuf;
        while (!body.empty() && body.back() == '\n') body.remove_suffix(1);
        if (body.empty()) ImGui::TextUnformatted(" ");
        else ImGui::TextUnformatted(body.data(), body.data() + body.size());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    st.out.codeBlocksDrawn++;

    if (!st.opt->search.empty()) {
        size_t at;
        if (containsCi(st.codeBuf, st.opt->search, 0, at)) {
            const bool current = (st.out.matchCount == st.opt->currentMatch);
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                current ? pal.accent : pal.warning, 6.0f, 0, 2.0f);
            if (current) ImGui::SetScrollHereY(0.4f);
            st.out.matchCount++;
        }
    }
    st.codeBuf.clear();
    st.codeLang.clear();
}

// ---- table column measuring (from fwcom) -----------------------------------

constexpr float kCellSlack = 6.0f;

struct MeasureState {
    std::vector<TableMeasure> tables;
    bool inTable = false;
    int  col = 0;
    int  imageDepth = 0;
    int  flags = 0;
    std::string cell;
    float base = 16.0f;
};

void measureCell(MeasureState& ms)
{
    TableMeasure& m = ms.tables.back();
    if (ms.col < 0 || ms.col >= (int)m.widest.size()) return;
    // Bold headers are wider; measure with the bold face to be safe.
    ImFont* f = fonts::uiBold();
    const float w = measure(f, ms.base, ms.cell) + kCellSlack;
    m.widest[(size_t)ms.col] = std::max(m.widest[(size_t)ms.col], w);
    size_t i = 0;
    while (i < ms.cell.size()) {
        while (i < ms.cell.size() && ms.cell[i] == ' ') ++i;
        size_t e = i;
        while (e < ms.cell.size() && ms.cell[e] != ' ') ++e;
        if (e > i) {
            const float ww = measure(f, ms.base, std::string_view(ms.cell).substr(i, e - i)) + kCellSlack;
            m.longestWord[(size_t)ms.col] = std::max(m.longestWord[(size_t)ms.col], ww);
        }
        i = e;
    }
}

int cbMeasureEnterBlock(MD_BLOCKTYPE t, void* d, void* ud)
{
    MeasureState& ms = *(MeasureState*)ud;
    if (t == MD_BLOCK_TABLE) {
        const int n = (int)((MD_BLOCK_TABLE_DETAIL*)d)->col_count;
        TableMeasure m;
        m.widest.assign((size_t)std::max(n, 0), 0.0f);
        m.longestWord.assign((size_t)std::max(n, 0), 0.0f);
        m.hasImage.assign((size_t)std::max(n, 0), false);
        ms.tables.push_back(std::move(m));
        ms.inTable = true;
    } else if (t == MD_BLOCK_TR) {
        ms.col = 0;
    } else if (t == MD_BLOCK_TH || t == MD_BLOCK_TD) {
        ms.cell.clear();
    }
    return 0;
}

int cbMeasureLeaveBlock(MD_BLOCKTYPE t, void*, void* ud)
{
    MeasureState& ms = *(MeasureState*)ud;
    if (t == MD_BLOCK_TABLE) ms.inTable = false;
    else if ((t == MD_BLOCK_TH || t == MD_BLOCK_TD) && ms.inTable) { measureCell(ms); ms.col++; }
    return 0;
}

int cbMeasureEnterSpan(MD_SPANTYPE t, void*, void* ud)
{
    MeasureState& ms = *(MeasureState*)ud;
    if (t == MD_SPAN_IMG) {
        ms.imageDepth++;
        if (ms.inTable && !ms.tables.empty() && ms.col >= 0 &&
            ms.col < (int)ms.tables.back().hasImage.size())
            ms.tables.back().hasImage[(size_t)ms.col] = true;
    }
    return 0;
}

int cbMeasureLeaveSpan(MD_SPANTYPE t, void*, void* ud)
{
    MeasureState& ms = *(MeasureState*)ud;
    if (t == MD_SPAN_IMG && ms.imageDepth > 0) ms.imageDepth--;
    return 0;
}

int cbMeasureText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* ud)
{
    MeasureState& ms = *(MeasureState*)ud;
    if (!ms.inTable || ms.imageDepth > 0) return 0;
    switch (type) {
    case MD_TEXT_HTML: break;
    case MD_TEXT_NULLCHAR: ms.cell.push_back('?'); break;
    case MD_TEXT_BR:
    case MD_TEXT_SOFTBR: ms.cell.push_back(' '); break;
    case MD_TEXT_ENTITY: ms.cell += decodeEntity(std::string_view(text, size)); break;
    default: ms.cell.append(text, size); break;
    }
    return 0;
}

std::vector<TableMeasure> measureTables(std::string_view text, float base)
{
    if (text.find('|') == std::string_view::npos) return {};
    MeasureState ms;
    ms.base = base;
    MD_PARSER parser;
    std::memset(&parser, 0, sizeof(parser));
    parser.flags = kMd4cFlags;
    parser.enter_block = cbMeasureEnterBlock;
    parser.leave_block = cbMeasureLeaveBlock;
    parser.enter_span = cbMeasureEnterSpan;
    parser.leave_span = cbMeasureLeaveSpan;
    parser.text = cbMeasureText;
    if (md_parse(text.data(), (MD_SIZE)text.size(), &parser, &ms) != 0) return {};
    return std::move(ms.tables);
}

bool shareWidth(const std::vector<int>& cols, const std::vector<float>& weight,
                const std::vector<float>& floorW, float total, std::vector<float>& out)
{
    if (cols.empty()) return true;
    float floorSum = 0.0f;
    for (int c : cols) floorSum += floorW[(size_t)c];
    if (floorSum > total) return false;
    std::vector<bool> held(out.size(), false);
    for (;;) {
        float rest = total, wsum = 0.0f;
        int nFree = 0;
        for (int c : cols) {
            if (held[(size_t)c]) rest -= floorW[(size_t)c];
            else { wsum += weight[(size_t)c]; ++nFree; }
        }
        bool changed = false;
        for (int c : cols) {
            if (held[(size_t)c]) { out[(size_t)c] = floorW[(size_t)c]; continue; }
            out[(size_t)c] = wsum > 0.0f ? rest * weight[(size_t)c] / wsum : rest / (float)nFree;
            if (out[(size_t)c] < floorW[(size_t)c]) { held[(size_t)c] = true; changed = true; }
        }
        if (!changed) break;
    }
    float used = 0.0f;
    for (int c : cols) used += out[(size_t)c];
    if (used < total) out[(size_t)cols.back()] += total - used;
    return true;
}

// A column whose widest cell fits in about a third of the table keeps that
// width; the others share the rest, never narrower than their longest word
// while that is possible.
std::vector<float> columnWidths(const TableMeasure& m, float usable)
{
    const int n = (int)m.widest.size();
    std::vector<float> w((size_t)n, 0.0f), weight((size_t)n, 1.0f);
    std::vector<int> all, narrow, wide;
    for (int c = 0; c < n; ++c) {
        all.push_back(c);
        const size_t ci = (size_t)c;
        weight[ci] = m.hasImage[ci] ? std::max(usable, m.widest[ci]) : std::max(m.widest[ci], 1.0f);
        if (!m.hasImage[ci] && m.widest[ci] <= usable / 3.0f) narrow.push_back(c);
        else wide.push_back(c);
    }
    bool done;
    if (wide.empty()) {
        done = shareWidth(all, weight, m.widest, usable, w);
    } else {
        float fixedSum = 0.0f;
        for (int c : narrow) { w[(size_t)c] = m.widest[(size_t)c]; fixedSum += m.widest[(size_t)c]; }
        done = shareWidth(wide, weight, m.longestWord, usable - fixedSum, w);
    }
    if (!done) {
        if (!shareWidth(all, weight, m.longestWord, usable, w)) {
            const std::vector<float> none((size_t)n, 0.0f);
            shareWidth(all, weight, none, usable, w);
        }
    }
    float sum = 0.0f;
    for (float& x : w) { x = std::max(1.0f, std::floor(x)); sum += x; }
    if (n > 0 && usable > sum) w[(size_t)n - 1] += std::floor(usable - sum);
    return w;
}

// ---- md4c callbacks --------------------------------------------------------

int cbEnterBlock(MD_BLOCKTYPE t, void* d, void* ud)
{
    State& st = *(State*)ud;
    switch (t) {
    case MD_BLOCK_DOC: break;
    case MD_BLOCK_H: {
        auto* det = (MD_BLOCK_H_DETAIL*)d;
        blockGap(st, st.firstBlock ? 0.0f : 1.6f);
        st.inHeading = true;
        st.headingLevel = (int)det->level;
        // md4c has no block offsets; headings are rare enough to locate by
        // scanning from the last known heading position.
        st.headingSourceLine = -1;
        break;
    }
    case MD_BLOCK_P:
        if (st.lists.empty() || !st.markerPending) blockGap(st);
        break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL: {
        flushInline(st);
        ListCtx ctx;
        ctx.ordered = (t == MD_BLOCK_OL);
        ctx.next = ctx.ordered ? (int)((MD_BLOCK_OL_DETAIL*)d)->start : 1;
        ctx.depth = (int)st.lists.size();
        if (st.lists.empty()) blockGap(st);
        st.lists.push_back(ctx);
        ImGui::Indent(baseFontSize() * 1.6f);
        break;
    }
    case MD_BLOCK_LI: {
        auto* det = (MD_BLOCK_LI_DETAIL*)d;
        flushInline(st);
        if (!st.lists.empty() && st.lists.back().depth == (int)st.lists.size() - 1) {
            // Tight list items: a small gap, not a paragraph gap.
            ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y * 0.15f));
        }
        st.markerPending = true;
        st.itemIsTask = det->is_task != 0;
        st.itemChecked = det->task_mark == 'x' || det->task_mark == 'X';
        st.itemTaskOffset = det->is_task ? (int)det->task_mark_offset : -1;
        break;
    }
    case MD_BLOCK_HR: {
        flushInline(st);
        blockGap(st, 1.5f);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddLine(p0, ImVec2(p0.x + w, p0.y), palette().rule, 1.5f);
        ImGui::Dummy(ImVec2(w, 2.0f));
        break;
    }
    case MD_BLOCK_CODE: {
        auto* det = (MD_BLOCK_CODE_DETAIL*)d;
        flushInline(st);
        blockGap(st);
        st.inCode = true;
        st.codeBuf.clear();
        st.codeLang.assign(det->lang.text ? det->lang.text : "", det->lang.size);
        break;
    }
    case MD_BLOCK_HTML:
        flushInline(st);
        break;
    case MD_BLOCK_QUOTE:
        flushInline(st);
        if (st.quoteDepth == 0) blockGap(st);
        st.quoteDepth++;
        st.quoteStartY.push_back(ImGui::GetCursorScreenPos().y);
        ImGui::Indent(baseFontSize() * 1.1f);
        break;
    case MD_BLOCK_TABLE: {
        auto* det = (MD_BLOCK_TABLE_DETAIL*)d;
        flushInline(st);
        blockGap(st);
        st.tableCols = (int)det->col_count;
        st.tableCol = 0;
        st.cellAlign.assign((size_t)std::max(st.tableCols, 0), MD_ALIGN_DEFAULT);
        if (st.tableCols <= 0) { st.inTable = false; break; }
        const int tableIndex = st.tableId++;
        const std::string tid = "##table" + std::to_string(tableIndex);
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, palette().surfaceHovered);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(baseFontSize() * 0.6f, baseFontSize() * 0.35f));
        st.inTable = ImGui::BeginTable(tid.c_str(), st.tableCols,
                                       ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX);
        if (st.inTable) {
            const ImGuiTable* tbl = ImGui::GetCurrentTable();
            const int n = st.tableCols;
            const float usable = tbl->WorkRect.GetWidth() - tbl->OuterPaddingX * 2.0f -
                                 (tbl->CellSpacingX1 + tbl->CellSpacingX2) * (float)(n - 1) -
                                 tbl->CellPaddingX * 2.0f * (float)n;
            std::vector<float> widths;
            if (tableIndex < (int)st.tableMeasures.size() &&
                (int)st.tableMeasures[(size_t)tableIndex].widest.size() == n)
                widths = columnWidths(st.tableMeasures[(size_t)tableIndex], usable);
            else
                widths.assign((size_t)n, usable / (float)n);
            for (int i = 0; i < n; ++i)
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
                                        std::max(widths[(size_t)i], 1.0f));
        } else {
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        break;
    }
    case MD_BLOCK_THEAD: st.inTableHead = true; break;
    case MD_BLOCK_TBODY: st.inTableHead = false; break;
    case MD_BLOCK_TR:
        st.tableCol = 0;
        if (st.inTable) ImGui::TableNextRow();
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
        auto* det = (MD_BLOCK_TD_DETAIL*)d;
        if (st.inTable && st.tableCol < st.tableCols) {
            ImGui::TableSetColumnIndex(st.tableCol);
            if (t == MD_BLOCK_TH) st.cellAlign[(size_t)st.tableCol] = (int)det->align;
        }
        st.cur.clear();
        if (t == MD_BLOCK_TH) st.spanFlags |= F_Bold;
        break;
    }
    default: break;
    }
    return 0;
}

int cbLeaveBlock(MD_BLOCKTYPE t, void*, void* ud)
{
    State& st = *(State*)ud;
    switch (t) {
    case MD_BLOCK_H:
        flushInline(st);
        st.inHeading = false;
        st.headingLevel = 0;
        break;
    case MD_BLOCK_P:
        flushInline(st);
        break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        flushInline(st);
        ImGui::Unindent(baseFontSize() * 1.6f);
        if (!st.lists.empty()) st.lists.pop_back();
        break;
    case MD_BLOCK_LI:
        flushInline(st);
        st.markerPending = false;
        st.itemIsTask = false;
        break;
    case MD_BLOCK_CODE:
        drawCodeBlock(st);
        st.inCode = false;
        break;
    case MD_BLOCK_QUOTE: {
        flushInline(st);
        ImGui::Unindent(baseFontSize() * 1.1f);
        const float y0 = st.quoteStartY.empty() ? 0.0f : st.quoteStartY.back();
        if (!st.quoteStartY.empty()) st.quoteStartY.pop_back();
        const float y1 = ImGui::GetCursorScreenPos().y;
        const float x = ImGui::GetCursorScreenPos().x + 1.0f;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, y0), ImVec2(x + 3.0f, y1),
                                                  st.quoteDepth > 1 ? palette().textFaint : palette().accent, 1.5f);
        st.quoteDepth = std::max(0, st.quoteDepth - 1);
        break;
    }
    case MD_BLOCK_TABLE:
        if (st.inTable) {
            const ImGuiTable* tbl = ImGui::GetCurrentTable();
            st.out.lastTableColumnWidths.clear();
            for (int i = 0; tbl && i < tbl->ColumnsCount; ++i)
                st.out.lastTableColumnWidths.push_back(tbl->Columns[i].WidthGiven);
            ImGui::EndTable();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            st.out.tablesDrawn++;
        }
        st.inTable = false;
        st.tableCols = 0;
        break;
    case MD_BLOCK_THEAD: st.inTableHead = false; break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        if (st.inTable && st.tableCol < st.tableCols) {
            const int align = st.cellAlign[(size_t)st.tableCol];
            if (st.cur.empty()) ImGui::Dummy(ImVec2(1.0f, baseFontSize()));
            else flushInline(st, align);
        }
        st.cur.clear();
        if (t == MD_BLOCK_TH) st.spanFlags &= ~F_Bold;
        if (st.inTable) st.tableCol++;
        break;
    default: break;
    }
    return 0;
}

int cbEnterSpan(MD_SPANTYPE t, void* d, void* ud)
{
    State& st = *(State*)ud;
    switch (t) {
    case MD_SPAN_EM:     st.spanFlags |= F_Italic; break;
    case MD_SPAN_STRONG: st.spanFlags |= F_Bold; break;
    case MD_SPAN_DEL:    st.spanFlags |= F_Strike; break;
    case MD_SPAN_CODE:   st.spanFlags |= F_Code; break;
    case MD_SPAN_A: {
        auto* det = (MD_SPAN_A_DETAIL*)d;
        st.cur.links.emplace_back(det->href.text ? det->href.text : "", det->href.size);
        st.currentLink = (int)st.cur.links.size() - 1;
        st.spanFlags |= F_Link;
        break;
    }
    case MD_SPAN_IMG: {
        auto* det = (MD_SPAN_IMG_DETAIL*)d;
        ImageRef ref;
        ref.src.assign(det->src.text ? det->src.text : "", det->src.size);
        st.cur.images.push_back(std::move(ref));
        st.currentImage = (int)st.cur.images.size() - 1;
        Span s;
        s.imageIndex = st.currentImage;
        st.cur.spans.push_back(std::move(s));
        break;
    }
    default: break;
    }
    return 0;
}

int cbLeaveSpan(MD_SPANTYPE t, void*, void* ud)
{
    State& st = *(State*)ud;
    switch (t) {
    case MD_SPAN_EM:     st.spanFlags &= ~F_Italic; break;
    case MD_SPAN_STRONG: st.spanFlags &= ~F_Bold; break;
    case MD_SPAN_DEL:    st.spanFlags &= ~F_Strike; break;
    case MD_SPAN_CODE:   st.spanFlags &= ~F_Code; break;
    case MD_SPAN_A:      st.spanFlags &= ~F_Link; st.currentLink = -1; break;
    case MD_SPAN_IMG:    st.currentImage = -1; break;
    default: break;
    }
    return 0;
}

void appendText(State& st, std::string_view text)
{
    if (text.empty()) return;
    // Merge into the previous span when the style is unchanged, so words are
    // not split at arbitrary md4c chunk boundaries.
    if (!st.cur.spans.empty()) {
        Span& last = st.cur.spans.back();
        if (!last.hardBreak && last.imageIndex < 0 && last.flags == st.spanFlags &&
            last.linkIndex == st.currentLink) {
            last.text.append(text);
            return;
        }
    }
    Span s;
    s.text = std::string(text);
    s.flags = st.spanFlags;
    s.linkIndex = st.currentLink;
    st.cur.spans.push_back(std::move(s));
}

int cbText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* ud)
{
    State& st = *(State*)ud;
    const std::string_view sv(text, size);
    if (st.inCode) {
        st.codeBuf.append(text, size);
        return 0;
    }
    if (st.currentImage >= 0) {
        st.cur.images[(size_t)st.currentImage].alt.append(text, size);
        return 0;
    }
    if (st.inHeading && st.headingSourceLine < 0) {
        // Locate the heading in the source by its first text chunk; good
        // enough for outline navigation and cheap for the handful of
        // headings a document has.
        const size_t at = st.source.find(sv);
        st.headingSourceLine = at == std::string_view::npos ? 0 : sourceLineOf(st.source, at);
    }
    switch (type) {
    case MD_TEXT_NORMAL:
    case MD_TEXT_CODE:
        appendText(st, sv);
        break;
    case MD_TEXT_ENTITY:
        appendText(st, decodeEntity(sv));
        break;
    case MD_TEXT_HTML: {
        // Inline <br> is common in tables; everything else is hidden the way
        // a browser hides tags.
        std::string lower(sv);
        for (char& c : lower) c = (char)std::tolower((unsigned char)c);
        lower.erase(std::remove(lower.begin(), lower.end(), ' '), lower.end());
        if (lower == "<br>" || lower == "<br/>") {
            Span s; s.hardBreak = true; st.cur.spans.push_back(s);
        }
        break;
    }
    case MD_TEXT_NULLCHAR:
        appendText(st, "?");
        break;
    case MD_TEXT_BR: {
        Span s; s.hardBreak = true; st.cur.spans.push_back(s);
        break;
    }
    case MD_TEXT_SOFTBR:
        appendText(st, " ");
        break;
    default:
        appendText(st, sv);
        break;
    }
    return 0;
}

} // namespace

std::string decodeEntity(std::string_view e)
{
    if (e.size() < 3 || e.front() != '&' || e.back() != ';') return std::string(e);
    std::string_view name = e.substr(1, e.size() - 2);
    if (name.front() == '#') {
        unsigned cp = 0;
        if (name.size() > 1 && (name[1] == 'x' || name[1] == 'X'))
            cp = (unsigned)std::strtoul(std::string(name.substr(2)).c_str(), nullptr, 16);
        else
            cp = (unsigned)std::strtoul(std::string(name.substr(1)).c_str(), nullptr, 10);
        if (cp == 0 || cp > 0x10FFFF) return "?";
        std::string out;
        appendUtf8(out, cp);
        return out;
    }
    struct Named { const char* name; const char* utf8; };
    static const Named kNamed[] = {
        {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"},
        {"nbsp", "\xC2\xA0"}, {"copy", "\xC2\xA9"}, {"reg", "\xC2\xAE"}, {"trade", "\xE2\x84\xA2"},
        {"mdash", "\xE2\x80\x94"}, {"ndash", "\xE2\x80\x93"}, {"hellip", "\xE2\x80\xA6"},
        {"laquo", "\xC2\xAB"}, {"raquo", "\xC2\xBB"}, {"ldquo", "\xE2\x80\x9C"}, {"rdquo", "\xE2\x80\x9D"},
        {"lsquo", "\xE2\x80\x98"}, {"rsquo", "\xE2\x80\x99"}, {"bull", "\xE2\x80\xA2"},
        {"middot", "\xC2\xB7"}, {"deg", "\xC2\xB0"}, {"times", "\xC3\x97"}, {"divide", "\xC3\xB7"},
        {"larr", "\xE2\x86\x90"}, {"rarr", "\xE2\x86\x92"}, {"uarr", "\xE2\x86\x91"}, {"darr", "\xE2\x86\x93"},
        {"le", "\xE2\x89\xA4"}, {"ge", "\xE2\x89\xA5"}, {"ne", "\xE2\x89\xA0"}, {"plusmn", "\xC2\xB1"},
        {"eacute", "\xC3\xA9"}, {"egrave", "\xC3\xA8"}, {"uuml", "\xC3\xBC"}, {"ouml", "\xC3\xB6"},
        {"auml", "\xC3\xA4"}, {"szlig", "\xC3\x9F"}, {"ccedil", "\xC3\xA7"}, {"ntilde", "\xC3\xB1"},
    };
    for (const Named& n : kNamed)
        if (name == n.name) return n.utf8;
    return std::string(e);
}

Result render(std::string_view text, const Options& options)
{
    State st;
    st.opt = &options;
    st.source = text;
    st.tableMeasures = measureTables(text, baseFontSize());

    MD_PARSER parser;
    std::memset(&parser, 0, sizeof(parser));
    parser.flags = kMd4cFlags;
    parser.enter_block = cbEnterBlock;
    parser.leave_block = cbLeaveBlock;
    parser.enter_span = cbEnterSpan;
    parser.leave_span = cbLeaveSpan;
    parser.text = cbText;

    if (md_parse(text.data(), (MD_SIZE)text.size(), &parser, &st) != 0) {
        // Fallback: raw text so the reader still sees something.
        ImGui::TextWrapped("%.*s", (int)text.size(), text.data());
    }
    flushInline(st);
    // Unwind anything a malformed document left pushed.
    while (!st.lists.empty()) { ImGui::Unindent(baseFontSize() * 1.6f); st.lists.pop_back(); }
    while (st.quoteDepth > 0) { ImGui::Unindent(baseFontSize() * 1.1f); st.quoteDepth--; }
    return st.out;
}

} // namespace dd::ui::md
