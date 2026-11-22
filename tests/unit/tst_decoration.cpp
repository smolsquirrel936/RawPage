// tst_decoration -- Stage 7 T14 (`Stage7-Assembly-Plan.md` D7-7, D7-44): headers, footers and Bates numbers through the
// SHIPPED SINK and save, read back first-party (`Stage7-Assembly-Testing-Guide.md` §3.8, §5.16 -- A-91 and A-94 whole,
// A-95's first-party half): applied, saved, removed and saved leave every page, its `/Contents` and its `/Resources`
// equal to the original's in value; another producer's decoration is neither listed nor taken; a removal undone after a
// save lists the very streams it took; and the four commands' journal lines.
//
// WHAT IT DOES NOT COVER (convention 19): the INK. That a decorated page draws its text where it was placed, that the
// shrunk content clears the bands and that a removed decoration leaves 0 differing pixels are PDFium's to show --
// `tests/render/tst_decorationengine`. And the surfaces: every command is constructed here.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "TinyPdf.h"
#include "core/cmd/CommandRegistry.h"
#include "core/cmd/commands/DecorationCommands.h"
#include "core/doc/PageTree.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/DecorationOps.h"
#include "pdf/ops/PageOps.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace rawpage;
using pdf::cos::CosObject;
namespace fs = std::filesystem;

namespace {

struct Read {
    QByteArray bytes;
    pdf::cos::XrefTable table;
    pdf::ops::PageWalk walk;

    CosObject object(long long number)
    {
        auto fetched = table.fetch(number);
        REQUIRE(fetched);
        CosObject out = fetched.value().object;
        out.clearSpansRecursively();
        return out;
    }
    CosObject page(int index) { return object(walk.pages[static_cast<std::size_t>(index)].number); }
    pdf::ops::PageOps::ContentLayers layers(int index) { return pdf::ops::PageOps::contentLayers(table, page(index).get("Contents")); }
};

std::unique_ptr<Read> readBytes(QByteArray bytes)
{
    auto out = std::make_unique<Read>();
    out->bytes = std::move(bytes);
    auto opened = pdf::cos::openWithRepair(std::string_view(out->bytes.constData(), static_cast<std::size_t>(out->bytes.size())));
    REQUIRE(opened);
    out->table = std::move(opened.value().table);
    auto walk = pdf::ops::PageOps::walk(out->table);
    REQUIRE(walk);
    out->walk = std::move(walk).value();
    return out;
}

// What §3.8 compares, per page: the page object, its `/Contents` and its `/Resources` -- each as written, and where
// written as a reference to an ARRAY or a DICTIONARY, that object's value too (a stream's bytes are its own object's,
// which no decoration rewrites).
struct PageState {
    CosObject page;
    CosObject contents;
    CosObject resources;
    bool operator==(const PageState&) const = default;
};
std::vector<PageState> snapshot(Read& read)
{
    std::vector<PageState> out;
    for (int i = 0; i < static_cast<int>(read.walk.pages.size()); ++i) {
        PageState state;
        state.page = read.page(i);
        const auto deref = [&read](const CosObject& value) {
            if (!value.isReference()) return value;
            auto fetched = read.table.fetch(value.reference().number);
            if (!fetched || fetched.value().object.isStream()) return value;   // a stream: compared by reference
            CosObject held = fetched.value().object;
            held.clearSpansRecursively();
            return held;
        };
        state.contents = deref(state.page.get("Contents"));
        state.resources = deref(state.page.get("Resources"));
        out.push_back(std::move(state));
    }
    return out;
}

core::Status saveIt(tests::SaveSession& session)
{
    auto saved = session.coordinator->save(*session.journal, session.path);
    if (!saved) return saved.error();
    if (saved.value().kind == core::SaveOutcome::Kind::SaveAsRequired) {
        // A repaired file's first save is a Save As (Stage 3's rule): to a file of its own, which becomes the document's.
        const QString target = session.directory.filePath(QStringLiteral("saved-as.pdf"));
        auto as = session.coordinator->saveAs(*session.journal, target);
        if (!as) return as.error();
        session.path = target;
    }
    return core::Status{};
}

// Kept for `qpdf --check` in the portable-asan job (D7-18's oracle 2).
void keepWritten(const tests::SaveSession& session, const QString& name)
{
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);
    REQUIRE(QDir().mkpath(dir));
    const QString target = QDir(dir).filePath(QStringLiteral("decoration-%1.pdf").arg(name));
    QFile::remove(target);
    REQUIRE(QFile::copy(session.path, target));
}

