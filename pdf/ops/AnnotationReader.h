// pdf/ops/AnnotationReader -- every page's `/Annots`, read first-party into `core::AnnotationStore`'s
// reading. Stage 6 T2, decision **D6-3** (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// WHY FIRST-PARTY, WHEN PDFIUM READS ANNOTATIONS
// ===========================================================================================
// Editing needs what PDFium's public API does not give: each annotation's OBJECT NUMBER -- the
// identity a command names and the journal replays against (D6-4) -- and the dictionary as written,
// which an edit starts from (D6-5). We write through our own code, so we read the object we will
// rewrite through our own code. PDFium stays the renderer, and `core::Permissions::assessAnnotations`
// holds the two readings to agree page by page; this reader exists to be compared, not trusted alone.
//
// It runs off the walk `pdf/DocumentBackendImpl` already makes for every document (the page tree
// `MutationSinkImpl::open` checks against the engine's), as `TransitionReader` and
// `EmbeddedFileReader` do -- no second parse.
//
// ===========================================================================================
// TOLERANT, BOUNDED, AND NEVER SILENT
// ===========================================================================================
// Nothing here fails. A `/Annots` that is not an array lists nothing; an entry that is not a
// dictionary is a listing entry with no annotation; a non-finite `/Rect` keeps the annotation with an
// empty rectangle -- each REPORTED as a `core::AnnotationNote`. (The last is DEFENSIVE: `pdf/cos/Lexer`
// clamps an out-of-range number to a finite one, so no input reaches it today -- measured at T2, guide
// A-22 -- and it stays because the lexer's choice is not this reader's to rely on silently.) Each is
// reported because what the reader skipped is what someone will later ask about. And every array an attacker controls is bounded: no annotation
// keeps more than `kMaxCoordinates` numbers of geometry (a 1,000,000-point `/InkList` is truncated and
// reported, guide case A-22), and an annotation reached only through `/IRT`, `/Popup` or `/Parent` --
// listed by no page -- is reported once and never read further (D6-3: it has no page to belong to).

#ifndef RAWPAGE_PDF_OPS_ANNOTATIONREADER_H
#define RAWPAGE_PDF_OPS_ANNOTATIONREADER_H

#include "core/doc/AnnotationStore.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/PageOps.h"

#include <cstddef>

namespace rawpage::pdf::ops {

class AnnotationReader {
public:
    // Numbers of geometry one annotation keeps -- quadpoints, vertices, ink samples counted together.
    // 200,000 is 100,000 points: far past any hand-drawn stroke, and 1.6 MB of doubles at most.
    static constexpr std::size_t kMaxCoordinates = 200000;

    // Every page `walk` found, in document order. Never fails.
    static core::AnnotationStore::Reading read(cos::XrefTable& table, const PageWalk& walk);

    // ONE ANNOTATION DICTIONARY, read as `read` reads each -- every modelled field, bounded the same way; its
    // identity and page are the caller's (Stage 6 T13: an FDF's annotations are the same dictionaries, listed by
    // `/FDF /Annots` rather than by a page, `pdf/ops/FdfCodec`).
    struct Parsed {
        core::Annotation annotation;
        std::optional<long long> pageReference;   // its /P
        bool nonFiniteRect = false;
        bool truncated = false;
    };
    static Parsed parseDictionary(cos::XrefTable& table, const cos::CosObject& dictionary);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_ANNOTATIONREADER_H
