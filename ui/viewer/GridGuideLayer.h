// The grid, the guides and the snap resolution they feed -- Stage 4 T8, `Architecture-FileMap.md`
// §3.14. Decisions D4-6 (the resolver), D4-7 (guides are view state) and D4-11 (this is painted
// over the tiles, never into them).
//
// ===========================================================================================
// IT IS PAINTED AT PRESENTATION, AND THAT IS THE WHOLE REASON IT IS A LAYER AND NOT A FLAG.
// ===========================================================================================
// D4-11: a grid line, a guide and a ruler are paint operations over the pixels the scheduler
// already returned. Composing them into the tile would put the grid's spacing, colour and offset
// into the CACHE KEY -- so dragging the spacing slider would re-rasterize the document, and the
// 256 MiB O7 resolved to keep whole would hold one grid's worth of every page. Guide case A-50
// asserts exactly that: changing any `units/grid*` key enqueues no render request.
//
// The one thing that IS in the tile stays there: `RenderFlags::Grayscale` is a colour conversion
// inside the rasterizer rather than a map over pixels we have.
//
// ===========================================================================================
// A GUIDE IS A PAGE FACT. THE RULER READS THE SCREEN.
// ===========================================================================================
// `core::Guide::position` is PDF points in page space (D4-7, `core/settings/SessionState.h`), so a
// guide is in the same place at 50 % and at 400 % and after a view rotation -- it belongs to the
// page, not to the window. **`Orientation::Horizontal` means a horizontal line ON THE PAGE, which
// is a line of constant y**; `Vertical` is constant x. That is said here because the field is a
// direction and the thing it constrains is the other axis, which is what a reader gets backwards.
//
// Every mapping in this file goes through `PageLayout::toContent` / `toDocument`, which already
// carry the page's `/Rotate` COMBINED with the view rotation and the CropBox offset
// (`core/render/PagePresentation.cpp`). So a guide under a rotated view is drawn across the page
// the way the page is turned, and none of that arithmetic is repeated here.
//
// `ui/viewer/RulerWidget` is the other half and works the other way round: it measures what is on
// the SCREEN -- distance from the page's drawn top-left corner along the screen axis -- which is
// rotation-independent by construction. The two agree at every rotation because both are derived
// from the same `PageSlot`.
//
// ===========================================================================================
// THE SUMS ARE IN `core/Snap.h`, AND THAT IS NOT WHERE D4-6 PUT THEM.
// ===========================================================================================
// D4-6 names `GridGuideLayer::snap`. The precedence and the device-pixel threshold live one layer
// down, in `core/Snap.h`, because `Stage4-ViewingSurface-Plan.md` §5.1 and §5.6 both require the
// stage's snap arithmetic to be testable with **no display**: `tst_snap` is listed in
// `rawpage_tests`, which links neither Qt Widgets nor an engine. This class holds what a widget
// must hold -- the guides, the appearance, the drag in progress -- and calls down for the sums.
//
// `snap()` here also takes the PAGE INDEX and a `core::PdfPoint` rather than the plan's bare
// `QPointF`: a `QPointF` cannot say whether it is page space or device space, which is the single
// bug `core/Geometry.h` exists to prevent, and the guides that participate are the ones on that
// page. The object-edge span is D4-6's, unchanged, and **every product caller passes it EMPTY
// until Stage 6**.

#ifndef RAWPAGE_UI_VIEWER_GRIDGUIDELAYER_H
#define RAWPAGE_UI_VIEWER_GRIDGUIDELAYER_H

#include "core/Snap.h"

#include <functional>
#include "core/Units.h"
#include "core/render/PagePresentation.h"
#include "core/settings/SessionState.h"

#include <QColor>
#include <QObject>
#include <QString>

#include <optional>
#include <span>
#include <vector>

class QPainter;

namespace rawpage::ui {

// The eleven `units/*` rows, resolved into what a view needs -- a struct of plain values rather than
// a `Settings&`, for the reason `ViewSettings` and `core::render::PresentationOptions` both give:
// the composition root reads the table and hands values down, so a case can drive this class with
// no settings file and nothing below `app/` learns that a preference is where these came from.
//
// The defaults restate `SettingsKeys.def`'s, which is a second copy of a default and is tolerable
// for exactly the reason `ViewSettings` records: these are the absent-store fallbacks for a test,
// `DocumentWindow` overwrites every field from the table, and a case asserts the two agree.
struct GuideSettings {
    core::Unit unit = core::Unit::Inches;

