// core/doc/Annotation -- one annotation, as the document model holds it. Stage 6 T2, decisions D6-3,
// D6-4 and D6-12 (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// WHAT THIS MODELS, AND WHAT IT DELIBERATELY DOES NOT
// ===========================================================================================
// The fields a reviewer reads and an editor changes: the subtype, the rectangle, the geometry of
// each kind (quadpoints, vertices, a line, ink samples), colour, opacity, border, the text fields
// (`/Contents`, `/Subj`, `/T`, `/NM`, `/RC`), the two dates, the flags, the thread (`/IRT`, `/RT`) and
// a state reply's model and state. **It does not model every key a dictionary may carry**, and it
// is never what a dictionary is rewritten FROM: D6-5 has an edit start from the dictionary on disk
// and overwrite only what the model owns, so a key nobody here understands survives. A model that
// tried to hold everything would be the second copy of the file that D3-15 refused.
//
// ===========================================================================================
// IDENTITY (D6-4)
// ===========================================================================================
// An annotation read from a file is its OBJECT NUMBER -- the one name the journal can replay against
// the file as opened, because an incremental save never renumbers an object. A DIRECT dictionary in
// `/Annots` has no object number, so it is (page, position) until its first edit makes it indirect.
// An annotation RawPage creates is its `/NM`, generated once and serialized with the command, because
// the object number it will be written at is assigned at compose and differs between composes.
//
// ===========================================================================================
// GEOMETRY IS STORED AS WRITTEN, THE RECTANGLE EXCEPTED
// ===========================================================================================
// `/Rect` is normalized on read (ISO 32000-1 §7.9.5: *"any two diagonally opposite corners"*), so
// every consumer can ask for `left() <= right()`. Quadpoints, vertices and ink samples are kept in
// the order the file wrote them: a quadrilateral's corner ORDER is information -- Acrobat writes
// top-left, top-right, bottom-left, bottom-right, which is not the order ISO 32000-1's figure shows,
// and every reader follows Acrobat (guide case A-20) -- and reordering it on read would make a save
// rewrite geometry nobody edited.
//
// Pure: QtCore only, no engine, no display -- `tst_annotation` in `rawpage_tests`.

#ifndef RAWPAGE_CORE_DOC_ANNOTATION_H
#define RAWPAGE_CORE_DOC_ANNOTATION_H

#include "core/Geometry.h"
#include "core/Result.h"

#include <QJsonObject>
#include <QLatin1String>
#include <QString>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rawpage::core {

// The subtypes ISO 32000-1 Table 169 names, numbered as PDFium's `FPDF_ANNOT_*` constants are -- so a
// value is the same in both readers' facts -- with `Unknown` for every name neither knows (PDF 2.0's
// `/Projection`, a private subtype). `pdf/render/` maps PDFium's constants explicitly all the same:
// equal numbering is a convenience, never the contract.
enum class AnnotationSubtype : int {
    Unknown = 0, Text, Link, FreeText, Line, Square, Circle, Polygon, PolyLine, Highlight,
    Underline, Squiggly, StrikeOut, Stamp, Caret, Ink, Popup, FileAttachment, Sound, Movie, Widget,
    Screen, PrinterMark, TrapNet, Watermark, ThreeD, RichMedia, XfaWidget, Redact,
};

// The PDF name (`Highlight`, `3D`), or an empty string for `Unknown` and `XfaWidget`, which no file
// writes as a `/Subtype`.
QLatin1String annotationSubtypeName(AnnotationSubtype subtype);
AnnotationSubtype annotationSubtypeFromName(std::string_view name);

// The annotation flags, ISO 32000-1 Table 165.
namespace AnnotationFlag {
inline constexpr std::uint32_t Invisible = 1;
inline constexpr std::uint32_t Hidden = 2;
inline constexpr std::uint32_t Print = 4;
inline constexpr std::uint32_t NoZoom = 8;
inline constexpr std::uint32_t NoRotate = 16;
inline constexpr std::uint32_t NoView = 32;
inline constexpr std::uint32_t ReadOnly = 64;
inline constexpr std::uint32_t Locked = 128;
inline constexpr std::uint32_t ToggleNoView = 256;
inline constexpr std::uint32_t LockedContents = 512;
} // namespace AnnotationFlag

