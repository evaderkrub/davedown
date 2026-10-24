// Block-level model of a markdown document for the WYSIWYG editor.
//
// The editor works on blocks (a heading, a paragraph, one list item, a code
// fence...) because that is the unit a person edits: click a block, type,
// move on. The inline markup inside a block stays as markdown text while it
// is being edited and is rendered the moment the block loses focus -- the
// "live preview" approach of Typora/Obsidian, which avoids inventing a
// second rich-text representation that would have to be kept in sync with
// the file.
//
// Parsing is line based and deliberately simpler than a CommonMark parser:
// every block keeps enough of its source (marker characters, fence style,
// raw table text) to serialize back to the same markdown for the forms this
// editor itself produces, and anything it does not understand is preserved
// as a raw block rather than dropped.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace dd {

enum class BlockType {
    Paragraph,
    Heading,    // level 1..6
    Bullet,     // "- ", "* ", "+ "; level = indent depth (0-based)
    Numbered,   // "1. "; number kept for serialization
    Task,       // "- [ ] " / "- [x] "
    Quote,      // "> " lines, joined with '\n'
    Code,       // fenced; text is the body, lang the info string
    Rule,       // ---
    Table,      // raw lines preserved; edited as text
    Html,       // raw HTML block preserved
};

struct Block {
    BlockType   type = BlockType::Paragraph;
    int         level = 0;      // heading level (1-6) or list nesting depth (0-based)
    int         number = 1;     // ordered list item number
    bool        checked = false;
    std::string marker = "-";   // list bullet char, or fence string ("```"/"~~~")
    std::string lang;           // code fence info string
    std::string text;           // content without the block prefix

    bool isListItem() const {
        return type == BlockType::Bullet || type == BlockType::Numbered || type == BlockType::Task;
    }
};

// The markdown prefix a block's first line carries ("## ", "- [ ] ", "> ", ...).
// Empty for paragraphs, code, rules, tables and HTML.
std::string blockPrefix(const Block& block);

struct MarkdownModel {
    std::vector<Block> blocks;

    static MarkdownModel parse(std::string_view text);
    std::string serialize() const;

    // Splits block `index` at character offset `caret` of its text. The tail
    // becomes a new block after it: a list item continues the list (with the
    // next number), a heading/quote tail becomes a paragraph, a code block
    // simply gets a newline. Returns the index of the block the caret should
    // move to (the new one, caret at 0).
    size_t splitBlock(size_t index, size_t caret);

    // Appends block `index` onto the one before it and removes it. Returns the
    // caret offset in the merged block where the join happened, or
    // std::string::npos when there was nothing to merge into (index 0).
    size_t mergeWithPrevious(size_t index);

    void insertBlockAfter(size_t index, Block block);
    void removeBlock(size_t index);

    // Changes a block's kind, preserving its text. Heading level / list depth
    // go in `level`.
    void setType(size_t index, BlockType type, int level = 0);

    // Recognises a prefix typed at the start of a paragraph ("# ", "- ",
    // "1. ", "> ", "- [ ] ", "```") and converts the block in place, removing
    // the typed prefix. Returns true when a conversion happened.
    bool applyTypedPrefix(size_t index);

    // Re-numbers consecutive ordered items so a list reads 1,2,3 after edits.
    void renumberLists();
};

// Toggles an inline marker ("**", "*", "`", "~~") around [selStart, selEnd)
// of `text`. With no selection the marker pair is inserted at the caret. If
// the selection is already wrapped, the markers are removed instead.
// Updates selStart/selEnd to cover the same words afterwards.
std::string toggleInlineMarker(std::string_view text, int& selStart, int& selEnd,
                               std::string_view marker);

// Wraps the selection as a link: "[selection](url)". With no selection
// inserts "[text](url)" and selects the word "text".
std::string insertLink(std::string_view text, int& selStart, int& selEnd,
                       std::string_view url);

} // namespace dd
