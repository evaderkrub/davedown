#include "app/MarkdownModel.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace dd {
namespace {

bool isBlank(std::string_view line)
{
    for (char c : line)
        if (c != ' ' && c != '\t') return false;
    return true;
}

std::string_view trimLeft(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    return s;
}

std::string_view trimRight(std::string_view s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

std::string_view trim(std::string_view s) { return trimRight(trimLeft(s)); }

int leadingSpaces(std::string_view line)
{
    int n = 0;
    for (char c : line) {
        if (c == ' ') n += 1;
        else if (c == '\t') n += 4;
        else break;
    }
    return n;
}

// "```lang" / "~~~": returns the fence string, info string in `lang`.
bool parseFence(std::string_view line, std::string& fence, std::string& lang)
{
    std::string_view s = trimLeft(line);
    if (leadingSpaces(line) > 3) return false;
    if (s.size() < 3) return false;
    const char f = s[0];
    if (f != '`' && f != '~') return false;
    size_t n = 0;
    while (n < s.size() && s[n] == f) ++n;
    if (n < 3) return false;
    fence.assign(n, f);
    lang = std::string(trim(s.substr(n)));
    // A backtick fence's info string may not contain backticks (CommonMark).
    if (f == '`' && lang.find('`') != std::string::npos) return false;
    return true;
}

bool isClosingFence(std::string_view line, const std::string& fence)
{
    std::string_view s = trim(line);
    if (leadingSpaces(line) > 3) return false;
    if (s.size() < fence.size()) return false;
    for (char c : s)
        if (c != fence[0]) return false;
    return true;
}

bool parseHeading(std::string_view line, int& level, std::string& text)
{
    if (leadingSpaces(line) > 3) return false;
    std::string_view s = trimLeft(line);
    size_t n = 0;
    while (n < s.size() && s[n] == '#') ++n;
    if (n == 0 || n > 6) return false;
    if (n < s.size() && s[n] != ' ' && s[n] != '\t') return false;
    std::string_view body = trim(s.substr(n));
    // Optional closing hashes: "## Title ##".
    while (!body.empty() && body.back() == '#') body.remove_suffix(1);
    body = trimRight(body);
    level = (int)n;
    text = std::string(body);
    return true;
}

bool isRule(std::string_view line)
{
    if (leadingSpaces(line) > 3) return false;
    std::string_view s = trim(line);
    if (s.size() < 3) return false;
    const char c = s[0];
    if (c != '-' && c != '*' && c != '_') return false;
    int count = 0;
    for (char ch : s) {
        if (ch == c) ++count;
        else if (ch != ' ') return false;
    }
    return count >= 3;
}

// Setext underline: "===" or "---" (the latter only when the previous line
// is paragraph text, which the caller guarantees).
int setextLevel(std::string_view line)
{
    std::string_view s = trim(line);
    if (s.size() < 1 || leadingSpaces(line) > 3) return 0;
    const char c = s[0];
    if (c != '=' && c != '-') return 0;
    for (char ch : s)
        if (ch != c) return 0;
    return c == '=' ? 1 : 2;
}

struct ListStart {
    bool        ordered = false;
    bool        task = false;
    bool        checked = false;
    int         number = 1;
    int         indent = 0;         // columns before the marker
    int         contentIndent = 0;  // columns where the text begins
    std::string marker = "-";
    std::string text;
};

bool parseListStart(std::string_view line, ListStart& out)
{
    const int indent = leadingSpaces(line);
    std::string_view s = trimLeft(line);
    if (s.empty()) return false;
    size_t markerLen = 0;
    if (s[0] == '-' || s[0] == '*' || s[0] == '+') {
        out.ordered = false;
        out.marker = std::string(1, s[0]);
        markerLen = 1;
    } else if (std::isdigit((unsigned char)s[0])) {
        size_t n = 0;
        while (n < s.size() && n < 9 && std::isdigit((unsigned char)s[n])) ++n;
        if (n >= s.size() || (s[n] != '.' && s[n] != ')')) return false;
        out.ordered = true;
        out.number = std::atoi(std::string(s.substr(0, n)).c_str());
        out.marker = std::string(1, s[n]);
        markerLen = n + 1;
    } else {
        return false;
    }
    // The marker must be followed by a space, or end the line (empty item).
    if (markerLen < s.size() && s[markerLen] != ' ' && s[markerLen] != '\t') return false;
    std::string_view rest = s.substr(markerLen);
    size_t spaces = 0;
    while (spaces < rest.size() && (rest[spaces] == ' ' || rest[spaces] == '\t')) ++spaces;
    rest = rest.substr(spaces);
    out.indent = indent;
    out.contentIndent = indent + (int)markerLen + (int)(spaces == 0 ? 1 : spaces);
    out.task = false;
    if (!out.ordered && rest.size() >= 3 && rest[0] == '[' && rest[2] == ']' &&
        (rest[1] == ' ' || rest[1] == 'x' || rest[1] == 'X') &&
        (rest.size() == 3 || rest[3] == ' ')) {
        out.task = true;
        out.checked = rest[1] != ' ';
        rest = trimLeft(rest.substr(3));
    }
    out.text = std::string(rest);
    return true;
}

bool isQuoteLine(std::string_view line)
{
    return leadingSpaces(line) <= 3 && !trimLeft(line).empty() && trimLeft(line)[0] == '>';
}

std::string_view quoteBody(std::string_view line)
{
    std::string_view s = trimLeft(line).substr(1);
    if (!s.empty() && s[0] == ' ') s.remove_prefix(1);
    return s;
}

bool isTableDelimiterRow(std::string_view line)
{
    std::string_view s = trim(line);
    if (s.find('|') == std::string_view::npos) return false;
    if (s.find('-') == std::string_view::npos) return false;
    for (char c : s)
        if (c != '|' && c != '-' && c != ':' && c != ' ') return false;
    return true;
}

bool isHtmlBlockStart(std::string_view line)
{
    std::string_view s = trimLeft(line);
    if (s.size() < 2 || s[0] != '<') return false;
    return std::isalpha((unsigned char)s[1]) || s[1] == '/' || s[1] == '!';
}

std::vector<std::string_view> splitLines(std::string_view text)
{
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        if (nl == std::string_view::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    // A trailing newline produces an empty last element; it is not a line.
    if (!lines.empty() && lines.back().empty() && !text.empty() && text.back() == '\n')
        lines.pop_back();
    return lines;
}

// A line that would start a new block, so a paragraph cannot continue onto it.
bool startsBlock(std::string_view line)
{
    int lvl; std::string txt, fence, lang; ListStart ls;
    return isBlank(line) || parseHeading(line, lvl, txt) || parseFence(line, fence, lang) ||
           isRule(line) || parseListStart(line, ls) || isQuoteLine(line);
}

std::string indentString(int depth) { return std::string((size_t)depth * 2, ' '); }

} // namespace

std::string blockPrefix(const Block& b)
{
    switch (b.type) {
    case BlockType::Heading:  return std::string((size_t)std::clamp(b.level, 1, 6), '#') + " ";
    case BlockType::Bullet:   return indentString(b.level) + b.marker + " ";
    case BlockType::Task:     return indentString(b.level) + b.marker + (b.checked ? " [x] " : " [ ] ");
    case BlockType::Numbered: return indentString(b.level) + std::to_string(b.number) +
                                     (b.marker == ")" ? ") " : ". ");
    case BlockType::Quote:    return "> ";
    default:                  return std::string();
    }
}

MarkdownModel MarkdownModel::parse(std::string_view text)
{
    MarkdownModel model;
    const auto lines = splitLines(text);
    size_t i = 0;
    while (i < lines.size()) {
        std::string_view line = lines[i];
        if (isBlank(line)) { ++i; continue; }

        std::string fence, lang;
        if (parseFence(line, fence, lang)) {
            Block b;
            b.type = BlockType::Code;
            b.marker = fence;
            b.lang = lang;
            ++i;
            std::string body;
            bool closed = false;
            while (i < lines.size()) {
                if (isClosingFence(lines[i], fence)) { closed = true; ++i; break; }
                if (!body.empty() || !b.text.empty()) body.push_back('\n');
                body.append(lines[i]);
                b.text = body;   // keeps the "first line" guard simple
                ++i;
            }
            b.text = body;
            (void)closed;   // an unterminated fence still becomes a code block
            model.blocks.push_back(std::move(b));
            continue;
        }

        int level = 0;
        std::string headingText;
        if (parseHeading(line, level, headingText)) {
            Block b;
            b.type = BlockType::Heading;
            b.level = level;
            b.text = headingText;
            model.blocks.push_back(std::move(b));
            ++i;
            continue;
        }

        if (isRule(line)) {
            Block b;
            b.type = BlockType::Rule;
            b.text = std::string(trim(line));
            model.blocks.push_back(std::move(b));
            ++i;
            continue;
        }

        ListStart ls;
        if (parseListStart(line, ls)) {
            Block b;
            b.type = ls.task ? BlockType::Task : (ls.ordered ? BlockType::Numbered : BlockType::Bullet);
            b.level = ls.indent / 2;
            b.number = ls.number;
            b.checked = ls.checked;
            b.marker = ls.marker;
            b.text = ls.text;
            ++i;
            // Continuation lines: indented at least to the content column and
            // not themselves a list item / fence / heading.
            while (i < lines.size() && !isBlank(lines[i])) {
                ListStart next;
                std::string f2, l2;
                int lvl2; std::string t2;
                if (parseListStart(lines[i], next) || parseFence(lines[i], f2, l2) ||
                    parseHeading(lines[i], lvl2, t2) || isRule(lines[i]) || isQuoteLine(lines[i]))
                    break;
                b.text.push_back('\n');
                b.text.append(trimLeft(lines[i]));
                ++i;
            }
            model.blocks.push_back(std::move(b));
            continue;
        }

        if (isQuoteLine(line)) {
            Block b;
            b.type = BlockType::Quote;
            b.text = std::string(quoteBody(line));
            ++i;
            while (i < lines.size() && isQuoteLine(lines[i])) {
                b.text.push_back('\n');
                b.text.append(quoteBody(lines[i]));
                ++i;
            }
            model.blocks.push_back(std::move(b));
            continue;
        }

        if (line.find('|') != std::string_view::npos && i + 1 < lines.size() &&
            isTableDelimiterRow(lines[i + 1])) {
            Block b;
            b.type = BlockType::Table;
            b.text = std::string(line);
            ++i;
            while (i < lines.size() && !isBlank(lines[i]) &&
                   lines[i].find('|') != std::string_view::npos) {
                b.text.push_back('\n');
                b.text.append(lines[i]);
                ++i;
            }
            model.blocks.push_back(std::move(b));
            continue;
        }

        if (isHtmlBlockStart(line)) {
            Block b;
            b.type = BlockType::Html;
            b.text = std::string(line);
            ++i;
            while (i < lines.size() && !isBlank(lines[i])) {
                b.text.push_back('\n');
                b.text.append(lines[i]);
                ++i;
            }
            model.blocks.push_back(std::move(b));
            continue;
        }

        // Paragraph: gather until a blank line or something that starts a block.
        Block b;
        b.type = BlockType::Paragraph;
        b.text = std::string(trimRight(line));
        ++i;
        while (i < lines.size() && !startsBlock(lines[i])) {
            const int sx = setextLevel(lines[i]);
            if (sx != 0) {
                b.type = BlockType::Heading;
                b.level = sx;
                ++i;
                break;
            }
            b.text.push_back('\n');
            b.text.append(trim(lines[i]));
            ++i;
        }
        // A "---" directly under a single paragraph line is a setext H2, but
        // startsBlock() treats it as a rule first; handle that case here.
        if (b.type == BlockType::Paragraph && i < lines.size() && setextLevel(lines[i]) != 0 &&
            b.text.find('\n') == std::string::npos && isRule(lines[i]) && lines[i][0] == '-') {
            b.type = BlockType::Heading;
            b.level = 2;
            ++i;
        }
        model.blocks.push_back(std::move(b));
    }
    return model;
}

std::string MarkdownModel::serialize() const
{
    std::string out;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const Block& b = blocks[i];
        if (i > 0) {
            const Block& prev = blocks[i - 1];
            // Tight lists: consecutive items of the same list are separated by
            // one newline. A change of kind (ordered vs bullet) or of marker
            // character starts a new list in CommonMark, so it needs a gap.
            const bool sameKind = (prev.type == BlockType::Numbered) == (b.type == BlockType::Numbered);
            const bool tight = prev.isListItem() && b.isListItem() && sameKind && prev.marker == b.marker;
            out.append(tight ? "\n" : "\n\n");
        }
        switch (b.type) {
        case BlockType::Code: {
            std::string fence = b.marker.empty() ? "```" : b.marker;
            // A body that contains the fence needs a longer one.
            while (b.text.find(fence) != std::string::npos) fence.push_back(fence[0]);
            out += fence + b.lang + "\n" + b.text;
            if (!b.text.empty()) out.push_back('\n');
            out += fence;
            break;
        }
        case BlockType::Rule:
            out += b.text.empty() ? "---" : b.text;
            break;
        case BlockType::Table:
        case BlockType::Html:
        case BlockType::Paragraph:
            out += b.text;
            break;
        case BlockType::Heading:
            out += blockPrefix(b) + b.text;
            break;
        case BlockType::Quote: {
            size_t start = 0;
            bool first = true;
            while (start <= b.text.size()) {
                size_t nl = b.text.find('\n', start);
                std::string_view ln = std::string_view(b.text).substr(
                    start, nl == std::string::npos ? std::string::npos : nl - start);
                if (!first) out.push_back('\n');
                out += ln.empty() ? ">" : "> " + std::string(ln);
                first = false;
                if (nl == std::string::npos) break;
                start = nl + 1;
            }
            break;
        }
        case BlockType::Bullet:
        case BlockType::Task:
        case BlockType::Numbered: {
            const std::string prefix = blockPrefix(b);
            const std::string contIndent(prefix.size(), ' ');
            size_t start = 0;
            bool first = true;
            while (start <= b.text.size()) {
                size_t nl = b.text.find('\n', start);
                std::string_view ln = std::string_view(b.text).substr(
                    start, nl == std::string::npos ? std::string::npos : nl - start);
                if (first) out += prefix + std::string(ln);
                else out += "\n" + contIndent + std::string(ln);
                first = false;
                if (nl == std::string::npos) break;
                start = nl + 1;
            }
            break;
        }
        }
    }
    if (!out.empty()) out.push_back('\n');
    return out;
}

size_t MarkdownModel::splitBlock(size_t index, size_t caret)
{
    if (index >= blocks.size()) return index;
    Block& b = blocks[index];
    caret = std::min(caret, b.text.size());

    if (b.type == BlockType::Code || b.type == BlockType::Table || b.type == BlockType::Html) {
        // These are edited as raw multi-line text; a split is just a newline.
        b.text.insert(caret, 1, '\n');
        return index;
    }

    Block tail;
    tail.text = b.text.substr(caret);
    b.text.erase(caret);

    if (b.isListItem()) {
        tail.type = b.type;
        tail.level = b.level;
        tail.marker = b.marker;
        tail.number = b.number + 1;
        tail.checked = false;
    } else if (b.type == BlockType::Quote) {
        tail.type = BlockType::Quote;
    } else {
        tail.type = BlockType::Paragraph;
    }
    blocks.insert(blocks.begin() + (std::ptrdiff_t)index + 1, std::move(tail));
    renumberLists();
    return index + 1;
}

size_t MarkdownModel::mergeWithPrevious(size_t index)
{
    if (index == 0 || index >= blocks.size()) return std::string::npos;
    Block& prev = blocks[index - 1];
    Block& cur = blocks[index];
    if (prev.type == BlockType::Rule) {
        // Backspacing into a rule removes the rule, keeping the current block.
        blocks.erase(blocks.begin() + (std::ptrdiff_t)index - 1);
        return 0;
    }
    const size_t join = prev.text.size();
    if (!cur.text.empty()) {
        if (prev.type == BlockType::Code || prev.type == BlockType::Quote) prev.text.push_back('\n');
        else if (!prev.text.empty() && prev.text.back() != ' ' && cur.text.front() != ' ') prev.text.push_back(' ');
        prev.text += cur.text;
    }
    blocks.erase(blocks.begin() + (std::ptrdiff_t)index);
    renumberLists();
    return join;
}

void MarkdownModel::insertBlockAfter(size_t index, Block block)
{
    if (blocks.empty()) { blocks.push_back(std::move(block)); return; }
    index = std::min(index, blocks.size() - 1);
    blocks.insert(blocks.begin() + (std::ptrdiff_t)index + 1, std::move(block));
    renumberLists();
}

void MarkdownModel::removeBlock(size_t index)
{
    if (index >= blocks.size()) return;
    blocks.erase(blocks.begin() + (std::ptrdiff_t)index);
    renumberLists();
}

void MarkdownModel::setType(size_t index, BlockType type, int level)
{
    if (index >= blocks.size()) return;
    Block& b = blocks[index];
    const bool wasList = b.isListItem();
    b.type = type;
    switch (type) {
    case BlockType::Heading:  b.level = std::clamp(level, 1, 6); break;
    case BlockType::Bullet:
    case BlockType::Task:
    case BlockType::Numbered:
        b.level = std::max(level, 0);
        if (!wasList) b.marker = "-";
        if (type == BlockType::Numbered && b.marker != ")") b.marker = ".";
        if (type != BlockType::Numbered && (b.marker == "." || b.marker == ")")) b.marker = "-";
        break;
    case BlockType::Code:
        if (b.marker != "```" && b.marker != "~~~") b.marker = "```";
        break;
    default:
        b.level = 0;
        break;
    }
    renumberLists();
}

bool MarkdownModel::applyTypedPrefix(size_t index)
{
    if (index >= blocks.size()) return false;
    Block& b = blocks[index];
    if (b.type != BlockType::Paragraph) return false;
    const std::string_view t = b.text;

    int level; std::string headingText;
    if (parseHeading(t, level, headingText)) {
        b.type = BlockType::Heading;
        b.level = level;
        b.text = headingText;
        return true;
    }
    ListStart ls;
    if (parseListStart(t, ls) && t.find('\n') == std::string_view::npos) {
        b.type = ls.task ? BlockType::Task : (ls.ordered ? BlockType::Numbered : BlockType::Bullet);
        b.level = ls.indent / 2;
        b.number = ls.number;
        b.checked = ls.checked;
        b.marker = ls.marker;
        b.text = ls.text;
        renumberLists();
        return true;
    }
    if (isQuoteLine(t)) {
        b.type = BlockType::Quote;
        b.text = std::string(quoteBody(t));
        return true;
    }
    std::string fence, lang;
    if (parseFence(t, fence, lang)) {
        b.type = BlockType::Code;
        b.marker = fence;
        b.lang = lang;
        b.text.clear();
        return true;
    }
    if (isRule(t)) {
        b.type = BlockType::Rule;
        b.text = std::string(trim(t));
        return true;
    }
    return false;
}

void MarkdownModel::renumberLists()
{
    // Numbering restarts whenever the run of items is broken or the depth
    // drops below the item's depth; nested runs count independently.
    std::vector<int> counters;
    for (size_t i = 0; i < blocks.size(); ++i) {
        Block& b = blocks[i];
        if (!b.isListItem()) { counters.clear(); continue; }
        if ((size_t)b.level >= counters.size()) counters.resize((size_t)b.level + 1, 0);
        // Returning to a shallower level ends the deeper runs.
        counters.resize((size_t)b.level + 1);
        if (b.type == BlockType::Numbered) {
            // The first item of a run keeps its own start number.
            if (counters[(size_t)b.level] == 0) counters[(size_t)b.level] = b.number > 0 ? b.number : 1;
            else counters[(size_t)b.level] += 1;
            b.number = counters[(size_t)b.level];
        } else {
            counters[(size_t)b.level] = 0;
        }
    }
}

std::string toggleInlineMarker(std::string_view text, int& selStart, int& selEnd,
                               std::string_view marker)
{
    if (selStart > selEnd) std::swap(selStart, selEnd);
    selStart = std::clamp(selStart, 0, (int)text.size());
    selEnd = std::clamp(selEnd, 0, (int)text.size());
    const int m = (int)marker.size();
    std::string out(text);

    if (selStart == selEnd) {
        out.insert((size_t)selStart, std::string(marker) + std::string(marker));
        selStart += m;
        selEnd = selStart;
        return out;
    }
    // Already wrapped? Either the selection includes the markers...
    std::string_view sel = text.substr((size_t)selStart, (size_t)(selEnd - selStart));
    if ((int)sel.size() >= 2 * m && sel.substr(0, (size_t)m) == marker &&
        sel.substr(sel.size() - (size_t)m) == marker) {
        out.erase((size_t)selEnd - (size_t)m, (size_t)m);
        out.erase((size_t)selStart, (size_t)m);
        selEnd -= 2 * m;
        return out;
    }
    // ...or sits right between them.
    if (selStart >= m && selEnd + m <= (int)text.size() &&
        text.substr((size_t)(selStart - m), (size_t)m) == marker &&
        text.substr((size_t)selEnd, (size_t)m) == marker) {
        out.erase((size_t)selEnd, (size_t)m);
        out.erase((size_t)(selStart - m), (size_t)m);
        selStart -= m;
        selEnd -= m;
        return out;
    }
    out.insert((size_t)selEnd, marker);
    out.insert((size_t)selStart, marker);
    selStart += m;
    selEnd += m;
    return out;
}

std::string insertLink(std::string_view text, int& selStart, int& selEnd, std::string_view url)
{
    if (selStart > selEnd) std::swap(selStart, selEnd);
    selStart = std::clamp(selStart, 0, (int)text.size());
    selEnd = std::clamp(selEnd, 0, (int)text.size());
    std::string out(text);
    std::string label = selStart == selEnd ? std::string("text")
                                           : std::string(text.substr((size_t)selStart, (size_t)(selEnd - selStart)));
    std::string link = "[" + label + "](" + std::string(url.empty() ? "https://" : url) + ")";
    out.replace((size_t)selStart, (size_t)(selEnd - selStart), link);
    // Select the URL so the person can type over it right away.
    const int urlStart = selStart + 1 + (int)label.size() + 2;
    selEnd = urlStart + (int)(url.empty() ? 8 : url.size());
    selStart = urlStart;
    return out;
}

} // namespace dd
