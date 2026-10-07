// tst_annotfreetext -- Stage 6 T7 headless: what the typed tools write, below the widgets
// (`Stage6-Annotations-Testing-Guide.md` §5.9; A-55, A-56's file half, A-57; A-27's and A-32's T7 halves;
// D6-7, D6-8, D6-34).
//
//   * A-55: a box, a callout and a typewriter -- made as the overlay makes them (`core::fittedFreeText` over a
//     `core::newAnnotation`), saved, read back: `FreeText` with no `/IT`, `/IT /FreeTextCallout` with `/CL`
//     and an arrow, `/IT /FreeTextTypeWriter` with no border -- and NO content stream changed (Q2): every
//     page's `/Contents` names the same objects, and each reads the same bytes. An edit of the text is ONE
//     `EditAnnotationCommand`, redrawn.
//   * A-56, the file half: each font the appearance names is the one D6-7 names -- Helvetica in WinAnsi, a
//     CJK collection as a Type0 over its UCS-2 CMap -- and NONE is embedded; `/DA` names a font the
//     appearance's resources hold and its text uses. A refused character reaches no file: the command is
//     refused by key, and the update is empty.
//   * A-57: the appearance DECODED first-party -- the content stream tokenized by `pdf/cos/Lexer`, each `Tj`
//     read in the encoding its `Tf` font names, with a WinAnsi table written HERE rather than the product's --
//     is the text typed, character for character, its line breaks aside (a line break is where a line
//     starts, not a character drawn).
//
// WHAT IT DOES NOT COVER (convention 19): the engine. Whether PDFium draws these characters at all is
// `tests/render/tst_typedtext.cpp`'s; whether the box sits where it was typed and what the screen shows is what
// the file says is `tests/composition/tst_typedink.cpp`'s; whether Acrobat and Firefox agree is M-06's.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/TypedText.h"
#include "pdf/cos/ContentStreamBuilder.h"
#include "pdf/cos/Lexer.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/AppearanceStreamBuilder.h"
#include "pdf/ops/EmbeddedFileReader.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using core::AddAnnotationCommand;
using core::Annotation;
using core::AnnotationSubtype;
using core::FreeTextKind;
using core::TypedFont;
using pdf::cos::CosObject;