// A PDF date (ISO 32000-1 §7.9.4) as written, and the instant it names in UTC. A sort by date is a
// sort by `utcSeconds`: the raw strings carry different time zones and compare wrongly as text.
struct PdfDate {
    QString raw;                          // empty when the key is absent
    std::optional<std::int64_t> utcSeconds;   // nullopt when absent or unparseable

    static PdfDate parse(const QString& raw);
    // The date RawPage WRITES (Stage 6 T3): `D:YYYYMMDDHHmmSSZ`, the instant in UTC. ISO 32000-1 §7.9.4
    // allows `Z` for a zero offset, and a UTC stamp says nothing about where its author sat.
    static PdfDate fromUtcSeconds(std::int64_t utcSeconds);
    // `2026-09-01T01:15:00Z`, or empty when the instant is unknown -- the form the labels carry.
    QString utcIso() const;
    bool operator==(const PdfDate&) const = default;
};

struct AnnotationId {
    enum class Kind : std::uint8_t { Object, Direct, Created };

    Kind kind = Kind::Object;
    long long object = -1;   // Kind::Object
    int page = -1;           // Kind::Direct: the page whose /Annots holds it ...
    int position = -1;       // ... and where
    QString nm;              // Kind::Created

    static AnnotationId ofObject(long long number);
    static AnnotationId ofDirect(int page, int position);
    static AnnotationId ofCreated(const QString& nm);

    bool isValid() const noexcept;
    bool operator==(const AnnotationId&) const = default;
    bool operator<(const AnnotationId& other) const;
    // `obj:12`, `direct:0:3`, `nm:<nm>` -- stable text for a journal line and a log.
    QString toString() const;
    static std::optional<AnnotationId> fromString(const QString& text);
};

enum class ReplyType : std::uint8_t { Reply, Group };

struct Annotation {
    AnnotationId id;
    AnnotationSubtype subtype = AnnotationSubtype::Unknown;
    QString subtypeName;            // the file's name, kept ONLY when `subtype` is Unknown
    int page = -1;                  // the page that owns it: its /P where a listing page agrees, else
                                    // the first page that lists it (`AnnotationStore` says why)

    PdfRect rect;                   // normalized
    bool rectFinite = true;         // false: a coordinate was NaN or infinite; `rect` is then empty

    std::vector<double> quadPoints;             // 8 numbers a quadrilateral, as written
    std::vector<double> vertices;               // Polygon, PolyLine: x y pairs
    std::vector<double> line;                   // Line: x1 y1 x2 y2, or empty
    std::vector<std::vector<double>> inkList;   // Ink: one x y list per stroke
    std::vector<double> callout;                // FreeText /CL: 4 or 6 numbers
    std::vector<double> rectDifferences;        // /RD
    std::vector<QString> lineEndings;           // /LE, as written

    std::vector<double> color;                  // /C: 0, 1, 3 or 4 components
    std::vector<double> interiorColor;          // /IC
    std::optional<double> opacity;              // /CA
    std::optional<double> borderWidth;          // /BS /W, else /Border's third number

    QString nm;                                 // /NM
    QString author;                             // /T
    QString subject;                            // /Subj
    QString contents;                           // /Contents
    QString richText;                           // /RC, the XHTML as written
    QString intent;                             // /IT
    QString icon;                               // /Name
    QString defaultAppearance;                  // /DA
    PdfDate created;                            // /CreationDate
    PdfDate modified;                           // /M
    std::uint32_t flags = 0;                    // /F

