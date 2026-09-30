// The body of `fuzz_text_pipeline` -- Stage 5 T4, decision D5-21 (`Stage5-TextSearch-Plan.md` §3) --
// in a header for `LexerFuzzBody.h`'s reason: the fuzz target builds only under `portable-fuzz`
// (clang and libFuzzer, Linux), so `tests/unit/tst_layout.cpp` replays the SAME function over
// adversarial inputs on every Windows `ctest` run. That is not fuzzing; it keeps the entry point
// compiling and makes a Linux finding reproducible in `x64-debug`.
//
// WHY THIS TARGET EXISTS. Every coordinate `core/text/` receives comes from an untrusted PDF: a
// hostile file controls every box PDFium hands back. The analyzer is geometric clustering over those
// numbers -- sorting, overlaps, gaps, ratios -- which is where degenerate input divides by zero, loops,
// allocates without bound, or breaks a sort comparator with a NaN (undefined behaviour, and a crash
// on a good day). So the bytes are decoded into a SYNTHETIC `PageTextModel` whose boxes include NaN,
// both infinities, zero and negative sizes, huge values, overlaps and duplicates, and it is driven
// through `LayoutAnalyzer` and `TextNormalizer` with their invariants checked.
//
// `TextSelection` and `SearchEngine` join this body when T5 and T6 build them (the plan's pipeline is
// bytes -> model -> analyzer -> selection -> normalizer -> search); at T4 the "search" is the
// normalized query found in the normalized page, which is what T6's literal match will be.
//
// THE DECODING. Each 12-byte record is one character or, when its first byte says so, one rule:
//   byte 0      kind: & 0x0F picks the codepoint's range, & 0x30 the flags, 0xF0 == 0xF0 a rule
//   byte 1      the codepoint within its range
//   bytes 2-9   x, y, width, height as signed 16-bit quarters of a point -- except that the values
//               0x7FFF, 0x7FFE and 0x7FFD mean NaN, +infinity and -infinity
//   byte 10     the font size in points, 0 meaning "none"
//   byte 11     reserved (its low bit offsets the origin by half a size, to make superscripts)

#ifndef RAWPAGE_TESTS_FUZZ_TEXTPIPELINEFUZZBODY_H
#define RAWPAGE_TESTS_FUZZ_TEXTPIPELINEFUZZBODY_H

#include "core/text/LayoutAnalyzer.h"
#include "core/text/PageTextModel.h"
#include "core/text/TextNormalizer.h"

#include <QString>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace rawpage::fuzz {

struct TextPipelineVerdict {
    bool ok = true;
    std::string failure;
    std::size_t characters = 0;
    std::size_t lines = 0;
};

namespace text_pipeline_detail {

inline double coordinate(const std::uint8_t* p)
{
    const std::int16_t raw = static_cast<std::int16_t>(static_cast<std::uint16_t>(p[0] | (p[1] << 8)));
    switch (raw) {
    case 0x7FFF: return std::numeric_limits<double>::quiet_NaN();
    case 0x7FFE: return std::numeric_limits<double>::infinity();
    case 0x7FFD: return -std::numeric_limits<double>::infinity();
    default: return raw / 4.0;
    }
}

inline char32_t codepoint(std::uint8_t range, std::uint8_t value)
{
    switch (range & 0x0F) {
    case 0: return U' ';
    case 1: return static_cast<char32_t>(U'A' + value % 26);
    case 2: return static_cast<char32_t>(U'a' + value % 26);
    case 3: return static_cast<char32_t>(U'0' + value % 10);
    case 4: return static_cast<char32_t>(0x05D0 + value % 27);    // Hebrew
    case 5: return static_cast<char32_t>(0x4E00 + value);         // CJK
    case 6: return static_cast<char32_t>(0x0300 + value % 0x70);  // combining marks
    case 7: return static_cast<char32_t>(0xFB00 + value % 7);     // Latin ligatures
    case 8: return U'-';
    case 9: return static_cast<char32_t>(0x0600 + value % 0x100); // Arabic, AL
    case 10: return static_cast<char32_t>(0xD800 + value);        // a lone surrogate
    case 11: return static_cast<char32_t>(0x10000 + value * 97);  // above the BMP
    case 12: return U'.';
    case 13: return 0x2022;                                        // a bullet
    case 14: return 0;
    default: return static_cast<char32_t>(value);                 // anything, controls included
    }
}

} // namespace text_pipeline_detail

