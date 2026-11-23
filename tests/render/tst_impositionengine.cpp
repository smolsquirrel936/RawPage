// tst_impositionengine -- guide case A-103 (`Stage7-Assembly-Testing-Guide.md` §5.18), Stage 7 T16, decisions D7-8 and
// D7-46 (`Stage7-Assembly-Plan.md` §3): each identity in its planned cell, read by PDFium.
//
// THE ORACLE IS THE ENGINE AND THE LABELS (D7-18), never the drawing code: a corpus file is opened by the SHIPPED backend,
// imposed through its snapshot (`IDocumentBackend::assemblySource`, `AssemblySource::impose`) as the product imposes, the
// result written to a file and opened by PDFium as any file is -- with no repair -- and on every side the identity text
// PDFium EXTRACTS (`C17-0005` ...) must
//   * be the page the plan put in that cell -- its number read from the identity itself, never from an index our code
//     computed -- with its box's centre inside the cell's placed page as it lands on the sheet (`core::onSheet`);
//   * read UPRIGHT on a side drawn upright and HALF-TURNED on a side the plan turns -- the angle PDFium reports, about 0
//     or about pi, and the first character's box on the side its reading direction puts it;
//   * appear once in the whole document, and never in a blank.
// Odd counts first, every layout: booklets left and right bound (the right-bound backs turned by a long-edge flip), and
// every n-up across, down and reversed, the 2-up's backs turned. `assembly/inherit-resources.pdf` -- inherited
// resources, CropBoxes, `/Rotate` 90 and 180 -- is imposed too, each identity in its cell.
//
// WHAT IT DOES NOT COVER (convention 19): WHICH sides are turned and WHICH page a cell holds are read from the plan, so a
// plan wrong in both agrees with itself here -- the duplex rule inverted stays green in this file (T16's planted P2) and is
// red against `tst_imposition`'s hand tables, which own it (A-102); the marks, frames and label as drawn (A-104's golden
// cases); and paper (M-04). The angle of a page turned by its own `/Rotate` IS graded, from its label (T16's planted P6, a page's
// `/Rotate` ignored, stayed green here while this case graded only the cell; it reads the angle since).

#include <catch2/catch_test_macros.hpp>

#include "TextOracle.h"
#include "core/backend.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/Assembly.h"
#include "core/doc/Imposition.h"
#include "core/text/PageTextModel.h"
#include "pdf/DocumentBackendImpl.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace rawpage;
using rawpage::test_oracle::corpusPath;

namespace {

constexpr double kPi = 3.14159265358979323846;

const core::ImpositionText kText{{QStringLiteral("Sheet %1 of %2"), QStringLiteral("Sheet %1 of %2, front"), QStringLiteral("Sheet %1 of %2, back")},
                                 core::TypedFont::Simplified};

// One identity as PDFium extracts it from a side: its text, its box's centre, its angle, and whether it reads
// left to right on the sheet.
struct Found {
    std::string identity;
    core::PdfPoint centre;
    double angle = 0.0;
    bool leftToRight = true;
    core::PdfPoint direction;   // from the first character's box centre to the last's, normalized: the reading direction
};

std::vector<Found> identitiesOn(core::IDocumentBackend& backend, core::DocumentHandle handle, int page)
{
    core::ITextExtractor* extractor = backend.textExtractor(handle);
    REQUIRE(extractor != nullptr);
    core::text::PageTextModel model;
    REQUIRE(extractor->extractPage(handle, page, model));
    // The characters' text and the model's text agree index for index once generated characters are counted in both.
    QString text;
    for (const core::text::TextChar& c : model.chars()) text += QString::fromUcs4(&c.codepoint, 1).isEmpty() ? QStringLiteral("?") : QString::fromUcs4(&c.codepoint, 1).left(1);
    static const QRegularExpression pattern(QStringLiteral("(?<![A-Za-z0-9-])[A-Z][A-Z0-9]*-[0-9]{4}(?![A-Za-z0-9-])"));
    std::vector<Found> out;
    auto matches = pattern.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        Found found;
        found.identity = match.captured(0).toStdString();
        double left = 1e9, right = -1e9, bottom = 1e9, top = -1e9;
        const auto& first = model.chars()[static_cast<std::size_t>(match.capturedStart())];
        const auto& last = model.chars()[static_cast<std::size_t>(match.capturedEnd() - 1)];
        for (qsizetype at = match.capturedStart(); at < match.capturedEnd(); ++at) {
            const core::PdfRect& box = model.chars()[static_cast<std::size_t>(at)].box;
            left = std::min(left, box.left());
            right = std::max(right, box.right());
            bottom = std::min(bottom, box.bottom());
            top = std::max(top, box.top());
        }
        found.centre = core::PdfPoint{(left + right) / 2.0, (bottom + top) / 2.0};
        found.angle = first.angle;
        found.leftToRight = first.box.left() < last.box.left();
        const double dx = (last.box.left() + last.box.right() - first.box.left() - first.box.right()) / 2.0;
        const double dy = (last.box.bottom() + last.box.top() - first.box.bottom() - first.box.top()) / 2.0;
        const double length = std::hypot(dx, dy);
        if (length > 0.0) found.direction = core::PdfPoint{dx / length, dy / length};
        out.push_back(found);
    }
    return out;
}

