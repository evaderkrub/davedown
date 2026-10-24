#include "ui/MainWindow.h"

#include "app/Outline.h"
#include "platform/FileAssociation.h"
#include "platform/FileDialog.h"
#include "platform/Paths.h"
#include "platform/Shell.h"
#include "platform/Textures.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "IconsMaterialDesign.h"

#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace dd::ui {

namespace {

constexpr const char* kHostWindow = "###davedown_host";
constexpr float kStatusTimeout = 4.0f;

bool endsWithCi(const std::string& s, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower((unsigned char)s[s.size() - n + i]) != std::tolower((unsigned char)suffix[i])) return false;
    return true;
}

std::string docWindowName(const OpenDocument& od)
{
    // "###" keeps the ImGui ID stable while the visible title changes
    // (rename on Save As, dirty marker).
    return od.doc.title() + "###doc" + std::to_string(od.id);
}

} // namespace

std::string headingSlug(std::string_view heading)
{
    std::string out;
    for (unsigned char c : heading) {
        if (std::isalnum(c) || c >= 0x80) out.push_back((char)std::tolower(c));
        else if (c == ' ' || c == '-' || c == '_') out.push_back('-');
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

MainWindow::MainWindow(Workspace& ws) : ws_(ws) {}

MainWindow::DocUi& MainWindow::uiFor(const OpenDocument& od)
{
    auto it = docUi_.find(od.id);
    if (it == docUi_.end()) it = docUi_.emplace(od.id, std::make_unique<DocUi>()).first;
    return *it->second;
}

void MainWindow::initialize(const std::string& welcomeText)
{
    std::string text, err;
    if (platform::readTextFile(platform::settingsPath(), text, err))
        ws_.settings = parseSettings(text);
    requestTheme(themeIndexByName(ws_.settings.themeName.c_str()));
    if (ws_.docs.empty()) {
        OpenDocument& od = ws_.newDocument(welcomeText);
        od.mode = ws_.settings.defaultMode;
    }
}

void MainWindow::pumpAppearance(float dpiScale)
{
    dd::ui::pumpAppearance(ws_.settings.uiScale, dpiScale);
}

void MainWindow::saveSettings()
{
    std::string err;
    platform::writeTextFile(platform::settingsPath(), serializeSettings(ws_.settings), err);
}

void MainWindow::status(std::string text)
{
    ws_.setStatus(std::move(text), ImGui::GetTime());
}

void MainWindow::showError(std::string text)
{
    errorText_ = std::move(text);
    errorOpen_ = true;
}

bool MainWindow::canUndo(const OpenDocument& od) const
{
    auto it = docUi_.find(od.id);
    return it != docUi_.end() && it->second->undo.canUndo();
}

void MainWindow::setMode(OpenDocument& od, ViewMode mode)
{
    if (od.mode == mode) return;
    od.mode = mode;
    od.editingBlock = -1;
    if (mode == ViewMode::Source) uiFor(od).focusEditor = true;
}

// ---- actions ----------------------------------------------------------------

void MainWindow::actionNew()
{
    ws_.newDocument();
    status("New document");
}

void MainWindow::actionOpen(const UiFrame& frame)
{
    std::string dir;
    if (OpenDocument* od = ws_.active()) dir = od->doc.directory();
    if (dir.empty() && !ws_.settings.recentFiles.empty()) dir = directoryOf(ws_.settings.recentFiles.front());
    platform::showOpenDialog(static_cast<SDL_Window*>(frame.nativeWindow), 0, dir);
}

void MainWindow::actionSave(OpenDocument& od, const UiFrame& frame, bool closeAfter)
{
    if (od.doc.isUntitled()) {
        actionSaveAs(od, frame, closeAfter);
        return;
    }
    std::string err;
    if (!ws_.save(od, err)) { showError(err); return; }
    status("Saved " + od.doc.title());
    saveSettings();
    if (closeAfter) ws_.close(od.id);
}

void MainWindow::actionSaveAs(OpenDocument& od, const UiFrame& frame, bool closeAfter)
{
    closeAfterSaveDocId_ = closeAfter ? od.id : -1;
    std::string def = od.doc.isUntitled() ? std::string() : od.doc.path;
    if (def.empty()) {
        std::string dir = ws_.settings.recentFiles.empty() ? std::string() : directoryOf(ws_.settings.recentFiles.front());
        if (!dir.empty()) def = dir + "/Untitled.md";
    }
    platform::showSaveDialog(static_cast<SDL_Window*>(frame.nativeWindow), od.id, def);
}

void MainWindow::actionClose(OpenDocument& od)
{
    if (od.doc.isDirty()) {
        closePromptDocId_ = od.id;
        closePromptOpen_ = true;
        return;
    }
    docUi_.erase(od.id);
    ws_.close(od.id);
}

void MainWindow::actionUndo(OpenDocument& od)
{
    DocUi& ui = uiFor(od);
    std::string text = od.doc.text;
    if (!ui.undo.undo(text)) return;
    ws_.setText(od, text, false);
    status("Undo");
}

void MainWindow::actionRedo(OpenDocument& od)
{
    DocUi& ui = uiFor(od);
    std::string text = od.doc.text;
    if (!ui.undo.redo(text)) return;
    ws_.setText(od, text, false);
    status("Redo");
}

void MainWindow::setZoom(float scale)
{
    scale = clampUiScale(std::round(scale * 10.0f) / 10.0f);
    if (scale == ws_.settings.uiScale) return;
    ws_.settings.uiScale = scale;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "Zoom %d%%", (int)std::lround(scale * 100.0f));
    status(buf);
    saveSettings();
}

void MainWindow::requestQuit()
{
    if (!ws_.anyDirty()) { quitConfirmed_ = true; return; }
    quitPending_ = true;
    quitPromptOpen_ = true;
}

void MainWindow::openFileFromOutside(const std::string& path)
{
    std::string err;
    if (!ws_.openFile(path, err)) showError(err);
    else status("Opened " + fileNameOf(path));
    saveSettings();
}

void MainWindow::handleDialogResults()
{
    platform::DialogResult r;
    while (platform::pollDialogResult(r)) {
        if (!r.accepted) {
            if (!r.error.empty()) showError("File dialog failed: " + r.error);
            closeAfterSaveDocId_ = -1;
            continue;
        }
        if (r.kind == platform::DialogKind::Open) {
            openFileFromOutside(r.path);
        } else {
            OpenDocument* od = ws_.byId(r.token);
            if (!od) continue;
            std::string path = r.path;
            if (!endsWithCi(path, ".md") && !endsWithCi(path, ".markdown") && !endsWithCi(path, ".txt") &&
                path.find('.', path.find_last_of("/\\") + 1) == std::string::npos)
                path += ".md";
            std::string err;
            if (!ws_.saveAs(*od, path, err)) { showError(err); continue; }
            status("Saved " + od->doc.title());
            saveSettings();
            if (closeAfterSaveDocId_ == od->id) {
                docUi_.erase(od->id);
                ws_.close(od->id);
            }
            closeAfterSaveDocId_ = -1;
        }
    }
    if (quitPending_ && !ws_.anyDirty()) quitConfirmed_ = true;
}

void MainWindow::handleLink(OpenDocument& od, const std::string& href)
{
    if (href.empty()) return;
    if (href[0] == '#') {
        const std::string want = href.substr(1);
        for (const OutlineEntry& h : buildOutline(od.doc.text)) {
            if (headingSlug(h.text) == want || h.text == want) {
                od.scrollToHeading = h.text;
                od.scrollToLine = h.line;
                return;
            }
        }
        status("No heading for " + href);
        return;
    }
    std::string err;
    if (href.find("://") != std::string::npos || href.rfind("mailto:", 0) == 0) {
        if (!platform::openExternal(href, err)) showError(err);
        return;
    }
    std::string target = href;
    const size_t hash = target.find('#');
    std::string anchor;
    if (hash != std::string::npos) { anchor = target.substr(hash + 1); target.erase(hash); }
    const std::string path = platform::joinPath(od.doc.directory(), target);
    if (endsWithCi(path, ".md") || endsWithCi(path, ".markdown")) {
        if (!ws_.openFile(path, err)) { showError(err); return; }
        if (!anchor.empty()) {
            if (OpenDocument* opened = ws_.active()) {
                for (const OutlineEntry& h : buildOutline(opened->doc.text))
                    if (headingSlug(h.text) == anchor) { opened->scrollToHeading = h.text; break; }
            }
        }
        return;
    }
    if (!platform::openExternal(path, err)) showError(err);
}

void MainWindow::applyFormat(OpenDocument& od, BlockCommand cmd)
{
    DocUi& ui = uiFor(od);
    const std::string before = od.doc.text;
    bool changed = false;
    if (od.mode == ViewMode::Edit) {
        changed = applyBlockCommand(ws_, od, ui.wysiwyg, cmd);
    } else if (od.mode == ViewMode::Source) {
        std::string& text = od.doc.text;
        SourceEditorState& st = ui.source;
        auto linePrefix = [&](const char* prefix) {
            int ls = st.area.cursor;
            while (ls > 0 && text[(size_t)ls - 1] != '\n') --ls;
            // Replace an existing heading/list prefix rather than stacking.
            int pe = ls;
            while (pe < (int)text.size() && (text[(size_t)pe] == '#' || text[(size_t)pe] == '>' ||
                                             text[(size_t)pe] == '-' || text[(size_t)pe] == '*')) ++pe;
            if (pe < (int)text.size() && text[(size_t)pe] == ' ' && pe > ls) ++pe; else pe = ls;
            text.replace((size_t)ls, (size_t)(pe - ls), prefix);
            st.area.cursor = st.area.anchor = ls + (int)std::strlen(prefix);
            changed = true;
        };
        switch (cmd) {
        case BlockCommand::Bold:   changed = applyInlineCommand(text, st, InlineCommand::Bold); break;
        case BlockCommand::Italic: changed = applyInlineCommand(text, st, InlineCommand::Italic); break;
        case BlockCommand::Strike: changed = applyInlineCommand(text, st, InlineCommand::Strike); break;
        case BlockCommand::Code:   changed = applyInlineCommand(text, st, InlineCommand::Code); break;
        case BlockCommand::Link:   changed = applyInlineCommand(text, st, InlineCommand::Link); break;
        case BlockCommand::Heading1: linePrefix("# "); break;
        case BlockCommand::Heading2: linePrefix("## "); break;
        case BlockCommand::Heading3: linePrefix("### "); break;
        case BlockCommand::Paragraph: linePrefix(""); break;
        case BlockCommand::Bullet:   linePrefix("- "); break;
        case BlockCommand::Numbered: linePrefix("1. "); break;
        case BlockCommand::Task:     linePrefix("- [ ] "); break;
        case BlockCommand::Quote:    linePrefix("> "); break;
        case BlockCommand::CodeBlock: {
            const int c = st.area.cursor;
            text.insert((size_t)c, "\n```\n\n```\n");
            st.area.cursor = st.area.anchor = c + 5;
            changed = true;
            break;
        }
        case BlockCommand::Rule: {
            const int c = st.area.cursor;
            text.insert((size_t)c, "\n---\n");
            st.area.cursor = st.area.anchor = c + 5;
            changed = true;
            break;
        }
        case BlockCommand::Image: {
            const int c = st.area.cursor;
            text.insert((size_t)c, "![alt text](image.png)");
            st.area.anchor = c + 2; st.area.cursor = c + 10;
            changed = true;
            break;
        }
        }
        st.area.requestFocus = true;
        if (changed) od.modelStale = true;
    }
    if (changed) ui.undo.record(before, ImGui::GetTime(), 99);
}

// ---- drawing ----------------------------------------------------------------

void MainWindow::draw(const UiFrame& frame)
{
    lastNativeWindow_ = frame.nativeWindow;
    handleDialogResults();

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    const ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(kHostWindow, nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    drawMenuBar(frame);
    drawToolbar(frame);

    const float statusH = ImGui::GetFrameHeight() + 6.0f;
    const ImGuiID dockId = ImGui::GetID("MainDockspace");
    ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockId);
    if (!node || !outlineDockBuilt_) {
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, ImVec2(vp->WorkSize.x, vp->WorkSize.y - statusH));
        ImGuiID left = 0, center = 0;
        ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, 0.22f, &left, &center);
        ImGui::DockBuilderDockWindow("Outline", left);
        ImGui::DockBuilderGetNode(center)->SetLocalFlags(ImGuiDockNodeFlags_CentralNode);
        ImGui::DockBuilderFinish(dockId);
        outlineDockBuilt_ = true;
    }
    ImGui::DockSpace(dockId, ImVec2(0.0f, -statusH), ImGuiDockNodeFlags_None);
    drawStatusBar();
    ImGui::End();

    if (ws_.settings.showOutline) drawOutline();

    // Documents. Iterate by id so a close inside the loop is safe.
    std::vector<int> ids;
    for (const auto& d : ws_.docs) ids.push_back(d->id);
    ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockId);
    for (int id : ids) {
        OpenDocument* od = ws_.byId(id);
        if (od) drawDocumentWindow(*od, frame, central ? central->ID : dockId);
    }
    for (int id : ids) {
        OpenDocument* od = ws_.byId(id);
        if (od && od->closeRequested) { od->closeRequested = false; actionClose(*od); }
    }

    handleShortcuts(frame);
    drawModals(frame);
}

