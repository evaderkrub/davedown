// Heading outline and document statistics, derived from the text.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace dd {

struct OutlineEntry {
    int         level = 1;  // 1..6
    std::string text;       // heading text without the '#' marks
    int         line = 0;   // 0-based source line
};

// Headings in document order. Lines inside fenced code blocks are skipped
// so a "# comment" in a shell snippet does not appear as a chapter.
std::vector<OutlineEntry> buildOutline(std::string_view text);

struct DocumentStats {
    int words = 0;
    int characters = 0;     // UTF-8 code points, not bytes
    int lines = 0;
    int readingMinutes = 0; // at ~220 words per minute, rounded up, min 1 when non-empty
};

DocumentStats computeStats(std::string_view text);

// 0-based line number containing byte offset `offset`.
int lineOfOffset(std::string_view text, size_t offset);

} // namespace dd
