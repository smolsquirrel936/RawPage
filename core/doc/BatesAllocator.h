// core/doc/BatesAllocator -- one Bates number line shared by several documents. Stage 7 T14, decision D7-14
// (`Stage7-Assembly-Plan.md` §3).
//
// *Bates numbering continued across many documents in a single run, in a chosen order* (`Features.md` §5.7) is
// ONE logical run: the documents the person picks, in the order they pick, take consecutive numbers -- the first
// page of the second document is one past the last page of the first. But a command belongs to ONE document's
// journal, so the run is several commands that share a number line: this allocator decides each document's first
// number before any command exists, and each document then gets its own `ApplyBatesCommand` starting there --
// undone, saved and recovered with its document alone (D7-14). A document an allocation skips (it refused) leaves
// no gap: the allocator is told what was actually applied, and the report states exactly that.
//
// Q5(c) ✅: documents that are not OPEN -- a file list, a folder -- are Stage 14's batch.
//
// Pure: std and QtCore only -- `tst_bates` in `rawpage_tests`.

#ifndef RAWPAGE_CORE_DOC_BATESALLOCATOR_H
#define RAWPAGE_CORE_DOC_BATESALLOCATOR_H

#include "core/Result.h"
#include "core/doc/HeaderFooter.h"

#include <QString>

#include <vector>

namespace rawpage::core {

// One document's share of a run: how many pages it numbers, and the numbers they get.
struct BatesShare {
    QString document;      // what the report calls it -- its file name, or its tab's title
    int pages = 0;         // the pages numbered
    long long first = 0;   // the first page's number
    long long last = 0;    // the last page's: first + pages - 1
    friend bool operator==(const BatesShare&, const BatesShare&) = default;
};

class BatesAllocator {
public:
    // A run starting at `format.start`, every number `format.digits` wide.
    explicit BatesAllocator(BatesFormat format) : format_(std::move(format)), next_(format_.start) {}

    // The next `pages` numbers, for `document`: `error.decorate.batesOverflow` when the last would need more digits
    // than the format has -- or pass `kMaxBatesNumber` -- and nothing is taken. Zero pages is refused
    // (`error.decorate.noPages`).
    Result<BatesShare> take(const QString& document, int pages);
    // Gives back the LAST share taken -- a document whose command then refused -- so the next one starts where it
    // would have. Refused for anything but the last share.
    Status giveBack(const BatesShare& share);

    // The shares still taken, in order: the run's report.
    const std::vector<BatesShare>& report() const noexcept { return shares_; }
    long long next() const noexcept { return next_; }
    const BatesFormat& format() const noexcept { return format_; }

private:
    BatesFormat format_;
    long long next_ = 1;
    std::vector<BatesShare> shares_;
};

// The largest number `digits` digits can write.
long long largestBates(int digits) noexcept;

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_BATESALLOCATOR_H
