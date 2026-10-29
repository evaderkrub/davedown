// The whole interface: menu bar, toolbar, dockspace with one window per
// document plus the outline, status bar, and every modal. It reads the
// Workspace and asks it for changes; the only state it owns is UI-only
// (editor carets, undo history, which modal is open).
#pragma once

#include "app/UndoStack.h"
#include "app/Workspace.h"
#include "ui/MarkdownView.h"
#include "ui/SourceEditor.h"
#include "ui/WysiwygEditor.h"

#include <map>
#include <memory>
#include <string>

namespace dd::ui {

struct UiFrame {
    void*  nativeWindow = nullptr;   // SDL_Window*, for file dialogs; may be null (tests)
    float  dpiScale = 1.0f;
};

class MainWindow {
public:
    explicit MainWindow(Workspace& ws);

    // Loads preferences from disk, applies the theme, opens the welcome
    // document when nothing else is open. Call once after fonts exist.
    void initialize(const std::string& welcomeText);

    // Apply pending theme/scale changes. Call right before ImGui::NewFrame().
    void pumpAppearance(float dpiScale);

    // Draw everything. Call between NewFrame and Render.
    void draw(const UiFrame& frame);

    // Quit protocol: the platform asks, the UI confirms once unsaved work is
    // dealt with (or there was none).
    void requestQuit();
    bool quitConfirmed() const { return quitConfirmed_; }

    void openFileFromOutside(const std::string& path);   // drag & drop, command line
    void saveSettings();

    // Test hooks.
    bool  aboutOpen() const { return aboutOpen_; }
    bool  preferencesOpen() const { return prefsOpen_; }
    void  setMode(OpenDocument& od, ViewMode mode);
    const std::string& lastError() const { return errorText_; }
    const md::Result& lastPreviewResult() const { return lastPreview_; }
    bool  canUndo(const OpenDocument& od) const;
    void  focusEditorForTests(OpenDocument& od) { uiFor(od).focusEditor = true; }
    md::Selection& previewSelectionForTests(OpenDocument& od) { return uiFor(od).previewSelection; }

private:
    struct DocUi {
        SourceEditorState source;
        WysiwygState      wysiwyg;
        UndoStack         undo;
        int               searchMatchCount = 0;
        bool              focusSearch = false;
        bool              focusEditor = false;
        float             previewScrollRestore = -1.0f;
        md::Selection     previewSelection;   // mouse selection in Preview mode
    };

    DocUi& uiFor(const OpenDocument& od);
    void drawMenuBar(const UiFrame& frame);
    void drawToolbar(const UiFrame& frame);
    void drawStatusBar();
    void drawOutline();
    void drawDocumentWindow(OpenDocument& od, const UiFrame& frame, unsigned dockId);
    void drawDocumentBody(OpenDocument& od, DocUi& ui);
    void drawFindBar(OpenDocument& od, DocUi& ui);
    void drawModals(const UiFrame& frame);
    void handleShortcuts(const UiFrame& frame);
    void handleDialogResults();
    void handleLink(OpenDocument& od, const std::string& href);
    void applyFormat(OpenDocument& od, BlockCommand cmd);

    void actionNew();
    void actionOpen(const UiFrame& frame);
    void actionSave(OpenDocument& od, const UiFrame& frame, bool closeAfter = false);
    void actionSaveAs(OpenDocument& od, const UiFrame& frame, bool closeAfter = false);
    void actionClose(OpenDocument& od);
    void actionUndo(OpenDocument& od);
    void actionRedo(OpenDocument& od);
    void setZoom(float scale);
    void showError(std::string text);
    void status(std::string text);

    Workspace& ws_;
    std::map<int, std::unique_ptr<DocUi>> docUi_;
    md::Result lastPreview_;
    std::string pendingOpenDir_;

    bool quitConfirmed_ = false;
    bool quitPending_ = false;
    bool aboutOpen_ = false;
    bool prefsOpen_ = false;
    bool errorOpen_ = false;
    bool closePromptOpen_ = false;
    bool quitPromptOpen_ = false;
    bool outlineDockBuilt_ = false;
    int  closePromptDocId_ = -1;
    int  closeAfterSaveDocId_ = -1;
    std::string errorText_;
    void* lastNativeWindow_ = nullptr;
    float toolbarRightWidth_ = 0.0f;   // measured each frame, used the next
};

// Slug for "#heading-links", GitHub style.
std::string headingSlug(std::string_view heading);

} // namespace dd::ui
