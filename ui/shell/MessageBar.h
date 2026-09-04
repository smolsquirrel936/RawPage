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
    enum class Kind {
        Repaired,    // S2 -- and the one with a consequence: Stage 3 forces its first save to Save-As
        Protected,   // S2 -- opened with a password, or refused for want of one
        Signed,      // S11
        HasForm,     // S9
        WriteMode,   // S13
    };

    static const char* kindName(Kind kind) noexcept;

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

    // Removes one kind, or everything. `clear()` is what a tab switch and a document close call:
    // a banner about the document that was closed, still on screen over the one that replaced it,
    // is worse than no banner at all.
    void dismiss(Kind kind);
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

    QVBoxLayout* column_ = nullptr;
    std::vector<Entry> entries_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_MESSAGEBAR_H
