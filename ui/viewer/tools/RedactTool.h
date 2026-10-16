// ui/viewer/tools/RedactTool -- the redaction MARK tool, `Ctrl+Alt+K`. Stage 6 T14, decision D6-25
// (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// TEXT WHERE THERE IS TEXT, AN AREA WHERE THERE IS NOT
// ===========================================================================================
// A press ON A GLYPH -- inside a character's box on the page's reading, with a point of slack -- starts a text
// selection exactly as the markup tools do (`ui/viewer/tools/MarkupTool`): in reading order, across pages, a word on
// two clicks, a line on three, and on the release the selection is MARKED, one region a line
// (`AnnotationOverlay::markText` with the redaction style). A press ANYWHERE ELSE -- a margin, a picture, a page whose
// text is not read yet -- drags out a RECTANGLE on that page, and a release past the click slop marks that area
// (`AnnotationOverlay::markArea`). Acrobat's tool behaves the same way, and a person reaches for both: a name in a
// paragraph, a signature in a scan.
//
// THE MARK REMOVES NOTHING (D6-25): what the tool makes is an annotation -- outlined in the tool's colour, carrying
// the fill, the overlay text and the repeat it will leave once applied -- and the page's content is never touched.
// Applying is Stage 10's. The style is the tool's defaults (`commenting/default<Property>.redactMark`, D6-13) under the
// session's change from the properties bar.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_REDACTTOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_REDACTTOOL_H

#include "core/doc/Redaction.h"
#include "ui/viewer/tools/AnnotationTool.h"
#include "ui/viewer/tools/Tool.h"

#include <optional>

namespace rawpage::ui {

class RedactTool final : public Tool {
public:
    static bool handles(AnnotationToolKind kind) noexcept { return kind == AnnotationToolKind::RedactMark; }

    ToolKind kind() const override { return ToolKind::Annotate; }
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }
    bool paintsOverlay() const override { return true; }

    void press(ToolHost& host, QPoint pos) override { pressWith(host, pos, PressContext{}); }
    void pressWith(ToolHost& host, QPoint pos, const PressContext& context) override;
    void move(ToolHost& host, QPoint pos) override;
    void release(ToolHost& host, QPoint pos) override;
    void cancel(ToolHost& host) override;
    bool isDragging() const override { return mode_ != Mode::None; }
    void paint(QPainter& painter, const ToolHost& host) const override;

    enum class Mode { None, Text, Area };
    Mode mode() const noexcept { return mode_; }

    // The style the tool marks with now: its defaults as the host has them.
    static core::RedactionStyle styleFor(const ToolHost& host);

private:
    Mode mode_ = Mode::None;
    int page_ = -1;
    QPoint pressedAt_;
    QPoint at_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_REDACTTOOL_H
