#include "ui/viewer/GridGuideLayer.h"

#include <QLineF>
#include <QPainter>
#include <QPointF>

#include <algorithm>
#include <cmath>

namespace rawpage::ui {
namespace {

using core::DevicePoint;
using core::Guide;
using core::PdfPoint;
using core::render::PageLayout;
using core::render::PageSlot;

// A guide's colour, and a locked guide's. FIXED in both themes, for the reason the `units/gridColour`
// row gives: these are painted over the PAGE, whose raster is the document's own -- a colour that
// followed the application palette would be chosen against a backdrop it never meets (convention
// 20's rule, read for a surface that is not part of the chrome).
QColor guideColour() { return QColor(0x00, 0x8C, 0xC8); }
QColor lockedGuideColour() { return QColor(0x8A, 0x8A, 0x8A); }

// Content space to WIDGET space, the one conversion in this file -- the same lambda shape
// `TilePresenter::paint` uses, because a second spelling of it is a second chance to divide by the
// device-pixel ratio in three places out of four.
QPointF toWidget(DevicePoint content, const core::DeviceRect& visible, double dpr)
{
    return QPointF((content.x - visible.left()) / dpr, (content.y - visible.top()) / dpr);
}

double distanceToSegment(DevicePoint p, DevicePoint a, DevicePoint b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 0.0) return std::hypot(p.x - a.x, p.y - a.y);

    // The projection, clamped to the segment: a guide is a finite line across its page, and an
    // infinite one would be grabbable from the page above it.
    const double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared, 0.0, 1.0);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

bool constrainsX(const Guide& guide) noexcept
{
    // A VERTICAL line on the page is a line of constant x. See the header: the field names the
    // direction and the constrained axis is the other one.
    return guide.orientation == Guide::Orientation::Vertical;
}

} // namespace

GridGuideLayer::GridGuideLayer(QObject* parent) : QObject(parent) {}

void GridGuideLayer::setSettings(const GuideSettings& settings)
{
    settings_ = settings;
    emit changed();
}

void GridGuideLayer::setGuides(std::vector<Guide> guides)
{
    guides_ = std::move(guides);
    drag_.reset();
    contextGuide_ = -1;
    emit changed();
}

int GridGuideLayer::addGuide(const Guide& guide)
{
    guides_.push_back(guide);
    emit changed();
    return static_cast<int>(guides_.size()) - 1;
}

bool GridGuideLayer::moveGuide(int index, double position)
{
    if (index < 0 || index >= static_cast<int>(guides_.size())) return false;

    // A LOCKED GUIDE DOES NOT MOVE, and the refusal is here rather than at each caller: the drag,
    // the context menu and a future batch step all go through this one function, and a lock
    // enforced at the gesture is a lock the next gesture forgets.
    if (guides_[static_cast<std::size_t>(index)].locked) return false;

    guides_[static_cast<std::size_t>(index)].position = position;
    emit changed();
    return true;
}

bool GridGuideLayer::setGuideLocked(int index, bool locked)
{
    if (index < 0 || index >= static_cast<int>(guides_.size())) return false;
    guides_[static_cast<std::size_t>(index)].locked = locked;
    emit changed();
    return true;
}

bool GridGuideLayer::removeGuide(int index)
{
    if (index < 0 || index >= static_cast<int>(guides_.size())) return false;

    // A drag on the removed guide is abandoned rather than left pointing at whatever slid into
    // that index.
    if (drag_ && drag_->index == index) drag_.reset();
    if (contextGuide_ == index) contextGuide_ = -1;
    guides_.erase(guides_.begin() + index);
    emit changed();
    return true;
}

void GridGuideLayer::clearGuides(int pageIndex)
{
    // A LOCKED GUIDE SURVIVES A CLEAR. That is what locking one is for -- `Features.md` §1.3 lists
    // lock and clear-all beside each other, and a clear that took the locked ones would make the
    // lock a decoration.
    const auto doomed = [pageIndex](const Guide& guide) {
        if (guide.locked) return false;
        return pageIndex < 0 || guide.pageIndex == pageIndex;
    };

    const auto first = std::remove_if(guides_.begin(), guides_.end(), doomed);
    if (first == guides_.end()) return;

    guides_.erase(first, guides_.end());
    drag_.reset();
    contextGuide_ = -1;
    emit changed();
}

int GridGuideLayer::guideCount(int pageIndex) const
{
    return static_cast<int>(std::count_if(guides_.begin(), guides_.end(),
                                          [pageIndex](const Guide& guide) {
                                              return pageIndex < 0 || guide.pageIndex == pageIndex;
                                          }));
}

