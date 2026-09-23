// The QApplication subclass: application lifetime, and the translator stack.
//
// ---------------------------------------------------------------------------------------------
// WHY TWO TRANSLATORS ARE INSTALLED AND NOT ONE.
//
// The catalog key IS the source string in this project (T6): `core/Result.h` forbids user-facing
// English in `core/`, `ActionId.def` carries one `action.*` key per action, and so there is no
// English in the sources for `lupdate` to extract. Every locale, `en` included, is a real catalog.
//
// The consequence is that an UNTRANSLATED string does not fall back to English -- it falls back to
// the key, and a button reads `action.file.open`. So `rawpage_en.qm` is installed first as a base
// and the active locale on top of it: Qt tries translators in reverse order of installation, so
// zh-TW answers where it has a key and `en` answers where it does not. A key added to `en` and not
// yet to a shipped locale therefore renders as English -- the difference between an incomplete
// translation and a broken UI.
//
// (Measured at T9 rather than assumed: the 28 zh-TW entries marked `unfinished` are NOT the case
// this covers. lrelease keeps an unfinished message that has text, so those reach the product as
// provisional Chinese and are exactly what M-06 reviews. Only an EMPTY translation is dropped.)
//
// A key missing from `en` remains a detectable DEFECT rather than an invisible fallback: it renders
// as the key, and `tools/i18n-lint`'s `missing-key` rule fails the build before anyone sees it.
// ---------------------------------------------------------------------------------------------
//
// ---------------------------------------------------------------------------------------------
// AND THE SESSION -- `app::SessionShell` below (Stage 4 T13, decision D4-9).
//
// It is in this file because it is the other half of what `Application` is for: the process's
// lifetime. `core/settings/SessionState` owns the FORMAT and the arithmetic -- the fingerprint, the
// clamp, the bounded arrangement list -- and knows nothing about a window; `SessionShell` is the
// part that reads a `ui::MainWindow` into that model on the way out and builds one back from it on
// the way in. Neither knows the other's job, which is what lets `SessionState` be tested headless
// and this class be tested through the product's own composition.
//
// THE ORDER AT LAUNCH IS THE DECISION, and `app/main.cpp` makes it visible (plan section 5.5):
//
//   1. `RecoveryPrompt::checkAndOffer`  -- a crashed session's UNSAVED work, first (D3-11)
//   2. the command line's files
//   3. `SessionShell::restore`          -- and it SKIPS any path the two above already opened
//
// Restore last is what makes step 3's skip possible at all. The alternative -- restore first, then
// replay a recovery into the document it just opened -- is the *recover into a file that has
// changed* path D3-11 deliberately routes to an untitled document, manufactured by the product
// itself. And a session NEVER carries unsaved work: a document closed with *Don't Save* comes back
// at the bytes on disk, which is the clause that separates this mechanism from that one.

#ifndef RAWPAGE_APP_APPLICATION_H
#define RAWPAGE_APP_APPLICATION_H

#include "core/settings/SessionState.h"

#include <QApplication>
#include <QHash>
#include <QRect>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

class QTranslator;

namespace rawpage::core {
class Settings;
}

namespace rawpage::platform {
class IDisplayTopology;
}

namespace rawpage::ui {
class MainWindow;
}

namespace rawpage::app {

struct Services;

class Application : public QApplication {
    Q_OBJECT

public:
    Application(int& argc, char** argv);
    ~Application() override;

    // Resolution order, highest first: `--lang`, then `language/uiLocale`, then the system locale.
    // Returns the locale actually in force, which may be `en` if the requested catalog is one of
    // the two declared `"shipped": false` in i18n/locales.json (D1-14).
    QString loadTranslators(const QString& requestedLocale, const QString& settingLocale);

    const QString& activeLocale() const { return activeLocale_; }

    // Which catalog files were installed, most specific last. Printed by --dump-ui-strings so that
    // "the translation did not load" and "the translation loaded and is incomplete" are
    // distinguishable without a debugger.
    const QStringList& loadedCatalogs() const { return loadedCatalogs_; }

    // Where the .qm files live: `<application dir>/i18n`. A directory beside the executable rather
    // than a Qt resource, because Stage 15 ships them as replaceable files -- a customer-supplied
    // catalog is a real deployment request and a resource would make it a rebuild.
    static QString catalogDirectory();

private:
    bool installCatalog(const QString& localeCode);