void MainWindow::drawMenuBar(const UiFrame& frame)
{
    if (!ImGui::BeginMenuBar()) return;
    OpenDocument* od = ws_.active();
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem(ICON_MD_NOTE_ADD " New", "Ctrl+N")) actionNew();
        if (ImGui::MenuItem(ICON_MD_FOLDER_OPEN " Open...", "Ctrl+O")) actionOpen(frame);
        if (ImGui::BeginMenu("Open Recent", !ws_.settings.recentFiles.empty())) {
            std::string chosen;
            for (const std::string& r : ws_.settings.recentFiles)
                if (ImGui::MenuItem(r.c_str())) chosen = r;
            ImGui::Separator();
            if (ImGui::MenuItem("Clear list")) { ws_.settings.recentFiles.clear(); saveSettings(); }
            ImGui::EndMenu();
            if (!chosen.empty()) openFileFromOutside(chosen);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_MD_SAVE " Save", "Ctrl+S", false, od != nullptr) && od) actionSave(*od, frame);
        if (ImGui::MenuItem(ICON_MD_SAVE_AS " Save As...", "Ctrl+Shift+S", false, od != nullptr) && od) actionSaveAs(*od, frame);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_MD_CLOSE " Close", "Ctrl+W", false, od != nullptr) && od) od->closeRequested = true;
        if (ImGui::MenuItem(ICON_MD_EXIT_TO_APP " Exit", "Alt+F4")) requestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        const bool has = od != nullptr;
        if (ImGui::MenuItem(ICON_MD_UNDO " Undo", "Ctrl+Z", false, has && canUndo(*od)) && od) actionUndo(*od);
        if (ImGui::MenuItem(ICON_MD_REDO " Redo", "Ctrl+Y", false, has && uiFor(*od).undo.canRedo()) && od) actionRedo(*od);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_MD_SEARCH " Find...", "Ctrl+F", false, has) && od) { od->showFind = true; uiFor(*od).focusSearch = true; }
        ImGui::Separator();
        const bool fmt = has && od->mode != ViewMode::Preview;
        if (ImGui::MenuItem("Bold", "Ctrl+B", false, fmt)) applyFormat(*od, BlockCommand::Bold);
        if (ImGui::MenuItem("Italic", "Ctrl+I", false, fmt)) applyFormat(*od, BlockCommand::Italic);
        if (ImGui::MenuItem("Strikethrough", nullptr, false, fmt)) applyFormat(*od, BlockCommand::Strike);
        if (ImGui::MenuItem("Inline code", "Ctrl+E", false, fmt)) applyFormat(*od, BlockCommand::Code);
        if (ImGui::MenuItem("Link", "Ctrl+K", false, fmt)) applyFormat(*od, BlockCommand::Link);
        ImGui::Separator();
        if (ImGui::MenuItem("Heading 1", nullptr, false, fmt)) applyFormat(*od, BlockCommand::Heading1);
        if (ImGui::MenuItem("Heading 2", nullptr, false, fmt)) applyFormat(*od, BlockCommand::Heading2);
        if (ImGui::MenuItem("Heading 3", nullptr, false, fmt)) applyFormat(*od, BlockCommand::Heading3);
        if (ImGui::MenuItem("Paragraph", nullptr, false, fmt)) applyFormat(*od, BlockCommand::Paragraph);
        if (ImGui::MenuItem("Bullet list", nullptr, false, fmt)) applyFormat(*od, BlockCommand::Bullet);
        if (ImGui::MenuItem("Numbered list", nullptr, false, fmt)) applyFormat(*od, BlockCommand::Numbered);
        if (ImGui::MenuItem("Task list", nullptr, false, fmt)) applyFormat(*od, BlockCommand::Task);
        if (ImGui::MenuItem("Quote", nullptr, false, fmt)) applyFormat(*od, BlockCommand::Quote);
        if (ImGui::MenuItem("Code block", nullptr, false, fmt)) applyFormat(*od, BlockCommand::CodeBlock);
        if (ImGui::MenuItem("Horizontal rule", nullptr, false, fmt)) applyFormat(*od, BlockCommand::Rule);
        if (ImGui::MenuItem("Image", nullptr, false, fmt)) applyFormat(*od, BlockCommand::Image);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        const bool has = od != nullptr;
        if (ImGui::MenuItem(ICON_MD_VISIBILITY " Preview", "Ctrl+1", has && od->mode == ViewMode::Preview, has)) setMode(*od, ViewMode::Preview);
        if (ImGui::MenuItem(ICON_MD_CODE " Source", "Ctrl+2", has && od->mode == ViewMode::Source, has)) setMode(*od, ViewMode::Source);
        if (ImGui::MenuItem(ICON_MD_EDIT_NOTE " Edit (WYSIWYG)", "Ctrl+3", has && od->mode == ViewMode::Edit, has)) setMode(*od, ViewMode::Edit);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_MD_TOC " Outline", "Ctrl+Shift+O", ws_.settings.showOutline)) {
            ws_.settings.showOutline = !ws_.settings.showOutline;
            saveSettings();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_MD_ZOOM_IN " Zoom In", "Ctrl+=")) setZoom(ws_.settings.uiScale + 0.1f);
        if (ImGui::MenuItem(ICON_MD_ZOOM_OUT " Zoom Out", "Ctrl+-")) setZoom(ws_.settings.uiScale - 0.1f);
        if (ImGui::MenuItem("Reset Zoom", "Ctrl+0")) setZoom(1.0f);
        ImGui::Separator();
        if (ImGui::BeginMenu(ICON_MD_PALETTE " Theme")) {
            for (int i = 0; i < themeCount(); ++i) {
                const bool sel = currentThemeIndex() == i;
                if (ImGui::MenuItem(themeAt(i).name, nullptr, sel)) {
                    requestTheme(i);
                    ws_.settings.themeName = themeAt(i).name;
                    saveSettings();
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(ICON_MD_SETTINGS " Preferences...")) prefsOpen_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem(ICON_MD_INFO " About davedown", "F1")) aboutOpen_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void MainWindow::drawToolbar(const UiFrame& frame)
{
    const Palette& pal = palette();
    OpenDocument* od = ws_.active();
    const float pad = ImGui::GetFontSize() * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad * 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, ImGui::GetStyle().ItemSpacing.y));
    ImGui::BeginChild("##toolbar", ImVec2(0, ImGui::GetFrameHeight() + pad * 1.4f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (iconButton("new", ICON_MD_NOTE_ADD, "New (Ctrl+N)")) actionNew();
    ImGui::SameLine();
    if (iconButton("open", ICON_MD_FOLDER_OPEN, "Open (Ctrl+O)")) actionOpen(frame);
    ImGui::SameLine();
    if (iconButton("save", ICON_MD_SAVE, "Save (Ctrl+S)", od != nullptr) && od) actionSave(*od, frame);
    ImGui::SameLine(0.0f, pad);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0.0f, pad);

    // Mode switch.
    {
        static const char* icons[] = { ICON_MD_VISIBILITY, ICON_MD_CODE, ICON_MD_EDIT_NOTE };
        static const char* labels[] = { "Preview", "Source", "Edit" };
        static const char* tips[] = { "Rendered view (Ctrl+1)", "Markdown source (Ctrl+2)", "WYSIWYG editor (Ctrl+3)" };
        int sel = od ? (int)od->mode : 0;
        ImGui::BeginDisabled(od == nullptr);
        if (segmentedControl("mode", icons, labels, tips, 3, &sel, KindNeutral) && od)
            setMode(*od, (ViewMode)sel);
        ImGui::EndDisabled();
    }
    ImGui::SameLine(0.0f, pad);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0.0f, pad);

    // Formatting.
    const bool fmt = od && od->mode != ViewMode::Preview;
    struct Btn { const char* id; const char* icon; const char* tip; BlockCommand cmd; };
    static const Btn inlineBtns[] = {
        { "bold", ICON_MD_FORMAT_BOLD, "Bold (Ctrl+B)", BlockCommand::Bold },
        { "italic", ICON_MD_FORMAT_ITALIC, "Italic (Ctrl+I)", BlockCommand::Italic },
        { "strike", ICON_MD_FORMAT_STRIKETHROUGH, "Strikethrough", BlockCommand::Strike },
        { "code", ICON_MD_CODE, "Inline code (Ctrl+E)", BlockCommand::Code },
        { "link", ICON_MD_LINK, "Link (Ctrl+K)", BlockCommand::Link },
    };
    static const Btn blockBtns[] = {
        { "h1", ICON_MD_TITLE, "Heading 1", BlockCommand::Heading1 },
        { "h2", ICON_MD_TEXT_FIELDS, "Heading 2", BlockCommand::Heading2 },
        { "h3", ICON_MD_SHORT_TEXT, "Heading 3", BlockCommand::Heading3 },
        { "bullet", ICON_MD_FORMAT_LIST_BULLETED, "Bullet list", BlockCommand::Bullet },
        { "numbered", ICON_MD_FORMAT_LIST_NUMBERED, "Numbered list", BlockCommand::Numbered },
        { "task", ICON_MD_CHECK_BOX, "Task list", BlockCommand::Task },
        { "quote", ICON_MD_FORMAT_QUOTE, "Quote", BlockCommand::Quote },
        { "codeblock", ICON_MD_DATA_OBJECT, "Code block", BlockCommand::CodeBlock },
        { "rule", ICON_MD_HORIZONTAL_RULE, "Horizontal rule", BlockCommand::Rule },
        { "image", ICON_MD_IMAGE, "Image", BlockCommand::Image },
    };
    for (const Btn& b : inlineBtns) {
        if (iconButton(b.id, b.icon, b.tip, fmt) && od) applyFormat(*od, b.cmd);
        ImGui::SameLine();
    }
    ImGui::SameLine(0.0f, pad);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0.0f, pad);
    for (const Btn& b : blockBtns) {
        if (iconButton(b.id, b.icon, b.tip, fmt) && od) applyFormat(*od, b.cmd);
        ImGui::SameLine();
    }

    // Right-aligned group.
    const float rightW = ImGui::GetFrameHeight() * 5.0f + ImGui::GetFontSize() * 3.2f + pad * 2.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - rightW));
    if (iconButton("find", ICON_MD_SEARCH, "Find (Ctrl+F)", od != nullptr) && od) {
        od->showFind = !od->showFind;
        uiFor(*od).focusSearch = od->showFind;
    }
    ImGui::SameLine();
    {
        const bool on = ws_.settings.showOutline;
        if (on) ImGui::PushStyleColor(ImGuiCol_Text, pal.accent);
        const bool clicked = iconButton("outline", ICON_MD_TOC, "Outline (Ctrl+Shift+O)");
        if (on) ImGui::PopStyleColor();
        if (clicked) { ws_.settings.showOutline = !on; saveSettings(); }
    }
    ImGui::SameLine(0.0f, pad);
    if (iconButton("zoomout", ICON_MD_ZOOM_OUT, "Zoom out (Ctrl+-)")) setZoom(ws_.settings.uiScale - 0.1f);
    ImGui::SameLine();
    char zoom[16];
    std::snprintf(zoom, sizeof(zoom), "%d%%", (int)std::lround(ws_.settings.uiScale * 100.0f));
    ImGui::AlignTextToFramePadding();
    mutedText("%s", zoom);
    ImGui::SameLine();
    if (iconButton("zoomin", ICON_MD_ZOOM_IN, "Zoom in (Ctrl+=)")) setZoom(ws_.settings.uiScale + 0.1f);
    ImGui::SameLine(0.0f, pad);
    if (iconButton("prefs", ICON_MD_SETTINGS, "Preferences")) prefsOpen_ = true;

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    hairline();
}

