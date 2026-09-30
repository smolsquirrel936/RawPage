// core/text/PageTextModel -- one page's text, as characters WITH GEOMETRY. Stage 5 T2, decision D5-2
// (`Stage5-TextSearch-Plan.md` §3).
//
// ---------------------------------------------------------------------------------------------
// WHAT IT HOLDS, AND WHY IT IS NOT A STRING
// ---------------------------------------------------------------------------------------------
// One entry per Unicode scalar, in the order the ENGINE reports them -- which is content order,
// with PDFium's own line-level reordering already applied (what that reordering does to
// right-to-left text is MEASURED, not assumed: `Stage5-TextSearch-Testing-Guide.md` A-23). Each
// entry carries its loose box and its origin in PAGE SPACE, its angle, its font identity and size,
// and three flags the engine already knows. Every consumer downstream -- selection, find's
// highlights, `LayoutAnalyzer`, the comparison's appearance pass -- needs the boxes, so the model
// keeps geometry and a string is something it can PRODUCE, never what it is.
//
// PAGE SPACE means PDF points in the page's UNROTATED user space, exactly as `core::Page`'s boxes
// are: neither the page's `/Rotate` nor the view rotation is in these numbers. Both are applied at
// presentation, through `core::PageTransform`, like every other page-space fact since D2-12 --
// so a model extracted once serves every rotation, and a rotation command does not invalidate it.
//
// ---------------------------------------------------------------------------------------------
// THE THREE FLAGS, AND THE ONE THAT IS AN S1 WHEN IGNORED
// ---------------------------------------------------------------------------------------------
//   * GENERATED -- a space or line break the ENGINE inferred from geometry rather than one the
//     content showed. Kept, because a consumer building a string wants the word breaks; flagged,
//     because a consumer comparing against the content (the corpus tests, the comparison's
//     whitespace ignore-list) must be able to leave them out.
//   * HYPHEN -- a line-final soft hyphen, as the engine judged it. Stage 5 T4's `TextNormalizer`
//     joins across one (D5-5); the model only records the judgement.
//   * UNMAPPED -- the glyph has no trustworthy Unicode. **Its codepoint is stored as U+FFFD, never as
//     whatever the engine guessed**, because the guess for a font with no `/ToUnicode` is usually the
//     glyph's CODE, which is a plausible-looking character that is not in the document. Presenting
//     it -- in a copy, a search hit, a comparison -- is the S1 this stage is most likely to ship
//     quietly (guide A-21). `hasExtractableText()` is the question every consumer asks first.
//
// ---------------------------------------------------------------------------------------------
// WHAT IT IS NOT
// ---------------------------------------------------------------------------------------------
// ONE THING BESIDES TEXT: RULING LINES (Stage 5 T4). `LayoutAnalyzer` finds a ruled table by its
// rules (D5-6: "tables from ruling lines plus alignment"), and a model of characters alone has none --
// so the extractor also records every THIN path the page draws, a horizontal or vertical stroke, as a
// page-space rectangle. It is not the page's graphics: no curves, no fills that are not thin, no
// colour, and a table drawn inside a form XObject is not seen. That is the bound the table detector
// inherits.
//
// It is not a reading order: lines, blocks, columns and the order a person reads them in are
// `core/text/LayoutAnalyzer`'s (Stage 5 T4, D5-6). It is not a normalized string: NFKC, case
// folding and diacritics are `core/text/TextNormalizer`'s (T4, D5-5). And it holds no engine type
// and no Qt GUI type -- `core/` may name neither (convention 3), and a model that did could not be
// built by the headless suite or the fuzz target that feeds it hostile geometry (D5-21).

#ifndef RAWPAGE_CORE_TEXT_PAGETEXTMODEL_H
#define RAWPAGE_CORE_TEXT_PAGETEXTMODEL_H

#include "core/Geometry.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rawpage::core::text {

// The replacement character. What an UNMAPPED glyph's codepoint is set to, so that no consumer can
// present the engine's guess by accident.
inline constexpr char32_t kReplacementCharacter = 0xFFFD;

// Bits, because a character carries any combination -- a generated line break is never a hyphen,
// but an unmapped glyph may well be one the engine thought was a hyphen.
enum class CharFlag : std::uint8_t {
    None = 0,
    Generated = 1u << 0,
    Hyphen = 1u << 1,
    Unmapped = 1u << 2,
};