    std::vector<std::unique_ptr<QTranslator>> translators_;
    QString     activeLocale_;
    QStringList loadedCatalogs_;
};

// ---------------------------------------------------------------------------------------------
// The session, where it meets the shell (S4 T13, D4-9)
// ---------------------------------------------------------------------------------------------
//
// WHEN A SESSION IS CAPTURED, AND WHY IT IS NOT SIMPLY `aboutToQuit`. A window's close runs a
// three-step protocol over every dirty document before the window goes (D3-14), and that protocol
// REMOVES the tab: a document the person answers *Don't Save* to is gone from the window by the
// time `aboutToQuit` arrives, so a session read only there would quietly forget it -- and
// forgetting it is the wrong answer, because the document WAS open and the user only declined to
// keep the edit.
//
// So this class stashes a window's state on `QEvent::Close`, which an application-level event
// filter sees BEFORE `MainWindow::closeEvent` runs and therefore before the first prompt. At
// `aboutToQuit` the session is assembled from the live window list, and for each window:
//
//   * the window is still VISIBLE -> capture it fresh. Its close was refused (somebody pressed
//     Cancel on a dirty document) or it never closed at all, and the stash is stale.
//   * the window is HIDDEN        -> use the stash. It closed, and the stash is what it held
//     before the close protocol emptied it.
//
// That pair is the whole rule, and the visibility test is what makes it exact rather than a guess
// about who called `quit()`. A window the user closed an hour ago is not in the list to ask: it was
// `WA_DeleteOnClose`, its `deleteLater` ran long ago, and `MainWindow::shellWindows()` no longer
// holds it. One closed inside the quit itself is still there, because the event loop is on its way
// out and has not delivered that DeferredDelete.
//
// NOTHING IS CAPTURED AND NOTHING IS WRITTEN when `general/restoreSession` is off -- not "captured
// and ignored". A person who turns the setting off has asked the product to stop keeping a list of
// what they read, and a file that went on being written would be the opposite of that answer.
//
// NO `Q_OBJECT`, deliberately: `eventFilter` is virtual on `QObject` and needs no meta-object, and
// the one connection this class makes is a lambda with a context object. Adding the macro would put
// this header into every moc run that includes it to buy nothing.
class SessionShell final : public QObject {
public:
    SessionShell(core::SessionState& state, core::Settings& settings,
                 const platform::IDisplayTopology* topology, QObject* parent = nullptr);

    // Installs the `QEvent::Close` filter and the `aboutToQuit` connection. Separate from the
    // constructor so that a case can build one, drive `captureNow()` by hand, and never install a
    // filter on the suite's own application.
    void install(QCoreApplication& application);

    // What `aboutToQuit` does: assemble the session, hand it to `SessionState::capture` and write
    // the file. Public because a test cannot quit a process and come back.
    void captureNow();

    // The session as it stands right now, without capturing or writing it.
    core::Session snapshot() const;

    // One window, read into the model. Static because it needs nothing this object holds, and
    // because a case asserting what a window captures should not have to build a shell to ask.
    static core::SessionWindow captureWindow(const ui::MainWindow& window);

    // ---- Restore ---------------------------------------------------------------------------

    struct Restored {
        bool                 disabled = false;   // `general/restoreSession` is off
        int                  windows = 0;
        int                  documents = 0;

        // Paths the session named that were ALREADY open when restore ran -- opened by
        // `RecoveryPrompt` or by the command line. Skipped, never opened twice: two tabs over one
        // file would be two journals over one file, which is two Saves racing for the same bytes.
        std::vector<QString> skipped;

        // Named by `SessionState::restore`: a file that is no longer there. Not an error.
        std::vector<QString> droppedPaths;

        // Documents whose guides were dropped because the file's identity no longer matches the one
        // captured -- somebody changed the file between the quit and this launch (**I-01**,
        // `Stage4-ViewingSurface-Testing-Guide.md` section 8). The document still opens.
        int                  guidesDropped = 0;

        bool                 clamped = false;
        bool                 usedFallbackArrangement = false;
    };

    // `first` is the window `main.cpp` has already built and shown; the session's first window is
    // restored INTO it rather than beside it, so a launch never flashes an empty shell and then
    // replaces it. Any further windows are created here, exactly as a tear-off creates one.
    Restored restore(ui::MainWindow& first, const Services& services);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;

    // The current arrangement, and the rectangles `SessionState::clampInto` measures against. Both
    // come from `IDisplayTopology` rather than from `QScreen`, because `MonitorInfo`'s physical
    // pixels are the coordinates the fingerprint and the stored geometry are already in -- joining
    // the two sources is what I-20 is about, and there is nothing here that needs the join.
    QString            fingerprintNow() const;
    std::vector<QRect> screensNow() const;

    core::SessionState&              state_;
    core::Settings&                  settings_;
    const platform::IDisplayTopology* topology_ = nullptr;

    // Filled at `QEvent::Close`, read at `aboutToQuit` for the windows that are hidden. Keyed by
    // the raw pointer AND swept on `QObject::destroyed`, because an address is reused: a stale
    // entry left behind by a window closed an hour ago could otherwise be handed to a new window
    // that the allocator happened to put in the same place.
    QHash<const QObject*, core::SessionWindow> closing_;
};

} // namespace rawpage::app

#endif // RAWPAGE_APP_APPLICATION_H
