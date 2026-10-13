// ui/viewer/SnapTargets -- one document's pages' PATHS, for the measuring tools to snap to. Stage 6 T12, decision
// D6-14 (`Stage6-Annotations-Plan.md` §3).
//
// THE PATHS LIVE IN THE TEXT INDEX, NOT HERE. A page's geometry is read once, on the engine thread, by
// `core/text/DocumentIndex` (`requestGeometry`) and kept beside the page's text -- so a second window over the same
// file reads nothing twice, and the second snap on a page asks the engine for nothing (guide A-91). This class is
// the document window's ONE OWNER of those requests: `geometry()` answers what the index holds, and asks at
// `Visible` -- a person is aiming -- for a page it does not hold yet; `arrived` says when it came, so the panes
// repaint the snap they could not show.
//
// A PAGE WITH TOO MANY SEGMENTS (`core::PageGeometry::kMaxSegments`) gives no object snaps, and `tooManySegments`
// says so ONCE per page per document -- not once per pointer move over it.
//
// One per `ui/shell/DocumentWindow`, handed to every pane of its split as the text selection is. Every member is
// called on the UI thread, which is the index's pumping thread.

#ifndef RAWPAGE_UI_VIEWER_SNAPTARGETS_H
#define RAWPAGE_UI_VIEWER_SNAPTARGETS_H

#include "core/Snap.h"
#include "core/text/DocumentIndex.h"

#include <QObject>

#include <memory>
#include <set>

namespace rawpage::ui {

class SnapTargets : public QObject {
    Q_OBJECT

public:
    explicit SnapTargets(core::text::DocumentIndex& index, QObject* parent = nullptr);
    ~SnapTargets() override;

    // The document; 0 detaches and lets go of every request.
    void setDocument(core::text::DocumentIndex::EntryId entry, int pageCount);
    core::text::DocumentIndex::EntryId entry() const noexcept { return entry_; }

    // Page `page`'s paths, or null while they are being read (asked for here, at `Visible`, the first time).
    std::shared_ptr<const core::PageGeometry> geometry(int page);
    // Every request let go of -- what the window does before its handle leaves the index.
    void release();

signals:
    void arrived(int page);
    void tooManySegments(int page, qulonglong counted);

private:
    void onIndexEvent(const core::text::DocumentIndex::Event& event);

    core::text::DocumentIndex& index_;
    const core::text::DocumentIndex::Owner owner_;
    core::text::DocumentIndex::SubscriptionToken subscription_ = 0;
    core::text::DocumentIndex::EntryId entry_ = 0;
    int pageCount_ = 0;
    std::set<int> said_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_SNAPTARGETS_H
