// tests/unit/tst_links.cpp -- links: what following one does, what verifying finds, which text is a web address, and a
// link's fields through Stage 6's commands and the SHIPPED sink. Stage 7 T12, decision D7-12, guide cases A-84 ... A-88
// (`Stage7-Assembly-Testing-Guide.md` §5.14) -- their headless halves.
//
// WHAT THIS FILE IS AND IS NOT (convention 19). The decisions (`core/doc/LinkActions`) are graded with no engine and no
// display. Every document case opens a copy of a corpus file through `tests/unit/CosBackend.h` -- the first-party reader
// and the real `pdf/ops/MutationSinkImpl` -- runs commands through the journal, saves, and reads the written file back
// with `pdf/ops/AnnotationReader` and the COS layer. **It does not see PDFium, and it presses no key**: a link drawn with
// the tool, made from a selection, followed with the Hand tool and the URL asked about are `tests/ui/tst_linktool.cpp`'s
// (A-83, A-85's surface half); addresses found in a corpus page's text as the engine extracts it, and what PDFium draws
// of a link's border, are `tests/render/tst_links.cpp`'s (A-84, A-86).

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/LinkActions.h"
#include "core/text/PageTextModel.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/PageOps.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace rawpage;
using core::LinkFollow;
using core::OutlineAction;
using pdf::cos::CosObject;

namespace {

OutlineAction actionOf(OutlineAction::Kind kind)
{
    OutlineAction action;
    action.kind = kind;
    return action;
}

OutlineAction goTo(int page, core::Destination::Fit fit = core::Destination::Fit::Fit)
{
    core::Destination at;
    at.pageIndex = page;
    at.fit = fit;
    return OutlineAction::goTo(at);
}

struct Written {
    QByteArray bytes;
    pdf::cos::XrefTable table;
    pdf::ops::PageWalk walk;
    core::AnnotationStore::Reading annotations;

    CosObject object(long long number)
    {
        auto fetched = table.fetch(number);
        REQUIRE(fetched);
        CosObject out = fetched.value().object;
        out.clearSpansRecursively();
        return out;
    }
    const core::Annotation* byName(const QString& nm) const
    {
        for (const core::Annotation& a : annotations.annotations) {
            if (a.nm == nm) return &a;
        }
        return nullptr;
    }
    const core::Annotation* byObject(long long number) const
    {
        for (const core::Annotation& a : annotations.annotations) {
            if (a.id == core::AnnotationId::ofObject(number)) return &a;
        }
        return nullptr;
    }
};

std::unique_ptr<Written> readBytes(QByteArray bytes)
{
    auto out = std::make_unique<Written>();
    out->bytes = std::move(bytes);
    auto opened = pdf::cos::openWithRepair(std::string_view(out->bytes.constData(), static_cast<std::size_t>(out->bytes.size())));
    REQUIRE(opened);
    out->table = std::move(opened.value().table);
    auto walk = pdf::ops::PageOps::walk(out->table);
    REQUIRE(walk);
    out->walk = std::move(walk).value();
    out->annotations = pdf::ops::AnnotationReader::read(out->table, out->walk);
    return out;
}

bool save(tests::SaveSession& session, const char* name)
{
    if (!session.coordinator->save(*session.journal, session.path)) return false;
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);   // kept for `qpdf --check` (D7-18's oracle 2)
    if (!QDir().mkpath(dir)) return false;
    const QString target = QDir(dir).filePath(QStringLiteral("links-%1.pdf").arg(QLatin1String(name)));
    QFile::remove(target);
    return QFile::copy(session.path, target);
}

core::Status run(tests::SaveSession& session, std::unique_ptr<core::Command> command)
{
    return session.journal->run(std::move(command));
}

QJsonObject label(const char* name)
{
    QFile file(tests::corpusFile(std::string("labels/") + name));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(file.readAll()).object();
}

