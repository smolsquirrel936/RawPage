// core/settings/SessionState -- what the application remembers about HOW somebody was reading, so a
// relaunch puts it back. Stage 4 T3; decisions D4-7, D4-8 and D4-9 (`Stage4-ViewingSurface-Plan.md`
// §3).
//
// ---------------------------------------------------------------------------------------------
// THE ONE RULE THIS FILE EXISTS TO ENFORCE: NONE OF THIS IS DOCUMENT STATE
// ---------------------------------------------------------------------------------------------
// A zoom, a scroll position, a split, a pane width, a guide, an open-document set -- every field
// below is the APPLICATION's memory of a reading session, and none of it belongs in the PDF. The
// moment a guide is written into the file, a reader's window layout becomes a document mutation
// that `CommandJournal` never saw (D4-1, D4-7). `ui/` is already forbidden to reach `pdf/` by
// `layer-lint`; this file is where the same rule is expressed as a place to PUT the state instead,
// which is the half a lint cannot supply.
//
// ---------------------------------------------------------------------------------------------
// WINDOW GEOMETRY IS STORED PER MONITOR ARRANGEMENT, AND CLAMPED ON RESTORE (D4-8)
// ---------------------------------------------------------------------------------------------
// A laptop docked to two monitors at the office and undocked on a train produces two arrangements a
// day, and Qt will happily restore a window to x = 3200 on a desktop 1920 wide -- a window nobody
// can reach, which is the S1 this stage most plausibly ships. So:
//
//   * every arrangement gets its own remembered geometry, keyed by `fingerprintOf()`, bounded at
//     `kMaxArrangements` and most-recent-first;
//   * an arrangement we have never seen falls back to the most recent one we have, because the
//     alternative -- no geometry at all -- throws away a layout the user built;
//   * and THEN every window is clamped, because the fallback is a guess. The clamp is what makes a
//     wrong fingerprint a cosmetic error instead of a lost window.
//
// **The fingerprint deliberately excludes `friendlyName`** -- it may be empty, it is not unique
// across two identical panels, and `platform.h` says both (`MonitorInfo`). It also excludes
// `physicalDpi`, which is a property of the glass rather than of the arrangement and cannot change
// without the panel itself changing. What it includes is `id`, position, pixel size, `scaleFactor`
// and `primary`, sorted by `id` so the order `monitors()` happened to return is not part of the
// identity.
//
// ---------------------------------------------------------------------------------------------
// RESTORE REOPENS DOCUMENTS; IT NEVER RESTORES UNSAVED WORK (D4-9)
// ---------------------------------------------------------------------------------------------
// Unsaved work has an owner -- the recovery journal (D3-11), flushed per command and offered after
// a crash -- and two mechanisms that both claim to bring an edit back would eventually both bring
// it back, i.e. twice. A document closed with *Don't Save* comes back CLEAN, at the bytes on disk.
// This class therefore stores a path and a `DocumentId` and no content of any kind, and the shell
// (T13) runs it AFTER `RecoveryPrompt` and skips any path recovery has already opened.
//
// ---------------------------------------------------------------------------------------------
// THE FILE, AND WHY ITS PATH IS A CONSTRUCTOR ARGUMENT
// ---------------------------------------------------------------------------------------------
// One JSON document, `{"format":1,"arrangements":[…],"session":{…}}`, written atomically through
// `QSaveFile`. `core/settings/` is the one part of `core/` permitted to write to the filesystem
// (`layer-lint`'s own note), so this needs no waiver.
//
// The path is passed in rather than resolved here, and that is not only for tidiness: **a test or a
// composition case that used the real location would write, and then reopen, the session of whoever
// ran the suite** -- the trap Stage 3 T11 met with the recovery root, now with a second subject
// (`CLAUDE.md` → *Traps*).
//
// A FILE WE CANNOT READ IS NOT AN ERROR PATH, IT IS THE ORDINARY ONE: a truncated write, a schema
// version from a newer build, an empty file, a directory where the file should be. Each yields an
// empty session, one log line, and a product that starts. The file is replaced on the next clean
// quit.
//
// ---------------------------------------------------------------------------------------------
// WHAT IS ABSENT, AND WHO OWNS IT
// ---------------------------------------------------------------------------------------------
// **Guides are keyed by `DocumentId`, as D4-7 requires, and a `DocumentId` changes when the
// document is saved** -- it is derived from the path, the size, the modification time AND the
// content hash (`core/doc/DocumentId.h`). Raised as **I-01**
// (`Stage4-ViewingSurface-Testing-Guide.md` §8) and owned by D4-7.
//
// **SETTLED AT T13, WHERE CAPTURE AND RESTORE MEET THE SHELL, AND THE REPAIR IS A CAPTURE-TIME
// RULE RATHER THAN A NEW KEY.** The identity written here is read at CAPTURE -- at quit, from
// `IDocumentBackend::documentId()`, which `core::Document::refreshFileState` already re-reads after
// every successful save (D3-16, Stage 3 T8). So a save during the session cannot orphan a guide:
// by the time anything is written to this file the identity is the one the bytes on disk have. The
// key then does the one job it was chosen for -- **a different file at the same path does not
// inherit the guides** -- and `app/Application`'s restore drops the guides, keeping the document,
// when the identity no longer matches.
//
// What did NOT survive the examination is the other half of D4-7's stated reasoning: *"a renamed
// file keeps its guides"* does not follow from this type and never did, because the path is part
// of the identity. **D4-7's `Why` column is corrected rather than the code** -- the behaviour is
// the one the decision wanted, and only its justification was over-stated.

