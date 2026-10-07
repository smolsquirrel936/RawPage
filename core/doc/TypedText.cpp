// core/doc/TypedText -- implementation. Stage 6 T7. See the header for the five faces, the refusal and the
// rule that chooses a collection.

#include "core/doc/TypedText.h"

#include <QChar>

#include <algorithm>
#include <bitset>
#include <charconv>
#include <cmath>
#include <memory>

namespace rawpage::core {
namespace {

// ---- The coverage table ------------------------------------------------------------------------------

struct CoveragePage {
    std::uint8_t page;
    const char* bits;   // 64 hexadecimal digits, the page's LAST code point first
};

#include "core/doc/CjkCoverage.inc"

using Coverage = std::bitset<65536>;

void load(Coverage& into, const CoveragePage* rows, std::size_t count)
{
    for (std::size_t r = 0; r < count; ++r) {
        const char* hex = rows[r].bits;
        for (int digit = 0; digit < 64; ++digit) {
            const char c = hex[digit];
            const int value = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0;
            // Digit 0 is the highest nibble: bits 255 … 252 of the page.
            for (int bit = 0; bit < 4; ++bit) {
                if (value & (1 << bit)) into.set((static_cast<std::size_t>(rows[r].page) << 8) + (63 - digit) * 4 + bit);
            }
        }
    }
}

const std::array<Coverage, 4>& coverage()
{
    static const std::unique_ptr<std::array<Coverage, 4>> tables = [] {
        auto out = std::make_unique<std::array<Coverage, 4>>();
        load((*out)[0], kSimplifiedCoverage, std::size(kSimplifiedCoverage));
        load((*out)[1], kTraditionalCoverage, std::size(kTraditionalCoverage));
        load((*out)[2], kJapaneseCoverage, std::size(kJapaneseCoverage));
        load((*out)[3], kKoreanCoverage, std::size(kKoreanCoverage));
        return out;
    }();
    return *tables;
}

// ---- The faces ------------------------------------------------------------------------------------------

// The supplements are the ones these four font names are published with (`cjk-readable.pdf` writes GB1-4).
// A reader resolves a collection by its registry and ordering; the coverage table, built on each
// collection's first character sets, stays inside supplements far older than these.
constexpr TypedFontSpec kSpecs[kTypedFontCount] = {
    {"Helv", "Helvetica", "WinAnsiEncoding", "", 0},
    {"STSong", "STSong-Light", "UniGB-UCS2-H", "GB1", 4},
    {"MSung", "MSung-Light", "UniCNS-UCS2-H", "CNS1", 4},
    {"KozMin", "KozMinPro-Regular", "UniJIS-UCS2-H", "Japan1", 4},
    {"HYSMyeongJo", "HYSMyeongJo-Medium", "UniKS-UCS2-H", "Korea1", 2},
};

// Helvetica's AFM advances for WinAnsi codes 32 … 255 -- 32 … 126 the corpus generator's own table
// (`corpus/make-text.py`, measured against Arial and the AFM), 128 … 255 the AFM's. Zero where WinAnsi
// defines no character (127, 129, 141, 143, 144, 157). `tests/render/tst_typedtext.cpp` holds every one to
// the advances PDFium's own Helvetica metrics give a font written with no `/Widths`.
constexpr int kHelvetica[224] = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,    //  32
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,    //  48
    1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,   //  64
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,    //  80
    333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,    //  96
    556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584, 0,      // 112
    556, 0, 222, 556, 333, 1000, 556, 556, 333, 1000, 667, 333, 1000, 0, 611, 0,       // 128
    0, 222, 222, 333, 333, 350, 556, 1000, 333, 1000, 500, 333, 944, 0, 500, 667,      // 144
    278, 333, 556, 556, 556, 556, 260, 556, 333, 737, 370, 556, 584, 333, 737, 333,    // 160
    400, 584, 333, 333, 333, 556, 537, 278, 333, 333, 365, 556, 834, 834, 834, 611,    // 176
    667, 667, 667, 667, 667, 667, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278,   // 192
    722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611,    // 208
    556, 556, 556, 556, 556, 556, 889, 500, 556, 556, 556, 556, 278, 278, 278, 278,    // 224
    556, 556, 556, 556, 556, 556, 556, 584, 611, 556, 556, 556, 556, 500, 556, 500,    // 240
};

// WinAnsi codes 128 … 159, as Unicode (ISO 32000-1 Annex D; Windows code page 1252). Zero: undefined.
constexpr char16_t kWinAnsiHigh[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178,
};

bool isKana(char32_t c) noexcept
{
    return (c >= 0x3040 && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF);
}

bool isHangul(char32_t c) noexcept
{
    return (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3130 && c <= 0x318F);
}

bool isBreak(char32_t c) noexcept
{
    return c == U'\n' || c == U'\r';
}

bool isSpace(char32_t c) noexcept
{
    return c == U' ' || c == U'\t';
}

Error refusedError(char32_t c)
{
    return Error{ErrorCode::InvalidArgument, QLatin1String("error.edit.typedCharacterRefused"), describeCharacter(c)};
}

// The text as code points, a tab shown as a space; a surrogate pair is one code point.
std::vector<char32_t> codePoints(QStringView text)
{
    std::vector<char32_t> out;
    out.reserve(static_cast<std::size_t>(text.size()));
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c.isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate()) {
            out.push_back(QChar::surrogateToUcs4(c, text[i + 1]));
            ++i;
        } else {
            out.push_back(c == QLatin1Char('\t') ? U' ' : static_cast<char32_t>(c.unicode()));
        }
    }
    return out;
}