core::FieldValues values()
{
    return core::FieldValues{QDate(2026, 11, 23), QTime(9, 30), QStringLiteral("file.pdf"), QStringLiteral("D:/x/file.pdf")};
}

core::HeaderFooterSpec headerFooter(bool shrink)
{
    core::HeaderFooterSpec spec;
    spec.areas[0] = QStringLiteral("<<file>>");
    spec.areas[2] = QStringLiteral("<<date>>");
    spec.areas[4] = QStringLiteral("Page <<page>> of <<count>>");
    spec.size = 9;
    spec.top = 12;
    spec.bottom = 12;
    spec.left = 18;
    spec.right = 18;
    spec.shrink = shrink;
    return spec;
}

core::HeaderFooterSpec bates(long long start = 1)
{
    core::HeaderFooterSpec spec;
    spec.areas[5] = QStringLiteral("<<bates>>");
    spec.size = 8;
    spec.bottom = 10;
    spec.right = 10;
    spec.bates = core::BatesFormat{QStringLiteral("ABC"), QString(), 6, start};
    return spec;
}

std::vector<fs::path> editableCorpus()
{
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(RAWPAGE_CORPUS_DIR)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf") continue;
        const fs::path relative = fs::relative(entry.path(), RAWPAGE_CORPUS_DIR);
        if (relative.begin()->string() == "encrypted") continue;
        if (relative.generic_string() == "malformed/pages-loop.pdf") continue;
        files.push_back(relative);
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool deltaIsEmpty(tests::SaveSession& session)
{
    auto delta = session.sink()->delta();
    REQUIRE(delta);
    return delta.value().isEmpty();
}

} // namespace

// ==========================================================================================
// A-91 -- apply, then remove: byte-identical (§3.8, first-party half)
// ==========================================================================================

TEST_CASE("decoration.roundtrip: headers, footers and Bates applied, saved, removed and saved leave every page as it was, over every editable corpus file", "[decoration][A-91]")
{
    std::size_t files = 0;
    std::size_t pages = 0;
    std::vector<std::string> notEditable;
    std::vector<std::string> refused;
    std::vector<std::string> failures;
    for (const fs::path& relative : editableCorpus()) {
        const std::string label = relative.generic_string();
        auto session = tests::openCopy(label);
        if (!session) {
            notEditable.push_back(label);
            continue;
        }
        if (!session->journal->document().permissions().may(core::Operation::EditPages)) {
            notEditable.push_back(label);
            continue;
        }
        auto original = readBytes(session->openedBytes);
        const std::vector<PageState> before = snapshot(*original);

        // Shrink where the page has room; a page too small for the bands is refused by key, and drawn without.
        auto header = std::make_unique<core::ApplyHeaderFooterCommand>(headerFooter(true), values());
        core::Status applied = session->journal->run(std::move(header));
        if (!applied && applied.error().messageKey == QLatin1String("error.decorate.noRoom"))
            applied = session->journal->run(std::make_unique<core::ApplyHeaderFooterCommand>(headerFooter(false), values()));
        if (!applied) {
            refused.push_back(label + ": " + applied.error().messageKey.data());
            continue;
        }
        ++files;
        pages += before.size();
        const auto step = [&](const char* what, core::Status status) {
            if (!status) failures.push_back(label + " " + what + ": " + status.error().messageKey.data() + " " + status.error().context.toStdString());
            return static_cast<bool>(status);
        };
        if (!step("save header", saveIt(*session))) continue;
        if (!step("bates", session->journal->run(std::make_unique<core::ApplyBatesCommand>(bates(), values()))))
            continue;
        if (!step("save bates", saveIt(*session))) continue;
        {
            // Both are listed, in the order applied, each in front of the content.
            auto both = readBytes(tests::readAllBytes(session->path));
            const auto layers = both->layers(0);
            if (layers.front.size() != 2 || layers.front[0].kind != core::DecorationKind::HeaderFooter
                || layers.front[1].kind != core::DecorationKind::Bates || layers.layers.size() != 2)
                failures.push_back(label + ": page 1 does not list a header, a Bates number and their two wrappers");
        }
        if (!step("remove header", session->journal->run(std::make_unique<core::RemoveHeaderFooterCommand>()))) continue;
        if (!step("save removed header", saveIt(*session))) continue;
        {
            auto bates = readBytes(tests::readAllBytes(session->path));
            const auto layers = bates->layers(0);
            if (layers.front.size() != 1 || layers.front[0].kind != core::DecorationKind::Bates)
                failures.push_back(label + ": removing the header took the Bates number too");
        }
        if (!step("remove bates", session->journal->run(std::make_unique<core::RemoveBatesCommand>()))) continue;
        if (!step("save removed bates", saveIt(*session))) continue;
        auto after = readBytes(tests::readAllBytes(session->path));
        const std::vector<PageState> now = snapshot(*after);
        if (now.size() != before.size()) {
            failures.push_back(label + ": the page count changed");
            continue;
        }
        for (std::size_t i = 0; i < now.size(); ++i) {
            if (!(now[i].page == before[i].page)) failures.push_back(label + " page " + std::to_string(i + 1) + ": the page dictionary differs");
            else if (!(now[i].contents == before[i].contents)) failures.push_back(label + " page " + std::to_string(i + 1) + ": /Contents differs");
            else if (!(now[i].resources == before[i].resources)) failures.push_back(label + " page " + std::to_string(i + 1) + ": /Resources differs");
        }
        if (label == "ordinary/text-letter.pdf") keepWritten(*session, QStringLiteral("roundtrip-text-letter"));
    }
    UNSCOPED_INFO(files << " files, " << pages << " pages decorated and taken off; not editable: " << notEditable.size()
                        << "; refused: " << refused.size());
    for (const std::string& r : refused) UNSCOPED_INFO("refused " << r);
    for (const std::string& f : failures) UNSCOPED_INFO(f);
    CHECK(failures.empty());
    CHECK(refused.empty());
    CHECK(files >= 100);   // the sweep reached the corpus, not a handful of it (121 editable files at T13)
}

