#include "ui/WysiwygEditor.h"

#include "app/Workspace.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "IconsMaterialDesign.h"

#include "imgui_internal.h"

#include <algorithm>
#include <string_view>

namespace dd::ui {

namespace {

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

// Short label shown in the gutter of the block being edited.
std::string blockLabel(const Block& b)
{
    switch (b.type) {
    case BlockType::Heading:  return "H" + std::to_string(b.level);
    case BlockType::Bullet:   return "\xE2\x80\xA2";                 // •
    case BlockType::Numbered: return std::to_string(b.number) + ".";
    case BlockType::Task:     return b.checked ? "[x]" : "[ ]";
    case BlockType::Quote:    return ">";
    case BlockType::Code:     return b.lang.empty() ? "code" : b.lang;
    case BlockType::Table:    return "table";
    case BlockType::Html:     return "html";
    case BlockType::Rule:     return "---";
    default:                  return "";
    }
}

// Commits the model back to the document text.
void commit(Workspace& ws, OpenDocument& od, WysiwygResult& r)
{
    od.model.renumberLists();
    ws.setText(od, od.model.serialize(), true);
    r.changed = true;
}

void loadBlock(OpenDocument& od, WysiwygState& st)
{
    if (od.editingBlock < 0 || od.editingBlock >= (int)od.model.blocks.size()) {
        st.loadedBlock = -1;
        return;
    }
    st.buffer = od.model.blocks[(size_t)od.editingBlock].text;
    st.loadedBlock = od.editingBlock;
    st.area = TextAreaState();
    st.area.requestFocus = true;
    const int caret = od.editingCaret;
    st.area.requestCursor = caret < 0 ? -2 : std::min(caret, (int)st.buffer.size());
    od.editingCaret = -1;
}

void beginEditing(OpenDocument& od, int block, int caret)
{
    od.editingBlock = block;
    od.editingCaret = caret;
}

// Inline-only colouring for the editing field (the block prefix is not in
// the buffer, so block-level rules would misfire).
void inlineHighlight(std::string_view line, std::vector<TextStyleRun>& runs)
{
    std::string padded = "  ";   // two leading spaces disable block-level matches
    padded.append(line);
    std::vector<TextStyleRun> tmp;
    markdownHighlight(padded, tmp);
    for (TextStyleRun& r : tmp) {
        r.start -= 2;
        r.end -= 2;
        if (r.end > 0) { r.start = std::max(0, r.start); runs.push_back(r); }
    }
}

} // namespace

WysiwygResult drawWysiwygEditor(const char* id, Workspace& ws, OpenDocument& od, WysiwygState& st,
                                const md::ImageProvider& images)
{
    WysiwygResult r;
    ws.ensureModel(od);
    MarkdownModel& model = od.model;
    const Palette& pal = palette();
    const float fs = ImGui::GetFontSize();
    const double now = ImGui::GetTime();

    if (od.editingBlock != st.loadedBlock) loadBlock(od, st);

    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    const bool open = ImGui::BeginChild("##wysiwyg", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    ImGui::PopStyleColor();
    if (!open) {
        ImGui::EndChild();
        ImGui::PopID();
        return r;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float gutter = fs * 2.6f;
    const bool windowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int newHovered = -1;

    ImGui::Indent(gutter);
    const float contentW = ImGui::GetContentRegionAvail().x - fs;

    if (model.blocks.empty()) {
        // Empty document: one click starts a paragraph.
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        dl->AddText(fonts::uiItalic(), fs, p0, pal.textFaint, "Click here to start writing...");
        ImGui::InvisibleButton("##start", ImVec2(std::max(contentW, 32.0f), fs * 1.5f));
        if (ImGui::IsItemClicked()) {
            Block b;
            model.blocks.push_back(b);
            beginEditing(od, 0, 0);
            commit(ws, od, r);
        }
    }

    // The block being edited is fixed for the frame: a split or merge inside
    // the loop changes od.editingBlock, and the new block must not be visited
    // as "editing" in the same frame (it would see the same Enter again).
    const int editingThisFrame = od.editingBlock;
    for (size_t i = 0; i < model.blocks.size(); ++i) {
        Block& b = model.blocks[(size_t)i];
        const bool editing = ((int)i == editingThisFrame);
        ImGui::PushID((int)i);
        const bool tightAfterList = i > 0 && model.blocks[i - 1].isListItem() && b.isListItem();
        if (i > 0) ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y * (tightAfterList ? 0.2f : 1.2f)));

        const ImVec2 blockTop = ImGui::GetCursorScreenPos();
        const float indentX = b.isListItem() ? (float)b.level * fs * 1.6f : 0.0f;
        if (indentX > 0.0f) ImGui::Indent(indentX);

        if (editing) {
            // Gutter label.
            const std::string label = blockLabel(b);
            if (!label.empty()) {
                const float lw = fonts::code()->CalcTextSizeA(fs * 0.8f, FLT_MAX, 0.0f, label.c_str()).x;
                dl->AddText(fonts::code(), fs * 0.8f,
                            ImVec2(blockTop.x - lw - fs * 0.6f, blockTop.y + fs * 0.35f), pal.accent, label.c_str());
            }
            // Field styling follows the block kind so the edit reads like the result.
            TextAreaOptions opt;
            opt.blockMode = true;
            opt.autoHeight = true;
            opt.highlight = inlineHighlight;
            opt.paddingX = fs * 0.4f;
            opt.placeholder = b.type == BlockType::Heading ? "Heading" : "Type here, or '/' for nothing: use # - > ``` prefixes";
            switch (b.type) {
            case BlockType::Heading:
                opt.font = fonts::uiBold();
                opt.fontSize = fs * headingScale(b.level);
                opt.lineHeightFactor = 1.3f;
                opt.textColor = pal.heading;
                opt.highlight = nullptr;
                break;
            case BlockType::Code:
            case BlockType::Table:
            case BlockType::Html:
                opt.font = fonts::code();
                opt.fontSize = fs * 0.92f;
                opt.highlight = nullptr;
                break;
            case BlockType::Quote:
                opt.textColor = pal.textMuted;
                break;
            default: break;
            }
            // Background card for the active block.
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const float w = std::max(ImGui::GetContentRegionAvail().x - fs * 0.5f, 32.0f);
            const TextAreaEvents ev = textArea("##edit", st.buffer, st.area, opt, ImVec2(w, 0));
            const ImVec2 p1 = ImGui::GetItemRectMax();
            dl->AddRect(ImVec2(p0.x - fs * 0.25f, p0.y - 2.0f), ImVec2(p0.x + w + fs * 0.25f, p1.y + 2.0f),
                        withAlpha(pal.accent, 110), 5.0f, 0, 1.0f);
            if (b.type == BlockType::Code)
                dl->AddRectFilled(ImVec2(p0.x - fs * 0.25f, p0.y - 2.0f), ImVec2(p0.x + w + fs * 0.25f, p1.y + 2.0f),
                                  withAlpha(pal.codeBackground, 120), 5.0f);
            r.cursorLine = ev.cursorLine;
            r.cursorColumn = ev.cursorColumn;

            if (ev.changed) {
                b.text = st.buffer;
                const int removed = (int)st.buffer.size();
                if (model.applyTypedPrefix(i)) {
                    // The prefix left the buffer; keep the caret on the same character.
                    st.buffer = b.text;
                    st.area.requestCursor = std::max(0, st.area.cursor - (removed - (int)b.text.size()));
                }
                commit(ws, od, r);
            }
            if (ev.enterPressed) {
                if (b.isListItem() && b.text.empty()) {
                    // Enter on an empty item ends the list.
                    model.setType(i, BlockType::Paragraph);
                    st.buffer = b.text;
                } else if (b.type == BlockType::Code) {
                    st.buffer.insert((size_t)st.area.cursor, 1, '\n');
                    b.text = st.buffer;
                    st.area.requestCursor = st.area.cursor + 1;
                } else {
                    const size_t next = model.splitBlock(i, (size_t)st.area.cursor);
                    beginEditing(od, (int)next, 0);
                }
                commit(ws, od, r);
            }
            if (ev.backspaceAtStart) {
                if (b.type != BlockType::Paragraph && b.type != BlockType::Code) {
                    // First Backspace strips the formatting, like Notion.
                    model.setType(i, BlockType::Paragraph);
                    st.buffer = b.text;
                    st.area.requestCursor = 0;
                    commit(ws, od, r);
                } else if (i > 0) {
                    const size_t join = model.mergeWithPrevious(i);
                    const int target = (int)i - 1;
                    beginEditing(od, std::max(0, target), join == std::string::npos ? -2 : (int)join);
                    commit(ws, od, r);
                }
            }
            if (ev.deleteAtEnd && i + 1 < model.blocks.size()) {
                const size_t join = model.mergeWithPrevious(i + 1);
                st.buffer = b.text;
                st.area.requestCursor = (int)join;
                commit(ws, od, r);
            }
            if (ev.upAtFirstLine && i > 0) beginEditing(od, (int)i - 1, -2);
            if (ev.downAtLastLine) {
                if (i + 1 < model.blocks.size()) beginEditing(od, (int)i + 1, 0);
                else {
                    // Below the last block: start a fresh paragraph.
                    Block nb;
                    model.insertBlockAfter(i, nb);
                    beginEditing(od, (int)i + 1, 0);
                    commit(ws, od, r);
                }
            }
            if (ev.tabPressed && b.isListItem()) {
                b.level = std::clamp(b.level + (ev.shiftHeld ? -1 : 1), 0, 6);
                commit(ws, od, r);
            }
            if (ev.escapePressed) od.editingBlock = -1;
            // A click elsewhere *inside the editor* (another block, empty
            // space) leaves the block. Clicks on the toolbar or menus do not,
            // so formatting buttons can act on the block being edited.
            if (!st.area.focused && od.editingBlock == (int)i && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                windowHovered)
                od.editingBlock = -1;
        } else {
            // Rendered block. Numbered items keep their number because the
            // serializer writes it and md4c honours a list's start number.
            const std::string chunk = [&]() {
                MarkdownModel one;
                one.blocks.push_back(b);
                return one.serialize();
            }();
            ImGui::BeginGroup();
            md::Options o;
            o.images = &images;
            o.interactiveTasks = true;
            if (b.type == BlockType::Rule) {
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                const float w = ImGui::GetContentRegionAvail().x - fs * 0.5f;
                dl->AddLine(ImVec2(p0.x, p0.y + fs * 0.6f), ImVec2(p0.x + w, p0.y + fs * 0.6f), pal.rule, 1.5f);
                ImGui::Dummy(ImVec2(w, fs * 1.2f));
            } else {
                const md::Result res = md::render(chunk, o);
                if (!res.clickedLink.empty()) r.clickedLink = res.clickedLink;
                if (!res.hoveredLink.empty()) r.hoveredLink = res.hoveredLink;
                if (!res.copiedCode.empty()) r.copiedCode = res.copiedCode;
                if (res.taskToggleOffset >= 0 && b.type == BlockType::Task) {
                    b.checked = !b.checked;
                    commit(ws, od, r);
                }
            }
            ImGui::EndGroup();
            const ImVec2 g0 = ImGui::GetItemRectMin();
            const ImVec2 g1(ImGui::GetItemRectMin().x + std::max(contentW - indentX, 32.0f), ImGui::GetItemRectMax().y);
            const bool hov = windowHovered && ImRect(g0, g1).Contains(mouse) && r.clickedLink.empty() && r.hoveredLink.empty();
            if (hov) {
                newHovered = (int)i;
                dl->AddRectFilled(ImVec2(g0.x - fs * 0.3f, g0.y - 2.0f), ImVec2(g1.x + fs * 0.3f, g1.y + 2.0f),
                                  withAlpha(pal.surfaceHovered, 70), 5.0f);
                // Pencil hint in the gutter.
                dl->AddText(fonts::ui(), fs * 0.9f, ImVec2(blockTop.x - fs * 1.6f, blockTop.y + fs * 0.2f),
                            withAlpha(pal.textFaint, 180), ICON_MD_EDIT);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    // Caret: before/after the midpoint of the block, a cheap
                    // but predictable approximation.
                    const bool upperHalf = mouse.y < (g0.y + g1.y) * 0.5f && mouse.x < (g0.x + g1.x) * 0.5f;
                    beginEditing(od, (int)i, upperHalf ? 0 : -2);
                }
            }
        }
        if (indentX > 0.0f) ImGui::Unindent(indentX);
        ImGui::PopID();
    }