// The face for each code point of one paragraph (no line breaks in it), or the first one none can show.
Result<std::vector<TypedFont>> facesOf(const std::vector<char32_t>& chars, TypedFont preference, TypedFont& primary,
                                       bool& sawCjk)
{
    std::vector<TypedFont> faces(chars.size(), TypedFont::Helvetica);
    std::size_t i = 0;
    while (i < chars.size()) {
        if (winAnsiCode(chars[i])) {
            ++i;
            continue;
        }
        // A RUN of characters WinAnsi does not hold: its face is decided by what is in it.
        std::size_t end = i;
        bool kana = false, hangul = false;
        while (end < chars.size() && !winAnsiCode(chars[end])) {
            kana = kana || isKana(chars[end]);
            hangul = hangul || isHangul(chars[end]);
            ++end;
        }
        TypedFont runFace = kana ? TypedFont::Japanese : hangul ? TypedFont::Korean : preference;
        if (!sawCjk) {
            primary = runFace;   // the first CJK run decides the annotation's face, which `/DA` names
            sawCjk = true;
        } else if (!kana && !hangul) {
            runFace = primary;
        }
        for (std::size_t k = i; k < end; ++k) {
            const char32_t c = chars[k];
            std::optional<TypedFont> face;
            for (const TypedFont candidate : {runFace, TypedFont::Simplified, TypedFont::Traditional,
                                              TypedFont::Japanese, TypedFont::Korean}) {
                if (cjkCovers(candidate, c)) {
                    face = candidate;
                    break;
                }
            }
            if (!face) return refusedError(c);
            faces[k] = *face;
        }
        i = end;
    }
    return faces;
}

// One unit the line breaker places whole: a word (or a CJK character) and the spaces after it.
struct Piece {
    std::size_t begin = 0;
    std::size_t end = 0;        // past the trailing spaces
    std::size_t inkEnd = 0;     // past the last character that is not a space
};

std::vector<Piece> piecesOf(const std::vector<char32_t>& chars, const std::vector<TypedFont>& faces)
{
    std::vector<Piece> out;
    std::size_t i = 0;
    while (i < chars.size()) {
        Piece piece;
        piece.begin = i;
        if (isSpace(chars[i])) {
            // Spaces that open a paragraph are a piece of their own, with nothing before them.
        } else if (isCjk(faces[i])) {
            ++i;
        } else {
            while (i < chars.size() && !isSpace(chars[i]) && !isCjk(faces[i])) ++i;
        }
        piece.inkEnd = i;
        while (i < chars.size() && isSpace(chars[i])) ++i;
        piece.end = i;
        out.push_back(piece);
    }
    return out;
}

