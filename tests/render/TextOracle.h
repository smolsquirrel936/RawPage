// tests/render/TextOracle -- what the GENERATOR wrote, and which of the model's characters answer
// for it. Stage 5 T2 wrote these for `tst_extract.cpp` (guide A-17 ... A-26); T4 moved them here
// unchanged so `tst_layoutcorpus.cpp` grades the analyzer through the SAME claim -- a second copy of
// an oracle is a second oracle, and two of them can disagree about the labels.
//
// The oracles, and what each is not, are argued at the top of `tst_extract.cpp`: text is graded
// against `corpus/labels/` (or, for the three files that predate labels, the runs in their content
// streams read first-party through `pdf/cos/`), never against PDFium's own `FPDFText_GetText`.

#ifndef RAWPAGE_TESTS_RENDER_TEXTORACLE_H
#define RAWPAGE_TESTS_RENDER_TEXTORACLE_H

#include <catch2/catch_test_macros.hpp>

#include "core/backend.h"
#include "core/doc/Page.h"
#include "core/doc/PageTree.h"
#include "core/text/PageTextModel.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/cos/filters/Filter.h"
#include "pdf/ops/MutationSinkImpl.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rawpage::test_oracle {

using core::DocumentHandle;
using core::PdfPoint;
using core::PdfRect;
using core::PdfSize;
using core::text::PageTextModel;
using core::text::TextChar;

// =============================================================================================
// The corpus, and opening it
// =============================================================================================

inline QString corpusPath(std::string_view relative)
{
    return QString::fromStdString((std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative).string());
}

struct Opened {
    pdf::DocumentBackendImpl backend;
    DocumentHandle handle;
    std::string file;

    const core::Page& page(int index) const
    {
        const core::Page* found = backend.pages(handle)->at(index);
        REQUIRE(found != nullptr);
        return *found;
    }
    int pageCount() const { return backend.pages(handle)->count(); }
};

inline std::unique_ptr<Opened> openCorpus(std::string_view relative)
{
    auto opened = std::make_unique<Opened>();
    opened->file = std::string(relative);
    auto handle = opened->backend.open(corpusPath(relative), nullptr);
    INFO(relative);
    REQUIRE(handle);
    opened->handle = handle.value();
    return opened;
}

inline PageTextModel extract(Opened& opened, int page)
{
    core::ITextExtractor* extractor = opened.backend.textExtractor(opened.handle);
    REQUIRE(extractor != nullptr);
    PageTextModel model;
    const core::Status status = extractor->extractPage(opened.handle, page, model);
    INFO(opened.file << " page " << page);
    REQUIRE(status);
    return model;
}

// =============================================================================================
// What the generator wrote: a LINE is one run, with its text and where it is
// =============================================================================================

// Advance widths per 1000 em for codes 32..126 -- the SAME constants `corpus/make-text.py` computes
// its label extents from (standard Helvetica and Helvetica-Bold). Here they place each character of
// a Helvetica run, which is the generator's position for it and the window its ink is looked for in.
inline constexpr int kHelvetica[95] = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556,
    556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667,
    611, 778, 722, 278, 500, 667, 556, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667,
    667, 611, 278, 278, 278, 469, 556, 333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500,
    222, 833, 556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584};
inline constexpr int kHelveticaBold[95] = {
    278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556,
    556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611, 975, 722, 722, 722, 722, 667,
    611, 778, 722, 278, 556, 722, 611, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667,
    667, 611, 333, 278, 333, 584, 556, 333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556,
    278, 889, 611, 611, 611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584};

inline bool isPrintableAscii(const QString& text)
{
    return std::all_of(text.begin(), text.end(),
                       [](QChar c) { return c.unicode() >= 32 && c.unicode() <= 126; });
}

// The advance of each character in points, or nothing for a string that is not printable ASCII.
inline std::vector<double> helveticaAdvances(const QString& text, double size, bool bold)
{
    std::vector<double> out;
    const int* table = bold ? kHelveticaBold : kHelvetica;
    for (QChar c : text) out.push_back(table[c.unicode() - 32] * size / 1000.0);
    return out;
}

inline double sum(const std::vector<double>& values)
{
    double total = 0.0;
    for (double v : values) total += v;
    return total;
}

enum class Direction { Ltr, Rtl, Ttb };
enum class Face { HelveticaRegular, HelveticaBold, Other };

