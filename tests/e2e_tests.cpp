// End-to-end scenarios run against the real interface by the ImGui Test
// Engine. Each test drives input the way a person would (clicks, key
// presses, typed characters) and then checks the application state.
#include "e2e_tests.h"

#include "app/Workspace.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "IconsMaterialDesign.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef Yield
#undef Yield
#endif

namespace dd::e2e {

namespace {

Context* g_ctx = nullptr;

std::string docWindow(const OpenDocument& od) { return "###doc" + std::to_string(od.id); }

// Opens a fresh document with `text`, makes it active and switches mode.
OpenDocument& freshDoc(ImGuiTestContext* ctx, const std::string& text, ViewMode mode)
{
    Workspace& ws = g_ctx->workspace;
    OpenDocument& od = ws.newDocument(text);
    g_ctx->ui.setMode(od, mode);
    ctx->Yield(3);
    ws.activeId = od.id;
    ctx->Yield(2);
    return od;
}

void closeAll(ImGuiTestContext* ctx)
{
    Workspace& ws = g_ctx->workspace;
    std::vector<int> ids;
    for (auto& d : ws.docs) ids.push_back(d->id);
    for (int id : ids) {
        if (OpenDocument* od = ws.byId(id)) od->doc.savedText = od->doc.text;   // never prompt
        ws.close(id);
    }
    ctx->Yield(2);
}

std::string tempPath(const char* name)
{
    std::error_code ec;
    auto dir = std::filesystem::temp_directory_path(ec);
    return (dir / name).string();
}

} // namespace

void registerTests(ImGuiTestEngine* e, Context& context)
{
    g_ctx = &context;
    ImGuiTest* t = nullptr;

    t = IM_REGISTER_TEST(e, "app", "opens_welcome_document");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        Workspace& ws = g_ctx->workspace;
        IM_CHECK(!ws.docs.empty());
        OpenDocument* od = ws.active();
        IM_CHECK(od != nullptr);
        IM_CHECK(od->doc.text.find("Welcome") != std::string::npos);
        ImGuiWindow* w = ctx->GetWindowByRef(("//" + docWindow(*od)).c_str());
        IM_CHECK(w != nullptr);
        IM_CHECK(ctx->GetWindowByRef("//Outline") != nullptr);
    };