void appendRun(TypedLine& line, TypedFont face, char32_t c, double advance)
{
    if (line.runs.empty() || line.runs.back().font != face) line.runs.push_back(TypedRun{face, QString(), 0.0});
    TypedRun& run = line.runs.back();
    if (c > 0xFFFF) {
        const char16_t high = QChar::highSurrogate(c);
        const char16_t low = QChar::lowSurrogate(c);
        run.text += QChar(high);
        run.text += QChar(low);
    } else {
        run.text += QChar(static_cast<char16_t>(c));
    }
    run.width += advance;
}

QString pdfNumber(double value)
{
    if (!std::isfinite(value)) value = 0.0;
    value = std::round(value * 10000.0) / 10000.0;
    if (value == 0.0) return QStringLiteral("0");
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed, 4);
    QString text = QString::fromLatin1(buffer, static_cast<qsizetype>(result.ptr - buffer));
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) text.chop(1);
        if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    }
    return text;
}

int normalizedQuarterTurns(int rotate, bool& whole)
{
    whole = rotate % 90 == 0;
    return ((rotate % 360) + 360) % 360;
}

} // namespace

PdfRect freeTextBoxAt(PdfPoint anchor, double width, double height, int rotate) noexcept
{
    switch (rotate) {
    case 90: return PdfRect{PdfPoint{anchor.x, anchor.y}, PdfSize{height, width}};
    case 180: return PdfRect{PdfPoint{anchor.x - width, anchor.y}, PdfSize{width, height}};
    case 270: return PdfRect{PdfPoint{anchor.x - height, anchor.y - width}, PdfSize{height, width}};
    default: return PdfRect{PdfPoint{anchor.x, anchor.y - height}, PdfSize{width, height}};
    }
}

namespace {

PdfRect unite(const PdfRect& a, double x, double y)
{
    const double left = std::min(a.left(), x), bottom = std::min(a.bottom(), y);
    const double right = std::max(a.right(), x), top = std::max(a.top(), y);
    return PdfRect{PdfPoint{left, bottom}, PdfSize{right - left, top - bottom}};
}

} // namespace

// ---- The faces -----------------------------------------------------------------------------------------

const TypedFontSpec& typedFontSpec(TypedFont font) noexcept
{
    return kSpecs[static_cast<int>(font)];
}

std::optional<TypedFont> typedFontByResource(QStringView resource) noexcept
{
    if (resource == QLatin1String("Helvetica")) return TypedFont::Helvetica;
    for (int i = 0; i < kTypedFontCount; ++i) {
        if (resource == QLatin1String(kSpecs[i].resource)) return static_cast<TypedFont>(i);
    }
    return std::nullopt;
}

std::optional<TypedFont> cjkFontForLanguage(QStringView language) noexcept
{
    const QString tag = language.toString().replace(QLatin1Char('_'), QLatin1Char('-')).toLower();
    const auto is = [&tag](const char* prefix) {
        const QLatin1String p(prefix);
        return tag == p || tag.startsWith(p + QLatin1Char('-'));
    };
    if (is("ja")) return TypedFont::Japanese;
    if (is("ko")) return TypedFont::Korean;
    if (is("zh")) {
        for (const char* traditional : {"zh-tw", "zh-hk", "zh-mo", "zh-hant"}) {
            if (is(traditional)) return TypedFont::Traditional;
        }
        return TypedFont::Simplified;
    }
    return std::nullopt;
}

TypedFont hanPreference(const QStringList& languages) noexcept
{
    for (const QString& language : languages) {
        if (const std::optional<TypedFont> font = cjkFontForLanguage(language)) return *font;
    }
    return TypedFont::Simplified;
}

// ---- Characters ----------------------------------------------------------------------------------------

std::optional<std::uint8_t> winAnsiCode(char32_t c) noexcept
{
    if ((c >= 0x20 && c <= 0x7E) || (c >= 0xA0 && c <= 0xFF)) return static_cast<std::uint8_t>(c);
    if (c < 0x100) return std::nullopt;
    for (int i = 0; i < 32; ++i) {
        if (kWinAnsiHigh[i] != 0 && kWinAnsiHigh[i] == c) return static_cast<std::uint8_t>(0x80 + i);
    }
    return std::nullopt;
}