namespace {

constexpr std::int64_t kNow = 1791676800;   // 2026-10-11T00:00:00Z, a fixed instant
const QString kAuthor = QStringLiteral("Typed Tester");

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

std::string_view viewOf(const QByteArray& bytes)
{
    return std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

// A typed comment as `AnnotationOverlay::addFreeText` makes one: its frame's top-left at `topLeft`, fitted.
Annotation typed(FreeTextKind kind, const QString& text, core::PdfPoint topLeft, TypedFont han = TypedFont::Simplified,
                 int rotate = 0, int page = 0)
{
    Annotation a = core::newAnnotation(AnnotationSubtype::FreeText, page, kAuthor, kNow);
    a.intent = QString(core::freeTextIntent(kind));
    a.rotate = rotate;
    a.contents = text;
    a.color = {1.0, 0.0, 0.0};
    a.borderWidth = kind == FreeTextKind::Typewriter ? 0.0 : 1.0;
    a.defaultAppearance = core::freeTextDefaultAppearance(TypedFont::Helvetica, 12.0, a.color);
    const double inset = *a.borderWidth + core::kFreeTextPadding;
    a.rect = core::freeTextBoxAt(topLeft, kind == FreeTextKind::Typewriter ? 20.0 : 180.0, 12 * 1.2 + 2 * inset, rotate);
    if (kind == FreeTextKind::Callout) {
        a.lineEndings = {QStringLiteral("OpenArrow")};
        a.callout = core::calloutLeader(core::PdfPoint{topLeft.x - 60, topLeft.y - 60}, a.rect);
    }
    core::Result<Annotation> fitted = core::fittedFreeText(a, han);
    REQUIRE(fitted);
    return std::move(fitted).value();
}

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
    CosObject resolved(const CosObject& value)
    {
        return value.isReference() ? object(value.reference().number) : value;
    }
    const Annotation* withNm(const QString& nm) const
    {
        for (const Annotation& a : store.all())
            if (a.nm == nm) return &a;
        return nullptr;
    }
    // The dictionary of the annotation with `/NM` `nm`, through the page listing that names it.
    CosObject dictionaryOf(const QString& nm)
    {
        const Annotation* a = withNm(nm);
        REQUIRE(a != nullptr);
        REQUIRE(a->id.kind == core::AnnotationId::Kind::Object);
        return object(a->id.object);
    }
    // The bytes every page's /Contents reads, page by page, each stream's own bytes as stored.
    std::vector<std::string> contentStreams()
    {
        std::vector<std::string> out;
        for (const pdf::ops::PageEntry& page : walk.pages) {
            const CosObject dictionary = object(page.number);
            const CosObject* contents = dictionary.find("Contents");
            std::string bytesOfPage = contents == nullptr ? std::string("<none>") : std::string();
            std::vector<CosObject> parts;
            if (contents != nullptr) {
                const CosObject value = resolved(*contents);
                if (value.isArray()) parts = value.items();
                else parts.push_back(*contents);
            }
            for (const CosObject& part : parts) {
                if (part.isReference()) bytesOfPage += "ref " + std::to_string(part.reference().number) + ":";
                const CosObject stream = resolved(part);
                if (stream.isStream()) bytesOfPage += std::string(stream.rawStreamData(table->source()));
            }
            out.push_back(std::move(bytesOfPage));
        }
        return out;
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

// ---- The first-party decoder (A-57) --------------------------------------------------------------------
// WinAnsiEncoding's codes 128 ... 159, from ISO 32000-1 Annex D -- WRITTEN HERE, not taken from the product,
// so a code the product maps wrong is read back as another character rather than agreed with.
constexpr char16_t kAnnexD[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178,
};

struct Decoded {
    QString text;                       // every Tj, in order, decoded by its font's encoding
    std::vector<std::string> fontsUsed; // every resource a Tf named, in order, once each
    QString failure;                    // why it could not be decoded, if it could not
};

Decoded decodeAppearance(ReadBack& file, const CosObject& form)
{
    Decoded out;
    const CosObject resources = file.resolved(form.get("Resources"));
    const CosObject fonts = file.resolved(resources.get("Font"));
    const std::string stream(form.rawStreamData(file.table->source()));
    pdf::cos::Lexer lexer(stream);
    std::string lastName;
    std::string encoding;
    std::vector<pdf::cos::Token> operands;
    for (pdf::cos::Token token = lexer.next(); !token.isEnd(); token = lexer.next()) {
        if (token.type != pdf::cos::TokenType::Keyword) {
            operands.push_back(token);
            continue;
        }
        if (token.text == "Tf" && operands.size() >= 2 && operands[operands.size() - 2].type == pdf::cos::TokenType::Name) {
            const std::string name = operands[operands.size() - 2].text;
            const CosObject* font = fonts.find(name);
            if (font == nullptr) {
                out.failure = QStringLiteral("Tf names %1, which the resources do not hold").arg(QString::fromStdString(name));
                return out;
            }
            const CosObject dictionary = file.resolved(*font);
            encoding = dictionary.get("Encoding").isName() ? dictionary.get("Encoding").text() : std::string();
            if (std::find(out.fontsUsed.begin(), out.fontsUsed.end(), name) == out.fontsUsed.end())
                out.fontsUsed.push_back(name);
        } else if (token.text == "Tj" && !operands.empty()) {
            const std::string& codes = operands.back().text;
            if (encoding == "WinAnsiEncoding") {
                for (const char c : codes) {
                    const auto b = static_cast<unsigned char>(c);
                    char16_t u = b;
                    if (b >= 0x80 && b < 0xA0) u = kAnnexD[b - 0x80];
                    if (u == 0 || b < 0x20) {
                        out.failure = QStringLiteral("code %1 has no WinAnsi character").arg(b);
                        return out;
                    }
                    out.text += QChar(u);
                }
            } else if (encoding.size() > 7 && encoding.compare(encoding.size() - 7, 7, "-UCS2-H") == 0) {
                if (codes.size() % 2 != 0) {
                    out.failure = QStringLiteral("an odd number of bytes for a two-byte CMap");
                    return out;
                }
                for (std::size_t i = 0; i < codes.size(); i += 2)
                    out.text += QChar(static_cast<char16_t>((static_cast<unsigned char>(codes[i]) << 8)
                                                            | static_cast<unsigned char>(codes[i + 1])));
            } else {
                out.failure = QStringLiteral("text in an encoding this decoder does not read: %1").arg(QString::fromStdString(encoding));
                return out;
            }
        }
        operands.clear();
    }
    return out;
}

QString withoutBreaks(QString text)
{
    text.remove(QLatin1Char('\r'));
    text.remove(QLatin1Char('\n'));
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    return text;
}

} // namespace

// ==========================================================================================
// A-55: the three kinds, made, saved, read back; no content stream changes
// ==========================================================================================

TEST_CASE("a box a callout and a typewriter are saved as their intents say and no content stream changes", "[annotfreetext][A-55][A-27][A-32]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    ReadBack original(session->openedBytes);
    const std::vector<std::string> before = original.contentStreams();

    const Annotation box = typed(FreeTextKind::Box, QStringLiteral("A text box, wrapped to its width by the layout."),
                                 core::PdfPoint{300, 780});
    const Annotation callout = typed(FreeTextKind::Callout, QStringLiteral("A callout"), core::PdfPoint{380, 640});
    const Annotation typewriter = typed(FreeTextKind::Typewriter, QStringLiteral("Typed in the margin"), core::PdfPoint{40, 60});
    REQUIRE(session->journal->run(std::make_unique<AddAnnotationCommand>(std::vector<Annotation>{box})));
    REQUIRE(session->journal->run(std::make_unique<AddAnnotationCommand>(std::vector<Annotation>{callout})));
    REQUIRE(session->journal->run(std::make_unique<AddAnnotationCommand>(std::vector<Annotation>{typewriter})));
    REQUIRE(session->journal->position() == 3);   // one gesture, one command, each
    ReadBack file(save(*session, QStringLiteral("S6T7-freetext-three.pdf")));

    const Annotation* b = file.withNm(box.nm);
    const Annotation* c = file.withNm(callout.nm);
    const Annotation* t = file.withNm(typewriter.nm);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    REQUIRE(t != nullptr);
    for (const Annotation* a : {b, c, t}) {
        CHECK(a->subtype == AnnotationSubtype::FreeText);
        CHECK(a->hasAppearance);
        CHECK(a->author == kAuthor);   // A-27: the identity keys
        CHECK(a->created.raw == core::PdfDate::fromUtcSeconds(kNow).raw);
        CHECK(core::DefaultAppearance::parse(a->defaultAppearance));
    }
    CHECK(b->intent.isEmpty());
    CHECK(b->contents == box.contents);
    CHECK(c->intent == QStringLiteral("FreeTextCallout"));
    CHECK(c->callout.size() == 6);
    CHECK(c->lineEndings == std::vector<QString>{QStringLiteral("OpenArrow")});
    CHECK(c->rectDifferences.size() == 4);
    CHECK(t->intent == QStringLiteral("FreeTextTypeWriter"));
    CHECK(t->borderWidth == std::optional<double>(0.0));
    // A-32: everything modelled reads back as made (within a written real's four decimals).
    for (const auto& [made, read] : {std::pair{&box, b}, std::pair{&callout, c}, std::pair{&typewriter, t}}) {
        CHECK(std::abs(made->rect.left() - read->rect.left()) < 1e-3);
        CHECK(std::abs(made->rect.top() - read->rect.top()) < 1e-3);
        CHECK(read->defaultAppearance == made->defaultAppearance);
    }

    // Q2: NO CONTENT STREAM CHANGED -- every page reads the same objects and the same bytes.
    const std::vector<std::string> after = file.contentStreams();
    REQUIRE(after.size() == before.size());
    for (std::size_t p = 0; p < after.size(); ++p) {
        INFO("page " << p + 1);
        CHECK(tests::sha256(QByteArray::fromStdString(after[p])) == tests::sha256(QByteArray::fromStdString(before[p])));
    }
}

TEST_CASE("free text edited in place is one command redrawn and keeps what it does not model", "[annotfreetext][A-55][A-26]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const Annotation box = typed(FreeTextKind::Box, QStringLiteral("First words"), core::PdfPoint{300, 780});
    REQUIRE(session->journal->run(std::make_unique<AddAnnotationCommand>(std::vector<Annotation>{box})));
    (void)save(*session, QStringLiteral("S6T7-freetext-before-edit.pdf"));

    // The overlay's edit: the text changed, refitted, ONE EditAnnotationCommand.
    const std::optional<std::size_t> index = session->journal->document().annotations().find(box.id);
    REQUIRE(index);
    Annotation edited = session->journal->document().annotations().at(*index);
    edited.contents = QStringLiteral("Second words, now long enough to wrap onto another line of the box");
    edited.modified = core::PdfDate::fromUtcSeconds(kNow + 60);
    core::Result<Annotation> fitted = core::fittedFreeText(edited, TypedFont::Simplified);
    REQUIRE(fitted);
    const std::size_t at = session->journal->position();
    REQUIRE(session->journal->run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{fitted.value()})));
    CHECK(session->journal->position() == at + 1);
    ReadBack file(save(*session, QStringLiteral("S6T7-freetext-edited.pdf")));
    const Annotation* read = file.withNm(box.nm);
    REQUIRE(read != nullptr);
    CHECK(read->contents == fitted.value().contents);
    CHECK(std::abs(read->rect.size.height - fitted.value().rect.size.height) < 1e-3);
    CHECK(read->rect.size.height > box.rect.size.height);   // it grew to its lines
    // The appearance redrawn says the new text (A-57's decoder, here as a check on the edit).
    const CosObject dictionary = file.dictionaryOf(box.nm);
    const CosObject form = file.resolved(dictionary.get("AP").get("N"));
    REQUIRE(form.isStream());
    const Decoded decoded = decodeAppearance(file, form);
    INFO(decoded.failure.toStdString());
    CHECK(decoded.failure.isEmpty());
    CHECK(decoded.text == withoutBreaks(fitted.value().contents));

