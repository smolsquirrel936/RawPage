// The in-app log viewer -- `Features.md` §9.5, Stage 1 T14.
//
// ---------------------------------------------------------------------------------------------
// WHERE IT SITS, AND WHY IT IS NOT IN THE SIDEBAR.
//
// `Architecture-FileMap.md` §3.15 lists this beside `ThumbnailsPane`, `BookmarksPane` and the rest
// of `ui/panes/`, and every one of those is a view of a DOCUMENT and is hosted by
// `ui/shell/SidebarDock` down one side. The log is not about a document -- it is about the
// application, it outlives every tab, and it is read in wide lines. So it is its own dock in the
// bottom area, which is where every editor and IDE puts the same thing, and `SidebarDock` is left
// alone for the panes that are actually its subject.
//
// ---------------------------------------------------------------------------------------------
// WHERE THE RECORDS COME FROM, AND THE ONE THING THAT WILL SURPRISE A READER.
//
// `core::LogBuffer`, handed down through `app::Services`. It is fed from inside the single sink
// `app/CompositionRoot` installs, which has ALREADY applied `logging/level` and
// `logging/subsystems` -- so this pane can only ever narrow what it was given. Raising the pane's
// own level filter above the global threshold shows nothing new, because those records were never
// recorded, and the pane says so on screen rather than leaving the user to conclude the filter is
// broken. The fix for a user who wants more is the Logging preference page, which T14 also ships,
// and the pane points at it.
//
// A subscription notification arrives ON THE THREAD THAT LOGGED, which from Stage 2 is routinely a
// render worker. Widgets may not be touched from there, so the handler does nothing but post to
// this object; `~QObject` drops posted events for itself, which is what makes that safe against
// the pane being destroyed with a record in flight.
//
// ---------------------------------------------------------------------------------------------
// WHY A PLAIN TEXT VIEW RATHER THAN A TABLE.
//
// §9.5 asks for filtering and copy. A `QPlainTextEdit` gives selection, multi-line copy, find and
// keyboard navigation for free and gets `Ctrl+C` right by being a text widget -- which is exactly
// the `ShortcutOverride` handshake `ShortcutBinder` defers to. A table would need each of those
// written, and would still copy cells rather than lines, which is not what anybody pastes into a
// support ticket.

#ifndef RAWPAGE_UI_PANES_LOGPANE_H
#define RAWPAGE_UI_PANES_LOGPANE_H

#include "core/Log.h"
#include "core/LogBuffer.h"
#include "core/settings/SettingsSchema.h"

#include <QDockWidget>
#include <QString>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace rawpage::ui {

class LogPane : public QDockWidget {
    Q_OBJECT

public:
    LogPane(core::LogBuffer& buffer, QWidget* parent = nullptr);
    ~LogPane() override;

    // Show records at this level and above -- `Error` is the narrowest, `Trace` the widest. It can
    // only narrow what the buffer holds; see the header.
    void          setLevelFilter(core::LogLevel level);
    core::LogLevel levelFilter() const { return levelFilter_; }

    void                    setSubsystemFilter(core::LogSubsystemMask mask);
    core::LogSubsystemMask  subsystemFilter() const { return subsystemFilter_; }

    // What the view is showing, one record per line, in the shape a support ticket wants. This is
    // also what `copyToClipboard` puts on the clipboard -- one function, so what a test reads and
    // what a user pastes cannot differ.
    QString visibleText() const;
    int     visibleRecordCount() const;

    void copyToClipboard();

    // Empties the VIEW and the buffer behind it. Both, because a pane that cleared only its own
    // view would refill itself the next time the filter changed, which reads as the button not
    // working.
    void clear();

    // The line a record becomes. Deliberately the same shape `app/LogFile` writes, so that a log
    // read on screen and the same log read in a support bundle are the same text.
    static QString formatEntry(const core::LogEntry& entry);

private:
    void rebuild();
    void appendIfVisible(const core::LogEntry& entry);
    bool passesFilter(const core::LogEntry& entry) const;
    void updateThresholdNote();

    core::LogBuffer&       buffer_;
    core::LogLevel         levelFilter_ = core::LogLevel::Trace;   // widest: show everything kept
    core::LogSubsystemMask subsystemFilter_ = core::LogSubsystemMask::all();

    QComboBox*      levelBox_ = nullptr;
    QComboBox*      subsystemBox_ = nullptr;
    QPlainTextEdit* view_ = nullptr;
    QLabel*         note_ = nullptr;

    int subscription_ = 0;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_PANES_LOGPANE_H