    bool showRulers = false;
    bool showGrid = false;
    bool showGuides = true;

    double  gridSpacing = 72.0;
    int     gridSubdivisions = 4;
    QString gridColour = QStringLiteral("#6699CC");
    double  gridOriginX = 0.0;
    double  gridOriginY = 0.0;

    bool snapToGrid = false;
    bool snapToGuides = true;
    // S6 T12 (D4-5, D6-14): `units/snapToObjects` -- the page's paths' points for a measurement, and its
    // annotations' sides for a measurement and a guide -- and `measuring/snapToPaths`, a point ON a path as well.
    bool snapToObjects = true;
    bool snapToPaths = true;
};

class GridGuideLayer : public QObject {
    Q_OBJECT

public:
    explicit GridGuideLayer(QObject* parent = nullptr);

    // How close, in DEVICE pixels, the pointer must be to grab a guide. Larger than the snap
    // threshold on purpose: missing a one-pixel line is a worse gesture than snapping slightly
    // early, and a line that cannot be picked up reads as a guide that cannot be moved.
    static constexpr double kGrabDevicePixels = 6.0;

    void setSettings(const GuideSettings& settings);
    const GuideSettings& settings() const noexcept { return settings_; }

    // ---- The guides (D4-7) ---------------------------------------------------------------------
    // Held here and handed to `core::SessionState` by the shell; **nothing about them reaches
    // `pdf/`**, which guide case A-48 asserts on the document's bytes rather than on an intention.
    const std::vector<core::Guide>& guides() const noexcept { return guides_; }
    void setGuides(std::vector<core::Guide> guides);

    int  addGuide(const core::Guide& guide);
    bool moveGuide(int index, double position);
    bool setGuideLocked(int index, bool locked);
    bool removeGuide(int index);

    // `pageIndex < 0` clears every page. Both are reachable: the ruler's context menu clears the
    // page in front of the person, and a document-wide clear is what *"clear all guides"*
    // (`Features.md` §1.3) means.
    void clearGuides(int pageIndex = -1);

    int guideCount(int pageIndex) const;

    // WHICH GUIDE A COMMAND ACTS ON. `view.lockGuide` and `view.deleteGuide` are dispatched
    // through `ActionRegistry` like everything else (D1-7(a)), and dispatch carries no argument --
    // so the target is state, set when the context menu is raised and cleared when it is not over
    // one. This is Stage 1's **I-50** in a second place: *what is under the cursor decides, not
    // which call site raised the menu.*
    void setContextGuide(int index);
    int  contextGuide() const noexcept { return contextGuide_; }

    // ---- Hit testing and the drag ---------------------------------------------------------------
    // `content` is a point in CONTENT device space -- the space `ViewportController::visibleRect`
    // and `PageLayout` are in -- not widget pixels. The caller converts once, at the widget edge.
    int guideAt(const core::render::PageLayout& layout, core::DevicePoint content) const;

    // A LOCKED guide is not picked up. It is still hit-testable (so a context menu can offer to
    // unlock it), which is why the lock is checked here and not inside `guideAt`.
    bool beginDrag(int index);
    bool dragTo(const core::render::PageLayout& layout, core::DevicePoint content);

    // Ends the drag, keeping where it was dragged to. Returns whether one was in progress.
    bool endDrag();

    // `Esc`: puts the guide back where it started. Returns whether anything was abandoned --
    // **false must not consume the key**, for the reason `ToolController::cancel` gives.
    bool cancelDrag();

    bool dragging() const noexcept { return drag_.has_value(); }
    int  draggedGuide() const noexcept { return drag_ ? drag_->index : -1; }

    // A guide dragged out of a ruler and dropped on the page. `rulerOrientation` is the RULER's --
    // `Qt::Horizontal` is the one along the top, and it produces a guide drawn vertically on
    // screen. Which PAGE axis that is depends on the view rotation, and is read from the slot
    // rather than assumed. Returns the new guide's index, or -1 when the drop was not on a page.
    int dropGuide(const core::render::PageLayout& layout, core::DevicePoint content,
                  Qt::Orientation rulerOrientation);