void MainWindow::drawStatusBar()
{
    const Palette& pal = palette();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 0.6f, 3.0f));
    ImGui::BeginChild("##status", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    OpenDocument* od = ws_.active();
    ImGui::AlignTextToFramePadding();

    const double now = ImGui::GetTime();
    if (!ws_.statusMessage.empty() && now - ws_.statusTime < kStatusTimeout) {
        ImGui::TextUnformatted(ws_.statusMessage.c_str());
    } else if (!lastPreview_.hoveredLink.empty() && od && od->mode == ViewMode::Preview) {
        mutedText(ICON_MD_LINK " %s", lastPreview_.hoveredLink.c_str());
    } else if (od) {
        mutedText("%s", od->doc.isUntitled() ? "Unsaved document" : od->doc.path.c_str());
    } else {
        mutedText("No document open");
    }

    if (od) {
        const DocumentStats st = computeStats(od->doc.text);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%d words  \xC2\xB7  %d chars  \xC2\xB7  %d lines  \xC2\xB7  %d min read",
                      st.words, st.characters, st.lines, st.readingMinutes);
        const float w = ImGui::CalcTextSize(buf).x;
        const float pillW = ImGui::GetFontSize() * 5.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - w - pillW - ImGui::GetFontSize() * 2.0f);
        mutedText("%s", buf);
        ImGui::SameLine(ImGui::GetWindowWidth() - pillW);
        const char* modeLabel = od->mode == ViewMode::Preview ? "PREVIEW" : od->mode == ViewMode::Source ? "SOURCE" : "EDIT";
        statusPill(modeLabel, od->mode == ViewMode::Edit ? KindAccent : KindNeutral);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void MainWindow::drawOutline()
{
    bool open = true;
    ImGui::Begin("Outline", &open, ImGuiWindowFlags_None);
    OpenDocument* od = ws_.active();
    if (!od) {
        faintText("Open a document to see its headings.");
    } else {
        const auto outline = buildOutline(od->doc.text);
        if (outline.empty()) faintText("No headings yet.");
        const float fs = ImGui::GetFontSize();
        for (size_t i = 0; i < outline.size(); ++i) {
            const OutlineEntry& h = outline[i];
            // Label-based ID ("Target##h1") so tests and tooltips can address it.
            const std::string label = h.text + "##h" + std::to_string(i);
            const float indent = (float)(h.level - 1) * fs * 0.9f;
            if (indent > 0.0f) ImGui::Indent(indent);
            if (h.level <= 2) ImGui::PushFont(fonts::uiBold(), 0.0f);
            if (h.level >= 4) ImGui::PushStyleColor(ImGuiCol_Text, palette().textMuted);
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None)) {
                od->scrollToHeading = h.text;
                od->scrollToLine = h.line;
                if (od->mode == ViewMode::Source) uiFor(*od).source.area.requestCursorLine = h.line;
            }
            if (h.level >= 4) ImGui::PopStyleColor();
            if (h.level <= 2) ImGui::PopFont();
            if (indent > 0.0f) ImGui::Unindent(indent);
        }
    }
    ImGui::End();
    if (!open) { ws_.settings.showOutline = false; saveSettings(); }
}

