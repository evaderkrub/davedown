// A markdown document: the text, where it came from, and whether it has
// unsaved changes. No UI, no SDL -- plain data plus file IO through the
// standard library, so it can be exercised by the console tests.
#pragma once

#include <string>

namespace dd {

struct Document {
    std::string path;       // empty = never saved ("Untitled")
    std::string text;       // current contents, '\n' line endings
    std::string savedText;  // contents as last loaded/saved; dirty = text != savedText

    bool isDirty() const { return text != savedText; }
    bool isUntitled() const { return path.empty(); }

    // File name without directory, or "Untitled" when unsaved.
    std::string title() const;
    // Directory the document lives in (for relative image paths); empty if untitled.
    std::string directory() const;
};

// Loads `path` into `out`. Converts CRLF to LF so every editor sees one
// convention; the original convention is not preserved on save (LF is
// written), which is what every modern markdown tool does.
bool loadDocument(const std::string& path, Document& out, std::string& error);

// Writes doc.text to doc.path. Fails (with a message) when the document has
// no path yet; callers use saveDocumentAs for that.
bool saveDocument(Document& doc, std::string& error);
bool saveDocumentAs(Document& doc, const std::string& path, std::string& error);

std::string normalizeNewlines(std::string text);

// File name component of a path ("C:/a/b.md" -> "b.md"). Handles both separators.
std::string fileNameOf(const std::string& path);
std::string directoryOf(const std::string& path);

} // namespace dd
