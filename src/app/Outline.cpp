#include "app/Outline.h"

#include <cctype>

namespace dd {

std::vector<OutlineEntry> buildOutline(std::string_view text)
{
    std::vector<OutlineEntry> out;
    bool inFence = false;
    char fenceChar = 0;
    int lineNo = 0;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string_view line = text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);

        std::string_view s = line;
        while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
        if (s.size() >= 3 && (s[0] == '`' || s[0] == '~') && s[1] == s[0] && s[2] == s[0]) {
            if (!inFence) { inFence = true; fenceChar = s[0]; }
            else if (s[0] == fenceChar) inFence = false;
        } else if (!inFence) {
            size_t n = 0;
            while (n < s.size() && s[n] == '#') ++n;
            if (n >= 1 && n <= 6 && n < s.size() && (s[n] == ' ' || s[n] == '\t')) {
                std::string_view body = s.substr(n);
                while (!body.empty() && (body.front() == ' ' || body.front() == '\t')) body.remove_prefix(1);
                while (!body.empty() && (body.back() == ' ' || body.back() == '#')) body.remove_suffix(1);
                if (!body.empty()) out.push_back({(int)n, std::string(body), lineNo});
            }
        }
        if (nl == std::string_view::npos) break;
        start = nl + 1;
        ++lineNo;
    }
    return out;
}

DocumentStats computeStats(std::string_view text)
{
    DocumentStats st;
    if (text.empty()) return st;
    bool inWord = false;
    st.lines = 1;
    for (unsigned char c : text) {
        if ((c & 0xC0) != 0x80) ++st.characters;   // count UTF-8 lead bytes only
        if (c == '\n') ++st.lines;
        const bool space = std::isspace(c) != 0;
        if (!space && !inWord) { inWord = true; ++st.words; }
        else if (space) inWord = false;
    }
    st.readingMinutes = st.words == 0 ? 0 : (st.words + 219) / 220;
    return st;
}

int lineOfOffset(std::string_view text, size_t offset)
{
    int line = 0;
    const size_t end = offset < text.size() ? offset : text.size();
    for (size_t i = 0; i < end; ++i)
        if (text[i] == '\n') ++line;
    return line;
}

} // namespace dd