constexpr std::uint8_t operator|(CharFlag a, CharFlag b) noexcept
{
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

// Which font drew a character. Deduplicated into the model's font table, because a page of 6,000
// characters in two faces would otherwise carry 6,000 copies of two names.
//
// `name` is what the ENGINE reports -- for an embedded subset that includes the six-letter tag
// (`ABCDEF+Foo`), for a base-14 font its standard name. It is an identity, not a face to draw with:
// Reflow view approximates by size, weight and italic (D5-16), never by looking this up.
struct FontIdentity {
    QString name;
    int flags = 0;     // ISO 32000-1 Table 123, the font descriptor's /Flags
    int weight = -1;   // 100..900, or -1 when the engine could not say

    bool isFixedPitch() const noexcept { return (flags & 1) != 0; }

    // BOLD AND ITALIC, from the best evidence there is, in order: the weight when the engine knows
    // it; the descriptor's ForceBold or Italic bit; and last the STYLE PART OF THE NAME. The last is
    // not a guess about arbitrary fonts -- it is how the fourteen standard fonts say what they are.
    // `Helvetica-Bold` has no descriptor, so the engine reports weight 0 and no flag (measured at
    // Stage 5 T2), and without the name every bold heading in the corpus would read as regular.
    bool isBold() const;
    bool isItalic() const;

    friend bool operator==(const FontIdentity& a, const FontIdentity& b) noexcept
    {
        return a.name == b.name && a.flags == b.flags && a.weight == b.weight;
    }
};

struct TextChar {
    char32_t codepoint = kReplacementCharacter;

    // The glyph's LOOSE box -- the whole em box at the glyph's advance, not its ink -- in page
    // space. Loose rather than tight because a selection highlight and a hit-test want the line's
    // full height: a tight box around an `x` is shorter than one around an `l`, and a highlight
    // built from tight boxes looks like a ransom note.
    PdfRect box;

    // Where the glyph's origin sits on its baseline, in page space. The baseline is what
    // `LayoutAnalyzer` clusters into lines (D5-6).
    PdfPoint origin;

    // Radians, counter-clockwise, as the engine reports it: 0 for ordinary horizontal text.
    double angle = 0.0;

    // Into `PageTextModel::fonts()`; -1 when the engine named no font for this character.
    int font = -1;
    double fontSize = 0.0;

    std::uint8_t flags = 0;

    bool isGenerated() const noexcept { return has(CharFlag::Generated); }
    bool isHyphen() const noexcept { return has(CharFlag::Hyphen); }
    bool isUnmapped() const noexcept { return has(CharFlag::Unmapped); }
    bool has(CharFlag flag) const noexcept
    {
        return (flags & static_cast<std::uint8_t>(flag)) != 0;
    }
};

class PageTextModel {
public:
    PageTextModel() = default;
    explicit PageTextModel(int pageIndex) : pageIndex_(pageIndex) {}

    int pageIndex() const noexcept { return pageIndex_; }

    // --- Building. The extractor's half; nothing else should need it. ------------------------
    // Returns the font's index, adding it only if an equal identity is not already there.
    int addFont(const FontIdentity& font);
    // An UNMAPPED character is stored as U+FFFD whatever codepoint it arrives with -- the rule in
    // the header is enforced HERE, where every producer passes through, rather than trusted to
    // each producer.
    void append(TextChar character);

    // A thin horizontal or vertical path, in page space (see the header). Rectangles that are not
    // thin in one direction are refused, so a producer cannot hand the table detector a filled box.
    void addRule(const PdfRect& rule);
    const std::vector<PdfRect>& rules() const noexcept { return rules_; }

    // --- Reading ------------------------------------------------------------------------------
    const std::vector<TextChar>& chars() const noexcept { return chars_; }
    const std::vector<FontIdentity>& fonts() const noexcept { return fonts_; }
    const FontIdentity* fontOf(const TextChar& character) const noexcept;

    std::size_t size() const noexcept { return chars_.size(); }
    bool isEmpty() const noexcept { return chars_.empty(); }

    // Characters the CONTENT showed -- everything not generated.
    std::size_t shownCount() const noexcept;
    // Shown characters with no trustworthy Unicode.
    std::size_t unmappedCount() const noexcept;

    // **THE QUESTION EVERY CONSUMER ASKS FIRST.** True when at least one shown character has a
    // trustworthy Unicode. A page of only unmapped glyphs is a page whose text CANNOT be extracted,
    // and the product says that rather than showing U+FFFD or searching it (guide A-21).
    bool hasExtractableText() const noexcept;

    // The characters as a string, in model order. `IncludeGenerated` keeps the engine's inferred
    // spaces and line breaks, which is what a person reading the string wants; `ShownOnly` drops
    // them, which is what a comparison against the content wants. Unmapped characters appear as
    // U+FFFD in both -- a consumer that must not show them asks `hasExtractableText()` first.
    enum class Whitespace { IncludeGenerated, ShownOnly };
    QString text(Whitespace whitespace = Whitespace::IncludeGenerated) const;

private:
    int pageIndex_ = -1;
    std::vector<TextChar> chars_;
    std::vector<FontIdentity> fonts_;
    std::vector<PdfRect> rules_;
};

// The thickness a path may have and still be a rule, and the length it needs to be one. A table's
// rules are hairlines to a point or two; a character-sized dash is not a rule.
inline constexpr double kRuleMaxThickness = 2.5;
inline constexpr double kRuleMinLength = 8.0;

} // namespace rawpage::core::text

#endif // RAWPAGE_CORE_TEXT_PAGETEXTMODEL_H