// The page number an identity names: `C17-0005` is page 5, index 4.
int indexOf(const std::string& identity) { return std::stoi(identity.substr(identity.size() - 4)) - 1; }

struct Graded {
    int identities = 0;
    int turned = 0;
};

// Imposes `relative` by `spec` through the shipped backend, opens the result, and grades every side.
// `shownTurn(page)`: the angle, counter-clockwise in degrees, a reader shows page `page`'s identity at -- 0 for an upright page,
// minus its `/Rotate` for a turned one (a reader turns a page CLOCKWISE). The sheet must show it so, plus a half turn on a
// turned side.
Graded imposeAndGrade(const std::string& relative, const core::ImpositionSpec& spec, const std::function<int(int)>& shownTurn)
{
    pdf::DocumentBackendImpl backend;
    auto source = backend.open(corpusPath(relative), nullptr);
    REQUIRE(source);
    const int count = backend.pages(source.value())->count();
    auto snapshot = backend.assemblySource(source.value());
    REQUIRE(snapshot);
    auto imposed = snapshot.value()->impose(spec, kText);
    INFO((imposed ? std::string() : std::string(imposed.error().messageKey.data()) + " " + imposed.error().context.toStdString()));
    REQUIRE(imposed);
    const core::ImpositionPlan& plan = imposed.value().plan;

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString path = scratch.filePath(QStringLiteral("imposed.pdf"));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(imposed.value().bytes) == imposed.value().bytes.size());
    file.close();
    auto handle = backend.open(path, nullptr);
    REQUIRE(handle);
    CHECK(backend.repairReport(handle.value())->isEmpty());
    REQUIRE(backend.pages(handle.value())->count() == static_cast<int>(plan.sides.size()));

    Graded graded;
    std::map<std::string, int> seen;
    for (std::size_t k = 0; k < plan.sides.size(); ++k) {
        const core::ImposedSide& side = plan.sides[k];
        INFO("side " << k << " (sheet " << side.sheet << (side.back ? " back" : " front") << (side.turned ? ", turned" : "") << ")");
        // Each page the plan puts on this side, where it lands on the sheet.
        std::map<int, core::PdfRect> landed;
        int planned = 0;
        for (const core::ImposedCell& cell : side.cells) {
            if (cell.page < 0) continue;
            ++planned;
            const core::PdfRect drawn{core::PdfPoint{std::max(cell.placed.left(), cell.cell.left()), std::max(cell.placed.bottom(), cell.cell.bottom())},
                                      core::PdfSize{std::min(cell.placed.right(), cell.cell.right()) - std::max(cell.placed.left(), cell.cell.left()),
                                                    std::min(cell.placed.top(), cell.cell.top()) - std::max(cell.placed.bottom(), cell.cell.bottom())}};
            landed[cell.page] = core::onSheet(drawn, side, plan.sheet);
        }
        const std::vector<Found> found = identitiesOn(backend, handle.value(), static_cast<int>(k));
        CHECK(static_cast<int>(found.size()) == planned);
        for (const Found& f : found) {
            INFO(f.identity << " at (" << f.centre.x << ", " << f.centre.y << ") angle " << f.angle);
            ++seen[f.identity];
            const int page = indexOf(f.identity);
            REQUIRE(page >= 0);
            REQUIRE(page < count);
            const auto at = landed.find(page);
            REQUIRE(at != landed.end());   // the page is on this side at all
            CHECK(at->second.contains(f.centre));
            // THE READING DIRECTION from where PDFium puts the characters -- not from the angle it reports, which stays 0
            // for text a form's own matrix turns (measured at T16 on `inherit-resources.pdf`: H-0003 drawn reading down,
            // as a reader shows the source page, reported at angle 0) while the side's half turn is reported as pi.
            const int turn = shownTurn(page);
            const double expected = (turn + (side.turned ? 180 : 0)) * kPi / 180.0;
            CHECK(f.direction.x * std::cos(expected) + f.direction.y * std::sin(expected) > 0.95);
            if (turn == 0) {
                CHECK(std::fabs(std::remainder(f.angle - expected, 2.0 * kPi)) < 0.01);
                CHECK(f.leftToRight != side.turned);   // read across as the side faces
            }
            if (side.turned) ++graded.turned;
            ++graded.identities;
        }
    }
    // Every page once, and only once, in the whole document.
    CHECK(static_cast<int>(seen.size()) == count);
    for (const auto& [identity, times] : seen) {
        INFO(identity);
        CHECK(times == 1);
    }
    backend.close(handle.value());
    backend.close(source.value());
    return graded;
}

} // namespace