    // A FREE TEXT FROM THE FILE -- `annots-every-type.pdf`'s box, `/DA` `/Helv` and a `/DS` the model does not
    // hold -- given new text: redrawn by our builder, its `/DS` kept (D6-5).
    auto corpus = tests::openCopy("ordinary/annots-every-type.pdf");
    REQUIRE(corpus);
    const core::AnnotationStore& store = corpus->journal->document().annotations();
    std::optional<Annotation> producer;
    for (const Annotation& a : store.all()) {
        if (a.subtype == AnnotationSubtype::FreeText && a.intent.isEmpty()) {
            producer = a;
            break;
        }
    }
    REQUIRE(producer);
    Annotation renamed = *producer;
    renamed.contents = QStringLiteral("Edited in RawPage");
    renamed.modified = core::PdfDate::fromUtcSeconds(kNow);
    REQUIRE(corpus->journal->run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{renamed})));
    ReadBack edited2(save(*corpus, QStringLiteral("S6T7-freetext-producer-edited.pdf")));
    REQUIRE(producer->id.kind == core::AnnotationId::Kind::Object);
    const CosObject was = ReadBack(corpus->openedBytes).object(producer->id.object);
    const CosObject now = edited2.object(producer->id.object);
    CHECK(now.get("DS") == was.get("DS"));
    CHECK_FALSE(now.get("AP").get("N") == was.get("AP").get("N"));   // a new appearance
    const Decoded redrawn = decodeAppearance(edited2, edited2.resolved(now.get("AP").get("N")));
    CHECK(redrawn.failure.isEmpty());
    CHECK(redrawn.text == QStringLiteral("Edited in RawPage"));
}

