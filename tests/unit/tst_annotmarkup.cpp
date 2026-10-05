// tst_annotmarkup -- Stage 6 T6 headless: what the text markup tools and the note write, below the widgets
// (`Stage6-Annotations-Testing-Guide.md` §5.8; A-27, A-28's and A-32's T6 halves; D6-4, D6-6, D6-12, D6-28).
//
//   * The APPEARANCES of underline, strikeout, squiggly, caret and note: well-formed, drawn in each
//     quadrilateral's own frame -- the same strokes whichever corner order the file wrote, and along the SIDE
//     of a vertical line -- and a note's icon that is not one of the two this build draws refused by key.
//   * A NOTE AND ITS POP-UP name each other before either has a number: saved, the note's `/Popup` is the
//     pop-up's object and the pop-up's `/Parent` the note's, the pop-up carries no `/AP`, and verify reads
//     both back. A REPLACE's strikeout names its caret (`/IRT`, `/RT /Group`) the same way.
//   * A reference whose target was deleted before the save is LEFT OUT, never written pointing at nothing;
//     a saved note edited later keeps naming the object its pop-up was saved at.
//   * Verify refuses a composed note whose `/Popup` names the wrong object (the T6 check), before any replace.
//
// WHAT IT DOES NOT COVER (convention 19): the engine, the tools and the glyphs -- whether the marks sit on
// the text is `tests/composition/tst_markupink.cpp`'s, graded against the rendered ink (A-49 … A-53); and
// whether another reader draws them as RawPage does is M-02's.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "pdf/cos/ContentStreamBuilder.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/AppearanceStreamBuilder.h"
#include "pdf/ops/RevisionWriter.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace rawpage;
using core::AddAnnotationCommand;
using core::Annotation;
using core::AnnotationId;
using core::AnnotationSubtype;
using pdf::cos::CosObject;
using pdf::ops::AppearanceStreamBuilder;

namespace {

constexpr std::int64_t kNow = 1791590400;   // 2026-10-10T00:00:00Z, a fixed instant
const QString kAuthor = QStringLiteral("Mara Reviewer");

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

std::string_view viewOf(const QByteArray& bytes)
{
    return std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

Annotation markup(AnnotationSubtype subtype, std::vector<double> quads)
{
    Annotation a = core::newAnnotation(subtype, 0, kAuthor, kNow);
    a.quadPoints = std::move(quads);
    a.rect = *core::markupRect(a.quadPoints);
    a.color = {0.0, 0.0, 1.0};
    return a;
}

// The stream's path points, in order: every `x y m` and `x y l`, read back from the bytes.
struct PathPoint {
    double x = 0.0, y = 0.0;
    char op = 'l';
};

std::vector<PathPoint> pathOf(const std::string& stream)
{
    std::vector<PathPoint> out;
    std::istringstream in(stream);
    std::vector<std::string> tokens;
    std::string token;
    while (in >> token) tokens.push_back(token);
    for (std::size_t i = 2; i < tokens.size(); ++i) {
        if (tokens[i] != "m" && tokens[i] != "l") continue;
        out.push_back(PathPoint{std::stod(tokens[i - 2]), std::stod(tokens[i - 1]), tokens[i][0]});
    }
    return out;
}

// A file read back from its bytes with the first-party reader.
struct ReadBack {
    QByteArray bytes;
    std::optional<pdf::cos::XrefTable> table;
    pdf::ops::PageWalk walk;
    core::AnnotationStore store;

    explicit ReadBack(QByteArray fileBytes) : bytes(std::move(fileBytes))
    {
        auto opened = pdf::cos::openWithRepair(viewOf(bytes));
        REQUIRE(opened);
        table.emplace(std::move(opened.value().table));
        auto walked = pdf::ops::PageOps::walk(*table);
        REQUIRE(walked);
        walk = std::move(walked).value();
        store = core::AnnotationStore::fromReading(pdf::ops::AnnotationReader::read(*table, walk));
    }
    CosObject object(long long number)
    {
        auto fetched = table->fetch(number);
        REQUIRE(fetched);
        return fetched.value().object;
    }
    const Annotation* withNm(const QString& nm) const
    {
        for (const Annotation& a : store.all())
            if (a.nm == nm) return &a;
        return nullptr;
    }
};

QByteArray save(tests::SaveSession& session, const QString& keepAs)
{
    auto result = session.coordinator->save(*session.journal, session.path);
    INFO("save error: " << (result ? std::string() : keyOf(result.error()) + " " + result.error().context.toStdString()));
    REQUIRE(result);
    REQUIRE(result.value().kind == core::SaveOutcome::Kind::Saved);
    // Kept for `qpdf --check` (A-31), as every file the headless suite saves is.
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);
    REQUIRE(QDir().mkpath(dir));
    const QString target = QDir(dir).filePath(keepAs);
    QFile::remove(target);
    REQUIRE(QFile::copy(session.path, target));
    return tests::readAllBytes(session.path);
}

// A note at (x, y) -- its icon's top-left corner -- and its pop-up, as `AnnotationOverlay::addNote` makes them.
std::vector<Annotation> noteAndPopup(double x, double y, const QString& text)
{
    Annotation note = core::newAnnotation(AnnotationSubtype::Text, 0, kAuthor, kNow);
    note.rect = core::PdfRect{core::PdfPoint{x, y - 20}, core::PdfSize{20, 20}};
    note.icon = QStringLiteral("Comment");
    note.contents = text;
    note.color = {1.0, 1.0, 0.0};
    note.flags = core::AnnotationFlag::Print | core::AnnotationFlag::NoZoom | core::AnnotationFlag::NoRotate;
    Annotation popup = core::newAnnotation(AnnotationSubtype::Popup, 0, QString(), kNow);
    popup.rect = core::PdfRect{core::PdfPoint{x + 24, y - 100}, core::PdfSize{200, 100}};
    popup.created = core::PdfDate{};
    popup.modified = core::PdfDate{};
    popup.flags = note.flags;
    popup.open = false;
    popup.hasAppearance = false;
    popup.parent = note.id;
    note.popup = popup.id;
    return {note, popup};
}

} // namespace