bool cjkCovers(TypedFont font, char32_t c) noexcept
{
    if (!isCjk(font) || c > 0xFFFF) return false;
    return coverage()[static_cast<std::size_t>(font) - 1].test(static_cast<std::size_t>(c));
}

int helveticaWidth(std::uint8_t code) noexcept
{
    return code < 32 ? 0 : kHelvetica[code - 32];
}

double typedAdvance(TypedFont font, char32_t c) noexcept
{
    if (font == TypedFont::Helvetica) {
        const std::optional<std::uint8_t> code = winAnsiCode(c == U'\t' ? U' ' : c);
        return code ? helveticaWidth(*code) : 0.0;
    }
    return cjkCovers(font, c) ? 1000.0 : 0.0;
}

std::optional<char32_t> firstRefusedCharacter(QStringView text) noexcept
{
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c.isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate())
            return QChar::surrogateToUcs4(c, text[i + 1]);   // past the BMP: no UCS-2 code for it
        if (c.isSurrogate()) return static_cast<char32_t>(c.unicode());
        const char32_t u = c.unicode();
        if (isBreak(u) || u == U'\t' || winAnsiCode(u)) continue;
        bool covered = false;
        for (const TypedFont font : {TypedFont::Simplified, TypedFont::Traditional, TypedFont::Japanese, TypedFont::Korean})
            covered = covered || cjkCovers(font, u);
        if (!covered) return u;
    }
    return std::nullopt;
}

QString describeCharacter(char32_t c)
{
    QString shown;
    if (c <= 0xFFFF) {
        const QChar q(static_cast<char16_t>(c));
        if (q.isPrint() && !q.isSurrogate()) shown = QString(q);
    } else {
        shown += QChar(QChar::highSurrogate(c));
        shown += QChar(QChar::lowSurrogate(c));
    }
    const QString code = QStringLiteral("U+%1").arg(static_cast<qulonglong>(c), c > 0xFFFF ? 5 : 4, 16, QLatin1Char('0')).toUpper();
    return shown.isEmpty() ? code : QStringLiteral("“%1” (%2)").arg(shown, code);
}

// ---- Layout --------------------------------------------------------------------------------------------