TEST_CASE("decoration.roundtrip: a removal undone after a save lists the very streams it took, and an apply undone after a save writes nothing of its own", "[decoration][A-91]")
{
    auto session = tests::openCopy("ordinary/text-letter.pdf");
    REQUIRE(session);
    auto original = readBytes(session->openedBytes);
    const std::vector<PageState> before = snapshot(*original);

    REQUIRE(session->journal->run(std::make_unique<core::ApplyHeaderFooterCommand>(headerFooter(true), values())));
    // Before a save, an undo writes nothing at all.
    REQUIRE(session->journal->undo());
    CHECK(deltaIsEmpty(*session));
    REQUIRE(session->journal->redo());
    REQUIRE(saveIt(*session));
    auto decorated = readBytes(tests::readAllBytes(session->path));
    const auto listed = decorated->layers(0);
    REQUIRE(listed.front.size() == 1);
    REQUIRE(listed.layers.size() == 1);
    CHECK(decorated->page(0).has(pdf::ops::DecorationOps::kResourcesRecord));

    REQUIRE(session->journal->run(std::make_unique<core::RemoveHeaderFooterCommand>()));
    REQUIRE(saveIt(*session));
    auto removed = readBytes(tests::readAllBytes(session->path));
    CHECK(snapshot(*removed) == before);

    // The removal undone AFTER the save: the same draw stream and the same wrappers, by reference -- nothing rewritten.
    REQUIRE(session->journal->undo());
    REQUIRE(saveIt(*session));
    auto back = readBytes(tests::readAllBytes(session->path));
    const auto again = back->layers(0);
    REQUIRE(again.front.size() == 1);
    CHECK(again.front[0].stream == listed.front[0].stream);
    REQUIRE(again.layers.size() == 1);
    CHECK(again.layers[0].before == listed.layers[0].before);
    CHECK(again.layers[0].after == listed.layers[0].after);
    CHECK(pdf::ops::DecorationOps::formOf(back->table, back->page(0).get("Resources"), again.front[0].id)
          == pdf::ops::DecorationOps::formOf(decorated->table, decorated->page(0).get("Resources"), listed.front[0].id));

    // And the apply undone after its save: the pages as they were.
    REQUIRE(session->journal->undo());   // the apply
    CHECK_FALSE(session->journal->canUndo());
    REQUIRE(saveIt(*session));
    auto undone = readBytes(tests::readAllBytes(session->path));
    CHECK(snapshot(*undone) == before);
    keepWritten(*session, QStringLiteral("undo-after-save"));
}