    // Trailing click area: a click below the last block appends a paragraph.
    if (!model.blocks.empty()) {
        ImGui::Dummy(ImVec2(0.0f, fs * 0.5f));
        const float tailH = std::max(fs * 3.0f, ImGui::GetContentRegionAvail().y);
        ImGui::InvisibleButton("##tail", ImVec2(std::max(contentW, 32.0f), tailH));
        if (ImGui::IsItemClicked()) {
            const Block& last = model.blocks.back();
            if (last.type == BlockType::Paragraph && last.text.empty()) {
                beginEditing(od, (int)model.blocks.size() - 1, 0);
            } else {
                Block nb;
                model.insertBlockAfter(model.blocks.size() - 1, nb);
                beginEditing(od, (int)model.blocks.size() - 1, 0);
                commit(ws, od, r);
            }
        }
    }
    ImGui::Unindent(gutter);
    st.hoveredBlock = newHovered;
    (void)now;

    ImGui::EndChild();
    ImGui::PopID();
    return r;
}

bool applyBlockCommand(Workspace& ws, OpenDocument& od, WysiwygState& st, BlockCommand cmd)
{
    ws.ensureModel(od);
    MarkdownModel& model = od.model;
    if (od.editingBlock < 0 || od.editingBlock >= (int)model.blocks.size()) {
        // Structural commands with nothing selected append a block.
        Block nb;
        bool structural = true;
        switch (cmd) {
        case BlockCommand::Heading1: nb.type = BlockType::Heading; nb.level = 1; break;
        case BlockCommand::Heading2: nb.type = BlockType::Heading; nb.level = 2; break;
        case BlockCommand::Heading3: nb.type = BlockType::Heading; nb.level = 3; break;
        case BlockCommand::Bullet:   nb.type = BlockType::Bullet; break;
        case BlockCommand::Numbered: nb.type = BlockType::Numbered; nb.marker = "."; break;
        case BlockCommand::Task:     nb.type = BlockType::Task; break;
        case BlockCommand::Quote:    nb.type = BlockType::Quote; break;
        case BlockCommand::CodeBlock: nb.type = BlockType::Code; nb.marker = "```"; break;
        case BlockCommand::Rule:     nb.type = BlockType::Rule; nb.text = "---"; break;
        case BlockCommand::Paragraph: break;
        case BlockCommand::Image:    nb.text = "![alt text](image.png)"; break;
        default: structural = false; break;
        }
        if (!structural) return false;
        model.blocks.push_back(nb);
        od.editingBlock = (int)model.blocks.size() - 1;
        od.editingCaret = -2;
        if (nb.type == BlockType::Rule) od.editingBlock = -1;
        ws.setText(od, model.serialize(), true);
        return true;
    }

    const size_t i = (size_t)od.editingBlock;
    Block& b = model.blocks[i];
    int a = st.area.selectionStart(), e = st.area.selectionEnd();
    auto inlineToggle = [&](std::string_view marker) {
        st.buffer = toggleInlineMarker(st.buffer, a, e, marker);
        st.area.anchor = a;
        st.area.cursor = e;
        st.area.requestFocus = true;
        b.text = st.buffer;
    };
    switch (cmd) {
    case BlockCommand::Bold:   inlineToggle("**"); break;
    case BlockCommand::Italic: inlineToggle("*"); break;
    case BlockCommand::Strike: inlineToggle("~~"); break;
    case BlockCommand::Code:   inlineToggle("`"); break;
    case BlockCommand::Link:
        st.buffer = insertLink(st.buffer, a, e, "");
        st.area.anchor = a; st.area.cursor = e; st.area.requestFocus = true;
        b.text = st.buffer;
        break;
    case BlockCommand::Image:
        st.buffer.insert((size_t)st.area.cursor, "![alt text](image.png)");
        st.area.anchor = st.area.cursor + 2; st.area.cursor = st.area.anchor + 8; st.area.requestFocus = true;
        b.text = st.buffer;
        break;
    case BlockCommand::Heading1: model.setType(i, b.type == BlockType::Heading && b.level == 1 ? BlockType::Paragraph : BlockType::Heading, 1); break;
    case BlockCommand::Heading2: model.setType(i, b.type == BlockType::Heading && b.level == 2 ? BlockType::Paragraph : BlockType::Heading, 2); break;
    case BlockCommand::Heading3: model.setType(i, b.type == BlockType::Heading && b.level == 3 ? BlockType::Paragraph : BlockType::Heading, 3); break;
    case BlockCommand::Paragraph: model.setType(i, BlockType::Paragraph); break;
    case BlockCommand::Bullet:   model.setType(i, b.type == BlockType::Bullet ? BlockType::Paragraph : BlockType::Bullet, b.isListItem() ? b.level : 0); break;
    case BlockCommand::Numbered: model.setType(i, b.type == BlockType::Numbered ? BlockType::Paragraph : BlockType::Numbered, b.isListItem() ? b.level : 0); break;
    case BlockCommand::Task:     model.setType(i, b.type == BlockType::Task ? BlockType::Paragraph : BlockType::Task, b.isListItem() ? b.level : 0); break;
    case BlockCommand::Quote:    model.setType(i, b.type == BlockType::Quote ? BlockType::Paragraph : BlockType::Quote); break;
    case BlockCommand::CodeBlock: model.setType(i, b.type == BlockType::Code ? BlockType::Paragraph : BlockType::Code); break;
    case BlockCommand::Rule: {
        Block rule;
        rule.type = BlockType::Rule;
        rule.text = "---";
        model.insertBlockAfter(i, rule);
        Block para;
        model.insertBlockAfter(i + 1, para);
        od.editingBlock = (int)i + 2;
        od.editingCaret = 0;
        break;
    }
    }
    st.area.requestFocus = true;
    ws.setText(od, model.serialize(), true);
    return true;
}

} // namespace dd::ui