void MainWindow::drawFindBar(OpenDocument& od, DocUi& ui)
{
    const Palette& pal = palette();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 0.5f, 4.0f));
    ImGui::BeginChild("##find", ImVec2(0, ImGui::GetFrameHeight() + 8.0f), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(ICON_MD_SEARCH);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    if (ui.focusSearch) { ImGui::SetKeyboardFocusHere(); ui.focusSearch = false; }
    if (ImGui::InputTextWithHint("##search", "Find in document", &od.search, ImGuiInputTextFlags_EnterReturnsTrue)) {
        od.searchIndex = ui.searchMatchCount > 0 ? (od.searchIndex + 1) % ui.searchMatchCount : 0;
    }
    if (ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Escape)) od.showFind = false;
    ImGui::SameLine();
    if (iconButton("prev", ICON_MD_KEYBOARD_ARROW_UP, "Previous match (Shift+F3)", ui.searchMatchCount > 0))
        od.searchIndex = (od.searchIndex - 1 + ui.searchMatchCount) % std::max(ui.searchMatchCount, 1);
    ImGui::SameLine();
    if (iconButton("next", ICON_MD_KEYBOARD_ARROW_DOWN, "Next match (F3)", ui.searchMatchCount > 0))
        od.searchIndex = (od.searchIndex + 1) % std::max(ui.searchMatchCount, 1);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (od.search.empty()) faintText("Type to search");
    else if (ui.searchMatchCount == 0) mutedText("No matches");
    else mutedText("%d of %d", std::min(od.searchIndex, ui.searchMatchCount - 1) + 1, ui.searchMatchCount);
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetFrameHeight() * 1.6f);
    if (iconButton("closefind", ICON_MD_CLOSE, "Close (Esc)")) od.showFind = false;
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void MainWindow::drawDocumentBody(OpenDocument& od, DocUi& ui)
{
    const Palette& pal = palette();
    const double now = ImGui::GetTime();
    const std::string before = od.doc.text;

    // Pictures resolve relative to the document's folder.
    const std::string docDir = od.doc.directory();
    md::ImageProvider images = [&docDir](std::string_view src, md::ImageInfo& out) {
        const std::string path = platform::joinPath(docDir, std::string(src));
        if (path.find("://") != std::string::npos) { out.note = "remote images are not fetched"; return false; }
        const platform::TextureInfo& t = platform::textureForFile(path);
        if (!t.id) { out.note = t.error; return false; }
        out.tex = (ImTextureID)t.id;
        out.w = t.width;
        out.h = t.height;
        return true;
    };

    switch (od.mode) {
    case ViewMode::Preview: {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
        ImGui::BeginChild("##preview", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);
        // A reading column: generous side margins and a capped measure.
        const float fs = ImGui::GetFontSize();
        const float availW = ImGui::GetContentRegionAvail().x;
        const float maxW = fs * 46.0f;
        const float colW = std::min(availW - fs * 2.0f, maxW);
        const float margin = std::max((availW - colW) * 0.5f, fs);
        ImGui::Indent(margin);
        ImGui::PushClipRect(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth(),
                                                          ImGui::GetWindowPos().y + ImGui::GetWindowHeight()), true);
        ImGui::Dummy(ImVec2(0.0f, fs * 0.6f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + colW);
        {
            // Constrain the content width for the renderer via a group of fixed width.
            ImGui::BeginGroup();
            ImGui::PushItemWidth(colW);
            ImGuiWindow* w = ImGui::GetCurrentWindow();
            const float savedMax = w->WorkRect.Max.x;
            w->WorkRect.Max.x = w->WorkRect.Min.x + colW;
            md::Options o;
            o.search = od.search;
            o.currentMatch = od.showFind && !od.search.empty() ? od.searchIndex : -1;
            o.scrollToHeading = od.scrollToHeading;
            o.scrollToLine = od.scrollToLine;
            o.images = &images;
            lastPreview_ = md::render(od.doc.text, o);
            w->WorkRect.Max.x = savedMax;
            ImGui::PopItemWidth();
            ImGui::EndGroup();
        }
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0.0f, fs * 2.0f));
        ImGui::PopClipRect();
        ImGui::Unindent(margin);
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ui.searchMatchCount = lastPreview_.matchCount;
        if (lastPreview_.headingFound || !od.scrollToHeading.empty()) { od.scrollToHeading.clear(); od.scrollToLine = -1; }
        if (!lastPreview_.clickedLink.empty()) handleLink(od, lastPreview_.clickedLink);
        if (!lastPreview_.copiedCode.empty()) { ImGui::SetClipboardText(lastPreview_.copiedCode.c_str()); status("Code copied"); }
        if (lastPreview_.taskToggleOffset >= 0 && lastPreview_.taskToggleOffset < (int)od.doc.text.size()) {
            std::string text = od.doc.text;
            char& c = text[(size_t)lastPreview_.taskToggleOffset];
            c = (c == ' ') ? 'x' : ' ';
            ui.undo.record(before, now, 50);
            ws_.setText(od, text, false);
        }
        break;
    }
    case ViewMode::Source: {
        std::string text = od.doc.text;
        const int scrollLine = od.scrollToLine;
        od.scrollToLine = -1;
        od.scrollToHeading.clear();
        const SourceEditorResult r = drawSourceEditor("##source", text, ui.source, scrollLine, ui.focusEditor);
        ui.focusEditor = false;
        if (r.changed) {
            ui.undo.record(before, now, 1);
            ws_.setText(od, text, false);
        }
        ui.searchMatchCount = 0;
        if (od.showFind && !od.search.empty()) {
            // Count matches and jump the caret to the current one.
            int count = 0;
            size_t at = 0;
            int targetOffset = -1;
            std::string lower = od.doc.text, needle = od.search;
            for (char& c : lower) c = (char)std::tolower((unsigned char)c);
            for (char& c : needle) c = (char)std::tolower((unsigned char)c);
            while ((at = lower.find(needle, at)) != std::string::npos) {
                if (count == od.searchIndex) targetOffset = (int)at;
                ++count;
                at += needle.size();
            }
            ui.searchMatchCount = count;
            if (count > 0 && od.searchIndex >= count) od.searchIndex = 0;
            static int lastShown = -1;
            static int lastDoc = -1;
            if (targetOffset >= 0 && (lastShown != od.searchIndex || lastDoc != od.id)) {
                ui.source.area.anchor = targetOffset;
                ui.source.area.cursor = targetOffset + (int)needle.size();
                ui.source.area.scrollToCursor = true;
                lastShown = od.searchIndex;
                lastDoc = od.id;
            }
        }
        break;
    }
    case ViewMode::Edit: {
        const WysiwygResult r = drawWysiwygEditor("##edit", ws_, od, ui.wysiwyg, images);
        if (r.changed) ui.undo.record(before, now, 1000 + std::max(od.editingBlock, 0));
        if (!r.clickedLink.empty()) handleLink(od, r.clickedLink);
        if (!r.copiedCode.empty()) { ImGui::SetClipboardText(r.copiedCode.c_str()); status("Code copied"); }
        od.scrollToHeading.clear();
        od.scrollToLine = -1;
        ui.searchMatchCount = 0;
        break;
    }
    }
    (void)pal;
}