const core::Annotation& linkObject(const core::Document& document, long long object)
{
    const std::optional<std::size_t> index = document.annotations().findObject(object);
    REQUIRE(index);
    return document.annotations().at(*index);
}

// A LINE OF TEXT as a page model holds it: each character 6 points wide and 12 high at `y`, a generated line break after.
void addLine(core::text::PageTextModel& model, const QString& line, double y)
{
    double x = 72;
    for (const char32_t c : line.toUcs4()) {
        core::text::TextChar ch;
        ch.codepoint = c;
        ch.box = core::PdfRect{core::PdfPoint{x, y}, core::PdfSize{6, 12}};
        ch.origin = core::PdfPoint{x, y + 2};
        ch.fontSize = 10;
        if (c == U' ') ch.flags = static_cast<std::uint8_t>(core::text::CharFlag::Generated);
        model.append(ch);
        x += 6;
    }
    core::text::TextChar end;
    end.codepoint = U'\n';
    end.box = core::PdfRect{core::PdfPoint{x, y}, core::PdfSize{0, 12}};
    end.flags = static_cast<std::uint8_t>(core::text::CharFlag::Generated);
    model.append(end);
}

} // namespace

// ==========================================================================================
// A-85 -- following: each kind, and what is never done
// ==========================================================================================

TEST_CASE("links.follow: a page and a name navigate, a file and a URL ask, a program is refused, a script never runs", "[links][follow][A-85]")
{
    core::Destinations table;
    core::NamedDestination chapter;
    chapter.name = QStringLiteral("chapter");
    chapter.destination.pageIndex = 2;
    chapter.destination.fit = core::Destination::Fit::FitH;
    table.add(chapter);
    core::NamedDestination ghost;
    ghost.name = QStringLiteral("ghost");
    table.add(ghost);
    const auto follow = [&table](const OutlineAction& action) { return core::followLink(action, table, 5); };

    // Navigate -- to the page, or the named destination's page as the table has it NOW.
    LinkFollow step = follow(goTo(3));
    CHECK(step.step == LinkFollow::Step::Navigate);
    CHECK(step.destination.pageIndex == 3);
    OutlineAction named = actionOf(OutlineAction::Kind::GoToNamed);
    named.name = QStringLiteral("chapter");
    step = follow(named);
    CHECK(step.step == LinkFollow::Step::Navigate);
    CHECK(step.destination.pageIndex == 2);
    CHECK(step.destination.fit == core::Destination::Fit::FitH);
    // Nowhere, and why.
    CHECK(follow(goTo(5)).reason == QStringLiteral("links.follow.pageMissing"));
    CHECK(follow(goTo(-1)).step == LinkFollow::Step::Nowhere);
    named.name = QStringLiteral("absent");
    CHECK(follow(named).reason == QStringLiteral("links.follow.nameMissing"));
    named.name = QStringLiteral("ghost");
    CHECK(follow(named).reason == QStringLiteral("links.follow.pageMissing"));
    CHECK(follow(OutlineAction{}).reason == QStringLiteral("links.follow.noTarget"));

    // Another PDF: ASKED, then opened at its page -- unless Windows would run it.
    OutlineAction remote = actionOf(OutlineAction::Kind::GoToRemote);
    remote.file = QStringLiteral("other.pdf");
    remote.destination.pageIndex = 4;
    step = follow(remote);
    CHECK(step.step == LinkFollow::Step::OpenRemote);
    CHECK(step.file == QStringLiteral("other.pdf"));
    CHECK(step.destination.pageIndex == 4);
    for (const char* runnable : {"setup.exe", "Setup.EXE", "evil.exe.", "tool.bat", "script.ps1", "short.lnk"}) {
        INFO(runnable);
        remote.file = QLatin1String(runnable);
        CHECK(follow(remote).step == LinkFollow::Step::Refused);
        OutlineAction launch = actionOf(OutlineAction::Kind::Launch);
        launch.file = QLatin1String(runnable);
        CHECK(follow(launch).step == LinkFollow::Step::Refused);
        CHECK(follow(launch).reason == QStringLiteral("links.follow.executable"));
    }
    OutlineAction launch = actionOf(OutlineAction::Kind::Launch);
    launch.file = QStringLiteral("notes.txt");
    CHECK(follow(launch).step == LinkFollow::Step::LaunchFile);

    // A web address: ASKED, then handed over -- four schemes, and `www.` taken as http.
    OutlineAction uri = actionOf(OutlineAction::Kind::Uri);
    for (const auto& [written, opened] : {std::pair{"https://example.com/a?b=1", "https://example.com/a?b=1"},
                                         std::pair{"http://example.com", "http://example.com"},
                                         std::pair{"mailto:someone@example.com", "mailto:someone@example.com"},
                                         std::pair{"ftp://files.example.com/x", "ftp://files.example.com/x"},
                                         std::pair{"www.example.com", "http://www.example.com"}}) {
        INFO(written);
        uri.uri = QLatin1String(written);
        step = follow(uri);
        CHECK(step.step == LinkFollow::Step::OpenUri);
        CHECK(step.uri == QLatin1String(opened));
    }
    for (const char* refused : {"javascript:alert(1)", "file:///C:/Windows/System32/calc.exe", "data:text/html,x", "vbscript:x",
                                "ms-settings:privacy", "example.com"}) {
        INFO(refused);
        uri.uri = QLatin1String(refused);
        CHECK(follow(uri).step == LinkFollow::Step::Refused);
        CHECK(follow(uri).reason == QStringLiteral("links.follow.uriScheme"));
    }

    // WRITTEN, NEVER RUN -- each says which.
    CHECK(follow(actionOf(OutlineAction::Kind::JavaScript)).reason == QStringLiteral("links.follow.script"));
    for (const OutlineAction::Kind form : {OutlineAction::Kind::SubmitForm, OutlineAction::Kind::ResetForm,
                                           OutlineAction::Kind::ImportData, OutlineAction::Kind::Hide}) {
        CHECK(follow(actionOf(form)).step == LinkFollow::Step::NotRun);
        CHECK(follow(actionOf(form)).reason == QStringLiteral("links.follow.form"));
    }
    CHECK(follow(actionOf(OutlineAction::Kind::Named)).reason == QStringLiteral("links.follow.menu"));
    CHECK(follow(actionOf(OutlineAction::Kind::Other)).reason == QStringLiteral("links.follow.other"));
}

