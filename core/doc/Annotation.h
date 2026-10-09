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

#include <QByteArray>
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

// THE PIXELS AN IMAGE STAMP DRAWS (Stage 6 T10, D6-16) -- held by the model for a stamp RawPage made from an
// image, and by the command that made it, so a redraw, a replay and an undo all draw the same image. A stamp
// READ from a file has none: its pixels are in its own appearance, which is carried and never redrawn
// (`core/doc/Stamps`). A JPEG is kept as the file's own bytes and written as `/DCTDecode` byte for byte; any
// other image arrives as 8-bit samples, already zlib-compressed -- what `/FlateDecode` reads -- with its
// alpha, when it has any, as a second compressed plane for an `/SMask`. `core/doc/Stamps` makes both.
struct StampImage {
    enum class Encoding : std::uint8_t { None, Jpeg, Flate };

    Encoding encoding = Encoding::None;
    int width = 0;
    int height = 0;
    int components = 0;   // 1 gray, 3 RGB
    QByteArray data;      // Jpeg: the file as it is; Flate: the samples, row by row, zlib-compressed
    QByteArray alpha;     // Flate only: 8-bit alpha, zlib-compressed; empty when every pixel is opaque

    bool isEmpty() const noexcept { return encoding == Encoding::None; }
    bool operator==(const StampImage&) const = default;
};

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
    // Stage 6 T8: the rest of how an outline is drawn. `/BS /S` as written -- S, D, B, I or U -- and empty when
    // absent (ISO 32000-1 Table 166: solid); `/BS /D`, the dash array a `D` border is drawn with; `/BE /I` when
    // `/BE /S` is `/C`, a CLOUDY border of that intensity (Table 167) -- nullopt for none; and `/BM`, the blend
    // mode ISO 32000-2 Table 166 lets an annotation name for its appearance -- empty for the kind's own.
    QString borderStyle;
    std::vector<double> dashPattern;
    std::optional<double> cloudIntensity;
    QString blendMode;

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
    int rotate = 0;                             // /Rotate, a FreeText on a turned page; a Stamp's turn (T10)
    int justification = 0;                      // /Q, free text: 0 left, 1 centred, 2 right (Stage 6 T7)
    bool hasAppearance = false;                 // /AP present
    StampImage image;                           // Stage 6 T10: an image stamp's pixels -- never read from a file

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

// ---- Replies and states (Stage 6 T9, D6-12, D6-36) -------------------------------------------------
// A REPLY to `parent`: a `Text` annotation with the `Comment` icon, `/IRT` the parent, `/RT /R` (the default,
// so nothing is written for it), flags `Print | NoZoom | NoRotate` as a note's -- and its rectangle the
// generator's convention (`corpus/make-annots.py`): the parent's own when the parent is a note, so the reply's
// icon sits under it; otherwise a note-sized square in the right margin at the parent's height, 52 points in
// from `pageBox`'s right edge. `colour` is used where the parent is not a note (a note's reply takes its
// colour). No pop-up: the Comments pane is where a reply is read and written.
Annotation newReply(const Annotation& parent, const PdfRect& pageBox, const QString& contents,
                    const std::vector<double>& colour, const QString& author, std::int64_t utcNow);

// A STATE REPLY to `parent` -- a review status or a checkmark (ISO 32000-1 §12.5.6.3): a `Text` with `/IRT` the
// parent, `/StateModel` `model` and `/State` `state`, at the parent's rectangle, HIDDEN (flags 30, Acrobat's)
// and with no `/AP` and no `/Contents` -- it is a status, not a comment, and its words are the reader's to show.
Annotation newStateReply(const Annotation& parent, const QString& model, const QString& state, const QString& author,
                         std::int64_t utcNow);

// The two state models ISO 32000-1 Table 172 names, and the states each allows.
bool isStateModelName(const QString& model);
bool isStateOfModel(const QString& model, const QString& state);

// Stage 6 T8: whether `name` is one of ISO 32000-1 Table 166's border styles -- S, D, B, I, U -- and whether it
// is one of the blend modes ISO 32000-2 Table 136 names (`Normal` … `Luminosity`, and the deprecated
// `Compatible`, which means `Normal`).
bool isBorderStyleName(const QString& name);
bool isBlendModeName(const QString& name);

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
