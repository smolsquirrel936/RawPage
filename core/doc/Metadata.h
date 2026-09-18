// core/doc/Metadata -- the document's Info dictionary, read. Stage 4 T4; decision D4-4
// (`Stage4-ViewingSurface-Plan.md` §3).
//
// ---------------------------------------------------------------------------------------------
// XMP IS NOT READ, NOT SHOWN, AND NOT CLAIMED (D4-4)
// ---------------------------------------------------------------------------------------------
// The FileMap's row for this file promises *"Info dictionary and XMP as one editable model, with
// round-trip preservation of XMP we do not understand"* -- and every clause after *"Info
// dictionary"* is about **writing**, which is Stage 8's. Reading an XMP packet in order to display
// it duplicates the Info fields for most documents and disagrees with them for some, and deciding
// which wins is a policy that belongs with the writer that has to keep both consistent (D3-5 left
// `/Info` and XMP untouched on save for exactly that reason). A properties dialog that shows what
// it read is honest; one that shows a merge nobody specified is a second source of truth.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS DELIBERATELY DOES NOT HOLD
// ---------------------------------------------------------------------------------------------
// The file's path, size and hash (`IDocumentBackend::documentId`), whether it was repaired
// (`repairReport`), whether it is encrypted and what it permits (`permissions`), and how many pages
// it has (`PageTree`). D4-4 lists all of those among what Document Properties SHOWS, and the dialog
// composes them from the backend it already has. Copying them here would make this the second place
// each is true -- the failure `SettingsKeys.def`'s rule 2 is written against, in another subject.
//
// What IS here is what only the Info dictionary and the file header can answer.
//
// ---------------------------------------------------------------------------------------------
// A DATE IS CARRIED AS IT WAS STORED
// ---------------------------------------------------------------------------------------------
// `/CreationDate` is specified as `D:YYYYMMDDHHmmSSOHH'mm'` and real producers write every
// variation of it, including some that are not dates at all. The raw string is therefore what this
// model holds, and `parseDate()` is offered beside it: a caller that can show a formatted date does
// so, and one looking at a malformed value shows **what the file says** rather than an invented
// substitute or an empty field (guide case A-28). A date the product invented would be a fact about
// the document that the document does not contain.

#ifndef RAWPAGE_CORE_DOC_METADATA_H
#define RAWPAGE_CORE_DOC_METADATA_H

#include <QDateTime>
#include <QString>

#include <optional>

namespace rawpage::core {

struct Metadata {
    QString title;
    QString author;
    QString subject;
    QString keywords;
    QString creator;    // the application that made the ORIGINAL
    QString producer;   // the one that made the PDF

    // Raw, exactly as stored. See the header.
    QString creationDate;
    QString modificationDate;

    // From the file header (`%PDF-1.7`), not from the Info dictionary -- it is the other thing only
    // the file itself can say. Empty when the engine cannot report one.
    QString pdfVersion;

    // True when nothing above carries a value. An empty Info dictionary is ORDINARY -- most
    // generated files have none -- and is not a failure (guide case A-28).
    bool isEmpty() const noexcept;

    // `std::nullopt` when the string is absent or is not a PDF date. Accepts the ISO 32000-1 form
    // with or without its `D:` prefix and with any trailing part omitted, which is what producers
    // actually emit; the offset, when present, is applied.
    static std::optional<QDateTime> parseDate(const QString& stored);
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_METADATA_H
