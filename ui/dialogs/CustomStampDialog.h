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
class QTabWidget;

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
    void setName(const QString& name);
    void setCategory(const QString& category);
    void setFavourite(bool favourite);
    void setTypedInitials(const QString& text);
    InitialsPad* pad() const noexcept { return pad_; }

    // What `accept` would make now, or nothing -- and why, as the line under the source says it.
    std::optional<Made> made() const;
    QString refusal() const;
    // The browser's filter: image types only (A-78).
    static QString imageFilter();

public slots:
    void accept() override;

private:
    void browse();
    void refresh();
    QImage typedPicture() const;

    QTabWidget* sources_ = nullptr;
    QLineEdit* path_ = nullptr;
    QLabel* preview_ = nullptr;
    InitialsPad* pad_ = nullptr;
    QLineEdit* typed_ = nullptr;
    QLabel* typedPreview_ = nullptr;
    QLineEdit* name_ = nullptr;
    QComboBox* category_ = nullptr;
    QCheckBox* favourite_ = nullptr;
    QLabel* refusal_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;

    QByteArray imageBytes_;
    QString imageSource_;
    QString imageRefusal_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_CUSTOMSTAMPDIALOG_H
