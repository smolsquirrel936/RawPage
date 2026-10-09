// core/doc/Stamps -- what a stamp IS, before anything draws it: its kind, its name and words, its natural size,
// its turn, the text a dynamic stamp bakes, and the pixels an image stamp carries. Stage 6 T10, decision D6-16
// (`Stage6-Annotations-Plan.md` §3) and D6-37.
//
// ===========================================================================================
// FOUR KINDS, TOLD APART BY WHAT THE DICTIONARY SAYS
// ===========================================================================================
// A STANDARD stamp is a `/Name` from ISO 32000-1 Table 181 (`/Approved`, `/NotForPublicRelease`, …) -- the
// names every reader knows -- drawn from the name alone: its words are the name's own, split where its
// capitals are (`NotForPublicRelease` reads NOT FOR PUBLIC RELEASE), in its colour. Those words are the
// NAME's, not this product's English: convention 5 keeps catalog text out of `core/`, and a stamp's words are
// what the file already says. A DYNAMIC stamp is one of five `/Name`s of RawPage's own -- `DynamicApproved` …
// -- that adds the identity and the instant it was placed, BAKED into `/Contents` at placement and drawn from
// there: nothing is recomputed on open (guide A-76). An IMAGE stamp is `/IT /StampImage` (ISO 32000-2 Table
// 184), carrying no `/Name`, its pixels in the model (`StampImage`, `core/doc/Annotation.h`). Anything else --
// Acrobat's `/SBApproved`, a dynamic stamp of another producer's, an image stamp read from a file, which the
// model holds no pixels for -- is FOREIGN: its appearance is the producer's, carried when it moves or is
// resized and never redrawn (`error.edit.stampNotRedrawable` for a change that would need it).
//
// ===========================================================================================
// A STAMP IS DRAWN AT ITS NATURAL SIZE AND SCALED BY ITS RECTANGLE
// ===========================================================================================
// Unlike every other kind, a stamp's appearance is NOT drawn into its `/Rect`: its form's `/BBox` is the
// stamp's natural box, `0 0 W H`, its `/Matrix` the turn, and ISO 32000-1 Algorithm 8.1 maps the turned box
// onto the rectangle. So a stamp RESIZES as a picture does -- the rectangle changes and nothing is redrawn,
// the sink carrying the appearance (`pdf/ops/MutationSinkImpl`) -- and a producer's stamp resizes the same
// way. A turn is a redraw (a new `/Matrix`), its centre and its scale kept: the new rectangle is the turned
// box's bounds at the old scale, so the rectangle always bounds the ink (rule 6, guide A-80).
//
// ===========================================================================================
// IMAGES
// ===========================================================================================
// A JPEG is passed through: its header read here for its size and its components (baseline, extended or
// progressive, 8-bit, gray or RGB -- CMYK and 12-bit refused by key, `error.edit.stampImageUnsupported`) and
// its bytes written as they are (guide A-77). Any other image arrives as pixels -- `ui/` decodes it, since
// `core/` names no Qt GUI type (convention 3) -- and is held compressed, ready for `/FlateDecode`, its alpha
// a second plane for an `/SMask`, dropped when every pixel is opaque. One image placed many times is ONE
// image object in a save: `stampImageKey` is what the writer shares it by (guide A-78).
//
// Pure: QtCore only, no engine, no display -- `tst_stamps` in `rawpage_tests`.

#ifndef RAWPAGE_CORE_DOC_STAMPS_H
#define RAWPAGE_CORE_DOC_STAMPS_H

#include "core/Geometry.h"
#include "core/Result.h"
#include "core/doc/Annotation.h"
#include "core/doc/TypedText.h"

#include <QByteArray>
#include <QLatin1String>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <cstdint>
#include <optional>
#include <vector>

