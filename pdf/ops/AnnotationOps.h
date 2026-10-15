// pdf/ops/AnnotationOps -- what an annotation intent means as COS objects. Stage 6 T3, decisions D6-4
// and D6-5 (`Stage6-Annotations-Plan.md` §3). `pdf/ops/MutationSinkImpl` decides WHEN an object is
// written; this file decides what goes in it.
//
// ===========================================================================================
// A CREATED ANNOTATION CARRIES ITS IDENTITY (D6-4)
// ===========================================================================================
// Every annotation RawPage writes has `/NM` (its identity, from `core::newAnnotation`), `/M`,
// `/CreationDate`, `/T` and `/P` -- `/NM` because XFDF and FDF merge on it and Acrobat writes one on
// every annotation, `/P` because a reader that finds an annotation through its own index rather than
// a page's `/Annots` needs to know its page. Every other modelled field is written when it is set, and
// the `/AP` reference when there is one.
//
// ===========================================================================================
// AN EDIT CHANGES WHAT IT MODELS AND KEEPS EVERYTHING ELSE (D6-5)
// ===========================================================================================
// `editedDictionary()` starts from the dictionary ON DISK and overwrites only the keys of the modelled
// fields that differ between `before` and `after`; every other key -- a producer's `/PieceInfo`, a
// private key nobody here understands -- is kept, value for value, in its place. Two keys are the
// exception, and both are the rule's own consequence:
//   * **`/RC` is rewritten when `/Contents` changes** (unless the edit set `/RC` itself), as a plain
//     XHTML body of the new text: Acrobat shows `/RC` in preference to `/Contents`, so a stale one
//     would show the old text in the reader that matters most. A dictionary with no `/RC` gets none.
//   * **`/AP` is never touched here.** Whether an edit changes the appearance -- and so whether
//     `AppearanceStreamBuilder` must draw a new one -- is the sink's question; an annotation RawPage
//     only reads keeps its producer's appearance.
// Stage 6 T3 builds and tests this rule; `EditAnnotationCommand`, its first caller, is T5's.
//
// ===========================================================================================
// A REFERENCE TO ANOTHER ANNOTATION IS RESOLVED BY THE CALLER (Stage 6 T6)
// ===========================================================================================
// `/IRT`, `/Popup` and `/Parent` name an annotation by identity (`core::Annotation`): an object read
// from the file, or one RawPage created that has no number until the compose that writes it gives it
// one. Which number that is, is the sink's knowledge -- this delta's, or the save that wrote it -- so
// both functions take a `ReferenceResolver`. One that cannot resolve a reference (a created target
// that is no longer listed anywhere, so nothing writes it) leaves the key out rather than point at
// nothing. The default resolves object numbers only, at generation 0.

#ifndef RAWPAGE_PDF_OPS_ANNOTATIONOPS_H
#define RAWPAGE_PDF_OPS_ANNOTATIONOPS_H

#include "core/Result.h"
#include "core/doc/Annotation.h"
#include "pdf/cos/CosObject.h"

#include <QString>

#include <functional>
#include <optional>
#include <string>

namespace rawpage::pdf::ops {

// The object a reference to an annotation is written as, or none to leave the key out.
using ReferenceResolver = std::function<std::optional<cos::CosRef>(const core::AnnotationId&)>;

class AnnotationOps {
public:
    // Object numbers as written, at generation 0; a created identity resolves to nothing.
    static ReferenceResolver objectReferences();

    // A PDF text string (ISO 32000-1 §7.9.2.2): PDFDocEncoding's ASCII range as a literal when every
    // character is in it, else UTF-16BE behind its byte-order mark, as a hex string.
    static cos::CosObject textString(const QString& text);

    // The dictionary of an annotation RawPage created: the identity keys above and every modelled field
    // that is set. `page` is its page's reference; `appearance`, when present, becomes `/AP << /N … >>`.
    static core::Result<cos::CosObject> createdDictionary(const core::Annotation& annotation,
                                                          cos::CosRef page,
                                                          std::optional<cos::CosRef> appearance,
                                                          const ReferenceResolver& resolve = objectReferences());

    // THE SAME DICTIONARY AS AN FDF CARRIES IT (Stage 6 T13, D6-20): every modelled field from the same table, its
    // page as `/Page` -- the 0-based index ISO 32000-1 Table 247 gives an FDF annotation -- and no `/P` and no `/AP`.
    static core::Result<cos::CosObject> exchangeDictionary(const core::Annotation& annotation,
                                                           const ReferenceResolver& resolve);

    // D6-5, as above: `onDisk` with only the keys of the fields that differ between `before` and `after`
    // overwritten -- or removed, where `after` no longer has the field. A stream stays a stream.
    static cos::CosObject editedDictionary(const cos::CosObject& onDisk, const core::Annotation& before,
                                           const core::Annotation& after,
                                           const ReferenceResolver& resolve = objectReferences());

    // The `/RC` an edit writes for `contents`: a plain XHTML body, one `<p>` per line, escaped.
    static QString richTextFor(const QString& contents);

    // `dictionary` with `key` set to `value` -- in its place when present, appended when not -- and
    // every other entry kept in order. A stream keeps its data descriptor.
    static cos::CosObject withEntry(const cos::CosObject& dictionary, const std::string& key, cos::CosObject value);
    // `dictionary` without `key`.
    static cos::CosObject withoutEntry(const cos::CosObject& dictionary, const std::string& key);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_ANNOTATIONOPS_H
