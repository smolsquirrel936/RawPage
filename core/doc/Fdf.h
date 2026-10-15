// core/doc/Fdf -- the EXCHANGE MODEL: annotations as FDF and XFDF carry them between documents and programs, and
// what an import of them into a document MEANS. Stage 6 T13, decisions D6-20 and D6-40 (`Stage6-Annotations-Plan.md`
// §3).
//
// ===========================================================================================
// ONE MODEL, TWO SYNTAXES, READ IN TWO LAYERS (D6-20)
// ===========================================================================================
// FDF is PDF syntax -- objects, `/Annots`, `/IRT 12 0 R` -- so it is read and written by `pdf/ops/FdfCodec` over
// `pdf/cos/`, the one layer that may hold the lexer. XFDF is XML, read by `core/doc/XfdfCodec` over QtCore's
// `QXmlStreamReader`. Both produce THIS: a list of `core::Annotation`s, each named by its `/NM` (an `AnnotationId`
// of kind `Created`), each reference to another (`/IRT`, `/Popup`, `/Parent`) by that other's name, and each page
// as an index. So a merge, an export and a migration (`core/doc/CommentMigrator`) are one piece of code whichever
// syntax the file was in, and the round trip of guide §3.7 compares one model with itself.
//
// ===========================================================================================
// A NAME FOR EVERYTHING
// ===========================================================================================
// FDF and XFDF name an annotation by `/NM` -- what a merge matches on (ISO 32000-1 §12.7.8, and Acrobat writes one on
// every comment). Two things carry none and still need one here, each given one that is DETERMINED BY THE FILE, so
// reading a file twice gives the same model:
//   * A POP-UP, which XFDF writes inside its parent and FDF as an object its parent points at: `<parent>-popup`
//     (`popupNameFor`). It is never matched -- a pop-up follows its parent through a merge.
//   * An annotation whose producer wrote no `/NM`: `rawpage-content-` and a digest of what it says
//     (`contentNameOf`) -- its kind, page, place, words, author and dates. Two such annotations that say the same
//     thing ARE the same comment, so an export of a document whose comments carry no `/NM`, imported back into it,
//     finds each one rather than adding a copy: the merge computes the same name for the document's own.
//
// ===========================================================================================
// THE MERGE (D6-20): BY `/NM`, NEWER WINS, AND EVERYTHING IS SAID
// ===========================================================================================
// `AnnotationStore::importFdf` decides, for each annotation the exchange holds -- pop-ups follow their parents and
// are not counted:
//   * a name the document does not hold is ADDED -- a `Created` annotation with that `/NM`;
//   * a name it holds, with a NEWER `/M` (else `/CreationDate`) than the document's, REPLACES it in place: the same
//     identity, the same page, every modelled field the exchange's -- an edit, which keeps every key the model does not
//     hold (D6-5);
//   * a name it holds, as new or newer, is KEPT, and said to be;
//   * anything that cannot be done is REFUSED BY KEY and said: a page the document does not have, a name it holds on
//     another kind or page, a comment locked against the change (D6-27), a kind this build cannot write yet (the
//     backend's answer, `IDocumentBackend::annotationWritable`), a name the exchange gives twice.
// A REFERENCE resolves to an annotation the import adds, or to one the document holds; one that names nothing either
// holds -- a reply whose parent was refused, or never sent -- is DROPPED and noted, so the reply arrives as a comment
// of its own (it would be shown that way anyway: D6-12). The result is ONE `ImportAnnotationsCommand` -- one undo step
// -- and a `MergeResult` the shell shows.
//
// Pure: QtCore only, no engine, no display -- `tst_fdf` in `rawpage_tests`.

#ifndef RAWPAGE_CORE_DOC_FDF_H
#define RAWPAGE_CORE_DOC_FDF_H

#include "core/Result.h"
#include "core/doc/Annotation.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace rawpage::core {

enum class ExchangeFormat : std::uint8_t { Fdf, Xfdf };

// By the file's suffix, case-insensitively: `.fdf`, `.xfdf`. Nullopt for any other.
std::optional<ExchangeFormat> exchangeFormatOfPath(const QString& path);

// THE BOUNDS OF AN EXCHANGE FILE -- its every byte is someone else's. 256 MiB holds an FDF of a 900-page document's
// comments with its attached files many times over; the annotation count is a command's own bound
// (`AnnotationCommandLimits::kMaxAnnotations`), since an import is one command.
inline constexpr qsizetype kMaxExchangeBytes = qsizetype{256} * 1024 * 1024;
inline constexpr std::size_t kMaxExchangeAnnotations = 100000;
// A `/NM` an import keeps: 240 characters, so its pop-up's name (`-popup`, `popupNameFor`) is still within the 256 a
// command carries (`AnnotationCommandLimits::kMaxNameLength`).
inline constexpr qsizetype kMaxExchangeNameLength = 240;
// Numbers of geometry one annotation keeps -- quadpoints, vertices and ink samples together -- as the first-party
// reader keeps them (`pdf/ops/AnnotationReader::kMaxCoordinates`) and a command carries them.
inline constexpr std::size_t kMaxExchangeCoordinates = 200000;
// One text field's length in characters -- a `/Contents`, an `/RC`, a `/DA`. Far past any comment a person writes.
inline constexpr qsizetype kMaxExchangeTextLength = 1 << 20;