// ==========================================================================================
// A-56 and A-57: the faces, none embedded, and the appearance decoded
// ==========================================================================================

TEST_CASE("the appearance says what was typed in the faces D6-7 names and none is embedded", "[annotfreetext][A-56][A-57]")
{
    struct Typed {
        QString text;
        TypedFont han;
        const char* primary;   // the resource `/DA` must name
    };
    const std::vector<Typed> cases{
        {QStringLiteral("café € “quoted”"), TypedFont::Simplified, "Helv"},
        {QStringLiteral("中文 繁體 日本語 한국어"), TypedFont::Traditional, "MSung"},
        {QStringLiteral("简体中文"), TypedFont::Simplified, "STSong"},
        {QStringLiteral("ひらがなと漢字\nand English"), TypedFont::Traditional, "KozMin"},
        {QStringLiteral("한국어 문장"), TypedFont::Simplified, "HYSMyeongJo"},
    };
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    std::vector<Annotation> made;
    double y = 800;
    for (const Typed& t : cases) {
        made.push_back(typed(FreeTextKind::Box, t.text, core::PdfPoint{300, y}, t.han));
        y -= 80;
    }
    REQUIRE(session->journal->run(std::make_unique<AddAnnotationCommand>(made)));
    ReadBack file(save(*session, QStringLiteral("S6T7-freetext-scripts.pdf")));

    for (std::size_t i = 0; i < cases.size(); ++i) {
        INFO("case " << i << ": " << cases[i].text.toStdString());
        const CosObject dictionary = file.dictionaryOf(made[i].nm);
        const CosObject form = file.resolved(dictionary.get("AP").get("N"));
        REQUIRE(form.isStream());
        const Decoded decoded = decodeAppearance(file, form);
        INFO(decoded.failure.toStdString());
        REQUIRE(decoded.failure.isEmpty());
        // A-57: character for character, its line breaks aside.
        CHECK(decoded.text == withoutBreaks(cases[i].text));

        // /DA names the primary face, the resources hold it, and the text is set in it.
        const auto da = core::DefaultAppearance::parse(file.withNm(made[i].nm)->defaultAppearance);
        REQUIRE(da);
        CHECK(da->font.toStdString() == cases[i].primary);
        CHECK(std::find(decoded.fontsUsed.begin(), decoded.fontsUsed.end(), da->font.toStdString()) != decoded.fontsUsed.end());

        // Every font as D6-7 names it, and NOT EMBEDDED.
        const CosObject fonts = file.resolved(file.resolved(form.get("Resources")).get("Font"));
        for (const CosObject::Entry& entry : fonts.entries()) {
            const CosObject font = file.resolved(entry.value);
            const std::optional<TypedFont> face = core::typedFontByResource(QString::fromStdString(entry.key));
            REQUIRE(face);
            const core::TypedFontSpec& spec = core::typedFontSpec(*face);
            CHECK(font.get("BaseFont").text() == spec.baseFont);
            CHECK(font.get("Encoding").text() == spec.encoding);
            if (*face == TypedFont::Helvetica) {
                CHECK(font.get("Subtype").text() == "Type1");
                CHECK_FALSE(font.has("FontDescriptor"));
                CHECK(font.get("Widths").items().size() == 224u);
            } else {
                CHECK(font.get("Subtype").text() == "Type0");
                const CosObject cid = file.resolved(font.get("DescendantFonts").items().front());
                CHECK(cid.get("Subtype").text() == "CIDFontType0");
                CHECK(file.resolved(cid.get("CIDSystemInfo")).get("Ordering").text() == spec.ordering);
                CHECK(cid.get("FontDescriptor").isReference());   // ISO 32000-1 Table 117: indirect
                const CosObject descriptor = file.resolved(cid.get("FontDescriptor"));
                for (const char* embedded : {"FontFile", "FontFile2", "FontFile3"}) CHECK_FALSE(descriptor.has(embedded));
            }
        }
    }
}