    // THE DOTTED PREVIEW of a guide being pulled off a ruler, on the user's request of 2026-09-26
    // (`Stage4-ViewingSurface-Testing-Guide.md` A-89): shown while the button is still down over a
    // page, at exactly the coordinate `dropGuide` would store -- snapped included -- and gone on the
    // release or when the pointer leaves the page. It is NOT a guide: it is not in `guides()`, is
    // never captured into the session and cannot be grabbed. Returns whether there is one.
    bool previewGuide(const core::render::PageLayout& layout, core::DevicePoint content,
                      Qt::Orientation rulerOrientation);
    void clearPreview();
    const std::optional<core::Guide>& preview() const noexcept { return preview_; }

    // A placed guide's colour, public so a case can find its pixels without a second copy of it.
    static QColor guideColour();

    // ---- Snapping (D4-6) -------------------------------------------------------------------------
    // The flags the `units/snap*` rows currently ask for. **Since S6 T12 `ObjectEdges` is set by
    // `units/snapToObjects`**, the key Stage 4 left to Stage 6 (D4-5) -- and the edges come from the source
    // below, so the flag is never set over a span nobody fills.
    core::SnapFlags activeFlags() const noexcept;

    // S6 T12: WHERE OBJECT EDGES COME FROM -- the sides of the annotations on a page, which `PageView` knows and this
    // layer does not. A drag or a drop with `ObjectEdges` set asks it for the page's.
    using ObjectEdgeSource = std::function<std::vector<core::SnapEdge>(int pageIndex)>;
    void setObjectEdgeSource(ObjectEdgeSource source) { objectEdges_ = std::move(source); }

    // S6 T12 (D6-14): A MEASURED POINT -- `points` (a page's path targets near it, `core::snapPointsNear`) first,
    // then the page's annotation sides, the guides and the grid, as `activeFlags()` and `units/snapToObjects` allow.
    core::SnapResult snapMeasured(int pageIndex, core::PdfPoint point, std::span<const core::SnapPoint> points,
                                  double scale) const;

    // `scale` is device pixels per PDF point -- `PageLayout::presentedScale()`. **This is what
    // makes the tolerance the same at 50 % and 400 %** (D4-6).
    //
    // **`dragTo` and `dropGuide` resolve through this with `activeFlags()`, and until I-16 nothing
    // did** (`Stage4-ViewingSurface-Testing-Guide.md` §8): the method existed, `tst_snap` proved
    // the sums under it, and no gesture called it -- so `Ctrl+;` toggled a setting and a checked
    // state and moved no guide. Guide case A-87 drags through the widgets and asserts the landing.
    core::SnapResult snap(int pageIndex, core::PdfPoint point, core::SnapFlags flags,
                          std::span<const core::SnapEdge> objectEdges, double scale) const;

    // ---- Painting (D4-11) -------------------------------------------------------------------------
    // `visible` is the content-space rectangle on screen and `dpr` converts it to widget pixels --
    // the same two arguments `TilePresenter::paint` takes, and for the same reason.
    void paint(QPainter& painter, const core::render::PageLayout& layout,
               const core::DeviceRect& visible, double devicePixelRatio) const;

signals:
    // Anything that changes what is painted. `PageView` connects it to a repaint and to NOTHING
    // ELSE: a grid change must not reach the scheduler (A-50).
    void changed();

private:
    struct Drag {
        int    index = -1;
        double startPosition = 0.0;
    };

    // The guide's line across its page, in content device space, clipped to the CropBox. Nothing
    // when the guide's page is not laid out.
    std::optional<std::pair<core::DevicePoint, core::DevicePoint>> segmentFor(
        const core::render::PageLayout& layout, const core::Guide& guide) const;

    QColor gridColour() const;

    // The guide a release at `content` off a ruler of `rulerOrientation` would make, snapped; nothing
    // off the page. One function for `dropGuide` and `previewGuide`, so the two cannot disagree.
    std::optional<core::Guide> guideFromRuler(const core::render::PageLayout& layout,
                                              core::DevicePoint content,
                                              Qt::Orientation rulerOrientation) const;

    // `snap()` with one guide left out of the candidates -- the one being dragged, which is always
    // within the threshold of the pointer and would otherwise pin itself in place. -1 leaves none out.
    core::SnapResult resolve(int pageIndex, core::PdfPoint point, core::SnapFlags flags,
                             std::span<const core::SnapEdge> objectEdges, double scale,
                             int excludedGuide, std::span<const core::SnapPoint> objectPoints = {}) const;
    ObjectEdgeSource objectEdges_;

    GuideSettings          settings_;
    std::vector<core::Guide> guides_;
    std::optional<Drag>     drag_;
    std::optional<core::Guide> preview_;
    int                     contextGuide_ = -1;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_GRIDGUIDELAYER_H
