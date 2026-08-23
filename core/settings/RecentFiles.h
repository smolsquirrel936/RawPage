// The recent-documents list, with pinning.
//
// ---------------------------------------------------------------------------------------------
// WHY IT TAKES A PATH AND NOT A `DocumentId`.
//
// [`Implementation-Plan.md`](../../claude-plan/Implementation-Plan.md) sketches `void
// note(DocumentId)`. `DocumentId` arrives with `core/doc/` at Stage 2, and Stage 1 has no document
// of any kind (D1-2) -- so the choice is between inventing a Stage 1 `DocumentId` that Stage 2 would
// immediately replace, and keying on the thing that actually identifies an entry in a recent list.
// A recent list is a list of PATHS: it must survive the document being closed, the process
// exiting, and the file being opened again as a different `Document` object. Stage 2 adds a
// `note(const Document&)` overload that resolves to the same path; it does not change this key.
//
// ---------------------------------------------------------------------------------------------
// THE TWO SEMANTICS WORTH STATING BEFORE ANYONE RELIES ON THE OPPOSITE ONE:
//
//   * A PINNED ENTRY IS NOT SUBJECT TO EVICTION and does not count against
//     `documents/recentCount`. A pin is a statement that this document matters; silently dropping
//     it once ten others were opened would make pinning worthless exactly for the user who uses
//     the product most.
//   * `clear()` CLEARS THE HISTORY AND KEEPS THE PINS. The clear-list action of Features.md 9.2 is
//     about the history; a pin is a bookmark the user placed deliberately. `clearIncludingPins()`
//     is the other one, and a Preferences page that means "forget everything" must call it by name
//     rather than hoping `clear()` does it.

#ifndef RAWPAGE_CORE_SETTINGS_RECENTFILES_H
#define RAWPAGE_CORE_SETTINGS_RECENTFILES_H

#include "core/Result.h"
#include "core/settings/Settings.h"

#include <QString>

#include <vector>

namespace rawpage::core {

class RecentFiles {
public:
    struct Entry {
        QString path;
        bool    pinned = false;
    };

    // Reads its state from `settings` immediately and writes back through it on every change, so a
    // crash between two opens does not lose the list. `settings.sync()` is still the thing that
    // reaches the disk -- this class never touches the filesystem itself.
    explicit RecentFiles(Settings& settings);

    // Most-recent first, pinned entries first within that. Idempotent: noting a path already in the
    // list moves it to the front rather than duplicating it, and noting a PINNED path leaves it
    // pinned.
    void note(const QString& path);

    Result<void> pin(const QString& path);
    void unpin(const QString& path);
    bool isPinned(const QString& path) const;

    void remove(const QString& path);

    void clear();                 // history only; pins survive
    void clearIncludingPins();    // the "forget everything" action, called by name

    // Pinned entries first (in pin order), then the history, most recent first.
    std::vector<Entry> entries() const;

    // How many unpinned entries are kept -- `documents/recentCount`, read live so that lowering it
    // in Preferences takes effect without a restart.
    int capacity() const;

    std::size_t size() const { return pinned_.size() + history_.size(); }

private:
    void evict();
    void persist();

    Settings&            settings_;
    std::vector<QString> pinned_;    // in the order they were pinned
    std::vector<QString> history_;   // most recent first, never contains a pinned path
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_SETTINGS_RECENTFILES_H
