// pdf/cos/ContentStreamBuilder -- writes a content stream one operator at a time, and re-reads one
// against an allowlist. Stage 6 T3, decision D6-6 (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// WHY A BUILDER RATHER THAN STRING CONCATENATION
// ===========================================================================================
// Every appearance stream RawPage writes is read by other people's readers (D6-6), and the two ways a
// hand-assembled stream goes wrong are both invisible in our own renderer: a NUMBER spelled in a way
// PDF does not have (`1e-05`, `nan`, a locale's comma), and an unbalanced `q`/`Q` or `BT`/`ET` that
// PDFium tolerates and another reader does not. So numbers are spelled in exactly one place -- fixed
// notation, at most four decimals, never an exponent, never `-0` -- and each operator is a method, so
// an operand count cannot be wrong.
//
// ===========================================================================================
// `check()`: THE RE-READ D6-6 REQUIRES
// ===========================================================================================
// Every stream a builder produced is re-read by `check()` before it is written (and again by
// `RevisionWriter::verify` from the bytes on disk): tokenized by `pdf/cos/Lexer` -- the reader, not
// the builder -- every operator in ISO 32000-1 Annex A's table of content-stream operators, `q`/`Q`
// balanced and never deeper than 28 (Annex C's limit), `BT`/`ET` balanced and never nested, and no
// token the lexer had to tolerate. It proves the stream is WELL-FORMED; whether it draws the right
// thing is the golden suite's and the other oracles' (D6-6), never this function's.
//
// Portable std C++, like the rest of `pdf/cos/` (D3-3): `portable-asan` and the fuzz target build it.

#ifndef RAWPAGE_PDF_COS_CONTENTSTREAMBUILDER_H
#define RAWPAGE_PDF_COS_CONTENTSTREAMBUILDER_H

#include "core/Result.h"

#include <string>
#include <string_view>

namespace rawpage::pdf::cos {

class ContentStreamBuilder {
public:
    // Graphics state.
    ContentStreamBuilder& save();                                   // q
    ContentStreamBuilder& restore();                                // Q
    ContentStreamBuilder& concat(double a, double b, double c, double d, double e, double f);   // cm
    ContentStreamBuilder& lineWidth(double width);                  // w
    // 0 butt, 1 round, 2 projecting square; anything else clamped into that range (Stage 6 T6).
    ContentStreamBuilder& lineCap(int style);                       // J
    // 0 miter, 1 round, 2 bevel; likewise.
    ContentStreamBuilder& lineJoin(int style);                      // j
    ContentStreamBuilder& graphicsState(std::string_view name);     // /name gs

    // Colour, by component count: 1 is gray, 3 RGB, 4 CMYK -- ISO 32000-1's three device spaces, which
    // is what an annotation's `/C` and `/IC` arrays mean. Any other count sets nothing and is refused
    // by the caller before it gets here.
    ContentStreamBuilder& fillColor(const double* components, int count);     // g / rg / k
    ContentStreamBuilder& strokeColor(const double* components, int count);   // G / RG / K

    // Paths.
    ContentStreamBuilder& moveTo(double x, double y);               // m
    ContentStreamBuilder& lineTo(double x, double y);               // l
    ContentStreamBuilder& curveTo(double x1, double y1, double x2, double y2, double x3, double y3);   // c
    ContentStreamBuilder& closePath();                              // h
    ContentStreamBuilder& rectangle(double x, double y, double width, double height);   // re
    ContentStreamBuilder& fill();                                   // f
    ContentStreamBuilder& stroke();                                 // S
    ContentStreamBuilder& fillAndStroke();                          // B
    ContentStreamBuilder& endPath();                                // n
    // Stage 6 T7: the current path becomes the clip, nonzero winding -- followed by a painting operator,
    // usually `endPath()`, as ISO 32000-1 §8.5.4 requires.
    ContentStreamBuilder& clip();                                   // W

    // Text (Stage 6 T7). `BT`/`ET` are balanced by the caller and checked by `check()`; nothing inside them
    // saves or restores the graphics state, which ISO 32000-1 Table 51 does not allow in a text object.
    ContentStreamBuilder& beginText();                              // BT
    ContentStreamBuilder& endText();                                // ET
    ContentStreamBuilder& font(std::string_view resource, double size);   // /resource size Tf
    ContentStreamBuilder& textMatrix(double a, double b, double c, double d, double e, double f);   // Tm
    // Shows `bytes` -- codes in the current font's encoding -- written as a hexadecimal string, which no
    // byte can break out of.
    ContentStreamBuilder& showText(std::string_view bytes);         // <…> Tj

    const std::string& bytes() const noexcept { return bytes_; }

    // A number as this builder writes it. Public so a case can hold the spelling to its rule.
    static std::string number(double value);

    // The re-read. Success, or `error.pdf.contentStreamInvalid` with a context naming the first
    // defect and its byte offset.
    static core::Status check(std::string_view stream);

    // Deepest `q` nesting `check()` accepts: ISO 32000-1 Annex C.2's limit on the graphics state stack.
    static constexpr int kMaxSaveDepth = 28;

private:
    ContentStreamBuilder& op(std::string_view keyword);
    void operand(double value);
    void separate();

    std::string bytes_;
};

} // namespace rawpage::pdf::cos

#endif // RAWPAGE_PDF_COS_CONTENTSTREAMBUILDER_H
