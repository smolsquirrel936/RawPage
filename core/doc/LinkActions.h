// core/doc/LinkActions -- what FOLLOWING a link does, what VERIFYING the links finds, and which runs of a page's text
// are web addresses. Stage 7 T12, decision D7-12 (`Stage7-Assembly-Plan.md` §3).
//
// ===========================================================================================
// FOLLOWING IS A DECISION, AND THE DECISION IS MADE HERE
// ===========================================================================================
// `followLink` turns a link's action into ONE STEP the surface takes -- navigate, ask then open a URL, ask then open
// another PDF, ask then open a file, or nothing -- and says why when it is nothing. The surface asks and hands over
// (`app/Services::openExternally`); it never decides. So what is refused is decided once, with no display:
//   * `GoTo` and a named destination NAVIGATE -- the name looked up in the document's table NOW, so a destination
//     retargeted is followed where it points; a page the document no longer has, or a name not in the table, goes
//     nowhere and says so.
//   * `GoToR` ASKS, then opens the file in a tab at its page or its named destination; a file whose name Windows would
//     RUN is refused (D6-17's list), whatever the link calls it.
//   * `URI` ASKS, then hands the address to the platform -- only `http`, `https`, `ftp` and `mailto`; any other scheme
//     (`javascript:`, `file:`, `data:` ...) is REFUSED, because what it would do is a program's, not a page's. An
//     address written as `www.` and a host is taken as `http://`.
//   * `Launch` of a file Windows would run is REFUSED by D6-17's list (`isExecutableName`), and asks for any other.
//   * JavaScript, every form action, media and a menu command are WRITTEN AND EDITED, NEVER RUN: the step says which.
//     The pinned PDFium is the non-V8 build (`cmake/PdfiumAssert.cmake`'s `PA-4`), and the product has no script engine.
//
// ===========================================================================================
// VERIFY FETCHES NOTHING
// ===========================================================================================
// `verifyLinks` reports a destination page that does not exist, a named destination not in the table, and a file a
// `GoToR` or a `Launch` names that is not on disk -- asked through the caller's `fileExists`, so this file reads no disk.
// A web address is never fetched: whether a server answers is not a property of the document.
//
// ===========================================================================================
// A WEB ADDRESS IN THE TEXT
// ===========================================================================================
// `detectUrls` finds `http://` and `https://` followed by a host with a dot, `www.` followed by one, and `mailto:`
// followed by an address -- on one line, ending at white space or at trailing punctuation a sentence puts after it --
// and gives each its quadrilateral over the glyphs' boxes, in the corner order Acrobat writes. Anything else is not
// a link: a bare `example.com`, `http:/` with one slash, `www.` with no dot after it.
//
// Pure: QtCore only, no engine, no display -- `tst_links` in `rawpage_tests`.

#ifndef RAWPAGE_CORE_DOC_LINKACTIONS_H
#define RAWPAGE_CORE_DOC_LINKACTIONS_H

#include "core/Geometry.h"
#include "core/doc/Annotation.h"
#include "core/doc/Destinations.h"
#include "core/doc/Outline.h"

#include <QLatin1String>
#include <QString>

#include <cstdint>
#include <functional>
#include <vector>

namespace rawpage::core {

class Document;

namespace text {
class PageTextModel;
}

struct LinkFollow {
    enum class Step : std::uint8_t {
        Navigate,     // to `destination`, in this document
        OpenRemote,   // ASK, then open `file` in a tab -- at page `destination.pageIndex`, or at the destination `name`
        OpenUri,      // ASK, then hand `uri` to the platform
        LaunchFile,   // ASK, then open `file` with what the system opens it with
        Refused,      // never done -- `reason` says why
        NotRun,       // written, never run -- `reason` says which kind
        Nowhere,      // goes nowhere -- `reason` says why
    };

    Step step = Step::Nowhere;
    Destination destination;
    QString file;     // as the link writes it
    QString name;
    QString uri;
    QLatin1String reason;   // a catalog key (`links.follow.*`), a literal
};

LinkFollow followLink(const OutlineAction& action, const Destinations& table, int pageCount);

// The address a `URI` action is followed to: the scheme checked, `www.` given `http://`. Empty when it is refused.
QString followableUri(const QString& uri);

struct LinkProblem {
    enum class Kind : std::uint8_t {
        PageMissing,   // a destination page the document does not have
        NameMissing,   // a named destination not in the table
        FileMissing,   // a file a GoToR or a Launch names, not on disk
    };
    Kind kind = Kind::PageMissing;
    AnnotationId link;
    int page = -1;     // the page the link is on
    QString target;    // the name, the file, or the page number as a person counts it
};

// `links.verify.*` -- the key a problem is shown by.
const char* linkProblemKey(LinkProblem::Kind kind) noexcept;

// Every link of the document, in page order, whose target is broken -- see the header. `fileExists` answers for a file
// as the link writes it (the caller resolves it against the document's folder).
std::vector<LinkProblem> verifyLinks(const Document& document, const std::function<bool(const QString& file)>& fileExists);

struct DetectedUrl {
    QString text;                    // as the page shows it
    QString uri;                     // what the link opens: `www.` given `http://`
    std::vector<double> quadPoints;  // 8 numbers: top-left, top-right, bottom-left, bottom-right
    PdfRect rect;
};

// Every web address on the page -- see the header.
std::vector<DetectedUrl> detectUrls(const text::PageTextModel& page);

// THE LINKS AUTO-DETECT ADDS to page `page`: one per address `detectUrls` found that no link already on the page covers
// (its rectangle's centre inside a link's), each made by `newLink` -- what `link.autoDetect` hands one `AddAnnotationCommand`.
std::vector<Annotation> linksForUrls(const Document& document, int page, const text::PageTextModel& text, const QString& author,
                                     std::int64_t utcNow);

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_LINKACTIONS_H