inline TextPipelineVerdict textPipelineFuzzOnce(const std::uint8_t* data, std::size_t size)
{
    using namespace rawpage::core;
    using namespace rawpage::core::text;
    namespace detail = text_pipeline_detail;

    TextPipelineVerdict verdict;
    const auto fail = [&verdict](std::string reason) {
        verdict.ok = false;
        verdict.failure = std::move(reason);
        return verdict;
    };

    PageTextModel model;
    constexpr std::size_t kRecord = 12;
    for (std::size_t at = 0; at + kRecord <= size; at += kRecord) {
        const std::uint8_t* r = data + at;
        const double x = detail::coordinate(r + 2);
        const double y = detail::coordinate(r + 4);
        const double w = detail::coordinate(r + 6);
        const double h = detail::coordinate(r + 8);
        if ((r[0] & 0xF0) == 0xF0) {
            model.addRule(PdfRect{PdfPoint{x, y}, PdfSize{w, h}});
            continue;
        }
        TextChar c;
        c.codepoint = detail::codepoint(r[0], r[1]);
        c.box = PdfRect{PdfPoint{x, y}, PdfSize{w, h}};
        c.fontSize = r[10];
        c.origin = PdfPoint{x, y + ((r[11] & 1) != 0 ? r[10] / 2.0 : 0.0)};
        const int flags = (r[0] >> 4) & 0x3;
        if (flags == 1) c.flags = static_cast<std::uint8_t>(CharFlag::Hyphen);
        if (flags == 2) c.flags = static_cast<std::uint8_t>(CharFlag::Unmapped);
        if (flags == 3) c.flags = static_cast<std::uint8_t>(CharFlag::Generated);
        model.append(c);
    }
    verdict.characters = model.size();

    const PageLayout layout = LayoutAnalyzer::analyze(model, nullptr);
    verdict.lines = layout.lines.size();

    // 1. Every shown character is laid out exactly once or reported unplaced; nothing else appears.
    std::vector<int> seen(model.size(), 0);
    for (const TextLine& line : layout.lines) {
        if (line.chars.size() != line.gapBefore.size()) return fail("a line's gap flags do not match its characters");
        for (const int c : line.chars) {
            if (c < 0 || static_cast<std::size_t>(c) >= model.size()) return fail("a line names a character that does not exist");
            if (model.chars()[static_cast<std::size_t>(c)].isGenerated()) return fail("a generated character was laid out");
            ++seen[static_cast<std::size_t>(c)];
        }
    }
    for (const int c : layout.unplaced) {
        if (c < 0 || static_cast<std::size_t>(c) >= model.size()) return fail("an unplaced index does not exist");
        ++seen[static_cast<std::size_t>(c)];
    }
    for (std::size_t i = 0; i < model.size(); ++i) {
        const int expected = model.chars()[i].isGenerated() ? 0 : 1;
        if (seen[i] != expected) return fail("character " + std::to_string(i) + " laid out " + std::to_string(seen[i]) + " times");
    }

    // 2. Every line is in exactly one block, and every block exactly once in the reading order.
    std::vector<int> lineUses(layout.lines.size(), 0);
    for (const TextBlock& block : layout.blocks)
        for (const int l : block.lines) {
            if (l < 0 || static_cast<std::size_t>(l) >= layout.lines.size()) return fail("a block names a line that does not exist");
            ++lineUses[static_cast<std::size_t>(l)];
        }
    for (const int uses : lineUses)
        if (uses != 1) return fail("a line is in " + std::to_string(uses) + " blocks");
    std::vector<int> blockUses(layout.blocks.size(), 0);
    for (const int b : layout.order) {
        if (b < 0 || static_cast<std::size_t>(b) >= layout.blocks.size()) return fail("the order names a block that does not exist");
        ++blockUses[static_cast<std::size_t>(b)];
    }
    for (const int uses : blockUses)
        if (uses != 1) return fail("a block is in the reading order " + std::to_string(uses) + " times");

    // 3. The normalizer's offset map stays inside the model, and a query found maps back to glyphs.
    const std::vector<ReadingItem> stream = LayoutAnalyzer::readingStream(layout);
    for (const bool fold : {true, false}) {
        NormalizeOptions options;
        options.foldCase = fold;
        options.stripDiacritics = !fold;
        const TextNormalizer normalizer(options);
        const NormalizedText normalized = normalizer.normalize(model, stream);
        if (static_cast<std::size_t>(normalized.text.size()) != normalized.first.size()
            || normalized.first.size() != normalized.last.size())
            return fail("the offset map is not one entry per unit");
        for (std::size_t u = 0; u < normalized.first.size(); ++u) {
            const int f = normalized.first[u];
            const int l = normalized.last[u];
            if (f < -1 || l < -1 || (f >= 0 && static_cast<std::size_t>(l) >= model.size()) || (f >= 0) != (l >= 0))
                return fail("an offset outside the model");
        }
        if (normalized.text.size() >= 3) {
            const QString query = normalized.text.mid(normalized.text.size() / 3, 3);
            const qsizetype hit = normalized.text.indexOf(normalizer.normalizeQuery(query));
            if (hit >= 0) (void)normalized.sourceRange(hit, hit + 3);
        }
    }
    return verdict;
}

} // namespace rawpage::fuzz

#endif // RAWPAGE_TESTS_FUZZ_TEXTPIPELINEFUZZBODY_H