TEST_CASE("a character no face can show reaches no file and is refused by name", "[annotfreetext][A-56]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    // Made directly -- not through the fitting, which would refuse it first -- so the SINK is what refuses.
    Annotation greek = core::newAnnotation(AnnotationSubtype::FreeText, 0, kAuthor, kNow);
    greek.rect = core::PdfRect{core::PdfPoint{100, 600}, core::PdfSize{200, 40}};
    greek.defaultAppearance = QStringLiteral("/Helv 12 Tf 0 g");
    greek.contents = QStringLiteral("Ελληνικά");
    const core::Status refused = session->journal->run(std::make_unique<AddAnnotationCommand>(std::vector<Annotation>{greek}));
    REQUIRE_FALSE(refused);
    CHECK(session->journal->position() == 0);
    auto delta = session->sink()->delta();
    REQUIRE(delta);
    CHECK(delta.value().isEmpty());

    // And an edit to a box that holds it: refused by key, the character named, nothing changed.
    const Annotation box = typed(FreeTextKind::Box, QStringLiteral("Latin"), core::PdfPoint{300, 700});
    REQUIRE(session->journal->run(std::make_unique<AddAnnotationCommand>(std::vector<Annotation>{box})));
    Annotation hebrew = box;
    hebrew.contents = QStringLiteral("Latin שלום");
    REQUIRE_FALSE(session->journal->run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{hebrew})));
    const core::AnnotationStore& store = session->journal->document().annotations();
    CHECK(store.at(*store.find(box.id)).contents == QStringLiteral("Latin"));
    // The builder says why, by name.
    auto built = pdf::ops::AppearanceStreamBuilder::normal(hebrew);
    REQUIRE_FALSE(built);
    CHECK(keyOf(built.error()) == "error.edit.typedCharacterRefused");
    CHECK(built.error().context == core::describeCharacter(0x05E9));
}