// ==========================================================================================
// The appearances (D6-6): drawn in the quadrilateral's own frame
// ==========================================================================================

TEST_CASE("text lines are drawn the same whichever corner order the file wrote", "[annotmarkup][appearance]")
{
    // A 14-point-tall line box from (100, 700) to (300, 714).
    const std::vector<double> acrobat{100, 714, 300, 714, 100, 700, 300, 700};   // TL TR BL BR
    const std::vector<double> iso{100, 700, 300, 700, 300, 714, 100, 714};       // BL BR TR TL
    for (const AnnotationSubtype subtype : {AnnotationSubtype::Underline, AnnotationSubtype::StrikeOut,
                                            AnnotationSubtype::Squiggly}) {
        INFO(core::annotationSubtypeName(subtype).data());
        auto a = AppearanceStreamBuilder::normal(markup(subtype, acrobat));
        auto b = AppearanceStreamBuilder::normal(markup(subtype, iso));
        REQUIRE(a);
        REQUIRE(b);
        CHECK(a.value().data == b.value().data);
        const std::vector<PathPoint> path = pathOf(a.value().data);
        REQUIRE(path.size() >= 2);
        // Along the line, from its left edge to its right.
        CHECK(path.front().x == 100.0);
        CHECK(std::abs(path.back().x - 300.0) < 1e-9);
        double low = 1e9, high = -1e9;
        for (const PathPoint& p : path) {
            low = std::min(low, p.y);
            high = std::max(high, p.y);
        }
        switch (subtype) {
        case AnnotationSubtype::Underline:
            CHECK(low == 701.0);   // one stroke's width above the foot, as PDFium's generation draws it
            CHECK(high == 701.0);
            break;
        case AnnotationSubtype::StrikeOut:
            CHECK(std::abs(low - (700.0 + 0.42 * 14.0)) < 1e-4);   // D6-32: through the x-height
            CHECK(low == high);
            break;
        default:   // the squiggle: between 7 % and 16 % of the height above the foot (D6-32)
            CHECK(low >= 700.0 + 0.07 * 14.0 - 1e-4);
            CHECK(high <= 700.0 + 0.16 * 14.0 + 1e-4);
            CHECK(high > low);
            break;
        }
    }
}

