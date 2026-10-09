// pdf/cos/ContentStreamBuilder -- implementation. Stage 6 T3. See the header for the number rule and
// what `check()` proves.

#include "pdf/cos/ContentStreamBuilder.h"

#include "pdf/cos/Lexer.h"

#include <QLatin1String>
#include <QString>

#include <array>
#include <charconv>
#include <cmath>

namespace rawpage::pdf::cos {
namespace {

// ISO 32000-1 Annex A, Table A.1, less the inline-image triple BI / ID / EI: the data after `ID` is
// binary that no tokenizer can read, and RawPage never writes an inline image -- an appearance's image
// is an XObject (D6-16).
constexpr std::array<std::string_view, 70> kOperators = {
    "b",   "B",   "b*",  "B*",  "BDC", "BMC", "BT",  "BX",  "c",  "cm", "CS", "cs", "d",  "d0",
    "d1",  "Do",  "DP",  "EMC", "ET",  "EX",  "f",   "F",   "f*", "G",  "g",  "gs", "h",  "i",
    "j",   "J",   "K",   "k",   "l",   "m",   "M",   "MP",  "n",  "q",  "Q",  "re", "RG", "rg",
    "ri",  "s",   "S",   "SC",  "sc",  "SCN", "scn", "sh",  "T*", "Tc", "Td", "TD", "Tf", "Tj",
    "TJ",  "TL",  "Tm",  "Tr",  "Ts",  "Tw",  "Tz",  "v",   "w",  "W",  "W*", "y",  "'",  "\"",
};

bool isOperator(std::string_view keyword)
{
    for (const std::string_view known : kOperators) {
        if (keyword == known) return true;
    }
    return false;
}

core::Error invalid(const std::string& what, std::size_t offset)
{
    return core::Error{core::ErrorCode::Corrupted, QLatin1String("error.pdf.contentStreamInvalid"),
                       QString::fromStdString(what) + QStringLiteral(" at %1").arg(static_cast<qulonglong>(offset))};
}

} // namespace

std::string ContentStreamBuilder::number(double value)
{
    if (!std::isfinite(value)) value = 0.0;
    // Four decimals: 1/10,000 of a point is far below a device pixel at any zoom a reader offers, and
    // a fixed precision keeps the bytes the same on every machine for the same geometry.
    value = std::round(value * 10000.0) / 10000.0;
    if (value == 0.0) return "0";   // and never "-0"
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed, 4);
    std::string text(buffer, static_cast<std::size_t>(result.ptr - buffer));
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
    }
    return text;
}

void ContentStreamBuilder::separate()
{
    if (!bytes_.empty() && bytes_.back() != '\n') bytes_ += ' ';
}

void ContentStreamBuilder::operand(double value)
{
    separate();
    bytes_ += number(value);
}

ContentStreamBuilder& ContentStreamBuilder::op(std::string_view keyword)
{
    separate();
    bytes_ += keyword;
    bytes_ += '\n';
    return *this;
}

ContentStreamBuilder& ContentStreamBuilder::save() { return op("q"); }
ContentStreamBuilder& ContentStreamBuilder::restore() { return op("Q"); }

ContentStreamBuilder& ContentStreamBuilder::concat(double a, double b, double c, double d, double e, double f)
{
    for (const double v : {a, b, c, d, e, f}) operand(v);
    return op("cm");
}

ContentStreamBuilder& ContentStreamBuilder::lineWidth(double width)
{
    operand(width);
    return op("w");
}

ContentStreamBuilder& ContentStreamBuilder::lineCap(int style)
{
    operand(static_cast<double>(style < 0 ? 0 : style > 2 ? 2 : style));
    return op("J");
}

ContentStreamBuilder& ContentStreamBuilder::lineJoin(int style)
{
    operand(static_cast<double>(style < 0 ? 0 : style > 2 ? 2 : style));
    return op("j");
}

ContentStreamBuilder& ContentStreamBuilder::dash(const double* values, int count, double phase)
{
    separate();
    bytes_ += '[';
    for (int i = 0; i < count; ++i) {
        if (i > 0) bytes_ += ' ';
        bytes_ += number(values[i]);
    }
    bytes_ += ']';
    operand(phase);
    return op("d");
}

ContentStreamBuilder& ContentStreamBuilder::graphicsState(std::string_view name)
{
    separate();
    bytes_ += '/';
    bytes_ += name;
    return op("gs");
}

ContentStreamBuilder& ContentStreamBuilder::fillColor(const double* components, int count)
{
    if (count != 1 && count != 3 && count != 4) return *this;
    for (int i = 0; i < count; ++i) operand(components[i]);
    return op(count == 1 ? "g" : count == 3 ? "rg" : "k");
}

ContentStreamBuilder& ContentStreamBuilder::strokeColor(const double* components, int count)
{
    if (count != 1 && count != 3 && count != 4) return *this;
    for (int i = 0; i < count; ++i) operand(components[i]);
    return op(count == 1 ? "G" : count == 3 ? "RG" : "K");
}

