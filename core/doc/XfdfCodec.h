// core/doc/XfdfCodec -- XFDF, the XML form of FDF (ISO 19444-1; Adobe's XFDF 3.0), read into and written from the
// exchange model `core/doc/Fdf`. Stage 6 T13, decision D6-20 (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// NO DOCUMENT TYPE DECLARATION, EVER (D6-20)
// ===========================================================================================
// XFDF is untrusted XML from someone else's program. A document type declaration is how XML names an EXTERNAL
// ENTITY (`<!ENTITY x SYSTEM "file:///C:/Windows/win.ini">`, the read a file importer must never make) and how it
// expands a few hundred bytes into gigabytes (*billion laughs*). Acrobat writes no DTD, and XFDF needs none -- so a
// file that HAS one is refused, by `error.exchange.dtd`, the moment the reader meets it and before any entity is
// declared, let alone resolved. `QXmlStreamReader` resolves no external entity of its own accord; this does not rely
// on that, and its entity expansion limit is set to nothing as a second wall (guide case A-97).
//
// ===========================================================================================
// WHAT IS WRITTEN WHERE
// ===========================================================================================
// Every field XFDF 3.0 names is written where Acrobat writes it: the common attributes (`page`, `rect`, `name`,
// `title`, `subject`, `date`, `creationdate`, `flags`, `color`, `opacity`, `inreplyto`, `replyType`, `intent`,
// `width`, `style`, `dashes`, `intensity`, `fringe`, `rotation`, `icon`, `state`, `statemodel`) and each kind's own
// (`coords`, `start`/`end`, `head`/`tail`, `callout`, `justification`, the leader and caption attributes, the
// `vertices`, `inklist`, `contents`, `contents-richtext`, `defaultappearance`, `popup` and `data` children). What
// XFDF has no place for is written in RawPage's own namespace, `urn:rawpage:xfdf:1` (prefix `rp`), which every other
// reader ignores: a colour XFDF's `#RRGGBB` cannot say exactly (`rp:color`, `rp:interior-color` -- XFDF holds a
// channel in eight bits, so `[1 0.92 0.23]` would come back `[1 0.922 0.231]`), a blend mode (`rp:blend-mode`), a
// border style beside a cloud (`rp:border-style`) and a measurement's scale (`<rp:measure>`). So RawPage's own
// round trip is exact (guide §3.7), and another reader keeps everything it understands.
//
// A rich-text body (`/RC`) is XHTML: it is written INSIDE `<contents-richtext>` as the elements it is -- which is
// why this file writes XML by hand rather than through `QXmlStreamWriter`, which writes elements only from calls
// and would re-prefix the body's namespaces -- and read back as the same text, its declaration restored. A comment
// whose text comes ONLY as `<contents-richtext>`, which is how Acrobat 26.2 writes every one (measured in Stage 6's
// M-04 walk, S6 guide I-27), is given the body's plain text as its `/Contents`: a paragraph a line, as RawPage writes
// them. Until then such a comment arrived with no text the Comments pane could show.
//
// Bounded as the FDF reader is (`core/doc/Fdf.h`): the file's size, the annotation count, each annotation's
// coordinates (`pdf/ops/AnnotationReader::kMaxCoordinates`'s 200,000) and each text field's length; past a bound an
// annotation is cut and NOTED, and a file past the size or the count is refused.
//
// Pure: QtCore only -- `tst_fdf` in `rawpage_tests`, and `fuzz_annot_pipeline`'s XFDF branch (D6-29).

#ifndef RAWPAGE_CORE_DOC_XFDFCODEC_H
#define RAWPAGE_CORE_DOC_XFDFCODEC_H

#include "core/Result.h"
#include "core/doc/Fdf.h"

#include <QByteArray>

namespace rawpage::core {

class XfdfCodec {
public:
    // RawPage's namespace for what XFDF has no place for (see the header).
    static constexpr const char* kRawPageNamespace = "urn:rawpage:xfdf:1";

    // The exchange `bytes` say. Refused by key: `error.exchange.tooLarge` past `kMaxExchangeBytes`,
    // `error.exchange.dtd` for a document type declaration, `error.exchange.malformed` (the line) for XML that is not
    // well-formed or a root that is not `<xfdf>`, `error.exchange.tooMany` past `kMaxExchangeAnnotations`.
    static Result<Fdf> read(const QByteArray& bytes);

    // `exchange` as XFDF, UTF-8. A FileAttachment whose file is not carried is written without its `<data>` (the
    // backend fills the bytes before it writes: `IDocumentBackend::writeAnnotationExchange`).
    static QByteArray write(const Fdf& exchange);
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_XFDFCODEC_H