TEST_CASE("imposition each identity lands in its planned cell upright or turned as planned", "[imposition][A-103]")
{
    std::vector<std::pair<std::string, core::ImpositionSpec>> specs;
    for (const bool left : {true, false}) {
        core::ImpositionSpec spec;
        spec.layout = core::ImpositionLayout::Booklet;
        spec.binding = left ? core::ImpositionBinding::Left : core::ImpositionBinding::Right;
        spec.duplex = left ? core::DuplexFlip::ShortEdge : core::DuplexFlip::LongEdge;
        spec.creep = 1.5;
        spec.cropMarks = spec.sheetLabels = true;
        specs.emplace_back(left ? "booklet left, short edge" : "booklet right, long edge (backs turned)", spec);
    }
    for (const int n : core::kNUpCounts) {
        for (const auto order : {core::ImpositionOrder::Across, core::ImpositionOrder::Down}) {
            for (const bool reversed : {false, true}) {
                core::ImpositionSpec spec;
                spec.perSheet = n;
                spec.order = order;
                spec.reversed = reversed;
                spec.gutter = 4.0;
                if (n == 2) spec.duplex = core::DuplexFlip::LongEdge;   // 2-up is landscape: its backs turned
                specs.emplace_back(std::to_string(n) + "-up " + (order == core::ImpositionOrder::Across ? "across" : "down")
                                       + (reversed ? " reversed" : ""),
                                   spec);
            }
        }
    }
    int identities = 0;
    int turned = 0;
    int impositions = 0;
    for (const int count : {1, 3, 5, 7, 9, 17, 2}) {
        const std::string relative = "assembly/odd-count-" + std::to_string(count) + ".pdf";
        for (const auto& [label, spec] : specs) {
            INFO(relative << ": " << label);
            const Graded graded = imposeAndGrade(relative, spec, [](int) { return 0; });
            identities += graded.identities;
            turned += graded.turned;
            ++impositions;
        }
    }
    std::cout << "A-103: " << impositions << " impositions of the odd-count files, " << identities << " identities each in its planned cell, "
              << turned << " of them on a turned back" << std::endl;
    CHECK(turned > 0);
}