    // THE THREE REFERENCES TO ANOTHER ANNOTATION name it as identity does (Stage 6 T6): by object number
    // when it was read from the file, by `/NM` when RawPage created it and it has no number yet -- a note
    // and the pop-up made with it, a replace edit's strikeout and its caret. A reference is never `Direct`:
    // PDF can only point at an indirect object. The number is the sink's to work out at compose.
    std::optional<AnnotationId> inReplyTo;      // /IRT
    ReplyType replyType = ReplyType::Reply;     // /RT, /R when absent
    QString stateModel;                         // /StateModel -- set only on a state reply
    QString state;                              // /State

    std::optional<AnnotationId> popup;          // /Popup
    std::optional<AnnotationId> parent;         // a Popup's /Parent
    std::optional<bool> open;                   // /Open
    int rotate = 0;                             // /Rotate, a FreeText on a turned page
    bool hasAppearance = false;                 // /AP present

    bool hasFlag(std::uint32_t flag) const noexcept { return (flags & flag) != 0; }
    // A reply that carries a review or marked state rather than text (ISO 32000-1 §12.5.6.3).
    bool isStateReply() const noexcept { return !stateModel.isEmpty(); }

    // Every field, keyed by the names the labels use. Identity for every subtype (guide case A-20):
    // `fromJson(toJson(a)) == a`. The form a command's serialization will carry from T3.
    QJsonObject toJson() const;
    static Result<Annotation> fromJson(const QJsonObject& json);

    bool operator==(const Annotation&) const = default;
};

// A rectangle from any two diagonally opposite corners: `x0 <= x1` and `y0 <= y1` after it. A
// non-finite coordinate yields nullopt -- the caller reports it, it is never silently zeroed.
std::optional<PdfRect> normalizedRect(double x0, double y0, double x1, double y1);

// WHERE AN ENTRY SITS IN A PAGE'S `/Annots` -- Stage 6 T3. Every annotation command names its changes
// to the listing this way, to the store and to the sink alike (`core/backend.h`), because the two hold
// the same listing read from the same array and must stay entry-for-entry the same: a position is
// what both mean by *the third entry of page 2*, including the entries that are not annotations at all.
struct AnnotationPlacement {
    int page = -1;
    int position = -1;
    bool operator==(const AnnotationPlacement&) const = default;
};

// THE IDENTITY AND METADATA OF AN ANNOTATION RAWPAGE CREATES (D6-4): a `/NM` of 128 random bits in
// lowercase hex, generated here ONCE and then carried by the command that adds it -- so a replay, an
// undo and a redo all name the same annotation -- `/T` from `author`, `/CreationDate` and `/M` at
// `utcNow`, and the `Print` flag, which is what Acrobat sets on every comment it creates. Geometry,
// colour and text are the caller's.
Annotation newAnnotation(AnnotationSubtype subtype, int page, const QString& author, std::int64_t utcNow);

// 32 lowercase hex digits from the system's random source.
QString newAnnotationName();

// WHO A COMMENT IS BY (D6-13, Stage 6 T6): `identity/author` when it says anything, else the login name
// the HOST supplied -- the rule is the domain's, the name is the host's (convention 11), so `core/` reads
// no environment and asks no operating system. Surrounding white space is not a name.
QString identityAuthor(const QString& configured, const QString& loginName);

// The smallest rectangle holding every point of `coordinates` (x y pairs); nullopt for none, or for a
// non-finite one.
std::optional<PdfRect> boundsOfPoints(const std::vector<double>& coordinates);

// THE RECTANGLE OF TEXT MARKUP RawPage CREATES (Stage 6 T6): its quadrilaterals' bounds, grown so the lines
// drawn on and just past their edges -- an underline's stroke, a squiggle's lower half -- lie inside it. The
// appearance's box IS the rectangle (ISO 32000-1 Algorithm 8.1 would scale any other into it), so a stroke
// past it is clipped in every reader. Grown by 15 % of the tallest quadrilateral, and never less than two
// points. Nullopt for no whole quadrilateral, or a non-finite coordinate.
std::optional<PdfRect> markupRect(const std::vector<double>& quadPoints);

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_ANNOTATION_H