TEST_CASE("decoration.roundtrip: update replaces every header in one step, and remove with none refuses with nothing changed", "[decoration][A-91]")
{
    auto session = tests::openCopy("ordinary/mixed-page-sizes.pdf");
    REQUIRE(session);
    const core::Status none = session->journal->run(std::make_unique<core::RemoveHeaderFooterCommand>());
    REQUIRE_FALSE(none);
    CHECK(none.error().messageKey == QLatin1String("error.decorate.noneFound"));
    CHECK_FALSE(session->journal->canUndo());
    CHECK(deltaIsEmpty(*session));

    core::HeaderFooterSpec odd = headerFooter(false);
    odd.parity = core::PageParity::Odd;
    REQUIRE(session->journal->run(std::make_unique<core::ApplyHeaderFooterCommand>(odd, values())));
    REQUIRE(saveIt(*session));
    core::HeaderFooterSpec all = headerFooter(false);
    all.areas[4] = QStringLiteral("Updated <<page>>");
    REQUIRE(session->journal->run(std::make_unique<core::ApplyHeaderFooterCommand>(all, values(), true)));
    REQUIRE(saveIt(*session));
    auto updated = readBytes(tests::readAllBytes(session->path));
    for (int i = 0; i < static_cast<int>(updated->walk.pages.size()); ++i) {
        INFO("page " << i + 1);
        const auto layers = updated->layers(i);
        CHECK(layers.front.size() == 1);   // ONE header on every page: the old ones taken, the new one on all
        CHECK(layers.layers.size() == 1);
    }
    // One undo puts the odd pages' header back, as it was saved.
    REQUIRE(session->journal->undo());
    REQUIRE(saveIt(*session));
    auto odd2 = readBytes(tests::readAllBytes(session->path));
    for (int i = 0; i < static_cast<int>(odd2->walk.pages.size()); ++i) {
        INFO("page " << i + 1);
        CHECK(odd2->layers(i).front.size() == (i % 2 == 0 ? 1u : 0u));
    }
}

