#include "app/Workspace.h"

#include <algorithm>

namespace dd {

OpenDocument* Workspace::active() { return byId(activeId); }

OpenDocument* Workspace::byId(int id)
{
    for (auto& d : docs)
        if (d->id == id) return d.get();
    return nullptr;
}

OpenDocument* Workspace::byPath(const std::string& path)
{
    if (path.empty()) return nullptr;
    for (auto& d : docs)
        if (d->doc.path == path) return d.get();
    return nullptr;
}

OpenDocument& Workspace::newDocument(const std::string& initialText)
{
    auto od = std::make_unique<OpenDocument>();
    od->id = nextId++;
    od->doc.text = initialText;
    od->doc.savedText = initialText;
    od->mode = settings.defaultMode;
    od->modelStale = true;
    OpenDocument& ref = *od;
    docs.push_back(std::move(od));
    activeId = ref.id;
    return ref;
}

bool Workspace::openFile(const std::string& path, std::string& error)
{
    if (OpenDocument* existing = byPath(path)) {
        activeId = existing->id;
        addRecentFile(settings, path);
        return true;
    }
    Document loaded;
    if (!loadDocument(path, loaded, error)) return false;

    // An untouched Untitled tab (empty, or the welcome text nobody edited)
    // is replaced rather than left beside the opened file, the way most
    // editors behave on launch.
    OpenDocument* target = nullptr;
    if (docs.size() == 1 && docs[0]->doc.isUntitled() && !docs[0]->doc.isDirty())
        target = docs[0].get();
    if (!target) target = &newDocument();
    target->doc = std::move(loaded);
    target->modelStale = true;
    target->editingBlock = -1;
    activeId = target->id;
    addRecentFile(settings, path);
    return true;
}

bool Workspace::save(OpenDocument& od, std::string& error)
{
    if (!saveDocument(od.doc, error)) return false;
    addRecentFile(settings, od.doc.path);
    return true;
}

bool Workspace::saveAs(OpenDocument& od, const std::string& path, std::string& error)
{
    if (!saveDocumentAs(od.doc, path, error)) return false;
    addRecentFile(settings, od.doc.path);
    return true;
}

void Workspace::close(int id)
{
    auto it = std::find_if(docs.begin(), docs.end(),
                           [&](const std::unique_ptr<OpenDocument>& d) { return d->id == id; });
    if (it == docs.end()) return;
    const size_t index = (size_t)(it - docs.begin());
    docs.erase(it);
    if (activeId == id) {
        if (docs.empty()) activeId = -1;
        else activeId = docs[std::min(index, docs.size() - 1)]->id;
    }
}

bool Workspace::anyDirty() const
{
    for (const auto& d : docs)
        if (d->doc.isDirty()) return true;
    return false;
}

void Workspace::setText(OpenDocument& od, std::string text, bool fromBlockModel)
{
    if (od.doc.text == text) return;
    od.doc.text = std::move(text);
    if (!fromBlockModel) {
        od.modelStale = true;
        od.editingBlock = -1;
    }
}

void Workspace::ensureModel(OpenDocument& od)
{
    if (!od.modelStale) return;
    od.model = MarkdownModel::parse(od.doc.text);
    od.modelStale = false;
    if (od.editingBlock >= (int)od.model.blocks.size()) od.editingBlock = -1;
}

void Workspace::setStatus(std::string message, double now)
{
    statusMessage = std::move(message);
    statusTime = now;
}

} // namespace dd
