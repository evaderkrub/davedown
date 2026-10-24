// Console tests for the application layer: no window, no ImGui.
#include "app/Document.h"
#include "app/MarkdownModel.h"
#include "app/Outline.h"
#include "app/Settings.h"
#include "app/Workspace.h"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) {                                                                \
            ++g_failures;                                                             \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                             \
    } while (0)

#define CHECK_EQ(a, b)                                                                \
    do {                                                                              \
        ++g_checks;                                                                   \
        const auto va = (a);                                                          \
        const auto vb = (b);                                                          \
        if (!(va == vb)) {                                                            \
            ++g_failures;                                                             \
            std::printf("FAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b);        \
            std::printf("   left : [%s]\n   right: [%s]\n", std::string(va).c_str(), \
                        std::string(vb).c_str());                                     \
        }                                                                             \
    } while (0)

using namespace dd;

void testDocumentBasics()
{
    Document d;
    CHECK_EQ(d.title(), std::string("Untitled"));
    CHECK(!d.isDirty());
    d.path = "C:/docs/notes.md";
    CHECK_EQ(d.title(), std::string("notes.md"));
    CHECK_EQ(d.directory(), std::string("C:/docs"));
    d.text = "x";
    CHECK(d.isDirty());
    CHECK_EQ(normalizeNewlines("a\r\nb\rc\n"), std::string("a\nb\nc\n"));
    CHECK_EQ(fileNameOf("a\\b\\c.md"), std::string("c.md"));
}

void testModelRoundTrip()
{
    const std::string src =
        "# Title\n"
        "\n"
        "A paragraph with **bold** and a\n"
        "soft break.\n"
        "\n"
        "## Section\n"
        "\n"
        "- one\n"
        "- two\n"
        "  - nested\n"
        "- [ ] task\n"
        "- [x] done\n"
        "\n"
        "1. first\n"
        "2. second\n"
        "\n"
        "> quoted\n"
        "> lines\n"
        "\n"
        "```cpp\n"
        "int x = 1; // # not a heading\n"
        "```\n"
        "\n"
        "---\n"
        "\n"
        "| a | b |\n"
        "|---|---|\n"
        "| 1 | 2 |\n"
        "\n"
        "<div>raw</div>\n";
    MarkdownModel m = MarkdownModel::parse(src);
    CHECK_EQ(std::to_string(m.blocks.size()), std::string("15"));
    CHECK(m.blocks[0].type == BlockType::Heading && m.blocks[0].level == 1);
    CHECK_EQ(m.blocks[0].text, std::string("Title"));
    CHECK(m.blocks[1].type == BlockType::Paragraph);
    CHECK_EQ(m.blocks[1].text, std::string("A paragraph with **bold** and a\nsoft break."));
    CHECK(m.blocks[3].type == BlockType::Bullet && m.blocks[3].level == 0);
    CHECK(m.blocks[5].type == BlockType::Bullet && m.blocks[5].level == 1);
    CHECK(m.blocks[6].type == BlockType::Task && !m.blocks[6].checked);
    CHECK(m.blocks[7].type == BlockType::Task && m.blocks[7].checked);
    CHECK(m.blocks[8].type == BlockType::Numbered && m.blocks[8].number == 1);
    CHECK(m.blocks[9].type == BlockType::Numbered && m.blocks[9].number == 2);
    CHECK(m.blocks[10].type == BlockType::Quote);
    CHECK_EQ(m.blocks[10].text, std::string("quoted\nlines"));
    CHECK(m.blocks[11].type == BlockType::Code);
    CHECK_EQ(m.blocks[11].lang, std::string("cpp"));
    CHECK_EQ(m.blocks[11].text, std::string("int x = 1; // # not a heading"));
    CHECK(m.blocks[12].type == BlockType::Rule);
    CHECK(m.blocks[13].type == BlockType::Table);
    CHECK(m.blocks[14].type == BlockType::Html);
    CHECK_EQ(m.serialize(), src);
}

void testModelSetextAndEdgeCases()
{
    MarkdownModel m = MarkdownModel::parse("Title\n=====\n\nSub\n---\n\ntext\n");
    CHECK(m.blocks.size() == 3);
    CHECK(m.blocks[0].type == BlockType::Heading && m.blocks[0].level == 1);
    CHECK(m.blocks[1].type == BlockType::Heading && m.blocks[1].level == 2);
    CHECK_EQ(m.serialize(), std::string("# Title\n\n## Sub\n\ntext\n"));

    // Unterminated fence still becomes code; empty input is empty.
    MarkdownModel u = MarkdownModel::parse("```\ncode\n");
    CHECK(u.blocks.size() == 1 && u.blocks[0].type == BlockType::Code);
    CHECK(MarkdownModel::parse("").blocks.empty());
    CHECK(MarkdownModel::parse("\n\n\n").blocks.empty());

    // A fence inside a code body gets a longer fence on output.
    MarkdownModel f;
    Block code;
    code.type = BlockType::Code;
    code.marker = "```";
    code.text = "```\nnested\n```";
    f.blocks.push_back(code);
    const std::string out = f.serialize();
    CHECK(out.rfind("````", 0) == 0);
    MarkdownModel back = MarkdownModel::parse(out);
    CHECK(back.blocks.size() == 1 && back.blocks[0].text == code.text);
}

void testModelEditing()
{
    MarkdownModel m = MarkdownModel::parse("- alpha beta\n- gamma\n");
    size_t next = m.splitBlock(0, 6);
    CHECK(next == 1);
    CHECK(m.blocks.size() == 3);
    CHECK_EQ(m.blocks[0].text, std::string("alpha "));
    CHECK_EQ(m.blocks[1].text, std::string("beta"));
    CHECK(m.blocks[1].type == BlockType::Bullet);

    size_t join = m.mergeWithPrevious(1);
    CHECK(join == 6);
    CHECK(m.blocks.size() == 2);
    CHECK_EQ(m.blocks[0].text, std::string("alpha beta"));

    // Heading split: tail becomes a paragraph.
    MarkdownModel h = MarkdownModel::parse("# Head tail\n");
    h.splitBlock(0, 5);
    CHECK(h.blocks[1].type == BlockType::Paragraph);
    CHECK_EQ(h.blocks[1].text, std::string("tail"));

    // Typed prefixes convert a paragraph.
    MarkdownModel p = MarkdownModel::parse("plain\n");
    p.blocks[0].text = "## Now a heading";
    CHECK(p.applyTypedPrefix(0));
    CHECK(p.blocks[0].type == BlockType::Heading && p.blocks[0].level == 2);
    CHECK_EQ(p.blocks[0].text, std::string("Now a heading"));

    p.blocks[0].type = BlockType::Paragraph;
    p.blocks[0].text = "1. item";
    CHECK(p.applyTypedPrefix(0));
    CHECK(p.blocks[0].type == BlockType::Numbered);
    p.blocks[0].type = BlockType::Paragraph;
    p.blocks[0].text = "- [x] did";
    CHECK(p.applyTypedPrefix(0));
    CHECK(p.blocks[0].type == BlockType::Task && p.blocks[0].checked);
    p.blocks[0].type = BlockType::Paragraph;
    p.blocks[0].text = "no prefix";
    CHECK(!p.applyTypedPrefix(0));

    // Renumbering after a removal.
    MarkdownModel n = MarkdownModel::parse("1. a\n2. b\n3. c\n");
    n.removeBlock(1);
    CHECK(n.blocks[1].number == 2);
    CHECK_EQ(n.serialize(), std::string("1. a\n2. c\n"));

    // setType keeps text.
    n.setType(0, BlockType::Heading, 3);
    CHECK_EQ(blockPrefix(n.blocks[0]), std::string("### "));
    CHECK_EQ(n.blocks[0].text, std::string("a"));

    // Merging into a rule removes the rule.
    MarkdownModel r = MarkdownModel::parse("---\n\ntext\n");
    CHECK(r.mergeWithPrevious(1) == 0);
    CHECK(r.blocks.size() == 1 && r.blocks[0].type == BlockType::Paragraph);
}

void testInlineMarkers()
{
    int s = 6, e = 11;
    std::string t = toggleInlineMarker("hello world", s, e, "**");
    CHECK_EQ(t, std::string("hello **world**"));
    CHECK(s == 8 && e == 13);
    // Toggling the same selection again removes the markers.
    t = toggleInlineMarker(t, s, e, "**");
    CHECK_EQ(t, std::string("hello world"));
    CHECK(s == 6 && e == 11);
    // Selection that includes the markers.
    s = 6; e = 15;
    t = toggleInlineMarker("hello **world**", s, e, "**");
    CHECK_EQ(t, std::string("hello world"));
    // No selection: insert an empty pair with the caret between.
    s = e = 5;
    t = toggleInlineMarker("hello", s, e, "`");
    CHECK_EQ(t, std::string("hello``"));
    CHECK(s == 6 && e == 6);

    s = 0; e = 4;
    t = insertLink("docs here", s, e, "https://x.y");
    CHECK_EQ(t, std::string("[docs](https://x.y) here"));
    CHECK(t.substr((size_t)s, (size_t)(e - s)) == "https://x.y");
}

void testOutlineAndStats()
{
    const std::string src = "# A\n\ntext\n\n```sh\n# not heading\n```\n\n## B ##\n### C\n";
    auto outline = buildOutline(src);
    CHECK(outline.size() == 3);
    CHECK(outline[0].level == 1 && outline[0].text == "A" && outline[0].line == 0);
    CHECK(outline[1].level == 2 && outline[1].text == "B" && outline[1].line == 8);
    CHECK(outline[2].level == 3 && outline[2].text == "C");

    DocumentStats st = computeStats("one two  three\nfour");
    CHECK(st.words == 4);
    CHECK(st.lines == 2);
    CHECK(st.characters == 19);
    CHECK(st.readingMinutes == 1);
    CHECK(computeStats("").words == 0 && computeStats("").readingMinutes == 0);
    CHECK(computeStats("\xC3\xA9").characters == 1);   // one code point, two bytes
    CHECK(lineOfOffset("a\nb\nc", 4) == 2);
}

void testSettings()
{
    Settings s;
    s.themeName = "Light";
    s.uiScale = 1.25f;
    s.defaultMode = ViewMode::Edit;
    s.showOutline = false;
    s.windowWidth = 1000;
    s.windowHeight = 700;
    s.windowMaximized = true;
    addRecentFile(s, "C:/a.md");
    addRecentFile(s, "C:/b.md");
    addRecentFile(s, "C:/a.md");   // moves to front, no duplicate
    CHECK(s.recentFiles.size() == 2 && s.recentFiles[0] == "C:/a.md");

    Settings back = parseSettings(serializeSettings(s));
    CHECK_EQ(back.themeName, std::string("Light"));
    CHECK(back.uiScale > 1.24f && back.uiScale < 1.26f);
    CHECK(back.defaultMode == ViewMode::Edit);
    CHECK(!back.showOutline);
    CHECK(back.windowWidth == 1000 && back.windowHeight == 700 && back.windowMaximized);
    CHECK(back.recentFiles == s.recentFiles);

    // Garbage degrades to defaults.
    Settings g = parseSettings("ui_scale=abc\nwindow_width=-5\ndefault_mode=bogus\nnonsense\n=\n");
    CHECK(g.uiScale == 1.0f);
    CHECK(g.windowWidth == 400);
    CHECK(g.defaultMode == ViewMode::Preview);
    CHECK(clampUiScale(0.1f) == 0.7f && clampUiScale(9.0f) == 2.0f && clampUiScale(-1.0f) == 1.0f);

    for (int i = 0; i < 20; ++i) addRecentFile(s, "f" + std::to_string(i));
    CHECK(s.recentFiles.size() == Settings::kMaxRecent);
}

void testWorkspace()
{
    Workspace ws;
    OpenDocument& a = ws.newDocument();
    CHECK(ws.active() == &a);
    CHECK(a.doc.isUntitled());
    ws.setText(a, "# Hi\n", false);
    CHECK(a.modelStale);
    ws.ensureModel(a);
    CHECK(!a.modelStale && a.model.blocks.size() == 1);
    ws.setText(a, "# Hi\n\nmore\n", true);
    CHECK(!a.modelStale);   // the block editor produced it
    CHECK(a.doc.isDirty());

    OpenDocument& b = ws.newDocument("x");
    CHECK(ws.active() == &b);
    const int bId = b.id;
    ws.close(bId);
    CHECK(ws.active() == &a);
    CHECK(ws.anyDirty());

    std::string err;
    CHECK(!ws.openFile("Z:/definitely/not/here.md", err));
    CHECK(!err.empty());
    CHECK(ws.docs.size() == 1);
}

} // namespace

int main()
{
    testDocumentBasics();
    testModelRoundTrip();
    testModelSetextAndEdgeCases();
    testModelEditing();
    testInlineMarkers();
    testOutlineAndStats();
    testSettings();
    testWorkspace();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