#ifndef RAWPAGE_CORE_SETTINGS_SESSIONSTATE_H
#define RAWPAGE_CORE_SETTINGS_SESSIONSTATE_H

#include "core/Result.h"
#include "core/doc/DocumentId.h"
#include "core/render/DpiPolicy.h"
#include "core/render/PagePresentation.h"
#include "platform/platform.h"

#include <QRect>
#include <QString>

#include <cstddef>
#include <functional>
#include <vector>

namespace rawpage::core {

// A layout guide: a line a person dragged off a ruler. VIEW state (D4-7) -- PDF has no
// representation for one, and inventing a private key or an invisible annotation to hold it would
// be a document mutation this stage does not make.
struct Guide {
    enum class Orientation { Horizontal, Vertical };

    Orientation orientation = Orientation::Horizontal;
    double      position = 0.0;   // PDF points in page space, so a guide means the same at 50 % and 400 %
    int         pageIndex = 0;
    bool        locked = false;

    friend bool operator==(const Guide& a, const Guide& b)
    {
        return a.orientation == b.orientation && a.position == b.position
               && a.pageIndex == b.pageIndex && a.locked == b.locked;
    }
};

// One open document, as a tab. **No content, no dirty flag, no undo history** -- see D4-9 above.
struct SessionDocument {
    QString            path;
    DocumentId         id;
    render::LayoutMode layoutMode = render::LayoutMode::SingleContinuous;

    // The zoom is stored as the INTENT plus the multiplier it resolved to, because
    // `ViewportController` stores a fit as an intent and re-satisfies it on every resize: restoring
    // 143 % onto a window of a different width would put back a number that was never asked for,
    // where restoring *Fit Width* puts back what the person chose.
    render::FitMode    fit = render::FitMode::None;
    double             zoom = 1.0;

    int                pageIndex = 0;
    double             scrollX = 0.0;
    double             scrollY = 0.0;
    std::vector<Guide> guides;

    // How this tab was SPLIT, as `ui/` encodes it (S4 T13). **Opaque to `core/`**, for exactly the
    // reason `SessionWindow::paneLayout` below is: `SplitViewContainer::Mode` is a widget's enum
    // and teaching this layer its four spellings would put a `ui/` vocabulary in the domain layer
    // to hold a value this layer never reads. It round-trips and is never interpreted here.
    //
    // It is on the DOCUMENT rather than on the window because that is where a split lives: D4-13
    // makes a split N views of one document inside one `DocumentWindow`, so two tabs of one window
    // can be split differently and only one of them is the window's.
    QString            splitState;
};

// One top-level window. `documents` IS the tab order.
struct SessionWindow {
    QRect                        geometry;
    bool                         maximized = false;
    std::vector<SessionDocument> documents;
    int                          activeDocument = 0;

    // Which panes are open and how wide, as `ui/` encodes it. **Opaque to `core/`**, deliberately:
    // a dock layout is a Qt fact (`QMainWindow::saveState`), and teaching this layer to parse one
    // would put a widget vocabulary in the domain layer for no gain. It round-trips and is never
    // interpreted here.
    QString                      paneLayout;
};

// The geometry half of one remembered arrangement.
struct WindowGeometry {
    QRect rect;
    bool  maximized = false;
};

struct Arrangement {
    QString                     fingerprint;
    std::vector<WindowGeometry> windows;
};

// What a clean quit captures.
struct Session {
    QString                    fingerprint;   // the arrangement it was captured on
    std::vector<SessionWindow> windows;
};

class SessionState {
public:
    // How many arrangements are remembered. Small on purpose: a person has a desk, a dock and a
    // train, and an unbounded list would remember every hotel projector they ever plugged in.
    // **Guide case A-22 reads this constant rather than repeating it.**
    static constexpr std::size_t kMaxArrangements = 8;

