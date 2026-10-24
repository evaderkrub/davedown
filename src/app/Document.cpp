#include "app/Document.h"

#include <fstream>
#include <sstream>

namespace dd {

std::string fileNameOf(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string directoryOf(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string Document::title() const
{
    return path.empty() ? std::string("Untitled") : fileNameOf(path);
}

std::string Document::directory() const
{
    return directoryOf(path);
}

std::string normalizeNewlines(std::string text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            out.push_back('\n');
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

bool loadDocument(const std::string& path, Document& out, std::string& error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Could not open \"" + path + "\" for reading.";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) {
        error = "Read error on \"" + path + "\".";
        return false;
    }
    std::string text = normalizeNewlines(ss.str());
    // Drop a UTF-8 BOM: ImGui and md4c both treat it as text.
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        text.erase(0, 3);
    out.path = path;
    out.text = text;
    out.savedText = out.text;
    error.clear();
    return true;
}

bool saveDocumentAs(Document& doc, const std::string& path, std::string& error)
{
    if (path.empty()) {
        error = "No file name given.";
        return false;
    }
    std::ofstream outFile(path, std::ios::binary | std::ios::trunc);
    if (!outFile) {
        error = "Could not open \"" + path + "\" for writing.";
        return false;
    }
    outFile.write(doc.text.data(), (std::streamsize)doc.text.size());
    if (!outFile) {
        error = "Write error on \"" + path + "\".";
        return false;
    }
    doc.path = path;
    doc.savedText = doc.text;
    error.clear();
    return true;
}

bool saveDocument(Document& doc, std::string& error)
{
    if (doc.path.empty()) {
        error = "The document has no file name yet.";
        return false;
    }
    return saveDocumentAs(doc, doc.path, error);
}

} // namespace dd