Result<TypedLayout> layoutTypedText(QStringView text, double size, std::optional<double> wrapWidth,
                                    TypedFont preference)
{
    if (!(size > 0.0) || !std::isfinite(size))
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.pdf.appearanceGeometry"), QStringLiteral("FreeText")};
    if (!isCjk(preference)) preference = TypedFont::Simplified;

    TypedLayout out;
    out.size = size;
    const double scale = size / 1000.0;
    const std::vector<char32_t> all = codePoints(text);

    // Paragraphs: a line break ends one, and `\r\n` is one break.
    std::vector<std::vector<char32_t>> paragraphs(1);
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i] == U'\r' && i + 1 < all.size() && all[i + 1] == U'\n') continue;
        if (isBreak(all[i])) paragraphs.emplace_back();
        else paragraphs.back().push_back(all[i]);
    }

    TypedFont primary = TypedFont::Helvetica;
    bool sawCjk = false;
    for (const std::vector<char32_t>& chars : paragraphs) {
        Result<std::vector<TypedFont>> faces = facesOf(chars, preference, primary, sawCjk);
        if (!faces) return faces.error();
        const std::vector<TypedFont>& face = faces.value();
        const auto advance = [&](std::size_t k) { return typedAdvance(face[k], chars[k]) * scale; };

        TypedLine line;
        double full = 0.0;   // the line's width with its trailing spaces
        const auto finish = [&out, &line, &full] {
            out.lines.push_back(std::move(line));
            line = TypedLine{};
            full = 0.0;
        };
        const auto place = [&](std::size_t from, std::size_t to, std::size_t inkTo) {
            for (std::size_t k = from; k < to; ++k) {
                appendRun(line, face[k], chars[k], advance(k));
                full += advance(k);
                if (k < inkTo) line.width = full;
            }
        };
        for (const Piece& piece : piecesOf(chars, face)) {
            double ink = 0.0;
            for (std::size_t k = piece.begin; k < piece.inkEnd; ++k) ink += advance(k);
            if (!wrapWidth || line.runs.empty() || full + ink <= *wrapWidth + 1e-9) {
                if (wrapWidth && line.runs.empty() && ink > *wrapWidth + 1e-9) {
                    // A word wider than the line: broken between its letters.
                    std::size_t k = piece.begin;
                    while (k < piece.inkEnd) {
                        const double a = advance(k);
                        if (!line.runs.empty() && full + a > *wrapWidth + 1e-9) finish();
                        place(k, k + 1, k + 1);
                        ++k;
                    }
                    place(piece.inkEnd, piece.end, piece.inkEnd);
                } else {
                    const double before = full;
                    place(piece.begin, piece.end, piece.inkEnd);
                    if (piece.inkEnd == piece.begin) line.width = std::max(line.width, before);
                }
                continue;
            }
            finish();
            if (ink > *wrapWidth + 1e-9) {
                std::size_t k = piece.begin;
                while (k < piece.inkEnd) {
                    const double a = advance(k);
                    if (!line.runs.empty() && full + a > *wrapWidth + 1e-9) finish();
                    place(k, k + 1, k + 1);
                    ++k;
                }
                place(piece.inkEnd, piece.end, piece.inkEnd);
            } else {
                place(piece.begin, piece.end, piece.inkEnd);
            }
        }
        out.lines.push_back(std::move(line));
    }

    out.primary = sawCjk ? primary : TypedFont::Helvetica;
    out.hasCjk = sawCjk;
    for (const TypedLine& line : out.lines) out.width = std::max(out.width, line.width);
    out.height = static_cast<double>(out.lines.size()) * kTypedLeading * size;
    return out;
}

// ---- `/DA` ---------------------------------------------------------------------------------------------

std::optional<DefaultAppearance> DefaultAppearance::parse(const QString& da)
{
    // Tokens split on white space and before a `/` -- `/Helv 12 Tf` and `/Helv12 Tf` are both written.
    QStringList tokens;
    QString current;
    for (const QChar c : da) {
        if (c.isSpace()) {
            if (!current.isEmpty()) tokens.push_back(current);
            current.clear();
        } else if (c == QLatin1Char('/')) {
            if (!current.isEmpty()) tokens.push_back(current);
            current = QString(c);
        } else {
            current += c;
        }
    }
    if (!current.isEmpty()) tokens.push_back(current);

    DefaultAppearance out;
    bool any = false;
    std::vector<double> operands;
    QString name;
    for (const QString& token : tokens) {
        bool number = false;
        const double value = token.toDouble(&number);
        if (number && std::isfinite(value)) {
            operands.push_back(value);
            continue;
        }
        if (token.startsWith(QLatin1Char('/'))) {
            name = token.mid(1);
            operands.clear();
            continue;
        }
        if (token == QLatin1String("Tf") && !operands.empty() && !name.isEmpty()) {
            out.font = name;
            out.size = operands.back();
            any = true;
        } else if ((token == QLatin1String("g") && operands.size() >= 1) || (token == QLatin1String("rg") && operands.size() >= 3)
                   || (token == QLatin1String("k") && operands.size() >= 4)) {
            const std::size_t count = token == QLatin1String("g") ? 1 : token == QLatin1String("rg") ? 3 : 4;
            out.colour.assign(operands.end() - static_cast<std::ptrdiff_t>(count), operands.end());
            any = true;
        }
        operands.clear();
    }
    if (!any) return std::nullopt;
    return out;
}

QString DefaultAppearance::toString() const
{
    QStringList parts;
    if (!font.isEmpty()) parts << QLatin1Char('/') + font << pdfNumber(size) << QStringLiteral("Tf");
    if (colour.size() == 1 || colour.size() == 3 || colour.size() == 4) {
        for (const double v : colour) parts << pdfNumber(v);
        parts << (colour.size() == 1 ? QStringLiteral("g") : colour.size() == 3 ? QStringLiteral("rg") : QStringLiteral("k"));
    }
    return parts.join(QLatin1Char(' '));
}

