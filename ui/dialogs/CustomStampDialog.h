// ui/dialogs/CustomStampDialog -- makes a custom stamp for the person's library: from an image file, or as
// initials drawn or typed. Stage 6 T10, decision D6-16 (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// IT COLLECTS; IT CHANGES NO DOCUMENT
// ===========================================================================================
// Every dialog collects parameters and constructs a command (`CLAUDE.md`) -- and this one constructs none,
// because a custom stamp is not a document change: it is an entry in the person's library
// (`core::ProfileStore`), which the caller writes with what `made()` returns. Placing it is the stamp tool's
// command, made later.
//
// ===========================================================================================
// THREE SOURCES, AND ONE THAT IS NOT OFFERED
// ===========================================================================================
// An IMAGE FILE -- the browser lists image types only, and a file that is not an image (a PDF among them) is
// refused BY NAME before anything is made (`error.stamps.notAnImage`, guide A-78): a custom stamp from a PDF
// PAGE is Stage 7's (Q7), and the dialog offers no such source rather than a disabled one. A PNG or a JPEG is
// kept as it is; any other image Qt reads (BMP, GIF, …) is kept as a PNG of itself. DRAWN initials, on a pad,
// in black on a transparent ground; TYPED initials, set in the system's script face where there is one, else
// an italic. Both are kept as PNGs and filed under Signatures by default -- markup, not a digital signature
// (Stage 11).
//
// ===========================================================================================
// SEVERAL IMAGES AT ONCE (S6 guide I-43, the user's request, 2026-10-27)
// ===========================================================================================
// The browser takes several files. Two or more usable images become a LIST -- each file beside a name, the file's
// own to start with and edited in place -- and the single Name box goes, its row hidden; one category and one
// Favourite tick serve them all, and OK says how many stamps it makes. A file that is not an image is LEFT OUT and
// named under the list, while the rest are made; only when nothing usable is left is that a refusal. `madeAll`
// answers every stamp; `made` the one, when there is exactly one.
//
// A CATEGORY MAY NOT BE A BUILT-IN HEADING'S NAME (S6 guide I-42): one typed as Favourites, Standard, Dynamic or
// Signatures, in any shipped language or case, is refused by name (`StampCategoryDialog::isReservedName`) --
// Signatures is chosen from the list, never typed.

#ifndef RAWPAGE_UI_DIALOGS_CUSTOMSTAMPDIALOG_H
#define RAWPAGE_UI_DIALOGS_CUSTOMSTAMPDIALOG_H

#include <QByteArray>
#include <QDialog>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QFormLayout;
class QTabWidget;
class QTableWidget;

namespace rawpage::ui {

// The drawing pad: strokes in black, kept as a picture with a transparent ground.
class InitialsPad final : public QWidget {
    Q_OBJECT
public:
    explicit InitialsPad(QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(360, 140); }
    bool isEmpty() const noexcept { return strokes_.empty(); }
    void clear();
    // What was drawn, cropped to its ink with a small margin, at twice the pad's size; null when nothing was.
    QImage picture() const;
    // A stroke through `points` (pad coordinates), as a drag would leave it -- for a case.
    void addStroke(const std::vector<QPointF>& points);

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    std::vector<std::vector<QPointF>> strokes_;
};

class CustomStampDialog final : public QDialog {
    Q_OBJECT

public:
    enum class Source { ImageFile = 0, Drawn = 1, Typed = 2 };

    struct Made {
        QString name;
        QString category;   // a person's own, or `core::ProfileStore::kSignaturesCategory`
        bool favourite = false;
        QByteArray image;   // a PNG or a JPEG's bytes
        QString sourceName; // the file chosen, for a message
    };

    // `categories`: the library's own, offered in the category box beside Signatures.
    explicit CustomStampDialog(const QStringList& categories, QWidget* parent = nullptr);

    void setSource(Source source);
    Source source() const;
    // The image file, as the browser or a person typing would set it: read, checked, previewed. False, and the
    // refusal shown naming the file, for one that is not an image.
    bool setImagePath(const QString& path);
    // Several, as the browser sets them (I-43): each usable image kept, each other file left out by name. False
    // when none is usable.
    bool setImagePaths(const QStringList& paths);
    void setName(const QString& name);
    void setCategory(const QString& category);
    void setFavourite(bool favourite);
    void setTypedInitials(const QString& text);
    InitialsPad* pad() const noexcept { return pad_; }

    // What `accept` would make now, or nothing -- and why, as the line under the source says it. `made` is the one
    // stamp when exactly one would be made; `madeAll` every one, empty when refused.
    std::optional<Made> made() const;
    std::vector<Made> madeAll() const;
    QString refusal() const;
    // The files left out of a choice of several, said under the list; empty when none was.
    QString notice() const;
    // The list of two or more images: the file in column 0, its stamp's name, editable, in column 1.
    QTableWidget* namesTable() const noexcept { return names_; }
    // Whether the single Name box is shown -- not while the list is.
    bool nameBoxShown() const;
    // The browser's filter: image types only (A-78).
    static QString imageFilter();

public slots:
    void accept() override;

private:
    void browse();
    void refresh();
    QImage typedPicture() const;
    void showPreview(int image);

    QTabWidget* sources_ = nullptr;
    QLineEdit* path_ = nullptr;
    QLabel* preview_ = nullptr;
    InitialsPad* pad_ = nullptr;
    QLineEdit* typed_ = nullptr;
    QLabel* typedPreview_ = nullptr;
    QTableWidget* names_ = nullptr;
    QFormLayout* form_ = nullptr;
    QLineEdit* name_ = nullptr;
    QComboBox* category_ = nullptr;
    QCheckBox* favourite_ = nullptr;
    QLabel* refusal_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;

    struct Picked {
        QString path;
        QByteArray bytes;   // a PNG or a JPEG's, as it will be kept
        QImage image;       // for the preview
    };
    std::vector<Picked> images_;
    QStringList leftOut_;     // the files chosen that are not images, by name
    QString shownPaths_;      // what the path box was given, so an edit of it is told from it
    QString okText_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_CUSTOMSTAMPDIALOG_H