TEST_CASE("decoration.roundtrip: a /Contents written as a reference to an array, and /Resources two pages share, come back exactly", "[decoration][A-91][I-34]")
{
    // NO CORPUS FILE HAS EITHER SHAPE -- found by planted control P2 at T14, which took the record of an indirect
    // /Contents away and stayed green over the whole corpus. Built here: two pages sharing one /Resources object, the
    // first page's /Contents a reference to an ARRAY of two streams.
    tests::TinyPdf pdf;
    const int catalog = pdf.reserve();
    const int tree = pdf.reserve();
    const int font = pdf.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    const int resources = pdf.add("<< /Font << /F1 " + std::to_string(font) + " 0 R >> >>");
    const std::string a = "BT /F1 24 Tf 72 700 Td (first) Tj ET";
    const std::string b = "BT /F1 24 Tf 72 600 Td (second) Tj ET";
    const int s1 = pdf.add("<< /Length " + std::to_string(a.size()) + " >>\nstream\n" + a + "\nendstream");
    const int s2 = pdf.add("<< /Length " + std::to_string(b.size()) + " >>\nstream\n" + b + "\nendstream");
    const int array = pdf.add("[" + std::to_string(s1) + " 0 R " + std::to_string(s2) + " 0 R]");
    const int p1 = pdf.add("<< /Type /Page /Parent " + std::to_string(tree) + " 0 R /MediaBox [0 0 612 792] /Resources "
                           + std::to_string(resources) + " 0 R /Contents " + std::to_string(array) + " 0 R >>");
    const int p2 = pdf.add("<< /Type /Page /Parent " + std::to_string(tree) + " 0 R /MediaBox [0 0 612 792] /Resources "
                           + std::to_string(resources) + " 0 R /Contents " + std::to_string(s2) + " 0 R >>");
    pdf.set(tree, "<< /Type /Pages /Kids [" + std::to_string(p1) + " 0 R " + std::to_string(p2) + " 0 R] /Count 2 >>");
    pdf.set(catalog, "<< /Type /Catalog /Pages " + std::to_string(tree) + " 0 R >>");
    auto session = tests::openBuilt(QStringLiteral("shared.pdf"), pdf.bytes(catalog));
    REQUIRE(session);
    auto original = readBytes(session->openedBytes);
    const std::vector<PageState> before = snapshot(*original);
    REQUIRE(original->page(0).get("Contents").isReference());

    core::HeaderFooterSpec spec = headerFooter(false);
    spec.range = QStringLiteral("1");
    REQUIRE(session->journal->run(std::make_unique<core::ApplyHeaderFooterCommand>(spec, values())));
    REQUIRE(saveIt(*session));
    auto decorated = readBytes(tests::readAllBytes(session->path));
    // The shared /Resources object is NOT edited: page 2 still names it, and page 1 has a dictionary of its own.
    CHECK(decorated->page(1).get("Resources") == before[1].page.get("Resources"));
    CHECK(decorated->page(0).get("Resources").isDictionary());
    CHECK(decorated->page(0).get(pdf::ops::DecorationOps::kContentsRecord) == before[0].page.get("Contents"));
    CHECK(decorated->object(resources) == original->object(resources));
    CHECK(decorated->object(array) == original->object(array));

    REQUIRE(session->journal->run(std::make_unique<core::RemoveHeaderFooterCommand>()));
    REQUIRE(saveIt(*session));
    auto removed = readBytes(tests::readAllBytes(session->path));
    CHECK(snapshot(*removed) == before);
    CHECK(removed->page(0).get("Contents").isReference());   // the very reference, not a direct array of its items

    // GUIDE I-34: T9's resize WRAPPER takes the same path -- before T14 its undo wrote this page's /Contents back as a
    // direct array of the two streams, not the reference it had.
    REQUIRE(session->journal->run(std::make_unique<core::ResizePagesCommand>(std::vector<int>{0}, core::PdfSize{595.276, 841.89}, true,
                                                                             core::ResizeAnchor::Centre)));
    REQUIRE(saveIt(*session));
    REQUIRE(session->journal->undo());
    REQUIRE(saveIt(*session));
    auto unwrapped = readBytes(tests::readAllBytes(session->path));
    CHECK(snapshot(*unwrapped) == before);
    CHECK(unwrapped->page(0).get("Contents") == before[0].page.get("Contents"));
}

// ==========================================================================================
// A-94 -- another producer's decoration is not ours
// ==========================================================================================

TEST_CASE("decoration.foreign: another producer's header and watermark are neither listed, taken nor changed", "[decoration][A-94]")
{
    auto session = tests::openCopy("assembly/decorated-elsewhere.pdf");
    REQUIRE(session);
    auto original = readBytes(session->openedBytes);
    const std::vector<PageState> before = snapshot(*original);
    for (int i = 0; i < static_cast<int>(original->walk.pages.size()); ++i) {
        const auto layers = original->layers(i);
        CHECK(layers.front.empty());
        CHECK(layers.behind.empty());
        CHECK(layers.layers.empty());
    }
    // Neither remove finds anything.
    CHECK(session->journal->run(std::make_unique<core::RemoveHeaderFooterCommand>()).error().messageKey == QLatin1String("error.decorate.noneFound"));
    CHECK(session->journal->run(std::make_unique<core::RemoveBatesCommand>()).error().messageKey == QLatin1String("error.decorate.noneFound"));

    // Ours beside theirs: every stream they listed still listed, in order, and both their forms still named.
    REQUIRE(session->journal->run(std::make_unique<core::ApplyHeaderFooterCommand>(headerFooter(false), values())));
    REQUIRE(saveIt(*session));
    auto both = readBytes(tests::readAllBytes(session->path));
    for (int i = 0; i < static_cast<int>(both->walk.pages.size()); ++i) {
        INFO("page " << i + 1);
        const auto layers = both->layers(i);
        REQUIRE(layers.front.size() == 1);
        std::vector<CosObject> theirs;
        const CosObject was = before[static_cast<std::size_t>(i)].contents;
        if (was.isArray()) theirs.assign(was.items().begin(), was.items().end());
        else theirs.push_back(was);
        for (CosObject& item : theirs) item.clearSpansRecursively();
        CHECK(layers.content == theirs);
        CosObject resources = both->page(i).get("Resources");
        const CosObject xobjects = resources.get("XObject");
        REQUIRE(xobjects.isDictionary());
        CHECK(xobjects.has("Xi0"));
        CHECK(xobjects.has("Xi1"));
    }
    // And ours taken off again: theirs exactly as they were.
    REQUIRE(session->journal->run(std::make_unique<core::RemoveHeaderFooterCommand>()));
    REQUIRE(saveIt(*session));
    auto after = readBytes(tests::readAllBytes(session->path));
    CHECK(snapshot(*after) == before);
    keepWritten(*session, QStringLiteral("foreign"));
}