// ==========================================================================================
// A-87 -- verify reports what is broken, and only that
// ==========================================================================================

TEST_CASE("links.verify: a deleted page, a page outside the tree and a missing file -- exactly the labelled ones", "[links][verify][A-87]")
{
    // refs-every-kind, its own name kept, so its GoToR to itself is on disk: nothing broken.
    auto session = tests::openCopy("assembly/refs-every-kind.pdf");
    REQUIRE(session);
    const QDir folder(session->directory.path());
    int asked = 0;
    const auto exists = [&folder, &asked](const QString& file) {
        ++asked;
        return QFileInfo::exists(folder.filePath(file));
    };
    CHECK(core::verifyLinks(session->journal->document(), exists).empty());
    CHECK(asked == 1);   // the GoToR alone: no web address is fetched

    // ONE PAGE DELETED -- the one the label's first `dest` link targets: that link, and nothing else, is broken.
    const QJsonObject refs = label("assembly-refs-every-kind.json");
    long long brokenLink = -1;
    int deleted = -1;
    for (const QJsonValue link : refs.value(QStringLiteral("links")).toArray()) {
        if (link.toObject().value(QStringLiteral("form")).toString() != QLatin1String("dest")) continue;
        brokenLink = link.toObject().value(QStringLiteral("object")).toInteger();
        deleted = QStringView(link.toObject().value(QStringLiteral("targetIdentity")).toString()).mid(2).toInt() - 1;
        break;
    }
    REQUIRE(brokenLink > 0);
    REQUIRE(run(*session, std::make_unique<core::DeletePagesCommand>(std::vector<int>{deleted})));
    std::vector<core::LinkProblem> problems = core::verifyLinks(session->journal->document(), exists);
    REQUIRE(problems.size() == 1);
    CHECK(problems.front().kind == core::LinkProblem::Kind::PageMissing);
    CHECK(problems.front().link == core::AnnotationId::ofObject(brokenLink));
    REQUIRE(session->journal->undo());
    CHECK(core::verifyLinks(session->journal->document(), exists).empty());

    // A NAMED DESTINATION GONE: every link naming it, and nothing else.
    for (const QJsonValue link : refs.value(QStringLiteral("links")).toArray()) {
        const QJsonObject l = link.toObject();
        if (!l.contains(QStringLiteral("name"))) continue;
        core::Document document = session->journal->document();
        auto removed = document.destinations().remove(l.value(QStringLiteral("name")).toString());
        REQUIRE(removed);
        problems = core::verifyLinks(document, exists);
        INFO(l.value(QStringLiteral("name")).toString().toStdString());
        REQUIRE(problems.size() == 1);
        CHECK(problems.front().kind == core::LinkProblem::Kind::NameMissing);
        CHECK(problems.front().link == core::AnnotationId::ofObject(l.value(QStringLiteral("object")).toInteger()));
    }

    // A GoToR TO A FILE NOT ON DISK: the same document under another name.
    auto renamed = std::make_unique<tests::SaveSession>();
    REQUIRE(renamed->directory.isValid());
    renamed->path = renamed->directory.filePath(QStringLiteral("copy.pdf"));
    REQUIRE(QFile::copy(tests::corpusFile("assembly/refs-every-kind.pdf"), renamed->path));
    QFile::setPermissions(renamed->path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    renamed = tests::openSession(std::move(renamed));
    REQUIRE(renamed);
    const QDir elsewhere(renamed->directory.path());
    problems = core::verifyLinks(renamed->journal->document(),
                                 [&elsewhere](const QString& file) { return QFileInfo::exists(elsewhere.filePath(file)); });
    REQUIRE(problems.size() == 1);
    CHECK(problems.front().kind == core::LinkProblem::Kind::FileMissing);
    CHECK(problems.front().target == QStringLiteral("refs-every-kind.pdf"));
    long long goToR = -1;
    for (const QJsonValue link : refs.value(QStringLiteral("links")).toArray()) {
        if (link.toObject().value(QStringLiteral("form")).toString() == QLatin1String("goToR"))
            goToR = link.toObject().value(QStringLiteral("object")).toInteger();
    }
    CHECK(problems.front().link == core::AnnotationId::ofObject(goToR));

    // refs-by-index: the label's link to a page OUTSIDE the tree, and nothing else.
    auto byIndex = tests::openCopy("assembly/refs-by-index.pdf");
    REQUIRE(byIndex);
    // ... and that link RECOLOURED keeps the `/Dest` its producer wrote: an unresolved target is the disk's, not a target the
    // edit cleared (a defect in T12's own sink, found reading it).
    {
        long long orphan = -1;
        for (const QJsonValue link : label("assembly-refs-by-index.json").value(QStringLiteral("links")).toArray()) {
            if (link.toObject().value(QStringLiteral("outsidePageTree")).toBool()) orphan = link.toObject().value(QStringLiteral("object")).toInteger();
        }
        REQUIRE(orphan > 0);
        const auto before = readBytes(byIndex->openedBytes);
        core::Annotation recoloured = linkObject(byIndex->journal->document(), orphan);
        REQUIRE(recoloured.action.kind == OutlineAction::Kind::GoTo);
        REQUIRE(recoloured.action.destination.pageIndex == -1);
        recoloured.color = {0, 1, 0};
        REQUIRE(run(*byIndex, std::make_unique<core::EditAnnotationCommand>(std::vector<core::Annotation>{recoloured})));
        REQUIRE(save(*byIndex, "orphan-recoloured"));
        const auto after = readBytes(tests::readAllBytes(byIndex->path));
        CHECK(after->object(orphan).get("Dest") == before->object(orphan).get("Dest"));
        REQUIRE(byIndex->journal->undo());
    }
    long long outside = -1;
    for (const QJsonValue link : label("assembly-refs-by-index.json").value(QStringLiteral("links")).toArray()) {
        if (link.toObject().value(QStringLiteral("outsidePageTree")).toBool()) outside = link.toObject().value(QStringLiteral("object")).toInteger();
    }
    problems = core::verifyLinks(byIndex->journal->document(), [](const QString&) { return true; });
    REQUIRE(problems.size() == 1);
    CHECK(problems.front().kind == core::LinkProblem::Kind::PageMissing);
    CHECK(problems.front().link == core::AnnotationId::ofObject(outside));
}

// ==========================================================================================
// A-84 -- addresses in the text, in one step
// ==========================================================================================

TEST_CASE("links.autodetect: exactly the addresses, no near-miss, each on its glyphs, in one command", "[links][autodetect][A-84]")
{
    core::text::PageTextModel page(0);
    const std::vector<std::pair<QString, std::vector<QString>>> lines{
        {QStringLiteral("Visit https://example.com/path?q=1 today."), {QStringLiteral("https://example.com/path?q=1")}},
        {QStringLiteral("Write to mailto:someone@example.org now"), {QStringLiteral("mailto:someone@example.org")}},
        {QStringLiteral("Docs at www.rawpage.test/docs."), {QStringLiteral("www.rawpage.test/docs")}},
        {QStringLiteral("(see https://example.com/a_(b))"), {QStringLiteral("https://example.com/a_(b)")}},
        {QStringLiteral("Two: http://a.example and HTTPS://B.EXAMPLE/X,"), {QStringLiteral("http://a.example"), QStringLiteral("HTTPS://B.EXAMPLE/X")}},
        // The near-misses: no scheme, one slash, no dot, a scheme this build does not link, a bare address, inside a word.
        {QStringLiteral("example.com http:/broken.example www.nodot http://localhost"), {}},
        {QStringLiteral("ftp://files.example.com emailme@example.com xhttps://bad.example"), {}},
    };
    std::vector<QString> expected;
    double y = 700;
    for (const auto& [text, urls] : lines) {
        addLine(page, text, y);
        y -= 20;
        for (const QString& url : urls) expected.push_back(url);
    }
    const std::vector<core::DetectedUrl> found = core::detectUrls(page);
    std::vector<QString> texts;
    for (const core::DetectedUrl& url : found) texts.push_back(url.text);
    CHECK(texts == expected);
    // On its glyphs: the first address starts at character 6 of its line -- 72 + 36 -- and is 28 characters long.
    REQUIRE_FALSE(found.empty());
    CHECK(found.front().quadPoints == std::vector<double>{108, 712, 276, 712, 108, 700, 276, 700});
    CHECK(found.front().rect == core::PdfRect{core::PdfPoint{108, 700}, core::PdfSize{168, 12}});
    CHECK(found[2].uri == QStringLiteral("http://www.rawpage.test/docs"));

    // ONE COMMAND, ONE UNDO STEP, through the shipped sink -- and a second run finds every address already linked.
    auto session = tests::openCopy("assembly/refs-none.pdf");
    REQUIRE(session);
    std::vector<core::Annotation> made = core::linksForUrls(session->journal->document(), 0, page, QStringLiteral("Tester"), 1791504000);
    REQUIRE(made.size() == expected.size());
    for (const core::Annotation& link : made) {
        CHECK(link.subtype == core::AnnotationSubtype::Link);
        CHECK(link.action.kind == OutlineAction::Kind::Uri);
    }
    const QByteArray before = session->journal->document().modelHash();
    REQUIRE(run(*session, std::make_unique<core::AddAnnotationCommand>(made)));
    CHECK(core::linksForUrls(session->journal->document(), 0, page, QStringLiteral("Tester"), 1791504000).empty());
    REQUIRE(save(*session, "autodetect"));
    auto written = readBytes(tests::readAllBytes(session->path));
    for (const core::Annotation& link : made) {
        const core::Annotation* back = written->byName(link.nm);
        REQUIRE(back != nullptr);
        CHECK(back->action.uri == link.action.uri);
        CHECK(back->quadPoints == link.quadPoints);
        CHECK(written->object(written->annotations.annotations.size() > 0 ? back->id.object : 0).get("A").get("S")
              == CosObject::makeName("URI"));
    }
    REQUIRE(session->journal->undo());
    CHECK(tests::sha256(session->journal->document().modelHash()) == tests::sha256(before));
    CHECK_FALSE(session->journal->canUndo());
}

// ==========================================================================================
// A-86 -- a link's appearance and highlight, written and read back
// ==========================================================================================

TEST_CASE("links.appearance: visible and invisible, width, style, colour and each highlight, written and read back", "[links][appearance][A-86]")
{
    auto session = tests::openCopy("assembly/refs-none.pdf");
    REQUIRE(session);
    std::vector<core::Annotation> links;
    int row = 0;
    for (const char* style : {"S", "D", "U", "B", "I"}) {
        for (const char* highlight : {"N", "I", "O", "P"}) {
            core::Annotation link = core::newLink(0, core::PdfRect{core::PdfPoint{36, 700.0 - row * 20}, core::PdfSize{100, 14}}, {},
                                                  goTo(1), QStringLiteral("Tester"), 1791504000);
            link.borderWidth = 0.5 + row % 3;
            link.borderStyle = QLatin1String(style);
            if (std::string(style) == "D") link.dashPattern = {4, 2};
            link.color = {static_cast<double>(row % 2), 0.25, 1};
            link.highlight = QLatin1String(highlight);
            links.push_back(std::move(link));
            ++row;
        }
    }
    // Invisible: no border.
    core::Annotation invisible = core::newLink(0, core::PdfRect{core::PdfPoint{300, 700}, core::PdfSize{100, 14}}, {}, goTo(1),
                                               QStringLiteral("Tester"), 1791504000);
    links.push_back(invisible);
    REQUIRE(run(*session, std::make_unique<core::AddAnnotationCommand>(links)));
    REQUIRE(save(*session, "appearance"));
    auto written = readBytes(tests::readAllBytes(session->path));
    for (const core::Annotation& link : links) {
        INFO(link.borderStyle.toStdString() << " " << link.highlight.toStdString());
        const core::Annotation* back = written->byName(link.nm);
        REQUIRE(back != nullptr);
        CHECK(back->borderWidth == link.borderWidth);
        CHECK(back->borderStyle == link.borderStyle);
        CHECK(back->dashPattern == link.dashPattern);
        CHECK(back->color == link.color);
        CHECK(back->highlight == link.highlight);
        CHECK(back->action.kind == OutlineAction::Kind::GoTo);
        CHECK(back->action.destination.pageIndex == 1);
        // Its target BY REFERENCE (D7-2): the page object, so a later move needs no write.
        const CosObject dictionary = written->object(back->id.object);
        CHECK(dictionary.get("Dest").items().front() == CosObject::makeReference(written->walk.pages[1].number, 0));
        CHECK_FALSE(dictionary.has("A"));
        // An appearance of its own (D6-6) -- an invisible one draws nothing.
        CHECK(dictionary.get("AP").get("N").isReference());
    }
    CHECK(written->byName(invisible.nm)->borderWidth == 0.0);
}

// ==========================================================================================
// A-88 and A-19's link half -- Stage 6's commands carry a link, and a page command remaps it
// ==========================================================================================

TEST_CASE("links: a recolour keeps the target's bytes, a retarget names its page by reference, a move needs no write", "[links][A-88]")
{
    auto session = tests::openCopy("assembly/refs-every-kind.pdf");
    REQUIRE(session);
    const auto original = readBytes(session->openedBytes);
    const auto& document = [&session]() -> const core::Document& { return session->journal->document(); };
    // Object 19 is the label's `/Dest` link to P-0009, page 8.
    REQUIRE(linkObject(document(), 19).action.kind == OutlineAction::Kind::GoTo);
    REQUIRE(linkObject(document(), 19).action.destination.pageIndex == 8);

    // RECOLOURED: its /Dest is written as it was, byte for byte (D6-5).
    core::Annotation recoloured = linkObject(document(), 19);
    recoloured.color = {1, 0, 0};
    REQUIRE(run(*session, std::make_unique<core::EditAnnotationCommand>(std::vector<core::Annotation>{recoloured})));
    // Link 20 writes its target as a GoTo ACTION: recoloured, it keeps its `/A` and gains no `/Dest`.
    core::Annotation actionRecoloured = linkObject(document(), 20);
    actionRecoloured.color = {0, 0.6, 0};
    const core::Status recolouredAction = run(*session, std::make_unique<core::EditAnnotationCommand>(std::vector<core::Annotation>{actionRecoloured}));
    INFO((recolouredAction ? std::string("ok") : QString(recolouredAction.error().messageKey).toStdString() + " " + recolouredAction.error().context.toStdString()));
    REQUIRE(recolouredAction);
    REQUIRE(save(*session, "recolour-action"));
    {
        auto recolouredFile = readBytes(tests::readAllBytes(session->path));
        CHECK(recolouredFile->object(20).get("A") == original->object(20).get("A"));
        CHECK_FALSE(recolouredFile->object(20).has("Dest"));
    }
    REQUIRE(session->journal->undo());
    // RETARGETED: link 20 to page 2, at a view.
    core::Annotation retargeted = linkObject(document(), 20);
    retargeted.action = goTo(2, core::Destination::Fit::XYZ);
    retargeted.action.destination.hasY = true;
    retargeted.action.destination.y = 300;
    REQUIRE(run(*session, std::make_unique<core::EditAnnotationCommand>(std::vector<core::Annotation>{retargeted})));
    REQUIRE(save(*session, "retarget"));
    auto written = readBytes(tests::readAllBytes(session->path));
    CHECK(written->object(19).get("Dest") == original->object(19).get("Dest"));
    // ... and its whole dictionary the file's, key for key IN ORDER, but `/C` (D6-5): a target rewritten from the model
    // writes the same array for a `/Fit`, and moves the key -- which only this sees (planted P1 stayed green without it).
    {
        const CosObject before = original->object(19);
        const CosObject after = written->object(19);
        std::vector<std::string> beforeKeys, afterKeys;
        for (const CosObject::Entry& entry : before.entries()) beforeKeys.push_back(entry.key);
        for (const CosObject::Entry& entry : after.entries()) afterKeys.push_back(entry.key);
        if (std::find(beforeKeys.begin(), beforeKeys.end(), "C") == beforeKeys.end()) beforeKeys.push_back("C");
        CHECK(afterKeys == beforeKeys);
        for (const CosObject::Entry& entry : after.entries()) {
            INFO(entry.key);
            if (entry.key != "C") CHECK(entry.value == before.get(entry.key));
        }
    }
    CHECK(written->object(19).get("C") == CosObject::makeArray({CosObject::makeReal(1), CosObject::makeReal(0), CosObject::makeReal(0)}));
    const CosObject dest = written->object(20).get("Dest");
    REQUIRE(dest.isArray());
    CHECK(dest.items().front() == CosObject::makeReference(written->walk.pages[2].number, 0));
    CHECK(dest.items()[1] == CosObject::makeName("XYZ"));
    CHECK_FALSE(written->object(20).has("A"));

    // A MOVE: the model carries both targets; the file needs no word about them -- a reference follows its page.
    const int last = document().pages().count() - 1;
    REQUIRE(run(*session, std::make_unique<core::MovePagesCommand>(std::vector<int>{2}, last)));
    CHECK(linkObject(document(), 20).action.destination.pageIndex == last);
    CHECK(linkObject(document(), 19).action.destination.pageIndex == 7);
    auto delta = session->sink()->delta();
    REQUIRE(delta);
    for (const auto& change : delta.value().changes()) {
        CHECK(change.number != 19);
        CHECK(change.number != 20);
    }
    session->sink()->composedNothing();
    REQUIRE(session->journal->undo());

    // A DELETE of the retargeted page: unresolved in the model, no target in the file; the undo puts it back -- after a
    // save, in the model and in the file.
    REQUIRE(run(*session, std::make_unique<core::DeletePagesCommand>(std::vector<int>{2})));
    CHECK(linkObject(document(), 20).action.destination.pageIndex == -1);
    CHECK(linkObject(document(), 20).action.kind == OutlineAction::Kind::GoTo);
    REQUIRE(save(*session, "retarget-deleted"));
    CHECK_FALSE(readBytes(tests::readAllBytes(session->path))->object(20).has("Dest"));
    REQUIRE(session->journal->undo());
    CHECK(linkObject(document(), 20).action.destination.pageIndex == 2);
    REQUIRE(save(*session, "retarget-restored"));
    auto restored = readBytes(tests::readAllBytes(session->path));
    REQUIRE(restored->object(20).get("Dest").isArray());
    CHECK(restored->object(20).get("Dest").items().front() == CosObject::makeReference(restored->walk.pages[2].number, 0));
    CHECK(restored->byObject(20)->action.destination.pageIndex == 2);

    // A link MADE to a page, then that page deleted before any save: no target written, and back with the undo.
    core::Annotation made = core::newLink(0, core::PdfRect{core::PdfPoint{36, 100}, core::PdfSize{100, 14}}, {}, goTo(5),
                                          QStringLiteral("Tester"), 1791504000);
    REQUIRE(run(*session, std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{made})));
    REQUIRE(run(*session, std::make_unique<core::DeletePagesCommand>(std::vector<int>{5})));
    REQUIRE(save(*session, "made-deleted"));
    auto gone = readBytes(tests::readAllBytes(session->path));
    REQUIRE(gone->byName(made.nm) != nullptr);
    CHECK_FALSE(gone->object(gone->byName(made.nm)->id.object).has("Dest"));
    REQUIRE(session->journal->undo());
    REQUIRE(save(*session, "made-restored"));
    auto back = readBytes(tests::readAllBytes(session->path));
    CHECK(back->byName(made.nm)->action.destination.pageIndex == 5);

    // THE CONTROL: a page past the last is refused, by key, with nothing changed.
    core::Annotation past = linkObject(document(), 20);
    past.action = goTo(document().pages().count());
    const core::Status refused = run(*session, std::make_unique<core::EditAnnotationCommand>(std::vector<core::Annotation>{past}));
    REQUIRE_FALSE(refused);
}

TEST_CASE("links: a link to a page inserted before a save is written to that page", "[links][A-88]")
{
    auto session = tests::openCopy("assembly/refs-none.pdf");
    REQUIRE(session);
    const int count = session->journal->document().pages().count();
    REQUIRE(run(*session, std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{0})));
    REQUIRE(session->journal->document().pages().count() == count + 1);
    // Page 1 is the duplicate -- a NEW page, numbered by the sink until the save.
    core::Annotation link = core::newLink(3, core::PdfRect{core::PdfPoint{36, 100}, core::PdfSize{100, 14}}, {}, goTo(1),
                                          QStringLiteral("Tester"), 1791504000);
    REQUIRE(run(*session, std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{link})));
    REQUIRE(save(*session, "to-new-page"));
    auto written = readBytes(tests::readAllBytes(session->path));
    const core::Annotation* back = written->byName(link.nm);
    REQUIRE(back != nullptr);
    CHECK(back->action.destination.pageIndex == 1);
}
