// ui/dialogs/CustomStampDialog -- makes a custom stamp for the person's library: from an image file, as initials
// drawn or typed, or -- since Stage 7 T13 -- from a page of a PDF. Stage 6 T10, decision D6-16
// (`Stage6-Annotations-Plan.md` §3); the page source `Stage7-Assembly-Plan.md` D7-36.
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
// FOUR SOURCES
// ===========================================================================================
// An IMAGE FILE -- the browser lists image types only, and a file that is not an image (a PDF among them) is
// refused BY NAME before anything is made (`error.stamps.notAnImage`, guide A-78): a PDF is the fourth tab's. A
// PNG or a JPEG is kept as it is; any other image Qt reads (BMP, GIF, …) is kept as a PNG of itself. DRAWN
// initials, on a pad, in black on a transparent ground; TYPED initials, set in the system's script face where
// there is one, else an italic. Both are kept as PNGs and filed under Signatures by default -- markup, not a
// digital signature (Stage 11).
//
// A PDF PAGE (Stage 7 T13, D7-36): the browser lists PDF files; a page number, 1 to the file's count, and the page
// read AS IT IS CHOSEN through the window's `PageSource` -- the backend's `filePageCount` and `stampPage`, which copy
// it out of the file -- so a page that cannot be a stamp (an encrypted file, content no filter here reads) is refused
// under the source by name before OK, and `made()` carries the page already copied. Under it, the page's number and
// the size it shows -- NOT its picture (the user's choice, 2026-11-13: drawing a page is the render thread's, and the
// dialog is not). Filed under the person's own category, as an image is.
//
// ===========================================================================================
// SEVERAL IMAGES AT ONCE (S6 guide I-43, the user's request, 2026-10-27)
// ===========================================================================================
// The browser takes several files. Two or more usable images become a LIST -- each file beside a name, the file's
// own to start with and edited in place -- and the single Name box goes, its row hidden; one category and one
// Favourite tick serve them all, and OK says how many stamps it makes. A file that is not an image is LEFT OUT and
// named under the list, while the rest are made; only when nothing usable is left is that a refusal. `madeAll`
// answers every stamp; `made` the one, when there is exactly one. The list is held tall enough for every row and
// the dialog grows to it, up to the screen's height, past which it scrolls; the row whose image is previewed is a
// plain wash, not the style's selection, whose Windows 11 accent bar covered each cell's first letter (I-46) -- a
// bar the table paints under each cell itself, so the list carries a style of its own that passes that row unselected.
//
// A CATEGORY MAY NOT BE A BUILT-IN HEADING'S NAME (S6 guide I-42): one typed as Favourites, Standard, Dynamic or
// Signatures, in any shipped language or case, is refused by name (`StampCategoryDialog::isReservedName`) --
// Signatures is chosen from the list, never typed.

#ifndef RAWPAGE_UI_DIALOGS_CUSTOMSTAMPDIALOG_H
#define RAWPAGE_UI_DIALOGS_CUSTOMSTAMPDIALOG_H

#include "core/Result.h"
#include "core/doc/Annotation.h"

#include <QByteArray>
#include <QDialog>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QCheckBox;
class QSpinBox;
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
    enum class Source { ImageFile = 0, Drawn = 1, Typed = 2, PdfPage = 3 };

    struct Made {
        QString name;
        QString category;   // a person's own, or `core::ProfileStore::kSignaturesCategory`
        bool favourite = false;
        QByteArray image;   // a PNG or a JPEG's bytes -- empty for a page
        QString sourceName; // the file chosen, for a message
        std::optional<core::StampPage> page;   // Stage 7 T13: the page, copied
    };

    // Stage 7 T13: how the PDF tab reads a file -- its page count, and one page as a stamp's page. The window passes
    // the backend's; without one the tab refuses every file.
    struct PageSource {
        std::function<core::Result<int>(const QString& path)> count;
        std::function<core::Result<core::StampPage>(const QString& path, int page)> page;
    };

    // `categories`: the library's own, offered in the category box beside Signatures.
    explicit CustomStampDialog(const QStringList& categories, QWidget* parent = nullptr);

    void setPageSource(PageSource source) { pageSource_ = std::move(source); }
    // The PDF file, as the browser or a person typing would set it: counted and its first page read. False, and the
    // refusal shown naming the file, for one that cannot give a page.
    bool setPdfPath(const QString& path);
    // The page, from 1, read as it is chosen.
    void setPdfPage(int oneBased);
    QSpinBox* pageNumberBox() const noexcept { return pageNumber_; }
    // What the PDF tab says of the page read: its number of the file's, and the size it shows.
    QString pageInfo() const;
    // The PDF browser's filter: PDF files only.
    static QString pdfFilter();

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
    void browsePdf();
    void readPage();
    void refresh();
    QImage typedPicture() const;
    void showPreview(int image);
    // The list held tall enough for every row, the dialog grown to it, up to the screen (I-46).
    void fitList();

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

    // Stage 7 T13: the PDF tab.
    PageSource pageSource_;
    QLineEdit* pdfPath_ = nullptr;
    QSpinBox* pageNumber_ = nullptr;
    QLabel* pageInfo_ = nullptr;
    QString pdfFile_;                       // the file counted, as given
    int pageCount_ = 0;                     // its pages; 0 when none was counted
    std::optional<core::StampPage> page_;   // the page read
    QString pageRefusal_;                   // why the file or page cannot be a stamp, said under the source
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_CUSTOMSTAMPDIALOG_H
