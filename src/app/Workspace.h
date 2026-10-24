// Application state: the set of open documents, which one is active, and
// the preferences. The UI reads this and asks for changes through the
// methods here; it never owns document state itself.
#pragma once

#include "app/Document.h"
#include "app/MarkdownModel.h"
#include "app/Settings.h"

#include <memory>
#include <string>
#include <vector>

namespace dd {

struct OpenDocument {
    int           id = 0;             // stable identity for UI windows
    Document      doc;
    ViewMode      mode = ViewMode::Preview;

    // WYSIWYG editing works on the block model; it is rebuilt from doc.text
    // whenever the text changed outside the editor (source view, reload).
    MarkdownModel model;
    bool          modelStale = true;
    int           editingBlock = -1;  // block with the caret, -1 = none
    int           editingCaret = -1;  // requested caret position within that block, -1 = end

    // Find bar.
    std::string   search;
    int           searchIndex = 0;
    bool          showFind = false;

    // One-shot request from the outline: scroll the view to this heading.
    std::string   scrollToHeading;
    int           scrollToLine = -1;

    bool          closeRequested = false;
};

struct Workspace {
    std::vector<std::unique_ptr<OpenDocument>> docs;
    int      activeId = -1;
    int      nextId = 1;
    Settings settings;

    // Transient one-line message for the status bar and when it was set.
    std::string statusMessage;
    double      statusTime = 0.0;

    OpenDocument* active();
    OpenDocument* byId(int id);
    OpenDocument* byPath(const std::string& path);

    OpenDocument& newDocument(const std::string& initialText = std::string());

    // Opens `path`, or activates it if it is already open. On failure
    // returns false and leaves `error` set; nothing is added.
    bool openFile(const std::string& path, std::string& error);

    // Saves to its current path; false with `error` if it has none or IO failed.
    bool save(OpenDocument& od, std::string& error);
    bool saveAs(OpenDocument& od, const std::string& path, std::string& error);

    void close(int id);
    bool anyDirty() const;

    // Replaces the text from an editor and marks the block model stale (or
    // not, when the WYSIWYG editor itself produced the text).
    void setText(OpenDocument& od, std::string text, bool fromBlockModel);
    void ensureModel(OpenDocument& od);

    void setStatus(std::string message, double now);
};

} // namespace dd