// ---- Free text -----------------------------------------------------------------------------------------

FreeTextKind freeTextKind(const Annotation& a) noexcept
{
    if (a.intent == QLatin1String("FreeTextCallout")) return FreeTextKind::Callout;
    if (a.intent == QLatin1String("FreeTextTypeWriter") || a.intent == QLatin1String("FreeTextTypewriter"))
        return FreeTextKind::Typewriter;
    return FreeTextKind::Box;
}

QLatin1String freeTextIntent(FreeTextKind kind) noexcept
{
    switch (kind) {
    case FreeTextKind::Callout: return QLatin1String("FreeTextCallout");
    case FreeTextKind::Typewriter: return QLatin1String("FreeTextTypeWriter");
    case FreeTextKind::Box: break;
    }
    return QLatin1String("");
}

FreeTextStyle freeTextStyle(const Annotation& a)
{
    FreeTextStyle style;
    if (const std::optional<DefaultAppearance> da = DefaultAppearance::parse(a.defaultAppearance)) {
        if (da->size > 0.0 && std::isfinite(da->size)) style.size = da->size;
        if (!da->colour.empty()) style.textColour = da->colour;
        style.font = da->font;
        if (const std::optional<TypedFont> face = typedFontByResource(da->font); face && isCjk(*face)) style.han = face;
    }
    return style;
}

double freeTextBorderWidth(const Annotation& a) noexcept
{
    if (a.borderWidth) return std::isfinite(*a.borderWidth) ? std::max(0.0, *a.borderWidth) : 0.0;
    return freeTextKind(a) == FreeTextKind::Typewriter ? 0.0 : 1.0;
}

PdfPoint FreeTextFrame::mapped(double x, double y) const noexcept
{
    return PdfPoint{toPage[0] * x + toPage[2] * y + toPage[4], toPage[1] * x + toPage[3] * y + toPage[5]};
}

std::optional<FreeTextFrame> freeTextFrame(const Annotation& a)
{
    if (!a.rectFinite) return std::nullopt;
    double left = a.rect.left(), bottom = a.rect.bottom(), right = a.rect.right(), top = a.rect.top();
    if (a.rectDifferences.size() == 4) {
        left += a.rectDifferences[0];
        bottom += a.rectDifferences[1];
        right -= a.rectDifferences[2];
        top -= a.rectDifferences[3];
    }
    for (const double v : {left, bottom, right, top}) {
        if (!std::isfinite(v)) return std::nullopt;
    }
    if (!(right > left) || !(top > bottom)) return std::nullopt;
    bool whole = true;
    const int rotate = normalizedQuarterTurns(a.rotate, whole);
    if (!whole) return std::nullopt;

    FreeTextFrame f;
    f.box = PdfRect{PdfPoint{left, bottom}, PdfSize{right - left, top - bottom}};
    f.rotate = rotate;
    const bool turned = rotate == 90 || rotate == 270;
    f.width = turned ? f.box.size.height : f.box.size.width;
    f.height = turned ? f.box.size.width : f.box.size.height;
    f.inset = freeTextBorderWidth(a) + kFreeTextPadding;
    // The frame turned a quarter at a time ANTI-clockwise in page space, which a page displayed at the same
    // `/Rotate` turns back upright -- ISO 32000-1 §12.5.6.6's `/Rotate` for free text.
    switch (rotate) {
    case 90: f.toPage = {0.0, 1.0, -1.0, 0.0, right, bottom}; break;
    case 180: f.toPage = {-1.0, 0.0, 0.0, -1.0, right, top}; break;
    case 270: f.toPage = {0.0, -1.0, 1.0, 0.0, left, top}; break;
    default: f.toPage = {1.0, 0.0, 0.0, 1.0, left, bottom}; break;
    }
    return f;
}