void MainWindow::drawDocumentWindow(OpenDocument& od, const UiFrame& frame, unsigned dockId)
{
    (void)frame;
    DocUi& ui = uiFor(od);
    ImGui::SetNextWindowDockID(dockId, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    bool open = true;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (od.doc.isDirty()) flags |= ImGuiWindowFlags_UnsavedDocument;
    if (ws_.activeId == od.id && ws_.docs.size() > 1) {
        // Newly opened documents come to the front.
        static int lastActivated = -1;
        if (lastActivated != od.id) { ImGui::SetNextWindowFocus(); lastActivated = od.id; }
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(docWindowName(od).c_str(), &open, flags);
    ImGui::PopStyleVar();
    if (visible) {
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) ws_.activeId = od.id;
        if (od.showFind) drawFindBar(od, ui);
        drawDocumentBody(od, ui);
    }
    ImGui::End();
    if (!open) od.closeRequested = true;
}

void MainWindow::handleShortcuts(const UiFrame& frame)
{
    OpenDocument* od = ws_.active();
    const ImGuiInputFlags g = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, g)) actionNew();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, g)) actionOpen(frame);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_O, g)) { ws_.settings.showOutline = !ws_.settings.showOutline; saveSettings(); }
    if (ImGui::Shortcut(ImGuiKey_F1, g)) aboutOpen_ = true;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Equal, g) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd, g)) setZoom(ws_.settings.uiScale + 0.1f);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Minus, g) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract, g)) setZoom(ws_.settings.uiScale - 0.1f);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_0, g)) setZoom(1.0f);
    if (!od) return;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, g)) actionSave(*od, frame);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, g)) actionSaveAs(*od, frame);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_W, g)) od->closeRequested = true;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F, g)) { od->showFind = true; uiFor(*od).focusSearch = true; }
    if (ImGui::Shortcut(ImGuiKey_F3, g) && uiFor(*od).searchMatchCount > 0) od->searchIndex = (od->searchIndex + 1) % uiFor(*od).searchMatchCount;
    if (ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_F3, g) && uiFor(*od).searchMatchCount > 0)
        od->searchIndex = (od->searchIndex - 1 + uiFor(*od).searchMatchCount) % uiFor(*od).searchMatchCount;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_1, g)) setMode(*od, ViewMode::Preview);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_2, g)) setMode(*od, ViewMode::Source);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_3, g)) setMode(*od, ViewMode::Edit);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, g)) actionUndo(*od);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, g) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, g)) actionRedo(*od);
    if (od->mode != ViewMode::Preview) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_B, g)) applyFormat(*od, BlockCommand::Bold);
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_I, g)) applyFormat(*od, BlockCommand::Italic);
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_E, g)) applyFormat(*od, BlockCommand::Code);
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_K, g)) applyFormat(*od, BlockCommand::Link);
    }
}