TEST_CASE("a vertical line is underlined along its side", "[annotmarkup][appearance]")
{
    // A column of vertical text from x 400 to 414, read downwards from y 700 to 600: its "top" is its right
    // side, so `TextSelection::markupQuadsOn` writes it (r,t) (r,b) (l,t) (l,b).
    const std::vector<double> quad{414, 700, 414, 600, 400, 700, 400, 600};
    auto built = AppearanceStreamBuilder::normal(markup(AnnotationSubtype::Underline, quad));
    REQUIRE(built);
    const std::vector<PathPoint> path = pathOf(built.value().data);
    REQUIRE(path.size() == 2);
    CHECK(path[0].x == 401.0);   // one stroke from the LEFT side, its foot in vertical writing
    CHECK(path[1].x == 401.0);
    CHECK(path[0].y == 700.0);
    CHECK(path[1].y == 600.0);
}

TEST_CASE("a caret and the two note icons are drawn and any other icon is refused by key", "[annotmarkup][appearance]")
{
    Annotation caret = core::newAnnotation(AnnotationSubtype::Caret, 0, kAuthor, kNow);
    caret.rect = core::PdfRect{core::PdfPoint{100, 700}, core::PdfSize{6, 8}};
    CHECK(AppearanceStreamBuilder::normal(caret));

    std::vector<Annotation> made = noteAndPopup(100, 700, QStringLiteral("x"));
    Annotation note = made[0];
    CHECK(AppearanceStreamBuilder::normal(note));
    note.icon.clear();   // `Note`, ISO's default
    CHECK(AppearanceStreamBuilder::normal(note));
    note.icon = QStringLiteral("Key");
    auto refused = AppearanceStreamBuilder::normal(note);
    REQUIRE_FALSE(refused);
    CHECK(keyOf(refused.error()) == "error.pdf.appearanceUnsupported");

    // A pop-up has no appearance at all.
    CHECK(AppearanceStreamBuilder::drawsNothing(AnnotationSubtype::Popup));
    CHECK_FALSE(AppearanceStreamBuilder::drawsNothing(AnnotationSubtype::Text));
    CHECK_FALSE(AppearanceStreamBuilder::normal(made[1]));
}

TEST_CASE("markup's rectangle holds its strokes and the author falls back to the login", "[annotmarkup][identity]")
{
    const std::vector<double> quad{100, 714, 300, 714, 100, 700, 300, 700};
    const std::optional<core::PdfRect> rect = core::markupRect(quad);
    REQUIRE(rect);
    CHECK(std::abs(rect->left() - (100.0 - 2.1)) < 1e-9);   // 15 % of the 14-point height: past the 2-point floor
    CHECK(std::abs(rect->bottom() - (700.0 - 2.1)) < 1e-9);
    CHECK_FALSE(core::markupRect({1, 2, 3}));

    // D6-13: the name set, else the login name the host supplied -- white space is not a name.
    CHECK(core::identityAuthor(QStringLiteral("Ada"), QStringLiteral("login")) == QStringLiteral("Ada"));
    CHECK(core::identityAuthor(QStringLiteral("   "), QStringLiteral("login")) == QStringLiteral("login"));
    CHECK(core::identityAuthor(QString(), QString()).isEmpty());
}

// ==========================================================================================
// References between created annotations (D6-4, D6-12, D6-28)
// ==========================================================================================