TEST_CASE("free text in a face this build does not draw is refused and never set in another", "[annotfreetext][D6-7]")
{
    Annotation courier = core::newAnnotation(AnnotationSubtype::FreeText, 0, kAuthor, kNow);
    courier.rect = core::PdfRect{core::PdfPoint{100, 600}, core::PdfSize{200, 40}};
    courier.defaultAppearance = QStringLiteral("/Cour 10 Tf 0 g");
    courier.contents = QStringLiteral("Monospaced");
    auto built = pdf::ops::AppearanceStreamBuilder::normal(courier);
    REQUIRE_FALSE(built);
    CHECK(keyOf(built.error()) == "error.pdf.appearanceUnsupported");
    // A callout ending this build does not draw yet (T8 draws line endings) is refused likewise.
    Annotation diamond = typed(FreeTextKind::Callout, QStringLiteral("Here"), core::PdfPoint{300, 700});
    diamond.lineEndings = {QStringLiteral("Diamond")};
    auto ending = pdf::ops::AppearanceStreamBuilder::normal(diamond);
    REQUIRE_FALSE(ending);
    CHECK(keyOf(ending.error()) == "error.pdf.appearanceUnsupported");
}

TEST_CASE("text on a turned page is set upright in its frame and one Helvetica serves ten comments", "[annotfreetext][D6-34]")
{
    // At /Rotate 90 the frame maps its x axis onto the page's +y: the appearance concatenates exactly that.
    const Annotation turned = typed(FreeTextKind::Box, QStringLiteral("Upright"), core::PdfPoint{300, 400},
                                    TypedFont::Simplified, 90);
    pdf::ops::CollectedAppearanceObjects objects;
    auto built = pdf::ops::AppearanceStreamBuilder::normal(turned, objects);
    REQUIRE(built);
    const auto frame = core::freeTextFrame(turned);
    REQUIRE(frame);
    const std::string expected = "0 1 -1 0 " + pdf::cos::ContentStreamBuilder::number(frame->box.right()) + " "
                                 + pdf::cos::ContentStreamBuilder::number(frame->box.bottom()) + " cm";
    CHECK(built.value().data.find(expected) != std::string::npos);
    CHECK(pdf::cos::ContentStreamBuilder::check(built.value().data));

    // Ten comments in one compose write ONE Helvetica.
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    std::vector<Annotation> ten;
    for (int i = 0; i < 10; ++i) ten.push_back(typed(FreeTextKind::Typewriter, QStringLiteral("Line %1").arg(i), core::PdfPoint{40, 700.0 - 20 * i}));
    REQUIRE(session->journal->run(std::make_unique<AddAnnotationCommand>(ten)));
    ReadBack file(save(*session, QStringLiteral("S6T7-freetext-ten.pdf")));
    std::set<long long> helvetica;
    for (const Annotation& a : ten) {
        const CosObject form = file.resolved(file.dictionaryOf(a.nm).get("AP").get("N"));
        const CosObject resources = file.resolved(form.get("Resources"));
        const CosObject fonts = file.resolved(resources.get("Font"));
        const CosObject* font = fonts.find("Helv");
        REQUIRE(font != nullptr);
        REQUIRE(font->isReference());
        helvetica.insert(font->reference().number);
    }
    CHECK(helvetica.size() == 1u);
}

// ==========================================================================================
// What T7 added beside the three kinds
// ==========================================================================================

TEST_CASE("a text object holds no graphics state save and the builder writes text that passes the re-read", "[annotfreetext][D6-6]")
{
    CHECK(pdf::cos::ContentStreamBuilder::check("BT /F 12 Tf 1 0 0 1 10 10 Tm <41> Tj ET"));
    for (const char* nested : {"BT q ET Q", "q BT Q ET", "BT /F 12 Tf q Q ET"}) {
        INFO(nested);
        const core::Status checked = pdf::cos::ContentStreamBuilder::check(nested);
        REQUIRE_FALSE(checked);
        CHECK(keyOf(checked.error()) == "error.pdf.contentStreamInvalid");
    }
    pdf::cos::ContentStreamBuilder text;
    text.save().beginText().font("Helv", 12).textMatrix(1, 0, 0, 1, 10, 20).showText(std::string("(A)\\", 4)).endText().restore();
    CHECK(text.bytes() == "q\nBT\n/Helv 12 Tf\n1 0 0 1 10 20 Tm\n<2841295C> Tj\nET\nQ\n");
    CHECK(pdf::cos::ContentStreamBuilder::check(text.bytes()));
}