// ==========================================================================================
// A-95 -- the wrapper a shrink writes, and that removing the header takes it (first-party half)
// ==========================================================================================

TEST_CASE("headerfooter.shrink: the content is drawn through one marked wrapper that scales it, and the header's removal takes it", "[decoration][A-95]")
{
    auto session = tests::openCopy("ordinary/text-letter.pdf");
    REQUIRE(session);
    REQUIRE(session->journal->run(std::make_unique<core::ApplyHeaderFooterCommand>(headerFooter(true), values())));
    REQUIRE(saveIt(*session));
    auto saved = readBytes(tests::readAllBytes(session->path));
    const auto layers = saved->layers(0);
    REQUIRE(layers.layers.size() == 1);
    REQUIRE(layers.front.size() == 1);
    CHECK(layers.layers[0].id == layers.front[0].id);   // the wrapper is the header's own
    const CosObject before = saved->object(layers.layers[0].before.reference().number);
    CHECK(pdf::ops::PageOps::wrapId(before) == layers.front[0].id);
    CHECK_FALSE(pdf::ops::PageOps::decorationOf(before).has_value());   // a wrapper is not a decoration
    REQUIRE(session->journal->run(std::make_unique<core::RemoveHeaderFooterCommand>()));
    REQUIRE(saveIt(*session));
    auto removed = readBytes(tests::readAllBytes(session->path));
    CHECK(removed->layers(0).layers.empty());
    CHECK(removed->layers(0).front.empty());
}

// ==========================================================================================
// The four commands' journal lines -- mutation-lint's [serialize:] cases
// ==========================================================================================

TEST_CASE("decoration commands: each journal line reads back as the command it was, and a hostile one is refused", "[decoration][serialize:ApplyHeaderFooterCommand][serialize:RemoveHeaderFooterCommand][serialize:ApplyBatesCommand][serialize:RemoveBatesCommand]")
{
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(std::make_unique<core::ApplyHeaderFooterCommand>(headerFooter(true), values(), true));
    commands.push_back(std::make_unique<core::RemoveHeaderFooterCommand>());
    commands.push_back(std::make_unique<core::ApplyBatesCommand>(bates(77), values()));
    commands.push_back(std::make_unique<core::RemoveBatesCommand>());
    for (const auto& command : commands) {
        const QJsonObject line = command->serialize();
        INFO(QJsonDocument(line).toJson(QJsonDocument::Compact).toStdString());
        auto back = core::CommandRegistry::deserialize(line);
        REQUIRE(back);
        CHECK(back.value()->serialize() == line);
        CHECK(back.value()->typeId() == command->typeId());
    }
    // Hostile lines: refused by key, never a crash.
    QJsonObject apply = commands[0]->serialize();
    for (const char* field : {"key", "spec", "values", "replace"}) {
        QJsonObject bad = apply;
        bad.insert(QLatin1String(field), 12);
        INFO(field);
        CHECK_FALSE(core::CommandRegistry::deserialize(bad));
    }
    QJsonObject noBates = commands[2]->serialize();
    QJsonObject spec = noBates.value(QStringLiteral("spec")).toObject();
    spec.insert(QStringLiteral("areas"), QJsonObject{{QStringLiteral("footerRight"), QStringLiteral("no number")}});
    noBates.insert(QStringLiteral("spec"), spec);
    CHECK_FALSE(core::CommandRegistry::deserialize(noBates));
    QJsonObject removeBad = commands[1]->serialize();
    removeBad.insert(QStringLiteral("key"), QStringLiteral("not-a-key"));
    CHECK_FALSE(core::CommandRegistry::deserialize(removeBad));
}