TEST_CASE("a note and its pop-up are saved naming each other and the pop-up has no appearance", "[annotmarkup][A-27][A-32]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const std::vector<Annotation> made = noteAndPopup(300, 600, QStringLiteral("A note made in T6"));
    REQUIRE(session->journal->run(std::make_unique<AddAnnotationCommand>(made)));
    REQUIRE(session->journal->position() == 1);   // one command, one step
    ReadBack file(save(*session, QStringLiteral("S6T6-note-popup.pdf")));

    const Annotation* note = file.withNm(made[0].nm);
    const Annotation* popup = file.withNm(made[1].nm);
    REQUIRE(note != nullptr);
    REQUIRE(popup != nullptr);
    REQUIRE(note->popup);
    REQUIRE(popup->parent);
    CHECK(*note->popup == popup->id);    // the object the pop-up was written at
    CHECK(*popup->parent == note->id);
    CHECK(note->hasAppearance);
    CHECK_FALSE(popup->hasAppearance);
    CHECK(note->icon == QStringLiteral("Comment"));
    CHECK(note->author == kAuthor);
    CHECK(note->contents == QStringLiteral("A note made in T6"));
    CHECK(note->flags == 28u);
    CHECK(popup->open == std::optional<bool>(false));
    // The pop-up is no comment of its own.
    CHECK(file.store.isTopLevel(*file.store.find(note->id)));
    CHECK_FALSE(file.store.isTopLevel(*file.store.find(popup->id)));

    // And undone before a save, the pair writes nothing.
    auto again = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(again);
    REQUIRE(again->journal->run(std::make_unique<AddAnnotationCommand>(noteAndPopup(300, 600, QStringLiteral("x")))));
    REQUIRE(again->journal->undo());
    auto delta = again->sink()->delta();
    REQUIRE(delta);
    CHECK(delta.value().isEmpty());
}

TEST_CASE("a replace's strikeout is saved in its caret's group", "[annotmarkup][A-51]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    Annotation caret = core::newAnnotation(AnnotationSubtype::Caret, 0, kAuthor, kNow);
    caret.rect = core::PdfRect{core::PdfPoint{297, 700}, core::PdfSize{6, 8}};
    caret.contents = QStringLiteral("replacement");
    Annotation strike = markup(AnnotationSubtype::StrikeOut, {100, 714, 300, 714, 100, 700, 300, 700});
    strike.inReplyTo = caret.id;
    strike.replyType = core::ReplyType::Group;
    REQUIRE(session->journal->run(std::make_unique<AddAnnotationCommand>(std::vector<Annotation>{caret, strike})));
    ReadBack file(save(*session, QStringLiteral("S6T6-replace-group.pdf")));
    const Annotation* c = file.withNm(caret.nm);
    const Annotation* s = file.withNm(strike.nm);
    REQUIRE(c != nullptr);
    REQUIRE(s != nullptr);
    CHECK(s->inReplyTo == std::optional<AnnotationId>(c->id));
    CHECK(s->replyType == core::ReplyType::Group);
    const std::size_t caretIndex = *file.store.find(c->id);
    CHECK(file.store.groupMembers(caretIndex) == std::vector<std::size_t>{*file.store.find(s->id)});
    CHECK(file.store.isTopLevel(caretIndex));
}

TEST_CASE("a reference to a created annotation deleted before the save is left out", "[annotmarkup][D6-4]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const std::vector<Annotation> made = noteAndPopup(300, 600, QStringLiteral("orphaned"));
    REQUIRE(session->journal->run(std::make_unique<AddAnnotationCommand>(made)));
    REQUIRE(session->journal->run(std::make_unique<core::DeleteAnnotationCommand>(std::vector<AnnotationId>{made[1].id})));
    ReadBack file(save(*session, QStringLiteral("S6T6-popup-deleted.pdf")));
    const Annotation* note = file.withNm(made[0].nm);
    REQUIRE(note != nullptr);
    CHECK_FALSE(note->popup.has_value());   // nothing written, so nothing named
    CHECK(file.withNm(made[1].nm) == nullptr);
}

TEST_CASE("a saved note moved later still names the object its pop-up was saved at", "[annotmarkup][D6-4]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const std::vector<Annotation> made = noteAndPopup(300, 600, QStringLiteral("moved after a save"));
    REQUIRE(session->journal->run(std::make_unique<AddAnnotationCommand>(made)));
    ReadBack first(save(*session, QStringLiteral("S6T6-note-first.pdf")));
    const Annotation* popupThen = first.withNm(made[1].nm);
    REQUIRE(popupThen != nullptr);

    REQUIRE(session->journal->run(std::make_unique<core::MoveAnnotationCommand>(std::vector<AnnotationId>{made[0].id},
                                                                                  30.0, -10.0, kNow + 60)));
    ReadBack second(save(*session, QStringLiteral("S6T6-note-moved.pdf")));
    const Annotation* note = second.withNm(made[0].nm);
    REQUIRE(note != nullptr);
    CHECK(std::abs(note->rect.left() - 330.0) < 1e-6);
    REQUIRE(note->popup);
    CHECK(*note->popup == popupThen->id);
}

