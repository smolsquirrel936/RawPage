// core/doc/Redaction -- a redaction MARK as RawPage makes one, and a sound comment. Stage 6 T14, decisions D6-25 and
// D6-18 (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// A MARK REMOVES NOTHING (D6-25)
// ===========================================================================================
// A `Redact` annotation (ISO 32000-1 §12.5.6.23) says WHERE content is to be removed and what the area will look like
// afterwards -- its fill (`/IC`), its overlay text (`/OverlayText`, in `/DA`'s face, size and colour) and whether that
// text repeats (`/Repeat`) -- and it removes nothing: applying it is Stage 10's, with the verification a removal needs.
// So a mark is an annotation like a highlight, made over a TEXT SELECTION (its quadrilaterals, one region a line)
// or over an AREA (its rectangle), drawn in `/C` -- red, Acrobat's outline -- and listed, moved, edited and deleted
// like any comment. The page's content is never read to make one, and never written.
//
// THE OVERLAY TEXT IS READABLE ON ITS FILL: `/DA`'s colour is white over a dark fill and black over a light one (or
// none), decided when the mark is made and again whenever its fill changes (`withRedactionFill`), so a person who
// chooses a fill never chooses an invisible text.
//
// ===========================================================================================
// A SOUND COMMENT (D6-18)
// ===========================================================================================
// A `Sound` (ISO 32000-1 §12.5.6.16) is an icon -- `Speaker`, Acrobat's default -- `kSoundIconSize` points square
// with its top-left corner where the page was clicked, carrying its samples (`core/doc/SoundData`).
//
// Pure: QtCore only, no engine, no display -- `tst_sounddata` and `tst_redaction` in `rawpage_tests`.

#ifndef RAWPAGE_CORE_DOC_REDACTION_H
#define RAWPAGE_CORE_DOC_REDACTION_H

#include "core/Geometry.h"
#include "core/doc/Annotation.h"
#include "core/doc/SoundData.h"

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace rawpage::core {

inline constexpr double kRedactionDefaultTextSize = 10.0;   // points
inline constexpr double kSoundIconSize = 20.0;              // points, square

// How a mark is drawn and what it will leave: the outline (`/C`), the fill (`/IC`, empty for none), the opacity, the
// overlay text, its size, and whether it repeats.
struct RedactionStyle {
    std::vector<double> outline{1.0, 0.0, 0.0};
    std::vector<double> fill{0.0, 0.0, 0.0};
    double opacity = 1.0;
    QString overlayText;
    bool repeat = false;
    double textSize = kRedactionDefaultTextSize;
};

// The overlay text's colour over `fill`: white over a fill darker than mid-grey, black over a lighter one or none.
std::vector<double> redactionTextColour(const std::vector<double>& fill);
// The `/DA` a mark is written with: base-14 Helvetica at `size`, in `redactionTextColour(fill)`.
QString redactionDefaultAppearance(double size, const std::vector<double>& fill);

// A mark over the text `quadPoints` cover (Acrobat's corner order, eight numbers a line), its rectangle their bounds
// grown to hold the outline (`markupRect`); nullopt for no whole quadrilateral or a coordinate that is not finite.
std::optional<Annotation> newTextRedaction(int page, const std::vector<double>& quadPoints, const RedactionStyle& style,
                                           const QString& author, std::int64_t utcNow);
// A mark over `area`; nullopt for an empty or non-finite one.
std::optional<Annotation> newAreaRedaction(int page, const PdfRect& area, const RedactionStyle& style, const QString& author,
                                           std::int64_t utcNow);

// `mark` with its fill set to `fill` -- empty for none -- and its overlay text's colour kept readable on it.
Annotation withRedactionFill(Annotation mark, const std::vector<double>& fill);

// A sound comment: the `Speaker` icon, `kSoundIconSize` square, its top-left corner at `topLeft`, in `colour`,
// carrying `sound`.
Annotation newSoundComment(int page, const PdfPoint& topLeft, const SoundData& sound, const std::vector<double>& colour,
                           const QString& author, std::int64_t utcNow);

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_REDACTION_H