    t = IM_REGISTER_TEST(e, "preview", "renders_blocks");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        const std::string md =
            "# Title\n\nSome **bold** text with a [link](https://example.com).\n\n"
            "## Second\n\n```cpp\nint x;\n```\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n- [ ] task\n- [x] done\n";
        OpenDocument& od = freshDoc(ctx, md, ViewMode::Preview);
        ctx->Yield(2);
        const ui::md::Result& r = g_ctx->ui.lastPreviewResult();
        IM_CHECK_EQ((int)r.headings.size(), 2);
        IM_CHECK_STR_EQ(r.headings[0].text.c_str(), "Title");
        IM_CHECK_EQ(r.codeBlocksDrawn, 1);
        IM_CHECK_EQ(r.tablesDrawn, 1);
        IM_CHECK_EQ((int)r.lastTableColumnWidths.size(), 2);
        (void)od;
        closeAll(ctx);
    };

    t = IM_REGISTER_TEST(e, "toolbar", "mode_switch_buttons");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        OpenDocument& od = freshDoc(ctx, "# Hi\n", ViewMode::Preview);
        ImGuiTestItemInfo toolbar = ctx->WindowInfo("//###davedown_host/##toolbar");
        IM_CHECK(toolbar.Window != nullptr);
        ctx->SetRef(toolbar.Window);
        ctx->ItemClick("mode/1/item");
        ctx->Yield(2);
        IM_CHECK(od.mode == ViewMode::Source);
        ctx->ItemClick("mode/2/item");
        ctx->Yield(2);
        IM_CHECK(od.mode == ViewMode::Edit);
        ctx->ItemClick("mode/0/item");
        ctx->Yield(2);
        IM_CHECK(od.mode == ViewMode::Preview);
        // Keyboard route as well.
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_2);
        ctx->Yield(2);
        IM_CHECK(od.mode == ViewMode::Source);
        closeAll(ctx);
    };

    t = IM_REGISTER_TEST(e, "source", "typing_edits_and_undo");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        OpenDocument& od = freshDoc(ctx, "", ViewMode::Source);
        g_ctx->ui.focusEditorForTests(od);
        ctx->Yield(3);
        ctx->KeyChars("# Hello");
        ctx->Yield(2);
        IM_CHECK_STR_EQ(od.doc.text.c_str(), "# Hello");
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->KeyChars("- item");
        ctx->KeyPress(ImGuiKey_Enter);          // continues the list marker
        ctx->KeyChars("two");
        ctx->Yield(2);
        IM_CHECK_STR_EQ(od.doc.text.c_str(), "# Hello\n\n- item\n- two");
        IM_CHECK(od.doc.isDirty());
        // Selection + bold shortcut.
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_LeftArrow);   // select the word
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_B);
        ctx->Yield(2);
        IM_CHECK(od.doc.text.find("- **two**") != std::string::npos);
        // Undo restores; redo reapplies.
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        ctx->Yield(2);
        IM_CHECK(od.doc.text.find("**two**") == std::string::npos);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
        ctx->Yield(2);
        IM_CHECK(od.doc.text.find("**two**") != std::string::npos);
        closeAll(ctx);
    };

    // SDL3 delivers character events only after SDL_StartTextInput(), which
    // the backend calls when ImGui reports io.WantTextInput. KeyChars() in
    // these tests bypasses SDL, so this checks the request itself.
    t = IM_REGISTER_TEST(e, "edit", "focused_block_wants_text_input");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        OpenDocument& od = freshDoc(ctx, "Hello", ViewMode::Edit);
        ctx->Yield(2);
        IM_CHECK(!ImGui::GetIO().WantTextInput);
        g_ctx->workspace.ensureModel(od);
        od.editingBlock = 0;
        od.editingCaret = -1;
        ctx->Yield(3);
        IM_CHECK(ImGui::GetIO().WantTextInput);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        IM_CHECK(!ImGui::GetIO().WantTextInput);
        closeAll(ctx);
    };

    t = IM_REGISTER_TEST(e, "source", "focused_editor_wants_text_input");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        OpenDocument& od = freshDoc(ctx, "", ViewMode::Source);
        g_ctx->ui.focusEditorForTests(od);
        ctx->Yield(3);
        IM_CHECK(ImGui::GetIO().WantTextInput);
        closeAll(ctx);
    };

    t = IM_REGISTER_TEST(e, "edit", "wysiwyg_blocks");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        OpenDocument& od = freshDoc(ctx, "# Title\n\nHello world\n", ViewMode::Edit);
        g_ctx->workspace.ensureModel(od);
        IM_CHECK_EQ((int)od.model.blocks.size(), 2);
        od.editingBlock = 1;
        od.editingCaret = -1;   // end of block
        ctx->Yield(3);
        ctx->KeyChars(" there");
        ctx->Yield(2);
        IM_CHECK_STR_EQ(od.doc.text.c_str(), "# Title\n\nHello world there\n");
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(2);
        IM_CHECK_EQ((int)od.model.blocks.size(), 3);
        IM_CHECK_EQ(od.editingBlock, 2);
        ctx->KeyChars("## Sub");
        ctx->Yield(2);
        IM_CHECK(od.model.blocks[2].type == BlockType::Heading);
        IM_CHECK_EQ(od.model.blocks[2].level, 2);
        IM_CHECK_STR_EQ(od.doc.text.c_str(), "# Title\n\nHello world there\n\n## Sub\n");
        // Enter after a heading makes a paragraph; typing "- " makes a list.
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->KeyChars("- first");
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->KeyChars("second");
        ctx->Yield(2);
        IM_CHECK(od.model.blocks[3].type == BlockType::Bullet);
        IM_CHECK(od.model.blocks[4].type == BlockType::Bullet);
        IM_CHECK_STR_EQ(od.model.blocks[4].text.c_str(), "second");
        // Backspace at the start of an item first strips the bullet, then merges.
        ctx->KeyPress(ImGuiKey_Home);
        ctx->KeyPress(ImGuiKey_Backspace);
        ctx->Yield(2);
        IM_CHECK(od.model.blocks[4].type == BlockType::Paragraph);
        ctx->KeyPress(ImGuiKey_Backspace);
        ctx->Yield(2);
        IM_CHECK_EQ((int)od.model.blocks.size(), 4);
        IM_CHECK_STR_EQ(od.model.blocks[3].text.c_str(), "first second");
        // Escape leaves editing; undo works at document level.
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(2);
        IM_CHECK_EQ(od.editingBlock, -1);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        ctx->Yield(2);
        IM_CHECK(od.doc.text.find("- second") != std::string::npos || od.doc.text.find("\nsecond") != std::string::npos);
        closeAll(ctx);
    };

    t = IM_REGISTER_TEST(e, "edit", "toolbar_formatting_buttons");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        OpenDocument& od = freshDoc(ctx, "plain text\n", ViewMode::Edit);
        g_ctx->workspace.ensureModel(od);
        od.editingBlock = 0;
        od.editingCaret = 0;
        ctx->Yield(3);
        ImGuiTestItemInfo toolbar = ctx->WindowInfo("//###davedown_host/##toolbar");
        IM_CHECK(toolbar.Window != nullptr);
        ctx->SetRef(toolbar.Window);
        ctx->ItemClick("###h2");
        ctx->Yield(2);
        IM_CHECK(od.model.blocks[0].type == BlockType::Heading);
        IM_CHECK_STR_EQ(od.doc.text.c_str(), "## plain text\n");
        ctx->ItemClick("###quote");
        ctx->Yield(2);
        IM_CHECK_STR_EQ(od.doc.text.c_str(), "> plain text\n");
        ctx->ItemClick("###task");
        ctx->Yield(2);
        IM_CHECK_STR_EQ(od.doc.text.c_str(), "- [ ] plain text\n");
        closeAll(ctx);
    };

    t = IM_REGISTER_TEST(e, "modal", "about_is_modal");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        IM_CHECK(!g_ctx->ui.aboutOpen());
        ctx->KeyPress(ImGuiKey_F1);
        ctx->Yield(3);
        IM_CHECK(g_ctx->ui.aboutOpen());
        ImGuiWindow* about = ctx->GetWindowByRef("//About davedown");
        IM_CHECK(about != nullptr);
        IM_CHECK((about->Flags & ImGuiWindowFlags_Modal) != 0);
        ctx->SetRef("//About davedown");
        ctx->ItemClick("OK");
        ctx->Yield(3);
        IM_CHECK(!g_ctx->ui.aboutOpen());
        // Preferences too.
        ImGuiTestItemInfo toolbar = ctx->WindowInfo("//###davedown_host/##toolbar");
        ctx->SetRef(toolbar.Window);
        ctx->ItemClick("###prefs");
        ctx->Yield(3);
        IM_CHECK(g_ctx->ui.preferencesOpen());
        ImGuiWindow* prefs = ctx->GetWindowByRef("//Preferences");
        IM_CHECK(prefs != nullptr && (prefs->Flags & ImGuiWindowFlags_Modal) != 0);
        ctx->SetRef("//Preferences");
        ctx->ItemClick("Done");
        ctx->Yield(2);
        IM_CHECK(!g_ctx->ui.preferencesOpen());
    };

    t = IM_REGISTER_TEST(e, "theme", "switch_and_scale");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        Workspace& ws = g_ctx->workspace;
        const int light = ui::themeIndexByName("Light");
        IM_CHECK(light > 0);
        ui::requestTheme(light);
        ctx->Yield(2);
        IM_CHECK_EQ(ui::currentThemeIndex(), light);
        const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        IM_CHECK(ImGui::ColorConvertFloat4ToU32(bg) == ui::themeAt(light).background);
        // Scaling changes both metrics and fonts and is idempotent.
        const float pad = ImGui::GetStyle().FramePadding.x;
        ws.settings.uiScale = 1.5f;
        ctx->Yield(2);
        IM_CHECK(ImGui::GetStyle().FontScaleMain > 1.49f);
        IM_CHECK(ImGui::GetStyle().FramePadding.x > pad);
        const unsigned applies = ui::themeApplyCountForTests();
        ctx->Yield(3);
        IM_CHECK_EQ(ui::themeApplyCountForTests(), applies);   // no re-apply when nothing changed
        ws.settings.uiScale = 1.0f;
        ui::requestTheme(0);
        ctx->Yield(2);
        IM_CHECK_EQ(ui::currentThemeIndex(), 0);
    };

    t = IM_REGISTER_TEST(e, "find", "search_highlights");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        OpenDocument& od = freshDoc(ctx, "foo bar\n\nFoo again and foo\n\n```\nfoo in code\n```\n", ViewMode::Preview);
        od.showFind = true;
        od.search = "foo";
        ctx->Yield(3);
        IM_CHECK_EQ(g_ctx->ui.lastPreviewResult().matchCount, 4);
        ctx->KeyPress(ImGuiKey_F3);
        ctx->Yield(2);
        IM_CHECK_EQ(od.searchIndex, 1);
        closeAll(ctx);
    };

    t = IM_REGISTER_TEST(e, "file", "open_edit_save_roundtrip");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        Workspace& ws = g_ctx->workspace;
        const std::string path = tempPath("davedown_e2e_roundtrip.md");
        { std::ofstream f(path, std::ios::binary); f << "# Disk\r\n\r\nline\r\n"; }
        std::string err;
        IM_CHECK(ws.openFile(path, err));
        ctx->Yield(2);
        OpenDocument* od = ws.active();
        IM_CHECK(od != nullptr);
        IM_CHECK_STR_EQ(od->doc.text.c_str(), "# Disk\n\nline\n");
        IM_CHECK(!od->doc.isDirty());
        g_ctx->ui.setMode(*od, ViewMode::Source);
        g_ctx->ui.focusEditorForTests(*od);
        ctx->Yield(3);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_End);
        ctx->KeyChars("more");
        ctx->Yield(2);
        IM_CHECK(od->doc.isDirty());
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_S);
        ctx->Yield(2);
        IM_CHECK(!od->doc.isDirty());
        std::ifstream in(path, std::ios::binary);
        std::string back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        IM_CHECK_STR_EQ(back.c_str(), "# Disk\n\nline\nmore");
        IM_CHECK(!ws.settings.recentFiles.empty() && ws.settings.recentFiles.front() == path);
        closeAll(ctx);
        std::error_code ec;
        std::filesystem::remove(path, ec);
    };

    t = IM_REGISTER_TEST(e, "file", "close_dirty_prompts");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        Workspace& ws = g_ctx->workspace;
        OpenDocument& od = freshDoc(ctx, "x", ViewMode::Preview);
        od.doc.text = "changed";
        const int id = od.id;
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_W);
        ctx->Yield(3);
        ImGuiWindow* prompt = ctx->GetWindowByRef("//Unsaved changes");
        IM_CHECK(prompt != nullptr && (prompt->Flags & ImGuiWindowFlags_Modal) != 0);
        IM_CHECK(ws.byId(id) != nullptr);
        ctx->SetRef("//Unsaved changes");
        ctx->ItemClick("Don't save");
        ctx->Yield(3);
        IM_CHECK(ws.byId(id) == nullptr);
        closeAll(ctx);
    };

    t = IM_REGISTER_TEST(e, "outline", "heading_navigation");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        std::string md = "# A\n\n";
        for (int i = 0; i < 80; ++i) md += "Filler paragraph number " + std::to_string(i) + ".\n\n";
        md += "# Target\n\n";
        for (int i = 0; i < 80; ++i) md += "Tail paragraph number " + std::to_string(i) + ".\n\n";
        OpenDocument& od = freshDoc(ctx, md, ViewMode::Preview);
        ctx->Yield(2);
        ctx->SetRef("//Outline");
        ctx->ItemClick("Target##h1");
        ctx->Yield(3);
        const ui::md::Result& r = g_ctx->ui.lastPreviewResult();
        IM_CHECK_EQ((int)r.headings.size(), 2);
        // After the scroll, "Target" sits near the top of the preview window.
        ImGuiWindow* win = ctx->GetWindowByRef(("//" + docWindow(od)).c_str());
        IM_CHECK(win != nullptr);
        IM_CHECK(r.headings[1].screenY >= win->Pos.y);
        IM_CHECK(r.headings[1].screenY < win->Pos.y + win->Size.y * 0.4f);
        // The first heading scrolled off the top.
        IM_CHECK(r.headings[0].screenY < win->Pos.y);
        closeAll(ctx);
    };
}

} // namespace dd::e2e
