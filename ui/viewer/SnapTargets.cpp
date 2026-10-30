// ui/viewer/SnapTargets -- implementation. Stage 6 T12. See the header for why the paths live in the index.

#include "ui/viewer/SnapTargets.h"

namespace rawpage::ui {

SnapTargets::SnapTargets(core::text::DocumentIndex& index, QObject* parent)
    : QObject(parent), index_(index), owner_(index.newOwner())
{
    subscription_ = index_.subscribe([this](const core::text::DocumentIndex::Event& event) { onIndexEvent(event); });
}

SnapTargets::~SnapTargets()
{
    index_.unsubscribe(subscription_);
    index_.release(owner_);
}

void SnapTargets::setDocument(core::text::DocumentIndex::EntryId entry, int pageCount)
{
    if (entry == entry_ && pageCount == pageCount_) return;
    index_.release(owner_);
    entry_ = entry;
    pageCount_ = pageCount;
    said_.clear();
}

std::shared_ptr<const core::PageGeometry> SnapTargets::geometry(int page)
{
    if (entry_ == 0 || page < 0 || page >= pageCount_) return nullptr;
    if (std::shared_ptr<const core::PageGeometry> held = index_.geometry(entry_, page)) return held;
    (void)index_.requestGeometry(owner_, entry_, page, core::render::RenderPriority::Visible);
    return index_.geometry(entry_, page);
}

void SnapTargets::release()
{
    index_.release(owner_);
}

void SnapTargets::onIndexEvent(const core::text::DocumentIndex::Event& event)
{
    if (event.entry != entry_ || entry_ == 0) return;
    using Kind = core::text::DocumentIndex::Event::Kind;
    if (event.kind == Kind::EntryDropped) {
        entry_ = 0;
        said_.clear();
        return;
    }
    // S7 T3 (D7-3): the pages it reads from are counted afresh; what it said about a page is said again.
    if (event.kind == Kind::PagesRemapped) {
        if (event.map != nullptr) pageCount_ = event.map->newCount();
        said_.clear();
        return;
    }
    if (event.kind != Kind::GeometryArrived) return;
    const std::shared_ptr<const core::PageGeometry> held = index_.geometry(entry_, event.page);
    if (held && held->overLimit() && said_.insert(event.page).second)
        emit tooManySegments(event.page, static_cast<qulonglong>(held->counted()));
    emit arrived(event.page);
}

} // namespace rawpage::ui
