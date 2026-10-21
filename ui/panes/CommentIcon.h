// ui/panes/CommentIcon -- the small picture at the head of each row of the Comments pane, saying what KIND of
// comment the row is. Stage 6 guide I-33 (`Stage6-Annotations-Testing-Guide.md` §8), asked for by the user after
// M-05's sign-off, 2026-10-22.
//
// ---------------------------------------------------------------------------------------------
// TEN PICTURES FOR SEVENTEEN KINDS
// ---------------------------------------------------------------------------------------------
// The pane lists every kind `core::isCommentSubtype` names. A person tells apart what they made differently -- a
// note, a highlight, an underline, a strikeout, a squiggle, an insertion, a REPLACEMENT (a caret holding a
// strikeout as its group member, D6-12), typed text, a drawing, a stamp, an attached file -- and the rest (a sound,
// a redaction mark, anything else) share one. Every shape of a drawing -- ink, line, rectangle, oval, polygon,
// polyline -- is one picture: what was drawn is on the page.
//
// ---------------------------------------------------------------------------------------------
// DRAWN, NOT SHIPPED
// ---------------------------------------------------------------------------------------------
// The product ships no icon files: every picture so far is painted in code (the properties bar's swatches, the
// stamp palette's previews). These are too, by a `QIconEngine` the item view asks to paint at the size and device
// pixel ratio it is drawing at -- so they are sharp on a 150 % screen -- and an SVG set would have meant a sixth
// Qt module for `license-audit` and `windeployqt` to carry. The OUTLINE is the list's own text colour (its
// highlighted text on a selected row) and the FILL is the comment's own `/C`, so a yellow highlight shows a yellow
// picture: the outline is what reads against the list in both themes (convention 20), the fill is the document's,
// not the product's. A comment with no `/C` is filled with the list's mid tone.
//
// It holds no catalog key: a picture says nothing in words, and the type's word is the row's tooltip.

#ifndef RAWPAGE_UI_PANES_COMMENTICON_H
#define RAWPAGE_UI_PANES_COMMENTICON_H

#include "core/doc/AnnotationStore.h"

#include <QColor>
#include <QIcon>

#include <cstddef>
#include <vector>

class QWidget;

namespace rawpage::ui {

enum class CommentGlyph { Note, Highlight, Underline, StrikeOut, Squiggly, Insert, Replace, TypedText, Drawing, Stamp, Attachment, Other };

// What `store.at(index)` is drawn as: its subtype, and for a caret whether it holds a strikeout (a replacement).
CommentGlyph glyphFor(const core::AnnotationStore& store, std::size_t index);

// A PDF colour array -- 1 gray, 3 RGB or 4 CMYK components -- as a colour; invalid for any other length.
QColor colourOf(const std::vector<double>& components);

// `glyph`, filled with `fill` (the list's mid tone when invalid) and outlined in `palette`'s text colour, read when
// it is painted. `palette` is the list it is painted in; null paints in the application's.
QIcon commentIcon(CommentGlyph glyph, QColor fill, const QWidget* palette);

} // namespace rawpage::ui

#endif // RAWPAGE_UI_PANES_COMMENTICON_H