struct Line {
    QString text;
    double x0 = 0.0, x1 = 0.0, baseline = 0.0, size = 0.0;
    double box[4] = {0, 0, 0, 0};   // left, bottom, right, top
    Direction direction = Direction::Ltr;
    Face face = Face::Other;
    std::string fontKey;            // a content run's resource name; empty for a label line
};

// Which Helvetica drew a label line is not in the label, and does not need to be: its extent is
// exact, so whichever width table reproduces `x1` is the face the generator used.
inline Face faceOf(const Line& line)
{
    if (line.direction != Direction::Ltr || !isPrintableAscii(line.text)) return Face::Other;
    const double regular = line.x0 + sum(helveticaAdvances(line.text, line.size, false));
    const double bold = line.x0 + sum(helveticaAdvances(line.text, line.size, true));
    if (std::abs(regular - line.x1) < 0.01) return Face::HelveticaRegular;
    if (std::abs(bold - line.x1) < 0.01) return Face::HelveticaBold;
    return Face::Other;
}

inline QJsonObject loadLabel(const std::string& name)
{
    QFile file(corpusPath("labels/" + name + ".json"));
    INFO(name);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    REQUIRE(document.isObject());
    return document.object();
}

// The label's page with `"index": index`, which is not always the array position -- `text-900`'s
// label writes out pages 0, 1 and 899 only.
inline std::vector<Line> labelLines(const QJsonObject& label, int index)
{
    std::vector<Line> lines;
    for (const QJsonValue pageValue : label.value(QStringLiteral("pages")).toArray()) {
        const QJsonObject page = pageValue.toObject();
        if (page.value(QStringLiteral("index")).toInt(-1) != index) continue;
        for (const QJsonValue lineValue : page.value(QStringLiteral("lines")).toArray()) {
            const QJsonObject object = lineValue.toObject();
            Line line;
            line.text = object.value(QStringLiteral("text")).toString();
            line.x0 = object.value(QStringLiteral("x0")).toDouble();
            line.x1 = object.value(QStringLiteral("x1")).toDouble();
            line.baseline = object.value(QStringLiteral("baseline")).toDouble();
            line.size = object.value(QStringLiteral("size")).toDouble();
            const QJsonArray box = object.value(QStringLiteral("box")).toArray();
            for (int i = 0; i < 4 && i < box.size(); ++i) line.box[i] = box.at(i).toDouble();
            const QString direction = object.value(QStringLiteral("direction")).toString();
            line.direction = direction == QLatin1String("rtl")   ? Direction::Rtl
                             : direction == QLatin1String("ttb") ? Direction::Ttb
                                                                 : Direction::Ltr;
            line.face = faceOf(line);
            lines.push_back(line);
        }
    }
    return lines;
}

// ---------------------------------------------------------------------------------------------
// The content stream, read FIRST-PARTY: `pdf/cos/` decodes, this scanner reads the operators.
// ---------------------------------------------------------------------------------------------

struct Run {
    std::string fontKey;
    double size = 0.0, x = 0.0, y = 0.0;
    std::string bytes;   // every string operand of the Tj or TJ, concatenated
};

