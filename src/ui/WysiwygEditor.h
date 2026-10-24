// Block-based "live preview" editor. Every block is drawn rendered until it
// is clicked; the clicked block becomes a text field showing its markdown
// (minus the block prefix, which the editor shows as a label), and turns
// back into rendered output when the caret leaves. Enter splits a block,
// Backspace at the start joins it with the previous one, arrow keys cross
// block boundaries, and typing a prefix such as "## " or "- " converts a
// paragraph on the fly.
#pragma once

#include "app/MarkdownModel.h"
#include "ui/MarkdownView.h"
#include "ui/TextArea.h"

#include <string>

namespace dd {
struct OpenDocument;
struct Workspace;
}

namespace dd::ui {

struct WysiwygState {
    TextAreaState area;
    std::string   buffer;         // text of the block being edited
    int           loadedBlock = -1;
    int           hoveredBlock = -1;
    int           pendingCaret = -1;   // caret to place once the block loads (-2 = end)
};

struct WysiwygResult {
    bool        changed = false;     // the document text was replaced
    std::string clickedLink;
    std::string hoveredLink;
    std::string copiedCode;
    int         cursorLine = 0;
    int         cursorColumn = 0;
};

// Draws the editor filling the available region. `images` resolves pictures
// for the rendered blocks.
WysiwygResult drawWysiwygEditor(const char* id, Workspace& ws, OpenDocument& od, WysiwygState& st,
                                const md::ImageProvider& images);

// Toolbar commands the host can issue (from buttons or shortcuts). They act
// on the block being edited; with no block selected they do nothing and
// return false.
enum class BlockCommand {
    Bold, Italic, Strike, Code, Link,
    Heading1, Heading2, Heading3, Paragraph,
    Bullet, Numbered, Task, Quote, CodeBlock, Rule, Image
};
bool applyBlockCommand(Workspace& ws, OpenDocument& od, WysiwygState& st, BlockCommand cmd);

} // namespace dd::ui
