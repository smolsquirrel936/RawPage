// ui/viewer/tools/StampTool -- places the stamp chosen in the palette. Stage 6 T10, decision D6-16
// (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// TWO GESTURES, ONE COMMAND EACH
// ===========================================================================================
// A CLICK places the stamp at its natural size, centred where the click was; a DRAG places it at the largest
// size whose box fits what was dragged, centred in it (`core::stampFittedTo`). Each is ONE
// `AddAnnotationCommand` through the overlay (D6-1), by the person (`identity/author`, else the login name), at
// the instant the overlay's clock gives -- which a DYNAMIC stamp bakes into its lines with the HOST's UTC
// offset at that instant (convention 11): nothing is recomputed later (guide A-76). A drag shows its box dashed
// as it goes, the marquee's way; a click shows nothing until the stamp is there.
//
// ===========================================================================================
// WHAT IS PLACED: THE SESSION'S CHOICE
// ===========================================================================================
// The palette (`ui/panes/StampPalettePane`) sets `currentStamp()`, shared by every window as the tools'
// session defaults are: a standard or dynamic stamp by its `/Name`, or a custom stamp by its library name and
// its pixels -- DECODED ONCE when it is chosen (`decodeStampImage`), never per click. A JPEG keeps its own
// bytes (`core::stampImageFromJpeg`); any other image becomes 8-bit RGB with its alpha, scaled down to
// `kMaxDecodedSide` on its longer side first -- a stamp drawn 216 points across has no use for 6,000 pixels.
// Decoding is here, in `ui/`, because `core/` names no Qt GUI type (convention 3).

#ifndef RAWPAGE_UI_VIEWER_TOOLS_STAMPTOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_STAMPTOOL_H

#include "core/Result.h"
#include "core/doc/Annotation.h"
#include "ui/viewer/tools/AnnotationTool.h"
#include "ui/viewer/tools/Tool.h"

#include <QByteArray>
#include <QImage>
#include <QString>

namespace rawpage::ui {

struct StampChoice {
    enum class Kind { Standard, Dynamic, Custom };
    Kind kind = Kind::Standard;
    QString name = QStringLiteral("Approved");   // a `/Name`, or a custom stamp's library name (its `/Subj`)
    QString libraryId;                           // Custom: which library stamp, for the palette to show it chosen
    core::StampImage image;                      // Custom: its pixels, decoded once
};

// What the stamp tool places now -- the session's, shared by every window.
const StampChoice& currentStamp();
void setCurrentStamp(StampChoice choice);

// An image file's bytes as a stamp's pixels: a JPEG passed through, anything Qt can read decoded (and scaled to
// `kMaxDecodedSide`). `error.stamps.notAnImage` for bytes no image reader takes, named by `sourceName`.
inline constexpr int kMaxDecodedSide = 2048;
core::Result<core::StampImage> decodeStampImage(const QByteArray& bytes, const QString& sourceName = QString());
// A picture already decoded -- an image pasted from the clipboard -- as a stamp's pixels, scaled likewise.
core::Result<core::StampImage> stampImageOf(QImage image);

class StampTool final : public Tool {
public:
    static bool handles(AnnotationToolKind kind) noexcept { return kind == AnnotationToolKind::Stamp; }

    ToolKind kind() const override { return ToolKind::Annotate; }
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }
    bool paintsOverlay() const override { return true; }

    void press(ToolHost& host, QPoint pos) override;
    void move(ToolHost& host, QPoint pos) override;
    void release(ToolHost& host, QPoint pos) override;
    void cancel(ToolHost& host) override;
    bool isDragging() const override { return dragging_; }
    void paint(QPainter& painter, const ToolHost& host) const override;

    // The stamp the tool would place on `page` centred on `centre`: the current choice, by the overlay's author at
    // its clock's instant. For a case, and for the palette's "place at the centre" (an image pasted).
    static core::Result<core::Annotation> stampAt(ToolHost& host, int page, core::PdfPoint centre);

private:
    bool dragging_ = false;
    int page_ = -1;
    QPoint pressedAt_;
    QPoint at_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_STAMPTOOL_H