TEST_CASE("a recoloured free text's text takes the colour and is redrawn in it", "[annotfreetext][A-26]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const Annotation box = typed(FreeTextKind::Box, QStringLiteral("Recolour me"), core::PdfPoint{300, 780});
    REQUIRE(session->journal->run(std::make_unique<AddAnnotationCommand>(std::vector<Annotation>{box})));
    core::AnnotationAppearance blue;
    blue.color = std::vector<double>{0.0, 0.0, 1.0};
    REQUIRE(session->journal->run(std::make_unique<core::SetAnnotationAppearanceCommand>(std::vector<core::AnnotationId>{box.id}, blue, kNow)));
    ReadBack file(save(*session, QStringLiteral("S6T7-freetext-recoloured.pdf")));
    const Annotation* read = file.withNm(box.nm);
    REQUIRE(read != nullptr);
    CHECK(read->color == std::vector<double>{0.0, 0.0, 1.0});
    const auto da = core::DefaultAppearance::parse(read->defaultAppearance);
    REQUIRE(da);
    CHECK(da->colour == std::vector<double>{0.0, 0.0, 1.0});
    CHECK(da->font == QStringLiteral("Helv"));   // only the colour moved
    const CosObject form = file.resolved(file.dictionaryOf(box.nm).get("AP").get("N"));
    const std::string stream(form.rawStreamData(file.table->source()));
    CHECK(stream.find("BT\n0 0 1 rg") != std::string::npos);   // the text is filled blue
}

TEST_CASE("free text's alignment is written and read back and centres each line", "[annotfreetext][A-32]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    Annotation centred = typed(FreeTextKind::Box, QStringLiteral("Centred"), core::PdfPoint{300, 780});
    centred.justification = 1;
    REQUIRE(session->journal->run(std::make_unique<AddAnnotationCommand>(std::vector<Annotation>{centred})));
    ReadBack file(save(*session, QStringLiteral("S6T7-freetext-centred.pdf")));
    const Annotation* read = file.withNm(centred.nm);
    REQUIRE(read != nullptr);
    CHECK(read->justification == 1);
    // Its one line starts where a centred line does: the frame's inset plus half the room the text leaves.
    const auto frame = core::freeTextFrame(centred);
    REQUIRE(frame);
    const core::Result<core::TypedLayout> layout =
        core::layoutTypedText(centred.contents, 12.0, frame->width - 2 * frame->inset, TypedFont::Simplified);
    REQUIRE(layout);
    const double x = frame->inset + (frame->width - 2 * frame->inset - layout.value().lines.front().width) / 2.0;
    const CosObject form = file.resolved(file.dictionaryOf(centred.nm).get("AP").get("N"));
    const std::string stream(form.rawStreamData(file.table->source()));
    CHECK(stream.find("1 0 0 1 " + pdf::cos::ContentStreamBuilder::number(x) + " ") != std::string::npos);
}

TEST_CASE("free text's alignment travels in a journal line and only as left centred or right", "[annotfreetext][A-29]")
{
    Annotation centred = typed(FreeTextKind::Box, QStringLiteral("Centred"), core::PdfPoint{300, 780});
    centred.justification = 1;
    const QJsonObject json = centred.toJson();
    CHECK(json.value(QStringLiteral("justification")).toInt() == 1);
    const core::Result<Annotation> back = Annotation::fromJson(json);
    REQUIRE(back);
    // The field itself; the whole annotation's identity under JSON is A-20's, over rectangles a double holds
    // exactly -- a fitted box's `bottom + height` comes back an ulp from its `top`, which is the model's
    // origin-and-size form and not this field.
    CHECK(back.value().justification == 1);
    CHECK(back.value().nm == centred.nm);
    // Left, the default, is written as nothing.
    centred.justification = 0;
    CHECK_FALSE(centred.toJson().contains(QStringLiteral("justification")));
    QJsonObject hostile = json;
    hostile.insert(QStringLiteral("justification"), 7);
    CHECK_FALSE(Annotation::fromJson(hostile));
}
