// core/doc/OutlineExchange -- the bookmark tree as a file of its own: indented text and XML. Stage 7 T11, decision
// D7-10 (`Stage7-Assembly-Plan.md` §3; `Features.md` §5.4's last row).
//
// -------------------------------------------------------------------------------------------
// TWO SYNTAXES, ONE TREE
// -------------------------------------------------------------------------------------------
// INDENTED TEXT, one bookmark a line: its depth as leading TABS, its title, and -- after a tab -- the page it goes to,
// counted from 1, and the view (`XYZ 72 700 1.5`, `Fit`, `FitH 700` ...), a null written `-`. A title's own tab or
// line break is written as a space. The first line `RawPage bookmarks 1` says what the file is and may be left out.
// What text cannot say -- an action that is no destination, a colour, a style -- it does not carry.
//
//   RawPage bookmarks 1
//   1 Assembly overview<TAB>1<TAB>XYZ 56.7 790 -
//   <TAB>1.1 Moving pages<TAB>1<TAB>XYZ 56.7 750 -
//
// XML carries all of it: `<bookmarks version="1">` holding `<bookmark>` elements, nested as the tree is, each with
// `title`, and `page` (from 1) with `fit`, `x`, `y`, `zoom` for a destination -- or `action` with `file`, `uri`,
// `script` or `name` for an action -- and `open`, `bold`, `italic`, `color` (`#rrggbb`).
//
// -------------------------------------------------------------------------------------------
// A FILE FROM SOMEWHERE ELSE IS HOSTILE INPUT
// -------------------------------------------------------------------------------------------
// Read bounded -- `kMaxOutlineExchangeBytes`, `kMaxOutlineNodes`, `kMaxOutlineDepth` -- and an XML file holding a
// DOCUMENT TYPE DECLARATION is refused whole, so no entity, internal or external, is ever expanded or resolved: XFDF's
// rule (`core/doc/XfdfCodec`, Stage 6 T13). A file that breaks any rule is refused by `error.outline.importMalformed`
// naming the line, and nothing is imported. A page past the document's last keeps its title and goes nowhere.
//
// The FILE is read and written by `pdf/` (`IDocumentBackend::readOutlineExchange` / `writeOutlineExchange`), the layer
// that owns the format's files, so no layer-lint waiver; this is the syntax alone, QtCore only.

#ifndef RAWPAGE_CORE_DOC_OUTLINEEXCHANGE_H
#define RAWPAGE_CORE_DOC_OUTLINEEXCHANGE_H

#include "core/Result.h"
#include "core/doc/Outline.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <vector>

namespace rawpage::core {

enum class OutlineFormat : std::uint8_t { Text, Xml };

inline constexpr qsizetype kMaxOutlineExchangeBytes = qsizetype{16} * 1024 * 1024;

// `items` as a file in `format`, UTF-8.
QByteArray writeOutlineExchange(const std::vector<OutlineItem>& items, OutlineFormat format);

// The tree a file holds -- XML when its first non-blank character is `<`, text otherwise -- every item NEW, its id
// `key`-0, `key`-1 ... in order, each destination checked against `pageCount`.
Result<std::vector<OutlineItem>> readOutlineExchange(const QByteArray& bytes, int pageCount, const QString& key);

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_OUTLINEEXCHANGE_H