ContentStreamBuilder& ContentStreamBuilder::moveTo(double x, double y)
{
    operand(x);
    operand(y);
    return op("m");
}

ContentStreamBuilder& ContentStreamBuilder::lineTo(double x, double y)
{
    operand(x);
    operand(y);
    return op("l");
}

ContentStreamBuilder& ContentStreamBuilder::curveTo(double x1, double y1, double x2, double y2, double x3, double y3)
{
    for (const double v : {x1, y1, x2, y2, x3, y3}) operand(v);
    return op("c");
}

ContentStreamBuilder& ContentStreamBuilder::closePath() { return op("h"); }

ContentStreamBuilder& ContentStreamBuilder::rectangle(double x, double y, double width, double height)
{
    for (const double v : {x, y, width, height}) operand(v);
    return op("re");
}

ContentStreamBuilder& ContentStreamBuilder::fill() { return op("f"); }
ContentStreamBuilder& ContentStreamBuilder::stroke() { return op("S"); }
ContentStreamBuilder& ContentStreamBuilder::fillAndStroke() { return op("B"); }
ContentStreamBuilder& ContentStreamBuilder::endPath() { return op("n"); }
ContentStreamBuilder& ContentStreamBuilder::clip() { return op("W"); }

ContentStreamBuilder& ContentStreamBuilder::beginText() { return op("BT"); }
ContentStreamBuilder& ContentStreamBuilder::endText() { return op("ET"); }

ContentStreamBuilder& ContentStreamBuilder::paintXObject(std::string_view resource)
{
    separate();
    bytes_ += '/';
    bytes_ += resource;
    return op("Do");
}

ContentStreamBuilder& ContentStreamBuilder::font(std::string_view resource, double size)
{
    separate();
    bytes_ += '/';
    bytes_ += resource;
    operand(size);
    return op("Tf");
}

ContentStreamBuilder& ContentStreamBuilder::textMatrix(double a, double b, double c, double d, double e, double f)
{
    for (const double v : {a, b, c, d, e, f}) operand(v);
    return op("Tm");
}

ContentStreamBuilder& ContentStreamBuilder::showText(std::string_view bytes)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    separate();
    bytes_ += '<';
    for (const char c : bytes) {
        const auto b = static_cast<unsigned char>(c);
        bytes_ += kHex[b >> 4];
        bytes_ += kHex[b & 0x0F];
    }
    bytes_ += '>';
    return op("Tj");
}

core::Status ContentStreamBuilder::check(std::string_view stream)
{
    Lexer lexer(stream);
    int saveDepth = 0;
    bool inText = false;
    int nesting = 0;   // arrays and dictionaries: `TJ`'s array, `BDC`'s properties, `d`'s dash array

    for (Token token = lexer.next(); !token.isEnd(); token = lexer.next()) {
        if (token.note != LexNote::None)
            return invalid(std::string("a token the reader had to tolerate (") + lexNoteName(token.note) + ")", token.offset);
        switch (token.type) {
        case TokenType::Comment:
        case TokenType::Integer:
        case TokenType::Real:
        case TokenType::LiteralString:
        case TokenType::HexString:
        case TokenType::Name:
            break;
        case TokenType::ArrayOpen:
        case TokenType::DictOpen:
            ++nesting;
            break;
        case TokenType::ArrayClose:
        case TokenType::DictClose:
            if (--nesting < 0) return invalid("a close with no open", token.offset);
            break;
        case TokenType::Keyword:
            if (token.text == "true" || token.text == "false" || token.text == "null") break;
            if (nesting > 0) return invalid("operator " + token.text + " inside an operand", token.offset);
            if (!isOperator(token.text)) return invalid("unknown operator " + token.text, token.offset);
            // Stage 6 T7: a text object holds no `q` or `Q` (ISO 32000-1 Figure 9 and Table 51).
            if (inText && (token.text == "q" || token.text == "Q"))
                return invalid(token.text + " inside a text object", token.offset);
            if (token.text == "q" && ++saveDepth > kMaxSaveDepth)
                return invalid("q nested deeper than 28", token.offset);
            if (token.text == "Q" && --saveDepth < 0) return invalid("Q with no q", token.offset);
            if (token.text == "BT") {
                if (inText) return invalid("BT inside BT", token.offset);
                inText = true;
            }
            if (token.text == "ET") {
                if (!inText) return invalid("ET with no BT", token.offset);
                inText = false;
            }
            break;
        default:
            return invalid(std::string("a token no content stream holds (") + tokenTypeName(token.type) + ")", token.offset);
        }
    }
    if (nesting != 0) return invalid("an operand left open", stream.size());
    if (saveDepth != 0) return invalid("q with no Q", stream.size());
    if (inText) return invalid("BT with no ET", stream.size());
    return core::Status{};
}

} // namespace rawpage::pdf::cos