QString freeTextDefaultAppearance(TypedFont primary, double size, const std::vector<double>& colour)
{
    DefaultAppearance da;
    da.font = QLatin1String(typedFontSpec(primary).resource);
    da.size = size;
    da.colour = colour.empty() ? std::vector<double>{0.0} : colour;
    return da.toString();
}

std::vector<double> calloutLeader(PdfPoint target, const PdfRect& box)
{
    const double midX = (box.left() + box.right()) / 2.0;
    const double midY = (box.bottom() + box.top()) / 2.0;
    PdfPoint end{box.left(), midY};
    PdfPoint knee{box.left() - kCalloutKnee, midY};
    if (target.x >= box.right()) {
        end = PdfPoint{box.right(), midY};
        knee = PdfPoint{box.right() + kCalloutKnee, midY};
    } else if (target.x > box.left()) {
        // Below or above the box: the leader leaves the side that faces it.
        if (target.y <= box.bottom()) {
            end = PdfPoint{midX, box.bottom()};
            knee = PdfPoint{midX, box.bottom() - kCalloutKnee};
        } else {
            end = PdfPoint{midX, box.top()};
            knee = PdfPoint{midX, box.top() + kCalloutKnee};
        }
    }
    return {target.x, target.y, knee.x, knee.y, end.x, end.y};
}

Result<Annotation> fittedFreeText(const Annotation& a, TypedFont hanFallback)
{
    const std::optional<FreeTextFrame> frame = freeTextFrame(a);
    if (!frame)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.pdf.appearanceGeometry"), QStringLiteral("FreeText")};
    const FreeTextStyle style = freeTextStyle(a);
    const FreeTextKind kind = freeTextKind(a);
    const double size = style.size;
    const std::optional<double> wrap =
        kind == FreeTextKind::Typewriter ? std::nullopt : std::optional<double>(std::max(frame->width - 2.0 * frame->inset, size));
    Result<TypedLayout> layout = layoutTypedText(a.contents, size, wrap, style.han.value_or(hanFallback));
    if (!layout) return layout.error();

    const double width = kind == FreeTextKind::Typewriter
                             ? std::max(layout.value().width, size) + 2.0 * frame->inset
                             : frame->width;
    const double height = layout.value().height + 2.0 * frame->inset;
    const PdfPoint anchor = frame->mapped(0.0, frame->height);
    const PdfRect box = freeTextBoxAt(anchor, width, height, frame->rotate);

    Annotation out = a;
    // `/DA`'s font is the layout's primary face; its size and colour are kept as `/DA` had them.
    DefaultAppearance da = DefaultAppearance::parse(a.defaultAppearance).value_or(DefaultAppearance{});
    da.font = QLatin1String(typedFontSpec(layout.value().primary).resource);
    da.size = size;
    if (da.colour.empty()) da.colour = style.textColour;
    out.defaultAppearance = da.toString();

    if (kind == FreeTextKind::Callout && a.callout.size() >= 4) {
        const PdfPoint target{a.callout[0], a.callout[1]};
        out.callout = calloutLeader(target, box);
        // The rectangle holds the box, the leader and its arrowhead -- and the leader's stroke.
        const double grow = kCalloutHead + freeTextBorderWidth(a);
        PdfRect rect = box;
        for (std::size_t k = 0; k + 1 < out.callout.size(); k += 2) {
            rect = unite(rect, out.callout[k] - grow, out.callout[k + 1] - grow);
            rect = unite(rect, out.callout[k] + grow, out.callout[k + 1] + grow);
        }
        out.rect = rect;
        out.rectDifferences = {box.left() - rect.left(), box.bottom() - rect.bottom(), rect.right() - box.right(),
                               rect.top() - box.top()};
        return out;
    }
    if (a.rectDifferences.size() == 4) {
        out.rect = PdfRect{PdfPoint{box.left() - a.rectDifferences[0], box.bottom() - a.rectDifferences[1]},
                           PdfSize{box.size.width + a.rectDifferences[0] + a.rectDifferences[2],
                                   box.size.height + a.rectDifferences[1] + a.rectDifferences[3]}};
    } else {
        out.rect = box;
    }
    return out;
}

} // namespace rawpage::core