void MainWindow::drawModals(const UiFrame& frame)
{
    const Palette& pal = palette();
    const float fs = ImGui::GetFontSize();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 1.2f, fs * 1.0f));

    // About -- always modal.
    if (aboutOpen_) ImGui::OpenPopup("About davedown");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("About davedown", &aboutOpen_, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::PushFont(fonts::uiBold(), fs * 1.8f);
        ImGui::PushStyleColor(ImGuiCol_Text, pal.accent);
        ImGui::TextUnformatted(ICON_MD_ARTICLE "  davedown");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        mutedText("Version %s", DAVEDOWN_VERSION);
        ImGui::Spacing();
        ImGui::TextUnformatted("A markdown viewer and editor: rendered preview,\nsyntax-coloured source, and a block-based WYSIWYG mode.");
        ImGui::Spacing();
        sectionHeader("Built with");
        ImGui::BulletText("SDL3 and Dear ImGui (docking)");
        ImGui::BulletText("md4c markdown parser");
        ImGui::BulletText("stb_image");
        ImGui::BulletText("Open Sans, Fira Code, Material Icons");
        ImGui::BulletText("Theme and icon set from fwcom");
        ImGui::Spacing();
        ImGui::Spacing();
        if (accentButton("OK", ImVec2(fs * 6.0f, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            aboutOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Preferences.
    if (prefsOpen_) ImGui::OpenPopup("Preferences");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Preferences", &prefsOpen_, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        bool changed = false;
        sectionHeader("Appearance");
        ImGui::SetNextItemWidth(fs * 12.0f);
        if (ImGui::BeginCombo("Theme", themeAt(currentThemeIndex()).name)) {
            for (int i = 0; i < themeCount(); ++i) {
                if (ImGui::Selectable(themeAt(i).name, currentThemeIndex() == i)) {
                    requestTheme(i);
                    ws_.settings.themeName = themeAt(i).name;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(fs * 12.0f);
        float scale = ws_.settings.uiScale;
        if (ImGui::SliderFloat("Interface scale", &scale, 0.7f, 2.0f, "%.1fx")) {
            ws_.settings.uiScale = clampUiScale(std::round(scale * 10.0f) / 10.0f);
            changed = true;
        }
        ImGui::Spacing();
        sectionHeader("Documents");
        const char* modes[] = { "Preview", "Source", "Edit" };
        int mode = (int)ws_.settings.defaultMode;
        ImGui::SetNextItemWidth(fs * 12.0f);
        if (ImGui::Combo("Open documents in", &mode, modes, 3)) { ws_.settings.defaultMode = (ViewMode)mode; changed = true; }
        if (ImGui::Checkbox("Show outline panel", &ws_.settings.showOutline)) changed = true;
        if (platform::fileAssociationSupported()) {
            ImGui::Spacing();
            sectionHeader("File associations");
            const bool isDefault = platform::isDefaultMarkdownHandler();
            if (isDefault) statusPill("davedown opens .md files", KindOk);
            else mutedText("Another program opens .md files.");
            if (ImGui::Button(isDefault ? "Change default app for .md..." : "Make davedown the default for .md files...")) {
                std::string err;
                if (!platform::promptMakeDefault(err)) showError(err);
                else status("In Settings, press \"Set default\" next to davedown");
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Registers davedown with Windows and opens Settings > Default apps,\nwhere \"Set default\" makes it the handler for markdown files.");
        }
        ImGui::Spacing();
        faintText("Settings file: %s", platform::settingsPath().c_str());
        ImGui::Spacing();
        if (accentButton("Done", ImVec2(fs * 6.0f, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            prefsOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        if (changed) saveSettings();
        ImGui::EndPopup();
    }

    // Error.
    if (errorOpen_) ImGui::OpenPopup("Something went wrong");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Something went wrong", &errorOpen_, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::PushStyleColor(ImGuiCol_Text, pal.danger);
        ImGui::TextUnformatted(ICON_MD_ERROR_OUTLINE);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushTextWrapPos(fs * 28.0f);
        ImGui::TextUnformatted(errorText_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(fs * 6.0f, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            errorOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Close a dirty document.
    if (closePromptOpen_) ImGui::OpenPopup("Unsaved changes");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved changes", &closePromptOpen_, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        OpenDocument* od = ws_.byId(closePromptDocId_);
        if (!od) { closePromptOpen_ = false; ImGui::CloseCurrentPopup(); }
        else {
            ImGui::Text("Save changes to \"%s\" before closing?", od->doc.title().c_str());
            ImGui::Spacing();
            if (accentButton("Save", ImVec2(fs * 6.0f, 0))) {
                closePromptOpen_ = false;
                ImGui::CloseCurrentPopup();
                actionSave(*od, frame, true);
            }
            ImGui::SameLine();
            if (dangerButton("Don't save", ImVec2(fs * 6.5f, 0))) {
                closePromptOpen_ = false;
                ImGui::CloseCurrentPopup();
                docUi_.erase(od->id);
                ws_.close(od->id);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(fs * 6.0f, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                closePromptOpen_ = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    // Quit with dirty documents.
    if (quitPromptOpen_) ImGui::OpenPopup("Quit davedown");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Quit davedown", &quitPromptOpen_, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        int dirty = 0;
        for (const auto& d : ws_.docs) if (d->doc.isDirty()) ++dirty;
        ImGui::Text("%d document%s unsaved changes.", dirty, dirty == 1 ? " has" : "s have");
        for (const auto& d : ws_.docs) if (d->doc.isDirty()) ImGui::BulletText("%s", d->doc.title().c_str());
        ImGui::Spacing();
        if (accentButton("Save all", ImVec2(fs * 6.0f, 0))) {
            quitPromptOpen_ = false;
            ImGui::CloseCurrentPopup();
            for (const auto& d : ws_.docs) {
                if (!d->doc.isDirty()) continue;
                if (d->doc.isUntitled()) { actionSaveAs(*d, frame, false); break; }   // one dialog at a time
                std::string err;
                if (!ws_.save(*d, err)) showError(err);
            }
            if (!ws_.anyDirty()) quitConfirmed_ = true;
        }
        ImGui::SameLine();
        if (dangerButton("Discard all", ImVec2(fs * 6.5f, 0))) {
            quitPromptOpen_ = false;
            ImGui::CloseCurrentPopup();
            quitConfirmed_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(fs * 6.0f, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            quitPromptOpen_ = false;
            quitPending_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!quitPromptOpen_ && quitPending_ && closeAfterSaveDocId_ < 0 && !ws_.anyDirty()) quitConfirmed_ = true;

    ImGui::PopStyleVar();
}

} // namespace dd::ui