// WHAT A READING OR A MERGE MET AND DID NOT KEEP AS IT WAS -- reported, never a reason to refuse the file.
enum class ExchangeNoteKind : std::uint8_t {
    UnknownElement,      // an entry that is no annotation this build reads (an XFDF `<link>`, a widget, a form field)
    GeometryTruncated,   // more coordinates than an annotation keeps -- the rest dropped
    DuplicateName,       // a second annotation with a name already read -- the later one dropped
    NameTooLong,         // a /NM past `kMaxExchangeNameLength` -- the annotation dropped
    TextTooLong,         // a text field past `kMaxExchangeTextLength` -- cut there
    ReferenceDropped,    // /IRT, /Popup or /Parent naming nothing the file or the document holds -- left out
    PopupWithoutParent,  // a pop-up whose parent is not in the file -- dropped
    NotExported,         // an annotation whose payload the model does not hold (a sound) -- left out of an export
};

const char* exchangeNoteName(ExchangeNoteKind kind) noexcept;

struct ExchangeNote {
    ExchangeNoteKind kind = ExchangeNoteKind::UnknownElement;
    QString name;        // the annotation's name, or the element's
    int page = -1;       // 0-based, -1 when none applies
};

// THE EXCHANGE: what one FDF or XFDF file says (see the header).
struct Fdf {
    QString file;                         // the document the comments were made on, as the file names it
    QString originalId;                   // the document's /ID, as hex -- empty when the file says none
    QString modifiedId;
    // Each annotation `Created(<its name>)`, its page an index, its references by name. A FileAttachment's file
    // CARRIES its bytes (`EmbeddedFile::carried`) wherever the file held them.
    std::vector<Annotation> annotations;
    std::vector<ExchangeNote> notes;
    // EXPORT ONLY: where each annotation came from in the document exported -- what the backend reads a file
    // attachment's bytes by (`IDocumentBackend::writeAnnotationExchange`). Empty, or parallel to `annotations`. Never
    // written to a file.
    std::vector<AnnotationId> sources;
};

// The name a pop-up is given here: its parent's, and `-popup`.
QString popupNameFor(const QString& parentName);
// THE NAME OF AN ANNOTATION WITH NO `/NM` (see the header): `rawpage-content-` and 32 hex digits of the SHA-256 of
// its JSON with its identity, its name, its references and its appearance flag left out.
QString contentNameOf(const Annotation& annotation);
// The name an annotation has in an exchange: its `/NM`, else `contentNameOf`.
QString exchangeNameOf(const Annotation& annotation);

// Whether a subtype is a COMMENT -- what an exchange carries. Not a link, a widget, a pop-up on its own, a movie, a
// screen, a printer's mark, a trap network, a watermark, 3D or rich media: those are not comments, and FDF/XFDF are
// how comments travel.
bool isCommentSubtype(AnnotationSubtype subtype);

// ---- The merge (D6-20) -----------------------------------------------------------------------------------------

struct MergeEntry {
    enum class Outcome : std::uint8_t { Added, Replaced, Kept, Refused };

    Outcome outcome = Outcome::Added;
    QString name;                         // the exchange's name for it
    AnnotationSubtype subtype = AnnotationSubtype::Unknown;
    int page = -1;                        // 0-based, as the exchange gives it
    std::optional<Error> why;             // Refused: the reason, by key
};

struct MergeResult {
    std::vector<MergeEntry> entries;      // one per annotation the exchange holds, pop-ups aside, in its order
    std::vector<ExchangeNote> notes;      // the reading's, then the merge's own

    int count(MergeEntry::Outcome outcome) const;
};

// What an import WILL do: the annotations it adds (each `Created`, in the order the command lists them -- a parent
// before its pop-up) and the ones it replaces (each with the identity the document gives it), and what it says.
struct FdfImport {
    std::vector<Annotation> added;
    std::vector<Annotation> replaced;
    MergeResult result;

    bool changesNothing() const noexcept { return added.empty() && replaced.empty(); }
};

// Whether an annotation this import would write can be written -- the backend's answer, and the document's
// permission for its page. Empty: everything can.
using ImportCheck = std::function<Status(const Annotation&)>;

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_FDF_H