// Also collects every STROKED LINE SEGMENT (`w`, `m`, `l`, `S`) as a page-space rectangle widened
// by half the line width, when `strokes` is given: ink the generator drew as a path is ink no text
// box owns, and an oracle that counted it as a glyph's would fail a correct model.
inline std::vector<Run> scanRuns(const std::string& content, std::vector<PdfRect>* strokes = nullptr)
{
    struct Operand {
        enum Kind { Number, Name, String } kind;
        double number = 0.0;
        std::string text;
    };
    std::vector<Run> runs;
    std::vector<Operand> stack;
    std::string arrayText;
    bool inArray = false;
    std::string font;
    double size = 0.0, x = 0.0, y = 0.0;
    double lineWidth = 1.0;
    PdfPoint pen;
    std::vector<std::pair<PdfPoint, PdfPoint>> segments;

    const auto isDelimiter = [](char c) {
        return std::string_view("()<>[]{}/%").find(c) != std::string_view::npos || std::isspace(
                   static_cast<unsigned char>(c));
    };
    std::size_t i = 0;
    while (i < content.size()) {
        const char c = content[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c == '%') { while (i < content.size() && content[i] != '\n') ++i; continue; }
        if (c == '/') {
            std::size_t j = i + 1;
            while (j < content.size() && !isDelimiter(content[j])) ++j;
            stack.push_back({Operand::Name, 0.0, content.substr(i + 1, j - i - 1)});
            i = j;
            continue;
        }
        if (c == '(') {
            std::string out;
            int depth = 1;
            ++i;
            while (i < content.size() && depth > 0) {
                char d = content[i++];
                if (d == '\\' && i < content.size()) {
                    const char e = content[i++];
                    switch (e) {
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case '\n': break;
                    default:
                        if (e >= '0' && e <= '7') {
                            int value = e - '0';
                            for (int k = 0; k < 2 && i < content.size() && content[i] >= '0'
                                            && content[i] <= '7'; ++k)
                                value = value * 8 + (content[i++] - '0');
                            out += static_cast<char>(value);
                        } else {
                            out += e;
                        }
                    }
                    continue;
                }
                if (d == '(') ++depth;
                if (d == ')' && --depth == 0) break;
                out += d;
            }
            if (inArray) arrayText += out; else stack.push_back({Operand::String, 0.0, out});
            continue;
        }
        if (c == '<' && i + 1 < content.size() && content[i + 1] != '<') {
            std::string hex;
            ++i;
            while (i < content.size() && content[i] != '>') {
                if (std::isxdigit(static_cast<unsigned char>(content[i]))) hex += content[i];
                ++i;
            }
            ++i;
            if (hex.size() % 2 != 0) hex += '0';
            std::string out;
            for (std::size_t k = 0; k < hex.size(); k += 2)
                out += static_cast<char>(std::stoi(hex.substr(k, 2), nullptr, 16));
            if (inArray) arrayText += out; else stack.push_back({Operand::String, 0.0, out});
            continue;
        }
        if (c == '[') { inArray = true; arrayText.clear(); ++i; continue; }
        if (c == ']') {
            inArray = false;
            stack.push_back({Operand::String, 0.0, arrayText});
            ++i;
            continue;
        }
        if (c == '<' || c == '>') { ++i; continue; }   // a dictionary delimiter: not used in text
        std::size_t j = i;
        while (j < content.size() && !isDelimiter(content[j])) ++j;
        if (j == i) { ++i; continue; }
        const std::string token = content.substr(i, j - i);
        i = j;
        if (std::isdigit(static_cast<unsigned char>(token[0])) || token[0] == '-' || token[0] == '+'
            || token[0] == '.') {
            if (!inArray) stack.push_back({Operand::Number, std::stod(token), {}});
            continue;
        }
        // An operator.
        if (token == "Tf" && stack.size() >= 2) {
            font = stack[stack.size() - 2].text;
            size = stack.back().number;
        } else if (token == "Tm" && stack.size() >= 2) {
            x = stack[stack.size() - 2].number;
            y = stack.back().number;
        } else if ((token == "Tj" || token == "TJ") && !stack.empty()) {
            runs.push_back(Run{font, size, x, y, stack.back().text});
        } else if (token == "w" && !stack.empty()) {
            lineWidth = stack.back().number;
        } else if (token == "m" && stack.size() >= 2) {
            pen = PdfPoint{stack[stack.size() - 2].number, stack.back().number};
        } else if (token == "l" && stack.size() >= 2) {
            const PdfPoint to{stack[stack.size() - 2].number, stack.back().number};
            segments.emplace_back(pen, to);
            pen = to;
        } else if (token == "S" && strokes != nullptr) {
            const double half = std::max(lineWidth, 1.0) / 2.0;
            for (const auto& [a, b] : segments) {
                const double left = std::min(a.x, b.x) - half;
                const double bottom = std::min(a.y, b.y) - half;
                strokes->push_back(PdfRect{PdfPoint{left, bottom},
                                           PdfSize{std::abs(a.x - b.x) + 2 * half,
                                                   std::abs(a.y - b.y) + 2 * half}});
            }
            segments.clear();
        } else if (token == "n" || token == "f" || token == "B") {
            segments.clear();
        }
        stack.clear();
    }
    return runs;
}

// Every run on `pageIndex`, from the content streams the first-party page walk finds.
inline std::vector<Run> contentRuns(std::string_view relative, int pageIndex,
                             std::vector<PdfRect>* strokes = nullptr)
{
    QFile file(corpusPath(relative));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    const std::string_view view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    auto sink = pdf::ops::MutationSinkImpl::open(view);
    INFO(relative);
    REQUIRE(sink);
    const auto& pages = sink.value()->pages().pages;
    REQUIRE(static_cast<std::size_t>(pageIndex) < pages.size());

    auto page = sink.value()->table().fetch(pages[static_cast<std::size_t>(pageIndex)].number);
    REQUIRE(page);
    const pdf::cos::CosObject& contents = page.value().object.get("Contents");
    std::vector<long long> streams;
    if (contents.isReference()) streams.push_back(contents.reference().number);
    for (const pdf::cos::CosObject& item : contents.items())
        if (item.isReference()) streams.push_back(item.reference().number);

    std::string content;
    for (const long long number : streams) {
        auto stream = sink.value()->table().fetch(number);
        REQUIRE(stream);
        auto decoded = pdf::cos::decodeStream(stream.value().object, view);
        REQUIRE(decoded);
        content.append(decoded.value().begin(), decoded.value().end());
        content += '\n';
    }
    return scanRuns(content, strokes);
}