std::optional<std::pair<DevicePoint, DevicePoint>> GridGuideLayer::segmentFor(
    const PageLayout& layout, const Guide& guide) const
{
    const PageSlot* slot = layout.slotForPage(guide.pageIndex);
    if (slot == nullptr) return std::nullopt;

    // The two ends in PAGE space, spanning the CropBox across the guide. `toContent` turns them
    // into content space carrying the rotation and the CropBox origin, so nothing here knows
    // which way the page is turned.
    PdfPoint a;
    PdfPoint b;
    if (constrainsX(guide)) {
        a = PdfPoint{guide.position, slot->cropBox.bottom()};
        b = PdfPoint{guide.position, slot->cropBox.top()};
    } else {
        a = PdfPoint{slot->cropBox.left(), guide.position};
        b = PdfPoint{slot->cropBox.right(), guide.position};
    }

    const auto first = layout.toContent(guide.pageIndex, a);
    const auto second = layout.toContent(guide.pageIndex, b);
    if (!first || !second) return std::nullopt;
    return std::make_pair(*first, *second);
}

void GridGuideLayer::setContextGuide(int index)
{
    contextGuide_ = (index >= 0 && index < static_cast<int>(guides_.size())) ? index : -1;
}

int GridGuideLayer::guideAt(const PageLayout& layout, DevicePoint content) const
{
    // A HIDDEN GUIDE IS NOT GRABBABLE. `view.toggleGuides` hides them without deleting them, and a
    // line a person cannot see that still catches their drag is the worst of both states.
    if (!settings_.showGuides) return -1;

    int best = -1;
    double bestDistance = kGrabDevicePixels;

    for (std::size_t i = 0; i < guides_.size(); ++i) {
        const auto segment = segmentFor(layout, guides_[i]);
        if (!segment) continue;

        const double distance = distanceToSegment(content, segment->first, segment->second);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

bool GridGuideLayer::beginDrag(int index)
{
    if (index < 0 || index >= static_cast<int>(guides_.size())) return false;
    if (guides_[static_cast<std::size_t>(index)].locked) return false;

    drag_ = Drag{index, guides_[static_cast<std::size_t>(index)].position};
    return true;
}

bool GridGuideLayer::dragTo(const PageLayout& layout, DevicePoint content)
{
    if (!drag_) return false;

    const Guide& guide = guides_[static_cast<std::size_t>(drag_->index)];
    const auto document = layout.toDocument(content);

    // A DRAG THAT LEAVES THE PAGE LEAVES THE GUIDE WHERE IT IS rather than dropping it or
    // clamping it to the edge: the pointer crosses the gutter constantly in a continuous layout,
    // and a guide that jumped to the margin every time would be unusable at any zoom that shows
    // two pages.
    if (!document || document->pageIndex != guide.pageIndex) return false;

    return moveGuide(drag_->index, constrainsX(guide) ? document->point.x : document->point.y);
}

bool GridGuideLayer::endDrag()
{
    if (!drag_) return false;
    drag_.reset();
    return true;
}

bool GridGuideLayer::cancelDrag()
{
    if (!drag_) return false;

    const Drag drag = *drag_;
    drag_.reset();
    if (drag.index >= 0 && drag.index < static_cast<int>(guides_.size())) {
        guides_[static_cast<std::size_t>(drag.index)].position = drag.startPosition;
        emit changed();
    }
    return true;
}

int GridGuideLayer::dropGuide(const PageLayout& layout, DevicePoint content,
                              Qt::Orientation rulerOrientation)
{
    const auto document = layout.toDocument(content);
    if (!document) return -1;

    const PageSlot* slot = layout.slotForPage(document->pageIndex);
    if (slot == nullptr) return -1;

    // WHICH PAGE AXIS A SCREEN AXIS IS depends on the combined rotation, which the slot already
    // carries (`PagePresentation.cpp` combines `/Rotate` with the view rotation before storing
    // it). The ruler along the TOP produces a line drawn vertically on screen; at 0 degrees that
    // is a line of constant page x, and at 90 or 270 it is a line of constant page y.
    const bool swapped = core::swapsAxes(slot->rotation);
    const bool vertical = (rulerOrientation == Qt::Horizontal) != swapped;

    Guide guide;
    guide.orientation = vertical ? Guide::Orientation::Vertical : Guide::Orientation::Horizontal;
    guide.position = vertical ? document->point.x : document->point.y;
    guide.pageIndex = document->pageIndex;
    guide.locked = false;
    return addGuide(guide);
}

core::SnapFlags GridGuideLayer::activeFlags() const noexcept
{
    core::SnapFlags flags = core::SnapFlags::None;
    if (settings_.snapToGrid && settings_.showGrid) flags = flags | core::SnapFlags::Grid;
    if (settings_.snapToGuides) flags = flags | core::SnapFlags::Guides;

    // `ObjectEdges` IS NEVER SET HERE -- D4-6 and D4-5. The key that would set it is Stage 6's,
    // and there is no annotation, field or content object at Stage 4 to be an edge.
    return flags;
}

core::SnapResult GridGuideLayer::snap(int pageIndex, PdfPoint point, core::SnapFlags flags,
                                      std::span<const core::SnapEdge> objectEdges,
                                      double scale) const
{
    // The guides of THIS page, as edges. A guide on page 3 must not attract a drag on page 4, and
    // the resolver takes coordinates with no page attached -- so the filter is here.
    std::vector<core::SnapEdge> edges;
    edges.reserve(guides_.size());
    for (const Guide& guide : guides_) {
        if (guide.pageIndex != pageIndex) continue;
        edges.push_back(core::SnapEdge{
            constrainsX(guide) ? core::SnapAxis::X : core::SnapAxis::Y, guide.position});
    }

    core::SnapContext context;
    context.objectEdges = objectEdges;
    context.guides = edges;
    context.grid = core::SnapGrid{settings_.gridSpacing, settings_.gridSubdivisions,
                                  settings_.gridOriginX, settings_.gridOriginY};
    context.scale = scale;
    context.thresholdDevicePixels = core::kSnapThresholdDevicePixels;
    return core::snap(point, flags, context);
}

QColor GridGuideLayer::gridColour() const
{
    const QColor colour(settings_.gridColour);

    // AN UNREADABLE VALUE FALLS BACK HERE rather than in the settings table: `core/` may not name
    // `QColor` (convention 3), so the table stores text and this is the only place that knows
    // whether the text is a colour. A hand-edited INI saying `units/gridColour=blueish` gets the
    // declared default, not an invisible grid.
    return colour.isValid() ? colour : QColor(QStringLiteral("#6699CC"));
}

void GridGuideLayer::paint(QPainter& painter, const PageLayout& layout,
                           const core::DeviceRect& visible, double devicePixelRatio) const
{
    const double dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    const double scale = layout.presentedScale();
    if (!(scale > 0.0)) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);

    if (settings_.showGrid) {
        const core::SnapGrid grid{settings_.gridSpacing, settings_.gridSubdivisions,
                                  settings_.gridOriginX, settings_.gridOriginY};
        const double step = grid.step();

        // A grid whose finest line is closer than one device pixel is a fill, not a grid. Skipping
        // it is not a cosmetic choice: at 8 % zoom the loop below would otherwise walk tens of
        // thousands of lines per page to paint a solid rectangle.
        if (step > 0.0 && step * scale >= 1.0) {
            const QColor colour = gridColour();
            QColor minor = colour;
            minor.setAlpha(96);   // the subdivisions, lighter than the major lines they divide

            for (const PageSlot& slot : layout.slotsIntersecting(visible)) {
                painter.setClipRect(QRectF(toWidget(slot.rect.origin, visible, dpr),
                                           QSizeF(slot.rect.size.width / dpr,
                                                  slot.rect.size.height / dpr)));

                const auto line = [&](PdfPoint a, PdfPoint b, const QColor& pen) {
                    const auto from = layout.toContent(slot.pageIndex, a);
                    const auto to = layout.toContent(slot.pageIndex, b);
                    if (!from || !to) return;
                    painter.setPen(QPen(pen, 0.0));
                    painter.drawLine(QLineF(toWidget(*from, visible, dpr),
                                            toWidget(*to, visible, dpr)));
                };

                // MAJOR EVERY `subdivisions` STEPS, counted from the origin rather than from the
                // page: `units/gridOriginX` moves the lines, so which line is major has to be
                // counted from the same place the lines are measured from, or the offset would
                // shift the pattern and the emphasis differently.
                const auto majorEvery = std::max(1, settings_.gridSubdivisions);

                const auto walk = [&](double origin, double low, double high, bool vertical) {
                    const long long firstIndex =
                        static_cast<long long>(std::ceil((low - origin) / step));
                    const long long lastIndex =
                        static_cast<long long>(std::floor((high - origin) / step));
                    for (long long i = firstIndex; i <= lastIndex; ++i) {
                        const double at = origin + static_cast<double>(i) * step;
                        const bool major = (((i % majorEvery) + majorEvery) % majorEvery) == 0;
                        if (vertical) {
                            line(PdfPoint{at, slot.cropBox.bottom()},
                                 PdfPoint{at, slot.cropBox.top()}, major ? colour : minor);
                        } else {
                            line(PdfPoint{slot.cropBox.left(), at},
                                 PdfPoint{slot.cropBox.right(), at}, major ? colour : minor);
                        }
                    }
                };

                walk(grid.originX, slot.cropBox.left(), slot.cropBox.right(), true);
                walk(grid.originY, slot.cropBox.bottom(), slot.cropBox.top(), false);
                painter.setClipping(false);
            }
        }
    }

    // THE GUIDES ARE PAINTED WHETHER OR NOT THE GRID IS SHOWN, and whether or not the rulers are --
    // `units/showGuides` is the one switch that hides them. A guide is something a person put on
    // this page; hiding it with a grid they never asked to see would lose work that has no other
    // surface.
    for (std::size_t i = 0; settings_.showGuides && i < guides_.size(); ++i) {
        const Guide& guide = guides_[i];
        const auto segment = segmentFor(layout, guide);
        if (!segment) continue;

        QPen pen(guide.locked ? lockedGuideColour() : guideColour(), 0.0);
        if (guide.locked) pen.setStyle(Qt::DashLine);
        if (drag_ && drag_->index == static_cast<int>(i)) pen.setWidthF(2.0);
        painter.setPen(pen);
        painter.drawLine(QLineF(toWidget(segment->first, visible, dpr),
                                toWidget(segment->second, visible, dpr)));
    }

    painter.restore();
}

} // namespace rawpage::ui