TEST_CASE("imposition takes the shipped backend's document as it stands unsaved edits included", "[imposition][A-103]")
{
    // odd-count-5 with page 2 DELETED and page 1 MOVED to the end, unsaved: the booklet's four pages are, in order,
    // C05-0003, C05-0004, C05-0005, C05-0001 -- read off the sheets where the plan for FOUR pages puts them.
    QTemporaryDir scratch;
    const QString path = QDir(scratch.path()).filePath(QStringLiteral("odd-count-5.pdf"));
    REQUIRE(QFile::copy(corpusPath("assembly/odd-count-5.pdf"), path));
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    pdf::DocumentBackendImpl backend;
    auto handle = backend.open(path, nullptr);
    REQUIRE(handle);
    core::Document document;
    REQUIRE(document.hydrate(backend, handle.value()));
    core::CommandJournal journal(std::move(document), backend.mutationSink(handle.value()));
    const core::DocumentHandle h = handle.value();
    journal.setShowHandler([&backend, h](const core::Scope&) { return backend.showEdits(h, {}); },
                           [&backend, h] { return backend.settleEdits(h); });
    REQUIRE(journal.run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{1})));
    REQUIRE(journal.run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, 3)));
    REQUIRE(journal.settle());

    core::ImpositionSpec spec;
    spec.layout = core::ImpositionLayout::Booklet;
    auto snapshot = backend.assemblySource(h);
    REQUIRE(snapshot);
    auto imposed = snapshot.value()->impose(spec, kText);
    REQUIRE(imposed);
    CHECK(imposed.value().sourcePages == 4);
    CHECK(imposed.value().blanks == 0);
    auto opened = backend.openBytes(imposed.value().bytes, QStringLiteral("Untitled"), nullptr, core::BytesOrigin::Assembly);
    REQUIRE(opened);
    // By hand, a left-bound booklet of four: front [4 | 1], back [2 | 3].
    const std::vector<std::vector<std::string>> expected{{"C05-0001", "C05-0003"}, {"C05-0004", "C05-0005"}};
    for (int side = 0; side < 2; ++side) {
        std::vector<Found> found = identitiesOn(backend, opened.value(), side);
        std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.centre.x < b.centre.x; });
        REQUIRE(found.size() == 2);
        CHECK(found[0].identity == expected[static_cast<std::size_t>(side)][0]);
        CHECK(found[1].identity == expected[static_cast<std::size_t>(side)][1]);
    }
    // The file on disk is untouched: an imposition writes nothing of its source.
    CHECK(QFileInfo(path).size() == QFileInfo(corpusPath("assembly/odd-count-5.pdf")).size());
    (void)backend.close(opened.value());
    (void)backend.close(h);
}

TEST_CASE("imposition pages with inherited resources CropBoxes and a turn each land in their cell", "[imposition][A-103]")
{
    // Each page's `/Rotate` from its LABEL, never from our reading of the file: the identity shown turned clockwise by it.
    QFile file(corpusPath("labels/assembly-inherit-resources.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonArray pages = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("pages")).toArray();
    REQUIRE(pages.size() == 5);
    std::vector<int> rotate;
    for (const QJsonValue& page : pages) rotate.push_back(page.toObject().value(QStringLiteral("rotate")).toInt());
    CHECK(rotate == std::vector<int>{90, 90, 90, 180, 0});
    const auto inheritTurn = [&rotate](int page) { return -rotate[static_cast<std::size_t>(page)]; };
    for (const int n : {2, 4}) {
        core::ImpositionSpec spec;
        spec.perSheet = n;
        INFO(n << "-up");
        const Graded graded = imposeAndGrade("assembly/inherit-resources.pdf", spec, inheritTurn);
        CHECK(graded.identities == 5);
    }
    core::ImpositionSpec booklet;
    booklet.layout = core::ImpositionLayout::Booklet;
    CHECK(imposeAndGrade("assembly/inherit-resources.pdf", booklet, inheritTurn).identities == 5);
}
