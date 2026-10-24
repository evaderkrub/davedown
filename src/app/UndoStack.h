// Whole-text undo history for one document. Snapshots are cheap at the
// sizes markdown files reach, and a snapshot model makes the two editors
// (source and WYSIWYG) share one history without either knowing about the
// other's edit operations.
#pragma once

#include <string>
#include <vector>

namespace dd {

class UndoStack {
public:
    // Records `before` as the state to return to. Edits closer together than
    // `coalesceSeconds` replace the pending snapshot instead of adding one, so
    // a burst of typing undoes as a unit. `kind` separates bursts: typing in
    // block 3 then block 4 yields two entries.
    void record(const std::string& before, double now, int kind = 0, double coalesceSeconds = 0.8)
    {
        future_.clear();
        if (!past_.empty() && kind == lastKind_ && now - lastTime_ < coalesceSeconds) {
            lastTime_ = now;
            return;   // keep the earlier "before"; this edit joins the burst
        }
        past_.push_back(before);
        if (past_.size() > kMaxDepth) past_.erase(past_.begin());
        lastKind_ = kind;
        lastTime_ = now;
    }

    bool canUndo() const { return !past_.empty(); }
    bool canRedo() const { return !future_.empty(); }

    // Swaps `current` with the previous snapshot. Returns false if none.
    bool undo(std::string& current)
    {
        if (past_.empty()) return false;
        future_.push_back(current);
        current = std::move(past_.back());
        past_.pop_back();
        lastTime_ = -1e9;   // the next edit starts a fresh burst
        return true;
    }

    bool redo(std::string& current)
    {
        if (future_.empty()) return false;
        past_.push_back(current);
        current = std::move(future_.back());
        future_.pop_back();
        lastTime_ = -1e9;
        return true;
    }

    void clear() { past_.clear(); future_.clear(); }

private:
    static constexpr size_t kMaxDepth = 200;
    std::vector<std::string> past_;
    std::vector<std::string> future_;
    int    lastKind_ = 0;
    double lastTime_ = -1e9;
};

} // namespace dd
