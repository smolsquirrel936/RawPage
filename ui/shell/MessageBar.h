// The document-condition banner. Stage 2 T14.
//
// ---------------------------------------------------------------------------------------------
// WHAT IT IS FOR, IN ONE SENTENCE THAT IS ALSO ITS DESIGN CONSTRAINT.
//
// It says something true about the document that the document itself cannot say: *this file was
// damaged and we repaired it*, *this file is protected*, *this file is signed*, *this file has a
// form*. Every one of those is a condition of the file rather than a result of an action, which is
// why it is a persistent bar and not a status-bar message that scrolls away — a repaired document
// stays repaired for as long as it is open, and **Stage 3 forces its first save to Save-As on that
// basis**. A banner the user missed is a banner that let them overwrite an original.
//
// ---------------------------------------------------------------------------------------------
// ONE BANNER PER KIND, KEYED ON THE KIND.
//
// Showing *repaired* twice replaces the first rather than stacking a second, because the second
// open of the same condition is the same condition. Different kinds stack, worst-first, so a
// document that is both protected and repaired says both — and says the one with consequences at
// the top.
//
// ---------------------------------------------------------------------------------------------
// THE ROUTE TO THE DETAIL, AND THE TRADE THAT WAS MADE THERE (guide case A-58).
//
// A-58 asks for "a route to the detail". The detail is `core::RepairReport`'s per-entry list, and
// its `RepairKind` names are declared ASCII, stable and **non-localized** by their own header —
// they are diagnostic codes, in the log and in a bug report. Giving thirteen xref-internals codes
// translated prose would be a translation burden for text no ordinary user can act on, and would
// have to be re-done in every locale the product ever ships.
//
// So the detail view localizes the FRAME — a title and a sentence saying what the list is — and
// prints the codes as codes, beside the object number and file offset. That is how every
// professional product shows an error code, and it means the string a user quotes in a bug report
// is the same string the log contains. Convention 5 is satisfied because nothing here is an English
// literal in a source file: the codes come from `repairKindName()`, which is a function.

#ifndef RAWPAGE_UI_SHELL_MESSAGEBAR_H
#define RAWPAGE_UI_SHELL_MESSAGEBAR_H

#include <QColor>
#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

class QVBoxLayout;

namespace rawpage::core {
class RepairReport;
}

namespace rawpage::ui {

class MessageBar : public QWidget {
    Q_OBJECT

public:
    // The five of `Architecture-FileMap.md` §3.13. **Stage 2 raises two of them** — `Repaired` and
    // `Protected` — and the other three are declared here rather than added later for
    // `platform/platform.h`'s reason: a surface whose vocabulary grows one enumerator at a time,
    // by whoever needed one, ends up with five spellings of the same idea.
    //
    // ORDERED BY CONSEQUENCE, worst first, because that is the order they are shown in.
    //
    // **`OpenFailed` IS THE ONE THAT IS NOT A DOCUMENT CONDITION** (Stage 2 issues I-31 and I-32).
    // A refused file never gets a tab, so there is no document for its banner to belong to: it is
    // WINDOW-scoped, survives `clearDocumentConditions()` and every tab change, and stays until it
    // is dismissed. It is first because it is the only kind that says *you do not have the file you
    // asked for*, and it is painted in the error colour rather than the condition tint.
    enum class Kind {
        OpenFailed,  // S2 -- a file this window was asked to open and could not; window-scoped
        Repaired,    // S2 -- and the one with a consequence: Stage 3 forces its first save to Save-As
        Protected,   // S2 -- opened with a password, or refused for want of one
        Signed,      // S11
        HasForm,     // S9
        WriteMode,   // S13
        // Stage 3 T10 -- the save and the recovery journal. All three belong to the DOCUMENT, and
        // `DocumentWindow` keeps them, so a tab switch away and back shows them again.
        SaveFailed,           // a Save or Save As failed, with the key-resolved reason
        SaveAsRequired,       // the explanation before a forced Save-As: repaired, or changed on disk
        RecoveryUnavailable,  // edits are kept in memory only -- shown ONCE per document (A-80)
    };

    static const char* kindName(Kind kind) noexcept;

    // True for a kind that belongs to the WINDOW rather than to the document in front of it.
    static bool isWindowScoped(Kind kind) noexcept;

    explicit MessageBar(QWidget* parent = nullptr);
    ~MessageBar() override;

    // `actionLabel` empty means no button. `action` is invoked on the UI thread when it is pressed.
    struct Message {
        Kind kind = Kind::Repaired;
        QString text;
        QString actionLabel;
        std::function<void()> action;
    };

    void showMessage(Message message);

    // The banner's colours, from `ThemeManager`'s `MessageBackground` and `MessageText` tokens.
    // Pushed in by the window that owns the theme rather than pulled from the application palette,
    // because no `QPalette` role means "a banner": the role this used to borrow, `AlternateBase`, is
    // the window grey, and that is the defect (issue I-29). Re-applied to every banner already on
    // screen, so a theme switch with a repaired document open recolours it at once.
    void setColors(const QColor& background, const QColor& text);
    QColor backgroundColor() const { return background_; }

    // The same, for `OpenFailed`, from the `ErrorBackground` and `ErrorText` tokens. A separate
    // colour because the two say different kinds of thing -- *this document has a condition* and
    // *you did not get your document* -- and a refusal in the condition tint reads as a condition.
    void setFailureColors(const QColor& background, const QColor& text);
    QColor failureBackgroundColor() const { return failureBackground_; }

    // Removes one kind, every document condition, or everything. `clearDocumentConditions()` is
    // what a tab switch and a document close call: a banner about the document that was closed,
    // still on screen over the one that replaced it, is worse than no banner at all -- and a
    // window-scoped banner is about no document, so it stays. `clear()` removes everything.
    void dismiss(Kind kind);
    void clearDocumentConditions();
    void clear();

    bool isShowing(Kind kind) const;
    int messageCount() const;

    // The resolved text currently shown for `kind`, or an empty string. For the suite, and for the
    // same reason `StatusBar` has named accessors: a case that looked a label up by object name
    // keeps passing after the label is replaced by a different one with the same name.
    QString textFor(Kind kind) const;

    // ---- The repair report, which is the one Stage 2 actually raises ---------------------------
    // Builds the banner from `RepairReport::summaryKey()` and its arguments — a catalog key plus
    // untranslated numbers, never a sentence, because `core/` holds no user-facing English. An
    // EMPTY report shows nothing at all, which is what "opening a clean file shows no bar" means.
    void showRepairReport(const core::RepairReport& report, const QString& documentName,
                          QWidget* detailParent);

    // The detail text the button opens: the frame is localized, the codes are not. Exposed so a
    // case can assert what it says without opening a modal dialog it would then have to close.
    static QString repairDetailText(const core::RepairReport& report);

signals:
    void messageCountChanged(int count);

private:
    struct Entry {
        Kind kind = Kind::Repaired;
        QWidget* widget = nullptr;
        QString text;
    };

    void rebuildOrder();
    void paintBanner(QWidget* banner, Kind kind) const;
    void removeWhere(const std::function<bool(Kind)>& predicate);

    QVBoxLayout* column_ = nullptr;
    std::vector<Entry> entries_;

    // Initialised from the tokens for the theme the OS reports, so a bar nobody has called
    // `setColors` on -- a unit case, a future second owner -- is still never the window grey.
    QColor background_;
    QColor text_;
    QColor failureBackground_;
    QColor failureText_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_MESSAGEBAR_H