// A content run as a LINE: its text and extent where the face is Helvetica and the bytes ASCII.
inline std::vector<Line> contentLines(std::string_view relative, int pageIndex)
{
    std::vector<Line> lines;
    for (const Run& run : contentRuns(relative, pageIndex)) {
        Line line;
        line.fontKey = run.fontKey;
        line.text = QString::fromLatin1(run.bytes.data(), static_cast<qsizetype>(run.bytes.size()));
        line.x0 = run.x;
        line.baseline = run.y;
        line.size = run.size;
        const bool helvetica = run.fontKey == "F1" || run.fontKey == "F2";
        if (helvetica && isPrintableAscii(line.text)) {
            line.face = run.fontKey == "F2" ? Face::HelveticaBold : Face::HelveticaRegular;
            line.x1 = run.x + sum(helveticaAdvances(line.text, run.size, line.face == Face::HelveticaBold));
        }
        lines.push_back(line);
    }
    return lines;
}

// =============================================================================================
// Claiming the model's characters for the generator's lines -- by where their ORIGINS are
// =============================================================================================

inline bool originIn(const TextChar& c, const Line& line)
{
    if (line.direction == Direction::Ttb) {
        return c.origin.x >= line.box[0] - 0.05 && c.origin.x <= line.box[2] + 0.05
               && c.origin.y >= line.box[1] - 1.0 && c.origin.y <= line.box[3] + 1.0;
    }
    // Half-open on the right: the next run on a baseline starts where this one ends.
    return std::abs(c.origin.y - line.baseline) <= 0.5 && c.origin.x >= line.x0 - 0.05
           && c.origin.x < line.x1 - 0.05;
}

struct Claims {
    std::vector<std::vector<std::size_t>> perLine;   // model indices, in MODEL order
    std::vector<std::size_t> unclaimed;              // shown characters no line accounts for
    std::vector<std::size_t> claimedTwice;
};

inline Claims claim(const PageTextModel& model, const std::vector<Line>& lines)
{
    Claims claims;
    claims.perLine.resize(lines.size());
    for (std::size_t i = 0; i < model.chars().size(); ++i) {
        const TextChar& c = model.chars()[i];
        if (c.isGenerated()) continue;
        int owner = -1;
        for (std::size_t l = 0; l < lines.size(); ++l) {
            if (!originIn(c, lines[l])) continue;
            if (owner >= 0) {
                claims.claimedTwice.push_back(i);
                break;
            }
            owner = static_cast<int>(l);
        }
        if (owner < 0) claims.unclaimed.push_back(i);
        else claims.perLine[static_cast<std::size_t>(owner)].push_back(i);
    }
    return claims;
}

inline QString textOf(const PageTextModel& model, const std::vector<std::size_t>& indices)
{
    QString out;
    for (const std::size_t i : indices) {
        const char32_t scalar = model.chars()[i].codepoint;
        out += QString::fromUcs4(&scalar, 1);
    }
    return out;
}

// **THE ONE ENGINE TRANSFORMATION THE EXPECTATION APPLIES**, measured at Stage 5 T2 and named here
// rather than folded into a general normalization: PDFium decomposes the presentation-form Latin
// ligatures U+FB00…U+FB06 into their letters. Nothing else is normalized -- the decomposed `e` +
// U+0301 in `text-tounicode.pdf` must come back as two characters, and does.
inline QString expectedText(const QString& generated)
{
    QString out;
    for (QChar c : generated) {
        if (c.unicode() >= 0xFB00 && c.unicode() <= 0xFB06)
            out += QString(c).normalized(QString::NormalizationForm_KC);
        else
            out += c;
    }
    return out;
}

inline std::u32string sortedScalars(const QString& text)
{
    std::u32string out;
    for (const char32_t scalar : text.toUcs4()) out += scalar;
    std::sort(out.begin(), out.end());
    return out;
}


} // namespace rawpage::test_oracle

#endif // RAWPAGE_TESTS_RENDER_TEXTORACLE_H