    // The clamp's "on screen" (D4-8). A window counts as reachable when its title strip overlaps a
    // screen by at least this much -- enough to grab with a pointer, which is the actual
    // requirement, rather than the one pixel a strict intersection test would accept.
    static constexpr int kTitleBarHeight = 32;
    static constexpr int kMinimumVisibleWidth = 160;

    // The schema. A file whose format is HIGHER than this was written by a newer build: it is
    // ignored rather than guessed at, because a newer build's fields are not knowable from here.
    static constexpr int kFormat = 1;

    // `%APPDATA%\RawPage\session.json` -- beside `settings.ini`, and resolved the same way
    // `Settings::defaultFilePath()` resolves that one, including why it is `AppDataLocation` and
    // not `GenericConfigLocation`. **Nothing in `tests/` may call it** (guide case A-79): a suite
    // that used the real location would write, and then reopen, the session of whoever ran it.
    static QString defaultFilePath();

    explicit SessionState(QString filePath);

    // --- the file ---------------------------------------------------------------------------

    // Why a load never fails: see the header. `reason` is empty when the file was read, and
    // otherwise names what was wrong with it, once.
    //
    // **It is a DIAGNOSTIC and never reaches a user**, which is why it is English in `core/` rather
    // than a catalog key: nothing renders it, one log line carries it, and a person debugging a
    // session file is the audience. A message a USER sees carries a key -- `save()` returns one.
    struct LoadReport {
        bool    fileExisted = false;
        bool    loaded = false;
        QString reason;
    };
    LoadReport load();
    Result<void> save() const;

    // --- capture ----------------------------------------------------------------------------

    // Records `session` as the session to restore, and its window geometry as the arrangement
    // `session.fingerprint` names -- moved to the front of the bounded list, so the list is
    // most-recent-first and eviction is of the least recently seen.
    void capture(const Session& session);

    const Session& session() const noexcept { return session_; }
    const std::vector<Arrangement>& arrangements() const noexcept { return arrangements_; }

    // --- restore ----------------------------------------------------------------------------

    // Answers whether a document's file is still there. A predicate rather than a filesystem call
    // so this class stays testable with no disk: the HOST answers a question of fact, and the
    // domain rule -- *a document that no longer exists is dropped, once, rather than offered* --
    // stays here (convention 11).
    using FileExists = std::function<bool(const QString&)>;

    struct Restored {
        std::vector<SessionWindow> windows;

        // Reported once, by path, and then gone: the next capture cannot carry what restore
        // dropped. It is not an error -- it is a file that moved.
        std::vector<QString>       droppedPaths;

        // True when `fingerprint` was not among the remembered arrangements and the most recent one
        // was used instead. The caller does not need it; a case does (A-20).
        bool                       usedFallbackArrangement = false;

        // True when at least one window's rectangle was moved or resized by the clamp (A-21).
        bool                       clamped = false;
    };

    // `screens` is the current virtual desktop, in the same physical-pixel coordinates
    // `MonitorInfo` uses. An empty `screens` clamps nothing, because there is nothing to clamp onto
    // and inventing a rectangle would be worse than leaving the geometry alone.
    Restored restore(const QString& fingerprint, const std::vector<QRect>& screens,
                     const FileExists& exists = FileExists{}) const;

    // --- the arithmetic, exposed because it is the part worth testing exhaustively -----------

    // Stable across `monitors()` order; sensitive to id, position, pixel size, `scaleFactor`,
    // `primary` and the monitor count; blind to `friendlyName` and `physicalDpi`. See the header.
    static QString fingerprintOf(const std::vector<platform::MonitorInfo>& monitors);

    // Returns `window` UNCHANGED when it is already reachable -- that is the control A-21 asks for,
    // and it is the first branch rather than an emergent property of the arithmetic. A window
    // straddling two adjacent screens is reachable and is left alone.
    static QRect clampInto(const QRect& window, const std::vector<QRect>& screens);

private:
    QString                  filePath_;
    Session                  session_;
    std::vector<Arrangement> arrangements_;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_SETTINGS_SESSIONSTATE_H