TEST_CASE("verify refuses a note whose pop-up reference names the wrong object", "[annotmarkup][A-30]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const std::vector<Annotation> made = noteAndPopup(300, 600, QStringLiteral("faulted"));
    REQUIRE(session->journal->run(std::make_unique<AddAnnotationCommand>(made)));
    const std::size_t original = static_cast<std::size_t>(session->openedBytes.size());

    // THE FAULT: the note's `/Popup N 0 R` turned to the note itself -- same digits, so the update still
    // parses and every other check passes. Only the T6 reference check can see it.
    bool applied = false;
    std::string seen;
    pdf::ops::RevisionWriter::setComposeFaultForTesting([original, &applied, &seen](QByteArray& bytes) {
        seen = bytes.mid(static_cast<qsizetype>(original)).toStdString();
        // The note's `/Popup` KEY -- not the pop-up's `/Subtype /Popup` -- and the POP-UP's `/Parent`, not the
        // page's. The writer may wrap a line between a key and its value.
        qsizetype popup = static_cast<qsizetype>(original);
        while ((popup = bytes.indexOf("/Popup", popup + 1)) >= 0 && bytes.at(popup + 6) == '/') {}
        const qsizetype popupObject = bytes.indexOf("/Subtype /Popup", static_cast<qsizetype>(original));
        const qsizetype parent = popupObject < 0 ? -1 : bytes.indexOf("/Parent ", popupObject);
        if (popup < 0 || parent < 0) return;
        // `/Parent` names the note: copy its number over `/Popup`'s.
        const qsizetype from = parent + 8, to = popup + 7;
        const qsizetype lengthFrom = bytes.indexOf(' ', from) - from;
        const qsizetype lengthTo = bytes.indexOf(' ', to) - to;
        if (lengthFrom != lengthTo || bytes.mid(from, lengthFrom) == bytes.mid(to, lengthTo)) return;
        bytes.replace(to, lengthTo, bytes.mid(from, lengthFrom));
        applied = true;
    });
    auto result = session->coordinator->save(*session->journal, session->path);
    pdf::ops::RevisionWriter::setComposeFaultForTesting({});
    INFO(seen);
    REQUIRE(applied);   // the fault met what it changes (convention 7)
    REQUIRE_FALSE(result);
    CHECK(keyOf(result.error()) == "error.save.verifyFailed");
    CHECK(session->durableFiles.replaceCalls == 0);
    CHECK(tests::sha256(tests::readAllBytes(session->path)) == tests::sha256(session->openedBytes));

    // THE CONTROL: unfaulted, the same save is written.
    (void)save(*session, QStringLiteral("S6T6-note-control.pdf"));
}

TEST_CASE("a serialized annotation's reference is an object or a created name and nothing else", "[annotmarkup][A-29]")
{
    std::vector<Annotation> made = noteAndPopup(300, 600, QStringLiteral("round trip"));
    const QJsonObject json = AddAnnotationCommand(made).serialize();
    auto back = AddAnnotationCommand::deserialize(json);
    REQUIRE(back);
    const auto& round = static_cast<const AddAnnotationCommand&>(*back.value()).annotations();
    CHECK(round == made);

    // A DIRECT identity cannot be pointed at; a 10 MB name is hostile.
    for (const QString& bad : {QStringLiteral("direct:0:3"), QStringLiteral("nm:") + QString(10'000'000, QLatin1Char('a'))}) {
        QJsonObject hostile = json;
        QJsonArray list = hostile.value(QStringLiteral("annotations")).toArray();
        QJsonObject first = list.at(0).toObject();
        first.insert(QStringLiteral("popup"), bad);
        list.replace(0, first);
        hostile.insert(QStringLiteral("annotations"), list);
        auto refused = AddAnnotationCommand::deserialize(hostile);
        REQUIRE_FALSE(refused);
        CHECK(keyOf(refused.error()) == "error.command.malformed");
    }
}
