// The unit a document tab represents -- and at Stage 1 it is deliberately a stub. Decision D1-2.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS CLASS DOES NOT HAVE, AND WHY THAT IS THE POINT.
//
// No `Document`. No `CommandJournal`. No page number, no zoom, no selection, no pane set, no
// per-document settings. It holds a PATH and the widgets that say what will be here later, and
// `Architecture-FileMap.md` §3.13 already describes what it becomes: the owner of the document, its
// journal, the viewer, the split container and the panes.
//
// D1-2 exists because the shell's genuinely hard parts -- tear-off and re-dock, reorder, close,
// overflow, the Explorer -> single-instance -> `openFiles` handoff -- are STRUCTURAL, and they are
// cheap to get right now and expensive to reshape once a real document hangs off them. The risk it
// names in return is that the stub calcifies: that Stage 2 inherits a shape built for a placeholder.
// The guard against that is this file staying empty of state, which guide case A-57 checks -- so
// every member below is either the path or a widget showing the path.
//
// ---------------------------------------------------------------------------------------------
// NOTHING HERE READS THE FILE.
//
// `MainWindow::openFiles` checks that a path exists and ends in `.pdf`, and that is the whole of it
// (guide case A-58). Deciding whether bytes are a PDF is `IDocumentBackend`'s job at Stage 2, and a
// content check inside `ui/` would pre-empt a boundary that is much harder to reclaim than to hold.
// This class never opens the file at all: it names it.

#ifndef RAWPAGE_UI_SHELL_DOCUMENTWINDOW_H
#define RAWPAGE_UI_SHELL_DOCUMENTWINDOW_H

#include <QString>
#include <QWidget>

namespace rawpage::ui {

class DocumentWindow : public QWidget {
    Q_OBJECT

public:
    explicit DocumentWindow(QString path, QWidget* parent = nullptr);

    // The absolute path this tab stands for. The identity of a document at Stage 1 IS its path --
    // `core/doc/DocumentId` arrives with `core/doc/` at Stage 2, and inventing a placeholder id here
    // would be a member Stage 2 has to remove rather than fill in.
    const QString& path() const { return path_; }

    // The tab's text and the window title's `%1`: the file's name, derived rather than stored, so
    // there is no second copy to disagree with the path after a rename.
    QString title() const;

private:
    QString path_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_DOCUMENTWINDOW_H