namespace rawpage::core {

enum class StampKind : std::uint8_t { Standard, Dynamic, Image, Foreign };

// Which of the four `a` is -- the question a redraw asks first. Only a Stamp is ever not Foreign.
StampKind stampKind(const Annotation& a) noexcept;

// ISO 32000-1 Table 181's fourteen names, in its order.
const std::vector<QLatin1String>& standardStampNames();
bool isStandardStampName(QStringView name) noexcept;
// RawPage's five dynamic stamps' `/Name`s: `DynamicApproved`, `DynamicReviewed`, `DynamicReceived`,
// `DynamicRevised`, `DynamicConfidential`.
const std::vector<QLatin1String>& dynamicStampNames();
bool isDynamicStampName(QStringView name) noexcept;
// `StampImage`, the intent an image stamp is written with (ISO 32000-2 Table 184).
QLatin1String stampImageIntent() noexcept;

// THE WORDS A STAMP SHOWS: its name less `Dynamic`, split before each capital, upper case.
QString stampTitle(QStringView name);
// Its colour: green for an approval, red for a restriction, blue for a working state. `/C` is written with it,
// so a recolour from the properties bar is the stamp's colour from then on.
std::vector<double> stampColour(QStringView name);

// The title's face: base-14 Helvetica-Bold in WinAnsi, NOT embedded and written with its `/Widths` (as T7
// writes Helvetica's, D6-34(d)) -- the AFM's advances for codes 32 to 90, the only ones a title holds. 0 for any
// other code.
inline constexpr int kHelveticaBoldFirst = 32;
inline constexpr int kHelveticaBoldLast = 90;
int helveticaBoldWidth(int code) noexcept;
// The width of `title` in points at `size`; nullopt for a character outside 32 to 90.
std::optional<double> helveticaBoldTextWidth(QStringView title, double size) noexcept;

// ---- Geometry ---------------------------------------------------------------------------------------
inline constexpr double kStampTitleSize = 20.0;     // a standard stamp's words, in points
inline constexpr double kDynamicTitleSize = 16.0;   // a dynamic stamp's
inline constexpr double kDynamicLineSize = 9.0;     // its baked lines'
inline constexpr double kStampBorder = 2.5;         // the corpus generator's, `corpus/make-annots.py`
inline constexpr double kStampPadding = 8.0;        // between the border and the words
inline constexpr double kStampCorner = 5.0;         // the border's corner radius
inline constexpr double kImageStampMinSide = 72.0;  // an image's longer side, in points, at least …
inline constexpr double kImageStampMaxSide = 216.0; // … and at most -- its pixels at 72 dpi otherwise

// THE BOX A STAMP IS DRAWN IN, before any turn or scale -- its appearance's `/BBox` is `0 0 width height`.
// `error.edit.stampNotRedrawable` for a Foreign stamp; a dynamic stamp's lines are laid out as typed text
// (`core/doc/TypedText`), so a line no face can show is refused by name.
Result<PdfSize> stampNaturalSize(const Annotation& a, TypedFont han = TypedFont::Simplified);

// A stamp's turn as written: `/Rotate` taken into 0 … 359.
int stampRotation(const Annotation& a) noexcept;
// The bounds of a `natural` box turned `degrees` counter-clockwise and scaled by `sx` and `sy`, centred on `centre`.
PdfRect stampRect(PdfPoint centre, PdfSize natural, double sx, double sy, int degrees) noexcept;
// The scale `a`'s rectangle draws its natural box at, each way.
std::optional<std::pair<double, double>> stampScale(const Annotation& a, PdfSize natural) noexcept;

// `a` turned to `degrees` (any integer, taken into 0 … 359), its centre and scale kept -- the rectangle the
// turned box's bounds (guide A-80). `error.edit.stampNotRedrawable` for a Foreign stamp.
Result<Annotation> rotatedStamp(const Annotation& a, int degrees, TypedFont han = TypedFont::Simplified);
// `a` at the largest uniform scale whose turned box fits `box`, centred in it -- a stamp dragged out.
Result<Annotation> stampFittedTo(const Annotation& a, const PdfRect& box, TypedFont han = TypedFont::Simplified);

// ---- Making one ------------------------------------------------------------------------------------
// What a dynamic stamp bakes: who placed it -- `author`, left out when empty -- and when, `YYYY-MM-DD HH:MM` at
// the HOST's UTC offset (convention 11), numerals only: a stamp's words are not catalog text.
QStringList dynamicStampLines(const QString& author, std::int64_t utcSeconds, int utcOffsetSeconds);

// A stamp centred on `centre` at its natural size, by `author` at `utcNow` (`newAnnotation`): its `/Name`, its
// `/Subj` (the name, `Dynamic` dropped), its `/C`. `error.edit.stampUnknown` for a name of neither list.
Result<Annotation> newStandardStamp(const QString& name, int page, PdfPoint centre, const QString& author,
                                    std::int64_t utcNow);
// A dynamic stamp: the same, and `/Contents` the baked lines (one per line). A line no face can show is
// refused by name (`error.edit.typedCharacterRefused`).
Result<Annotation> newDynamicStamp(const QString& name, int page, PdfPoint centre, const QString& author,
                                   std::int64_t utcNow, int utcOffsetSeconds, TypedFont han = TypedFont::Simplified);
// An image stamp: `/IT /StampImage`, no `/Name`, `image` its pixels; `subject` its `/Subj` (the library's name
// for it, when it has one).
Result<Annotation> newImageStamp(const StampImage& image, const QString& subject, int page, PdfPoint centre,
                                 const QString& author, std::int64_t utcNow);

// ---- Images ---------------------------------------------------------------------------------------
inline constexpr int kMaxStampImageSide = 16384;
inline constexpr qint64 kMaxStampImagePixels = 4096LL * 4096;    // a pixel image's -- `ui/` scales one larger
inline constexpr qint64 kMaxStampImageBytes = 64LL * 1024 * 1024;

// A JPEG's bytes as a stamp image, read from its header. `error.edit.stampImageUnsupported` for a file that is
// not a JPEG this writer passes through.
Result<StampImage> stampImageFromJpeg(const QByteArray& jpeg);
// 8-bit RGB samples (`width * height * 3` bytes, row by row) and, optionally, 8-bit alpha (`width * height`).
Result<StampImage> stampImageFromPixels(int width, int height, const QByteArray& rgb, const QByteArray& alpha);
// What a command's parser refuses an image for: the field it names, or nullopt.
std::optional<QString> invalidStampImage(const StampImage& image);
// SHA-256 over everything that decides the image's pixels, in hex -- what one save shares an image by.
QString stampImageKey(const StampImage& image);

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_STAMPS_H
