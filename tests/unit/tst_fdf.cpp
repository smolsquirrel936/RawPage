// tst_fdf -- comments exchanged as FDF and XFDF, below the widgets. Stage 6 T13, decisions D6-20 and D6-40
// (`Stage6-Annotations-Plan.md` §3), guide cases A-93, A-94, A-95, A-96's headless half and A-97
// (`Stage6-Annotations-Testing-Guide.md` §5.16).
//
//   * A-93 -- every comment the labelled corpus holds, exported to XFDF and separately to FDF, read back, imported
//     into a copy of its document with every annotation removed, SAVED and REOPENED: every modelled field equal,
//     dates compared as written (guide §3.7). What cannot be imported is what this build cannot WRITE (since T14 only
//     a producer's free text in a face this build does not draw), refused by the builder's own key -- checked against
//     the builder, and printed by name.
//   * A-94 -- `corpus/exchange/`'s Acrobat-shaped files imported into their targets, each field against the
//     generator's label; one `ImportAnnotationsCommand` in the journal.
//   * A-95 -- the merge by `/NM`: added, replaced, kept, refused, each said; one undo restores all of it.
//   * A-97 -- a hostile XFDF refused by key, in bounded time; no entity declared, let alone resolved.
//
// WHAT THIS DOES NOT COVER (convention 19): the readings are the FIRST-PARTY reader's through
// `tests/unit/CosBackend.h`; whether the engine DRAWS what was imported is the composition suite's
// (`tests/composition/tst_exchangeproduct`), and whether Acrobat reads what RawPage exports is M-04's. "No file is
// resolved" is proved by the refusal arriving at the document type declaration, before any entity exists -- a read
// of the probe file is not observed directly (Windows does not record one reliably).

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/AnnotationGeometry.h"
#include "core/doc/Fdf.h"
#include "core/doc/Redaction.h"
#include "core/doc/SoundData.h"
#include "core/doc/XfdfCodec.h"
#include "pdf/ops/FdfCodec.h"
#include "pdf/ops/MutationSinkImpl.h"
#include "pdf/ops/RevisionWriter.h"
#include "tests/fuzz/AnnotPipelineFuzzBody.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using core::Annotation;
using core::AnnotationId;
using core::AnnotationSubtype;
using core::Fdf;
using core::MergeEntry;

namespace {

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

std::string text(const QString& s)
{
    return s.toStdString();
}

core::Status writable(const Annotation& a)
{
    return pdf::ops::MutationSinkImpl::canCreate(a);
}

// Every lock cleared and every annotation deleted, through the journal: a copy of the document with every
// annotation removed (guide §3.7). The locks first, because a Locked comment is not deleted (D6-27).
void strip(tests::SaveSession& session)
{
    const core::AnnotationStore& store = session.journal->document().annotations();
    std::vector<AnnotationId> all;
    std::vector<AnnotationId> locked;
    for (const Annotation& a : store.all()) {
        all.push_back(a.id);
        if ((a.flags & core::LockAnnotationCommand::kLockFlags) != 0) locked.push_back(a.id);
    }
    if (!locked.empty()) {
        REQUIRE(session.journal->run(std::make_unique<core::LockAnnotationCommand>(
            locked, 0, core::LockAnnotationCommand::kLockFlags, 1791590400)));
    }
    if (!all.empty()) REQUIRE(session.journal->run(std::make_unique<core::DeleteAnnotationCommand>(all)));
    REQUIRE(session.journal->document().annotations().all().empty());
}

// The document's top-level comments -- what `comment.export` exports with no filter.
std::vector<std::size_t> everything(const core::AnnotationStore& store)
{
    return store.topLevel();
}

QString withoutDeclaration(QString rc)
{
    static const QRegularExpression declaration(QStringLiteral("^\\s*<\\?xml[^>]*\\?>\\s*"));
    return rc.remove(declaration);
}

// An annotation as A-93 compares it: every modelled field, its references as the NAMES of what they point at, a
// pop-up without a name of its own (one an import gives it is RawPage's, `core::popupNameFor`), a rich-text body
// without its XML declaration (XFDF carries the body; the reader restores the declaration), and neither its identity,
// its appearance flag nor a file's bytes -- the model holds what a file specification says, never its bytes.
QJsonObject comparable(const Annotation& a, const core::AnnotationStore& store)
{
    QJsonObject json = a.toJson();
    for (const char* key : {"id", "hasAppearance", "image", "inReplyTo", "popup", "parent"}) json.remove(QLatin1String(key));
    // ITS NAME IS THE ONE THE EXCHANGE GIVES IT: its /NM, or -- for a producer's comment with none -- the name its content
    // gives it, which the import writes, as everything RawPage writes carries a /NM (D6-4).
    if (a.subtype == AnnotationSubtype::Popup) json.remove(QStringLiteral("nm"));
    else json.insert(QStringLiteral("nm"), core::exchangeNameOf(a));
    if (!a.richText.isEmpty()) json.insert(QStringLiteral("richText"), withoutDeclaration(a.richText));
    if (json.contains(QStringLiteral("attachment"))) {
        QJsonObject file = json.value(QStringLiteral("attachment")).toObject();
        file.remove(QStringLiteral("data"));
        json.insert(QStringLiteral("attachment"), file);
    }
    // A reference to an annotation the document does not hold is no reference: the store shows such a reply as a
    // comment of its own (D6-12), and an import drops it and says so (D6-40).
    const auto nameOf = [&store](const std::optional<AnnotationId>& target) -> QString {
        if (!target) return QString();
        const std::optional<std::size_t> index = store.find(*target);
        return index ? core::exchangeNameOf(store.at(*index)) : QString();
    };
    if (!nameOf(a.inReplyTo).isEmpty()) json.insert(QStringLiteral("inReplyToName"), nameOf(a.inReplyTo));
    if (a.subtype == AnnotationSubtype::Popup) json.insert(QStringLiteral("parentName"), nameOf(a.parent));
    if (a.popup) json.insert(QStringLiteral("hasPopup"), true);
    return json;
}

std::string compact(const QJsonObject& json)
{
    return QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();
}

// The document's annotations by the name an exchange gives them -- a pop-up by its parent's.
std::map<QString, std::size_t> byName(const core::AnnotationStore& store)
{
    std::map<QString, std::size_t> out;
    for (std::size_t i = 0; i < store.size(); ++i) {
        const Annotation& a = store.at(i);
        if (a.subtype == AnnotationSubtype::Popup) {
            const std::optional<std::size_t> parent = a.parent ? store.find(*a.parent) : std::nullopt;
            if (parent) out.emplace(core::popupNameFor(core::exchangeNameOf(store.at(*parent))), i);
        } else {
            out.emplace(core::exchangeNameOf(a), i);
        }
    }
    return out;
}

struct Saved {
    tests::CosBackend backend;
    core::DocumentHandle handle;
    const core::AnnotationStore* store = nullptr;
};

std::unique_ptr<Saved> saveAndReopen(tests::SaveSession& session, const QString& name)
{
    const QString target = session.directory.filePath(name);
    auto saved = session.coordinator->saveAs(*session.journal, target);
    if (!saved) UNSCOPED_INFO("save: " << keyOf(saved.error()) << " " << text(saved.error().context));
    REQUIRE(saved);
    REQUIRE(saved.value().kind == core::SaveOutcome::Kind::Saved);
    auto out = std::make_unique<Saved>();
    auto handle = out->backend.open(target, nullptr);
    REQUIRE(handle);
    out->handle = handle.value();
    out->store = out->backend.annotations(handle.value());
    REQUIRE(out->store != nullptr);
    REQUIRE(out->store->status());
    return out;
}

// The labelled files A-93 sweeps -- every one but `annots-900.pdf`, whose scale is A-73's.
const std::vector<std::string> kRoundTrip = {
    "ordinary/annots-every-type.pdf", "ordinary/annots-threads.pdf",      "ordinary/annots-flags.pdf",
    "ordinary/annots-rotated.pdf",    "ordinary/annots-unknown-keys.pdf", "ordinary/annots-no-ap.pdf",
    "ordinary/annots-direct.pdf",     "ordinary/attachments.pdf",         "ordinary/measure-scale.pdf",
    "ordinary/annots-report-v1.pdf",  "ordinary/annots-mixed.pdf",
};

struct Tally {
    int exported = 0;
    int equal = 0;
    int refused = 0;
    std::vector<std::string> why;   // each refusal: its file, its kind and its key -- printed, so the figure explains itself
};

// A-93 for one file in one format. Returns what it counted; every difference is a CHECK failure naming the field.
Tally roundTrip(const std::string& relative, core::ExchangeFormat format)
{
    Tally tally;
    auto source = tests::openCopy(relative);
    REQUIRE(source);
    const core::AnnotationStore& original = source->journal->document().annotations();
    const Fdf exported = original.exportFdf(everything(original));
    const QString file = source->directory.filePath(format == core::ExchangeFormat::Xfdf ? QStringLiteral("out.xfdf")
                                                                                          : QStringLiteral("out.fdf"));
    REQUIRE(source->backend.writeAnnotationExchange(source->handle, exported, format, file, false));
    core::Result<Fdf> read = source->backend.readAnnotationExchange(file);
    if (!read) UNSCOPED_INFO("read: " << keyOf(read.error()) << " " << text(read.error().context));
    REQUIRE(read);

    // THE FILE SAYS WHAT WAS EXPORTED: one annotation for each, in the same order, the same names.
    REQUIRE(read.value().annotations.size() == exported.annotations.size());
    for (std::size_t i = 0; i < exported.annotations.size(); ++i)
        CHECK(text(read.value().annotations[i].id.nm) == text(exported.annotations[i].id.nm));

    // ... imported into a copy with every annotation removed, saved and reopened.
    auto target = tests::openCopy(relative);
    REQUIRE(target);
    strip(*target);
    const core::FdfImport plan =
        target->journal->document().annotations().importFdf(read.value(), core::importCheck(target->journal->document(), writable));
    for (const MergeEntry& e : plan.result.entries) {
        INFO(relative << " " << text(e.name) << " " << text(QString(core::annotationSubtypeName(e.subtype))));
        if (e.outcome == MergeEntry::Outcome::Refused) {
            ++tally.refused;
            REQUIRE(e.why.has_value());
            // REFUSED ONLY WHAT THIS BUILD CANNOT WRITE: the builder refuses the annotation itself, by the same key.
            const auto exchanged = std::find_if(read.value().annotations.begin(), read.value().annotations.end(),
                                                [&e](const Annotation& a) { return a.id.nm == e.name; });
            REQUIRE(exchanged != read.value().annotations.end());
            const core::Status builder = writable(*exchanged);
            CHECK_FALSE(builder);
            if (!builder) CHECK(keyOf(builder.error()) == keyOf(*e.why));
            tally.why.push_back(relative + " " + text(QString(core::annotationSubtypeName(e.subtype))) + " (" + keyOf(*e.why)
                                + " " + text(e.why->context) + ")");
        } else {
            CHECK(e.outcome == MergeEntry::Outcome::Added);
        }
    }
    REQUIRE_FALSE(plan.changesNothing());
    REQUIRE(target->journal->run(std::make_unique<core::ImportAnnotationsCommand>(plan.added, plan.replaced)));
    auto reopened = saveAndReopen(*target, QStringLiteral("imported.pdf"));

    const std::map<QString, std::size_t> before = byName(original);
    const std::map<QString, std::size_t> after = byName(*reopened->store);
    for (const Annotation& a : exported.annotations) {
        if (a.subtype == AnnotationSubtype::Popup) continue;
        ++tally.exported;
        const auto entry = std::find_if(plan.result.entries.begin(), plan.result.entries.end(),
                                        [&a](const MergeEntry& e) { return e.name == a.id.nm; });
        REQUIRE(entry != plan.result.entries.end());
        if (entry->outcome != MergeEntry::Outcome::Added) continue;
        INFO(relative << " " << (format == core::ExchangeFormat::Xfdf ? "XFDF" : "FDF") << ": " << text(a.id.nm));
        const auto was = before.find(a.id.nm);
        const auto now = after.find(a.id.nm);
        REQUIRE(was != before.end());
        REQUIRE(now != after.end());
        const std::string expected = compact(comparable(original.at(was->second), original));
        const std::string got = compact(comparable(reopened->store->at(now->second), *reopened->store));
        CHECK(got == expected);
        if (got == expected) ++tally.equal;
        // Its pop-up, by its parent's name.
        const auto popupWas = before.find(core::popupNameFor(a.id.nm));
        const auto popupNow = after.find(core::popupNameFor(a.id.nm));
        CHECK((popupWas == before.end()) == (popupNow == after.end()));
        if (popupWas != before.end() && popupNow != after.end()) {
            CHECK(compact(comparable(reopened->store->at(popupNow->second), *reopened->store))
                  == compact(comparable(original.at(popupWas->second), original)));
        }
    }
    return tally;
}

QByteArray xfdfWith(const QString& doctype, const QString& contents)
{
    return (QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n") + doctype
            + QStringLiteral("<xfdf xmlns=\"http://ns.adobe.com/xfdf/\"><annots>"
                             "<text name=\"probe\" page=\"0\" rect=\"10,10,30,30\"><contents>")
            + contents + QStringLiteral("</contents></text></annots></xfdf>"))
        .toUtf8();
}

} // namespace

// ==========================================================================================
// A-93 -- the round trip, both formats
// ==========================================================================================

TEST_CASE("every comment the labelled corpus holds round-trips through XFDF and FDF in every modelled field",
          "[fdf][exchange][A-93][D6-20]")
{
    for (const core::ExchangeFormat format : {core::ExchangeFormat::Xfdf, core::ExchangeFormat::Fdf}) {
        Tally total;
        for (const std::string& relative : kRoundTrip) {
            const Tally t = roundTrip(relative, format);
            total.exported += t.exported;
            total.equal += t.equal;
            total.refused += t.refused;
            total.why.insert(total.why.end(), t.why.begin(), t.why.end());
        }
        std::printf("A-93 %s: %d comment(s) exported over %d file(s); %d imported equal in every modelled field, %d refused "
                    "as this build cannot write them\n",
                    format == core::ExchangeFormat::Xfdf ? "XFDF" : "FDF", total.exported, static_cast<int>(kRoundTrip.size()),
                    total.equal, total.refused);
        for (const std::string& why : total.why) std::printf("  refused: %s\n", why.c_str());
        CHECK(total.equal + total.refused == total.exported);
        CHECK(total.equal > 0);
    }
}

TEST_CASE("an XFDF RawPage writes says every colour exactly and XFDF's own form for every other reader",
          "[fdf][exchange][A-93][D6-20]")
{
    Annotation a = core::newAnnotation(AnnotationSubtype::Square, 0, QStringLiteral("Zoë"), 1791590400);
    a.rect = core::PdfRect{core::PdfPoint{10, 20}, core::PdfSize{100, 50}};
    a.color = {1, 0.92, 0.23};
    a.interiorColor = {1, 1, 1};
    a.contents = QStringLiteral("line one\rline two & <three>");
    Fdf exchange;
    exchange.annotations = {a};
    const QByteArray xml = core::XfdfCodec::write(exchange);
    INFO(xml.toStdString());
    // Acrobat's #RRGGBB, AND the exact numbers in RawPage's namespace -- XFDF holds a channel in eight bits.
    CHECK(xml.contains("color=\"#FFEB3B\""));
    CHECK(xml.contains("rp:color=\"1 0.92 0.23\""));
    CHECK(xml.contains("interior-color=\"#FFFFFF\""));
    CHECK_FALSE(xml.contains("rp:interior-color"));   // white is exact in eight bits
    CHECK(xml.contains("&#13;"));                      // a carriage return survives XML's end-of-line rule
    core::Result<Fdf> back = core::XfdfCodec::read(xml);
    REQUIRE(back);
    REQUIRE(back.value().annotations.size() == 1);
    CHECK(back.value().annotations.front().color == a.color);
    CHECK(text(back.value().annotations.front().contents) == text(a.contents));
    // THE CONTROL: without RawPage's attribute, XFDF's own form comes back at its own precision.
    QByteArray acrobat = xml;
    acrobat.replace("rp:color=\"1 0.92 0.23\"", "");
    core::Result<Fdf> eightBits = core::XfdfCodec::read(acrobat);
    REQUIRE(eightBits);
    CHECK(std::fabs(eightBits.value().annotations.front().color[1] - 0.92) < 0.5 / 255.0 + 1e-12);
    CHECK(eightBits.value().annotations.front().color[1] != 0.92);
}

// ==========================================================================================
// A-94 -- the Acrobat-shaped fixtures
// ==========================================================================================

namespace {

QJsonObject label(const char* relative)
{
    return QJsonDocument::fromJson(tests::readAllBytes(tests::corpusFile(relative))).object();
}

bool near(double a, double b, double tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

bool sameNumbers(const std::vector<double>& ours, const QJsonValue& theirs, double tolerance = 1e-6)
{
    const QJsonArray array = theirs.toArray();
    if (static_cast<qsizetype>(ours.size()) != array.size()) return false;
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!near(ours[static_cast<std::size_t>(i)], array[i].toDouble(), tolerance)) return false;
    }
    return true;
}

// One label entry against what the import made of it -- the label's fields; a colour within XFDF's eight bits a
// channel when the file was XFDF; a reference by the NAME of the label's target object.
QStringList labelProblems(const QJsonObject& want, const Annotation& got, const core::AnnotationStore& store,
                          const std::map<long long, QString>& nameOfObject, double colourTolerance)
{
    QStringList problems;
    const auto bad = [&](const char* field) { problems << QLatin1String(field); };
    if (QString(core::annotationSubtypeName(got.subtype)) != want.value(QStringLiteral("subtype")).toString()) bad("subtype");
    if (!sameNumbers({got.rect.left(), got.rect.bottom(), got.rect.right(), got.rect.top()}, want.value(QStringLiteral("rect"))))
        bad("rect");
    if (static_cast<double>(got.flags) != want.value(QStringLiteral("flags")).toDouble()) bad("flags");
    struct Numbers { const char* key; const std::vector<double>* ours; double tolerance; };
    for (const Numbers& n : {Numbers{"quadPoints", &got.quadPoints, 1e-6}, Numbers{"vertices", &got.vertices, 1e-6},
                             Numbers{"line", &got.line, 1e-6}, Numbers{"color", &got.color, colourTolerance},
                             Numbers{"interiorColor", &got.interiorColor, colourTolerance},
                             Numbers{"callout", &got.callout, 1e-6}, Numbers{"rectDifferences", &got.rectDifferences, 1e-6}}) {
        const QJsonValue value = want.value(QLatin1String(n.key));
        if (value.isUndefined() ? !n.ours->empty() : !sameNumbers(*n.ours, value, n.tolerance)) bad(n.key);
    }
    const QJsonArray ink = want.value(QStringLiteral("inkList")).toArray();
    if (static_cast<qsizetype>(got.inkList.size()) != ink.size()) bad("inkList");
    for (qsizetype s = 0; s < ink.size() && s < static_cast<qsizetype>(got.inkList.size()); ++s) {
        if (!sameNumbers(got.inkList[static_cast<std::size_t>(s)], ink[s])) bad("inkList");
    }
    if (want.contains(QStringLiteral("opacity")) != got.opacity.has_value()
        || (got.opacity && !near(*got.opacity, want.value(QStringLiteral("opacity")).toDouble(), 1e-6)))
        bad("opacity");
    if (want.contains(QStringLiteral("borderWidth")) && (!got.borderWidth || !near(*got.borderWidth, want.value(QStringLiteral("borderWidth")).toDouble(), 1e-6)))
        bad("borderWidth");
    struct Text { const char* key; const QString* ours; };
    for (const Text& t : {Text{"nm", &got.nm}, Text{"author", &got.author}, Text{"subject", &got.subject},
                          Text{"contents", &got.contents}, Text{"intent", &got.intent}, Text{"icon", &got.icon},
                          Text{"defaultAppearance", &got.defaultAppearance}, Text{"stateModel", &got.stateModel},
                          Text{"state", &got.state}, Text{"created", &got.created.raw}, Text{"modified", &got.modified.raw}}) {
        if (*t.ours != want.value(QLatin1String(t.key)).toString()) problems << QLatin1String(t.key) + QStringLiteral(" '") + *t.ours + QLatin1Char('\'');
    }
    const QJsonObject irt = want.value(QStringLiteral("inReplyTo")).toObject();
    if (irt.isEmpty() != !got.inReplyTo.has_value()) {
        // A reply whose parent the label names and the file does not hold arrives as a comment of its own.
        const bool parentHeld = !irt.isEmpty() && nameOfObject.count(irt.value(QStringLiteral("object")).toInteger()) != 0;
        if (parentHeld || got.inReplyTo) bad("inReplyTo");
    } else if (got.inReplyTo) {
        const std::optional<std::size_t> parent = store.find(*got.inReplyTo);
        const auto wanted = nameOfObject.find(irt.value(QStringLiteral("object")).toInteger());
        if (!parent || wanted == nameOfObject.end() || store.at(*parent).nm != wanted->second) bad("inReplyTo");
    }
    if ((got.replyType == core::ReplyType::Group) != (want.value(QStringLiteral("replyType")).toString() == QLatin1String("Group")))
        bad("replyType");
    if (want.contains(QStringLiteral("popup")) != got.popup.has_value()) bad("popup");
    if (want.value(QStringLiteral("rotate")).toInt() != got.rotate) bad("rotate");
    if (want.value(QStringLiteral("justification")).toInt() != got.justification) bad("justification");
    return problems;
}

struct Imported {
    std::unique_ptr<tests::SaveSession> session;
    core::FdfImport plan;
    std::unique_ptr<Saved> reopened;
};

Imported importInto(const std::string& target, const QString& exchangeFile)
{
    Imported out;
    out.session = tests::openCopy(target);
    REQUIRE(out.session);
    strip(*out.session);
    core::Result<Fdf> read = out.session->backend.readAnnotationExchange(tests::corpusFile(exchangeFile.toStdString()));
    if (!read) UNSCOPED_INFO("read: " << keyOf(read.error()) << " " << text(read.error().context));
    REQUIRE(read);
    out.plan = out.session->journal->document().annotations().importFdf(
        read.value(), core::importCheck(out.session->journal->document(), writable));
    REQUIRE(out.session->journal->run(std::make_unique<core::ImportAnnotationsCommand>(out.plan.added, out.plan.replaced)));
    out.reopened = saveAndReopen(*out.session, QStringLiteral("imported.pdf"));
    return out;
}

} // namespace

TEST_CASE("the Acrobat-shaped exports import with every field the label holds", "[fdf][exchange][A-94][D6-20]")
{
    struct Fixture {
        const char* exchange;
        const char* target;
        const char* label;
        double colourTolerance;
    };
    const double eightBits = 0.5 / 255.0 + 1e-9;
    for (const Fixture& f : {Fixture{"exchange/every-type.xfdf", "ordinary/annots-every-type.pdf", "labels/annots-every-type.json", eightBits},
                             Fixture{"exchange/every-type.fdf", "ordinary/annots-every-type.pdf", "labels/annots-every-type.json", 1e-6},
                             Fixture{"exchange/threads.xfdf", "ordinary/annots-threads.pdf", "labels/annots-threads.json", eightBits},
                             Fixture{"exchange/threads.fdf", "ordinary/annots-threads.pdf", "labels/annots-threads.json", 1e-6}}) {
        INFO(f.exchange);
        Imported imported = importInto(f.target, QLatin1String(f.exchange));

        // THE JOURNAL: the strip, then ONE import.
        CHECK(imported.session->journal->canUndo());
        const std::optional<core::CatalogText> last = imported.session->journal->undoName();
        REQUIRE(last);
        CHECK(text(QString(last->key)) == "command.annot.import");

        const QJsonObject wanted = label(f.label);
        std::map<long long, QString> nameOfObject;
        std::vector<std::pair<int, QJsonObject>> entries;   // the page index the label lists it under, and its entry
        int pageIndex = 0;
        for (const QJsonValue page : wanted.value(QStringLiteral("pages")).toArray()) {
            for (const QJsonValue entry : page.toObject().value(QStringLiteral("annots")).toArray()) {
                const QJsonObject e = entry.toObject();
                if (!e.value(QStringLiteral("nm")).toString().isEmpty())
                    nameOfObject.emplace(e.value(QStringLiteral("object")).toInteger(), e.value(QStringLiteral("nm")).toString());
                entries.emplace_back(pageIndex, e);
            }
            ++pageIndex;
        }
        const std::map<QString, std::size_t> got = byName(*imported.reopened->store);
        int present = 0;
        int refused = 0;
        for (const auto& [wantPage, want] : entries) {
            const QString name = want.value(QStringLiteral("nm")).toString();
            if (want.value(QStringLiteral("subtype")).toString() == QLatin1String("Popup") || name.isEmpty()) continue;
            INFO(text(name) << " " << text(want.value(QStringLiteral("subtype")).toString()));
            const auto entry = std::find_if(imported.plan.result.entries.begin(), imported.plan.result.entries.end(),
                                            [&name](const MergeEntry& e) { return e.name == name; });
            REQUIRE(entry != imported.plan.result.entries.end());
            if (entry->outcome == MergeEntry::Outcome::Refused) {
                // SINCE T14 NOTHING these files hold is refused: until then a redaction mark and a sound were, by the
                // builder's key (D6-41(k)).
                ++refused;
                FAIL_CHECK("refused: " << want.value(QStringLiteral("subtype")).toString().toStdString());
                continue;
            }
            const auto found = got.find(name);
            REQUIRE(found != got.end());
            ++present;
            // The page it is ON: an exchange names pages by index, and a file whose comments span pages is the test.
            CHECK(imported.reopened->store->at(found->second).page == wantPage);
            const QStringList problems =
                labelProblems(want, imported.reopened->store->at(found->second), *imported.reopened->store, nameOfObject, f.colourTolerance);
            INFO(text(problems.join(QStringLiteral(", "))));
            CHECK(problems.isEmpty());
        }
        std::printf("A-94 %s: %d of the label's comments imported with every field, %d refused\n",
                    f.exchange, present, refused);
        CHECK(present > 0);
    }
}

TEST_CASE("a reply whose parent the file does not hold arrives as a comment of its own and is said to",
          "[fdf][exchange][A-94][D6-20]")
{
    Imported imported = importInto("ordinary/annots-threads.pdf", QStringLiteral("exchange/threads.xfdf"));
    const auto dropped = std::find_if(imported.plan.result.notes.begin(), imported.plan.result.notes.end(), [](const core::ExchangeNote& n) {
        return n.kind == core::ExchangeNoteKind::ReferenceDropped && n.name == QLatin1String("16491127-19db-64ad-4c48-c117c4f4abe9");
    });
    CHECK(dropped != imported.plan.result.notes.end());
    const std::map<QString, std::size_t> got = byName(*imported.reopened->store);
    const auto orphan = got.find(QStringLiteral("16491127-19db-64ad-4c48-c117c4f4abe9"));
    REQUIRE(orphan != got.end());
    CHECK_FALSE(imported.reopened->store->at(orphan->second).inReplyTo.has_value());
    CHECK(imported.reopened->store->isTopLevel(orphan->second));
    // A reply on page 2 to the page 1 note keeps its reference (D6-12 shows it top-level, as the label's file does).
    const auto across = got.find(QStringLiteral("0e00be23-c814-48b3-e88d-e9a4102f7a9d"));
    REQUIRE(across != got.end());
    CHECK(imported.reopened->store->at(across->second).inReplyTo.has_value());
}

// ==========================================================================================
// A-95 -- the merge by /NM
// ==========================================================================================

TEST_CASE("a merge adds the new replaces the newer keeps the older and one undo restores all three",
          "[fdf][exchange][A-95][D6-20]")
{
    auto session = tests::openCopy("ordinary/annots-threads.pdf");
    REQUIRE(session);
    const core::AnnotationStore& store = session->journal->document().annotations();
    const QByteArray original = session->journal->document().modelHash();
    Fdf exchange = store.exportFdf(store.topLevel());

    const QString newerName = QStringLiteral("4ea586ce-c29f-ed99-b8e0-2cfd3216b523");
    const QString olderName = QStringLiteral("9b08761b-616c-8c96-1fee-d951d70e0a1a");
    std::vector<Annotation> kept;
    for (Annotation a : exchange.annotations) {
        if (a.id.nm == newerName) {
            a.contents = QStringLiteral("Is this figure current? (revised)");
            a.modified = core::PdfDate::parse(QStringLiteral("D:20261010120000Z"));
            kept.push_back(a);
        } else if (a.id.nm == olderName) {
            a.contents = QStringLiteral("An older thought");
            a.modified = core::PdfDate::parse(QStringLiteral("D:20200101000000Z"));
            kept.push_back(a);
        }
    }
    Annotation fresh = core::newAnnotation(AnnotationSubtype::Highlight, 0, QStringLiteral("Imani Okoye"), 1791590400);
    fresh.nm = QStringLiteral("a-brand-new-comment");
    fresh.id = AnnotationId::ofCreated(fresh.nm);
    fresh.quadPoints = {72, 120, 200, 120, 72, 108, 200, 108};
    fresh.rect = *core::boundsOfPoints(fresh.quadPoints);
    kept.push_back(fresh);
    exchange.annotations = kept;
    exchange.sources.clear();

    const core::FdfImport plan = store.importFdf(exchange, core::importCheck(session->journal->document(), writable));
    REQUIRE(plan.result.entries.size() == 3);
    const auto outcomeOf = [&plan](const QString& name) {
        for (const MergeEntry& e : plan.result.entries) {
            if (e.name == name) return e.outcome;
        }
        FAIL("no entry for " << text(name));
        return MergeEntry::Outcome::Refused;
    };
    CHECK(outcomeOf(newerName) == MergeEntry::Outcome::Replaced);
    CHECK(outcomeOf(olderName) == MergeEntry::Outcome::Kept);
    CHECK(outcomeOf(fresh.nm) == MergeEntry::Outcome::Added);
    CHECK(plan.result.count(MergeEntry::Outcome::Added) == 1);
    CHECK(plan.result.count(MergeEntry::Outcome::Replaced) == 1);
    CHECK(plan.result.count(MergeEntry::Outcome::Kept) == 1);
    // The replaced note keeps the document's own pop-up; nothing else is added with it.
    REQUIRE(plan.added.size() == 1);
    REQUIRE(plan.replaced.size() == 1);
    CHECK(plan.replaced.front().popup.has_value());

    REQUIRE(session->journal->run(std::make_unique<core::ImportAnnotationsCommand>(plan.added, plan.replaced)));
    const auto contentsOf = [&store](const QString& name) {
        for (const Annotation& a : store.all()) {
            if (a.nm == name) return a.contents;
        }
        return QStringLiteral("(absent)");
    };
    CHECK(text(contentsOf(newerName)) == "Is this figure current? (revised)");
    CHECK(text(contentsOf(olderName)) == "Source for this number?");
    CHECK(text(contentsOf(fresh.nm)) == "");
    CHECK(store.find(fresh.id).has_value());

    // ONE UNDO restores all three.
    REQUIRE(session->journal->undo());
    CHECK(session->journal->document().modelHash() == original);
    auto delta = session->sink()->delta();
    REQUIRE(delta);
    CHECK(delta.value().isEmpty());
}

// ==========================================================================================
// A-29 -- recovery replays an import to the same model and the same bytes
// ==========================================================================================

TEST_CASE("recovery replays imports to the same model and the same bytes", "[fdf][exchange][A-29][recovery]")
{
    auto session = tests::openCopy("ordinary/annots-threads.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    auto recovery = core::RecoveryJournal::open(root.path(), session->path, session->journal->document().id(),
                                                &session->durableFiles);
    REQUIRE(recovery->isRecoverable());
    session->journal->setRecorder(recovery.get());
    core::CommandJournal& journal = *session->journal;
    const core::AnnotationStore& store = journal.document().annotations();

    // ONE: every kind an Acrobat-shaped file holds, added -- stamps, ink, a file attachment's file, rich text.
    core::Result<Fdf> everyType = session->backend.readAnnotationExchange(tests::corpusFile("exchange/every-type.xfdf"));
    REQUIRE(everyType);
    const core::FdfImport first = store.importFdf(everyType.value(), core::importCheck(journal.document(), writable));
    REQUIRE(first.added.size() > 10);
    REQUIRE(journal.run(std::make_unique<core::ImportAnnotationsCommand>(first.added, first.replaced)));

    // TWO: the document's own comment brought back newer -- REPLACED in place -- beside one new comment.
    Fdf own = store.exportFdf(store.topLevel());
    std::vector<Annotation> chosen;
    for (Annotation a : own.annotations) {
        if (a.id.nm != QLatin1String("4ea586ce-c29f-ed99-b8e0-2cfd3216b523")) continue;
        a.contents = QStringLiteral("Is this figure current? (replayed)");
        a.modified = core::PdfDate::parse(QStringLiteral("D:20261010120000Z"));
        chosen.push_back(a);
    }
    REQUIRE(chosen.size() == 1);
    Annotation fresh = core::newAnnotation(AnnotationSubtype::Square, 1, QStringLiteral("Imani Okoye"), 1791590400);
    fresh.nm = QStringLiteral("replayed-square");
    fresh.id = AnnotationId::ofCreated(fresh.nm);
    fresh.rect = core::PdfRect{core::PdfPoint{100, 100}, core::PdfSize{50, 40}};
    chosen.push_back(fresh);
    own.annotations = chosen;
    own.sources.clear();
    const core::FdfImport second = store.importFdf(own, core::importCheck(journal.document(), writable));
    REQUIRE(second.replaced.size() == 1);
    REQUIRE(second.added.size() == 1);
    REQUIRE(journal.run(std::make_unique<core::ImportAnnotationsCommand>(second.added, second.replaced)));

    // THREE: a third import, undone.
    Fdf more;
    Annotation undone = core::newAnnotation(AnnotationSubtype::Highlight, 0, QStringLiteral("Imani Okoye"), 1791590400);
    undone.nm = QStringLiteral("replayed-then-undone");
    undone.id = AnnotationId::ofCreated(undone.nm);
    undone.quadPoints = {72, 120, 200, 120, 72, 108, 200, 108};
    undone.rect = *core::boundsOfPoints(undone.quadPoints);
    more.annotations = {undone};
    const core::FdfImport third = store.importFdf(more, core::importCheck(journal.document(), writable));
    REQUIRE(journal.run(std::make_unique<core::ImportAnnotationsCommand>(third.added, third.replaced)));
    REQUIRE(journal.undo());

    const QByteArray expectedModel = journal.document().modelHash();
    auto expected = pdf::ops::RevisionWriter::compose(*session->sink(), false);
    REQUIRE(expected);
    recovery.reset();
    journal.setRecorder(nullptr);

    const auto candidates = core::RecoveryJournal::discover(root.path());
    REQUIRE(candidates.size() == 1);
    auto replayedSession = tests::openCopy("ordinary/annots-threads.pdf");
    REQUIRE(replayedSession);
    auto report = core::RecoveryJournal::replay(candidates.front().directory, *replayedSession->journal);
    REQUIRE(report);
    CHECK_FALSE(report.value().stoppedAtStep.has_value());
    CHECK(replayedSession->journal->document().modelHash() == expectedModel);
    CHECK(replayedSession->journal->canRedo());   // the undone import, redoable after the replay
    auto bytes = pdf::ops::RevisionWriter::compose(*replayedSession->sink(), false);
    REQUIRE(bytes);
    CHECK(tests::sha256(bytes.value().bytes) == tests::sha256(expected.value().bytes));
}

TEST_CASE("a merge refuses one comment by key and keeps the rest", "[fdf][exchange][A-95][D6-27]")
{
    SECTION("a locked comment is not replaced and its unlocked twin is")
    {
        auto session = tests::openCopy("ordinary/annots-flags.pdf");
        REQUIRE(session);
        const core::AnnotationStore& store = session->journal->document().annotations();
        Fdf exchange = store.exportFdf(store.topLevel());
        // Each comment comes back MOVED a point and with new words: `Locked` refuses the move, `LockedContents` the
        // words and `ReadOnly` both (D6-27) -- `Locked` alone would let the words change.
        for (Annotation& a : exchange.annotations) {
            if (a.subtype == AnnotationSubtype::Popup) continue;
            a = core::translated(a, 1.0, 0.0);
            a.contents += QStringLiteral(" (newer)");
            a.modified = core::PdfDate::parse(QStringLiteral("D:20261010120000Z"));
        }
        const core::FdfImport plan = store.importFdf(exchange, core::importCheck(session->journal->document(), writable));
        int lockedRefused = 0;
        int replaced = 0;
        for (const MergeEntry& e : plan.result.entries) {
            const auto held = std::find_if(store.all().begin(), store.all().end(), [&e](const Annotation& a) { return a.nm == e.name; });
            REQUIRE(held != store.all().end());
            const bool locked = held->hasFlag(core::AnnotationFlag::ReadOnly) || held->hasFlag(core::AnnotationFlag::Locked)
                                || held->hasFlag(core::AnnotationFlag::LockedContents);
            INFO(text(e.name) << " flags " << held->flags);
            if (locked) {
                CHECK(e.outcome == MergeEntry::Outcome::Refused);
                REQUIRE(e.why);
                CHECK(keyOf(*e.why).rfind("error.edit.annotation", 0) == 0);
                ++lockedRefused;
            } else if (e.outcome == MergeEntry::Outcome::Replaced) {
                ++replaced;
            }
        }
        CHECK(lockedRefused >= 3);
        CHECK(replaced > 0);
        // THE PLAN IS WHAT THE COMMAND'S PRE-FLIGHT ACCEPTS: no lock reaches it.
        core::ImportAnnotationsCommand command(plan.added, plan.replaced);
        CHECK(command.permitted(session->journal->document()));
        CHECK(session->journal->run(std::make_unique<core::ImportAnnotationsCommand>(plan.added, plan.replaced)));
    }
    SECTION("a name the document holds on another page, a page it does not have and a name given twice")
    {
        auto session = tests::openCopy("ordinary/annots-threads.pdf");
        REQUIRE(session);
        const core::AnnotationStore& store = session->journal->document().annotations();
        Fdf exchange = store.exportFdf(store.topLevel());
        std::vector<Annotation> chosen;
        for (Annotation a : exchange.annotations) {
            if (a.id.nm == QLatin1String("4ea586ce-c29f-ed99-b8e0-2cfd3216b523")) {
                a.page = 1;
                a.modified = core::PdfDate::parse(QStringLiteral("D:20261010120000Z"));
                chosen.push_back(a);
            }
        }
        Annotation far = core::newAnnotation(AnnotationSubtype::Highlight, 40, QStringLiteral("x"), 1791590400);
        far.quadPoints = {72, 120, 200, 120, 72, 108, 200, 108};
        far.rect = *core::boundsOfPoints(far.quadPoints);
        chosen.push_back(far);
        Annotation twice = core::newAnnotation(AnnotationSubtype::Highlight, 0, QStringLiteral("x"), 1791590400);
        twice.quadPoints = far.quadPoints;
        twice.rect = far.rect;
        chosen.push_back(twice);
        chosen.push_back(twice);
        exchange.annotations = chosen;
        const core::FdfImport plan = store.importFdf(exchange, core::importCheck(session->journal->document(), writable));
        REQUIRE(plan.result.entries.size() == 4);
        CHECK(keyOf(*plan.result.entries[0].why) == "error.exchange.conflict");
        CHECK(keyOf(*plan.result.entries[1].why) == "error.exchange.pageMissing");
        CHECK(plan.result.entries[2].outcome == MergeEntry::Outcome::Added);
        CHECK(keyOf(*plan.result.entries[3].why) == "error.exchange.duplicateName");
        CHECK(plan.added.size() == 1);
    }
}

TEST_CASE("a document's own export merged back changes nothing, a name or none", "[fdf][exchange][A-95][D6-20]")
{
    for (const char* relative : {"ordinary/annots-threads.pdf", "ordinary/annots-mixed.pdf"}) {
        INFO(relative);
        auto session = tests::openCopy(relative);
        REQUIRE(session);
        const core::AnnotationStore& store = session->journal->document().annotations();
        const Fdf exchange = store.exportFdf(store.topLevel());
        REQUIRE_FALSE(exchange.annotations.empty());
        const core::FdfImport plan = store.importFdf(exchange, core::importCheck(session->journal->document(), writable));
        CHECK(plan.changesNothing());
        CHECK(plan.result.count(MergeEntry::Outcome::Kept) == static_cast<int>(plan.result.entries.size()));
    }
    // annots-mixed.pdf's comments carry no /NM (Stage 2's fixture): each is named by what it says, the same name both
    // ways -- the control that says the names are not simply all /NM.
    auto mixed = tests::openCopy("ordinary/annots-mixed.pdf");
    REQUIRE(mixed);
    const Fdf exchange = mixed->journal->document().annotations().exportFdf(mixed->journal->document().annotations().topLevel());
    int named = 0;
    for (const Annotation& a : exchange.annotations) {
        if (a.id.nm.startsWith(QLatin1String("rawpage-content-"))) ++named;
    }
    CHECK(named > 0);
}

// ==========================================================================================
// A-96 (headless) -- export only the filtered
// ==========================================================================================

TEST_CASE("an export of a filtered list carries exactly those comments and their threads", "[fdf][exchange][A-96][D6-20]")
{
    auto session = tests::openCopy("ordinary/annots-threads.pdf");
    REQUIRE(session);
    const core::AnnotationStore& store = session->journal->document().annotations();
    core::AnnotationStore::Filter filter;
    filter.authors = {QStringLiteral("Ben Okafor")};
    const std::vector<std::size_t> listed = store.query(filter, core::AnnotationStore::SortKey::Page);
    REQUIRE_FALSE(listed.empty());
    const Fdf exchange = store.exportFdf(listed);
    std::set<QString> expected;
    for (const std::size_t item : listed) {
        for (const std::size_t member : store.threadOf(item)) {
            const Annotation& a = store.at(member);
            if (a.subtype != AnnotationSubtype::Popup) expected.insert(core::exchangeNameOf(a));
        }
    }
    std::set<QString> got;
    for (const Annotation& a : exchange.annotations) {
        if (a.subtype != AnnotationSubtype::Popup) got.insert(a.id.nm);
    }
    CHECK(got == expected);
    // A comment by someone else that is no reply in those threads is not in it.
    CHECK(got.count(QStringLiteral("9b08761b-616c-8c96-1fee-d951d70e0a1a")) == 0);
}

// ==========================================================================================
// A-97 -- a hostile XFDF
// ==========================================================================================

TEST_CASE("an XFDF with a document type declaration is refused by key before any entity exists", "[fdf][exchange][A-97][D6-20]")
{
    tests::CosBackend backend;
    SECTION("the corpus's external entity")
    {
        core::Result<Fdf> read = backend.readAnnotationExchange(tests::corpusFile("exchange/external-entity.xfdf"));
        REQUIRE_FALSE(read);
        CHECK(keyOf(read.error()) == "error.exchange.dtd");
    }
    SECTION("an entity naming a scratch file, and the same file without the declaration as the control")
    {
        QTemporaryDir directory;
        const QString probe = directory.filePath(QStringLiteral("probe.txt"));
        {
            QFile file(probe);
            REQUIRE(file.open(QIODevice::WriteOnly));
            file.write("RAWPAGE-PROBE-SECRET");
        }
        const QString url = QStringLiteral("file:///") + QString(probe).replace(QLatin1Char('\\'), QLatin1Char('/'));
        const QByteArray hostile = xfdfWith(QStringLiteral("<!DOCTYPE xfdf [ <!ENTITY probe SYSTEM \"") + url + QStringLiteral("\"> ]>\n"),
                                            QStringLiteral("&probe;"));
        core::Result<Fdf> read = core::XfdfCodec::read(hostile);
        REQUIRE_FALSE(read);
        CHECK(keyOf(read.error()) == "error.exchange.dtd");
        // THE CONTROL: the same file, the declaration gone and the contents literal, reads.
        core::Result<Fdf> plain = core::XfdfCodec::read(xfdfWith(QString(), QStringLiteral("a literal")));
        REQUIRE(plain);
        REQUIRE(plain.value().annotations.size() == 1);
        CHECK(text(plain.value().annotations.front().contents) == "a literal");
        // An entity XML never declared is not well-formed XML.
        core::Result<Fdf> undeclared = core::XfdfCodec::read(xfdfWith(QString(), QStringLiteral("&probe;")));
        REQUIRE_FALSE(undeclared);
        CHECK(keyOf(undeclared.error()) == "error.exchange.malformed");
    }
    SECTION("a billion laughs, in bounded time")
    {
        QString doctype = QStringLiteral("<!DOCTYPE xfdf [ <!ENTITY lol \"lol\">");
        for (int level = 1; level <= 10; ++level) {
            doctype += QStringLiteral("<!ENTITY lol%1 \"").arg(level);
            for (int i = 0; i < 10; ++i) doctype += level == 1 ? QStringLiteral("&lol;") : QStringLiteral("&lol%1;").arg(level - 1);
            doctype += QStringLiteral("\">");
        }
        doctype += QStringLiteral("]>\n");
        QElapsedTimer clock;
        clock.start();
        core::Result<Fdf> read = core::XfdfCodec::read(xfdfWith(doctype, QStringLiteral("&lol10;")));
        const qint64 elapsed = clock.elapsed();
        std::printf("A-97 billion laughs refused in %lld ms\n", static_cast<long long>(elapsed));
        REQUIRE_FALSE(read);
        CHECK(keyOf(read.error()) == "error.exchange.dtd");
        CHECK(elapsed < 2000);
    }
}

TEST_CASE("a truncated malformed or foreign exchange file is refused by key and a huge one read bounded",
          "[fdf][exchange][A-97][D6-20]")
{
    tests::CosBackend backend;
    core::Result<Fdf> truncated = backend.readAnnotationExchange(tests::corpusFile("exchange/malformed.xfdf"));
    REQUIRE_FALSE(truncated);
    CHECK(keyOf(truncated.error()) == "error.exchange.malformed");

    CHECK(keyOf(pdf::ops::FdfCodec::readEither(QByteArray("%PDF-1.7\n")).error()) == "error.exchange.notExchange");
    CHECK(keyOf(pdf::ops::FdfCodec::readEither(QByteArray("just some text")).error()) == "error.exchange.notExchange");
    CHECK_FALSE(core::XfdfCodec::read(QByteArray("<notxfdf/>")));

    // AN FDF CUT IN HALF: read as far as it goes, or refused -- never a crash.
    const QByteArray fdf = tests::readAllBytes(tests::corpusFile("exchange/threads.fdf"));
    for (const qsizetype cut : {fdf.size() / 4, fdf.size() / 2, fdf.size() - 20}) {
        core::Result<Fdf> part = pdf::ops::FdfCodec::read(fdf.left(cut));
        if (part) CHECK(part.value().annotations.size() <= 23);
    }

    // A MILLION-POINT GESTURE is cut at the bound and said to be; a 50,000-deep rich-text body is read in bounded time.
    QString gesture;
    gesture.reserve(12 * 500000);
    for (int i = 0; i < 500000; ++i) gesture += QStringLiteral("1,2;");
    const QByteArray ink = QStringLiteral("<xfdf xmlns=\"http://ns.adobe.com/xfdf/\"><annots><ink name=\"i\" page=\"0\" rect=\"0,0,9,9\">"
                                          "<inklist><gesture>%1</gesture></inklist></ink></annots></xfdf>").arg(gesture).toUtf8();
    core::Result<Fdf> inked = core::XfdfCodec::read(ink);
    REQUIRE(inked);
    REQUIRE(inked.value().annotations.size() == 1);
    std::size_t numbers = 0;
    for (const std::vector<double>& stroke : inked.value().annotations.front().inkList) numbers += stroke.size();
    CHECK(numbers == core::kMaxExchangeCoordinates);
    CHECK(std::any_of(inked.value().notes.begin(), inked.value().notes.end(),
                      [](const core::ExchangeNote& n) { return n.kind == core::ExchangeNoteKind::GeometryTruncated; }));

    QString deep;
    for (int i = 0; i < 50000; ++i) deep += QStringLiteral("<span>");
    deep += QStringLiteral("x");
    for (int i = 0; i < 50000; ++i) deep += QStringLiteral("</span>");
    QElapsedTimer clock;
    clock.start();
    core::Result<Fdf> nested = core::XfdfCodec::read(
        QStringLiteral("<xfdf xmlns=\"http://ns.adobe.com/xfdf/\"><annots><text name=\"t\" page=\"0\" rect=\"0,0,9,9\">"
                       "<contents-richtext><body>%1</body></contents-richtext></text></annots></xfdf>").arg(deep).toUtf8());
    CHECK(clock.elapsed() < 5000);
    CHECK(nested);
}

TEST_CASE("the annotation fuzz body's exchange branch holds over every exchange fixture and its truncations",
          "[fdf][exchange][fuzz][A-23][A-97][D6-29]")
{
    // The branch `fuzz_annot_pipeline` runs on Linux, replayed here over the corpus's six exchange files and each cut
    // at a quarter, a half and three quarters -- known inputs find nothing new by construction (convention 19); this
    // proves the branch runs and agrees with them.
    int files = 0;
    std::size_t annotations = 0;
    for (const char* name : {"every-type.xfdf", "every-type.fdf", "threads.xfdf", "threads.fdf", "external-entity.xfdf", "malformed.xfdf"}) {
        const QByteArray bytes = tests::readAllBytes(tests::corpusFile(std::string("exchange/") + name));
        for (const qsizetype cut : {bytes.size(), bytes.size() / 4, bytes.size() / 2, bytes.size() * 3 / 4}) {
            INFO(name << " cut at " << cut);
            std::size_t read = 0;
            const std::optional<std::string> failed = fuzz::exchangeFuzzOnce(bytes.left(cut), &read);
            if (failed) UNSCOPED_INFO(*failed);
            CHECK_FALSE(failed.has_value());
            if (cut == bytes.size()) annotations += read;
        }
        ++files;
    }
    std::printf("A-23 exchange replay: %d files and their truncations, %zu annotations read whole\n", files, annotations);
    CHECK(annotations > 40);
}

TEST_CASE("a pop-up never takes a name a comment of the file already has", "[fdf][exchange][I-20][D6-20]")
{
    // S6 guide I-20: a pop-up is named `<parent>-popup`; a comment of the file NAMED that would have been a second
    // annotation under one name, and the import's one command refused whole. The pop-up goes, and is said to.
    const QByteArray xml = QStringLiteral(
        "<xfdf xmlns=\"http://ns.adobe.com/xfdf/\"><annots>"
        "<text name=\"a\" page=\"0\" rect=\"10,10,30,30\"><contents>parent</contents><popup page=\"0\" rect=\"40,40,140,90\"/></text>"
        "<square name=\"a-popup\" page=\"0\" rect=\"50,50,90,90\"><contents>a comment named like a pop-up</contents></square>"
        "</annots></xfdf>").toUtf8();
    core::Result<Fdf> read = core::XfdfCodec::read(xml);
    REQUIRE(read);
    std::set<QString> names;
    for (const Annotation& a : read.value().annotations) CHECK(names.insert(a.id.nm).second);
    CHECK(names.count(QStringLiteral("a-popup")) == 1);
    // ... and the comment survives a merge into a page, the import one command the store accepts.
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const core::FdfImport plan = session->journal->document().annotations().importFdf(
        read.value(), core::importCheck(session->journal->document(), writable));
    REQUIRE(plan.added.size() >= 2);
    CHECK(session->journal->run(std::make_unique<core::ImportAnnotationsCommand>(plan.added, plan.replaced)));
    // The control: with the comment renamed, the pop-up is kept.
    QByteArray renamed = xml;
    renamed.replace("name=\"a-popup\"", "name=\"b\"");
    core::Result<Fdf> kept = core::XfdfCodec::read(renamed);
    REQUIRE(kept);
    CHECK(std::any_of(kept.value().annotations.begin(), kept.value().annotations.end(),
                      [](const Annotation& a) { return a.subtype == AnnotationSubtype::Popup; }));
}

TEST_CASE("an exchange file is written through pdf/ops and an existing one is replaced only when asked", "[fdf][exchange][A-96][D6-20]")
{
    auto session = tests::openCopy("ordinary/attachments.pdf");
    REQUIRE(session);
    const core::AnnotationStore& store = session->journal->document().annotations();
    const Fdf exchange = store.exportFdf(store.topLevel());
    const QString path = session->directory.filePath(QStringLiteral("comments.fdf"));
    REQUIRE(session->backend.writeAnnotationExchange(session->handle, exchange, core::ExchangeFormat::Fdf, path, false));
    const core::Status again = session->backend.writeAnnotationExchange(session->handle, exchange, core::ExchangeFormat::Fdf, path, false);
    REQUIRE_FALSE(again);
    CHECK(keyOf(again.error()) == "error.exchange.exists");
    CHECK(session->backend.writeAnnotationExchange(session->handle, exchange, core::ExchangeFormat::Fdf, path, true));
    // The pushpin's file travels with it: its bytes, read from the document, are in the FDF.
    core::Result<Fdf> back = session->backend.readAnnotationExchange(path);
    REQUIRE(back);
    const auto pin = std::find_if(back.value().annotations.begin(), back.value().annotations.end(),
                                  [](const Annotation& a) { return a.subtype == AnnotationSubtype::FileAttachment; });
    REQUIRE(pin != back.value().annotations.end());
    REQUIRE(pin->attachment);
    CHECK(pin->attachment->carried);
    CHECK(pin->attachment->data.size() == pin->attachment->size);
    CHECK_FALSE(pin->attachment->data.isEmpty());
}

TEST_CASE("an FDF we write reads back as written whatever a stream of it spells", "[fdf][exchange][fuzz][A-23][I-22]")
{
    // S6 guide I-22, the soak's finding (run 39226515635, `fuzz_annot_pipeline`): the writer gave its FDF no
    // cross-reference table, so reading it back was `openWithRepair`'s scan for `N G obj` -- which walks a stream's
    // bytes, as it must over a damaged file, and keeps the LAST definition of a number. A sound's samples spelling an
    // object 2 replaced the real one: the comment read back under the name in the samples. A carried file's bytes
    // are the same door. The impostor is placed AFTER the object it names, as last-wins needs.
    Annotation first = core::newAnnotation(AnnotationSubtype::Square, 0, QStringLiteral("Imani Okoye"), 1791590400);
    first.rect = core::PdfRect{core::PdfPoint{10, 10}, core::PdfSize{40, 40}};
    first.nm = QStringLiteral("first");
    first.id = AnnotationId::ofCreated(first.nm);
    core::SoundData sound;
    sound.samples = QByteArray("\n2 0 obj\n<< /Type /Annot /Subtype /Square /Rect [ 0 0 9 9 ] /Page 0 /NM (impostor) >>\nendobj\n");
    sound.carried = true;
    Annotation spoken = core::newSoundComment(0, core::PdfPoint{100, 700}, sound, {1.0, 0.85, 0.3}, QStringLiteral("Imani Okoye"), 1791590400);
    spoken.nm = QStringLiteral("spoken");
    spoken.id = AnnotationId::ofCreated(spoken.nm);
    Fdf exchange;
    exchange.annotations = {first, spoken};

    const QByteArray written = QByteArray::fromStdString(pdf::ops::FdfCodec::write(exchange));
    REQUIRE(written.contains("/NM (impostor)"));   // the stream really does spell an object in the file
    const core::Result<Fdf> back = pdf::ops::FdfCodec::read(written);
    REQUIRE(back);
    std::set<QString> names;
    for (const Annotation& a : back.value().annotations) names.insert(a.nm);
    CHECK(names == std::set<QString>{QStringLiteral("first"), QStringLiteral("spoken")});
    for (const Annotation& a : back.value().annotations) {
        if (a.subtype == AnnotationSubtype::Square) CHECK(a.rect == first.rect);   // the comment, not the impostor
        if (a.subtype != AnnotationSubtype::Sound) continue;
        REQUIRE(a.sound);
        CHECK(a.sound->samples == sound.samples);
    }
    // ... and the fuzz body's exchange branch over the written file, which writes and reads it once more.
    const std::optional<std::string> failed = fuzz::exchangeFuzzOnce(written);
    if (failed) UNSCOPED_INFO(*failed);
    CHECK_FALSE(failed.has_value());

    // The shape a person makes: a pushpin carrying a PDF, whose bytes spell `1 0 obj` -- a catalog, where the FDF's
    // own catalog is object 1. Without the table the import found no `/FDF` and refused the whole file.
    Annotation pin = core::newAnnotation(AnnotationSubtype::FileAttachment, 0, QStringLiteral("Imani Okoye"), 1791590400);
    pin.rect = core::PdfRect{core::PdfPoint{60, 60}, core::PdfSize{16, 24}};
    pin.nm = QStringLiteral("pinned");
    pin.id = AnnotationId::ofCreated(pin.nm);
    core::EmbeddedFile file;
    file.name = QStringLiteral("attached.pdf");
    file.mime = QStringLiteral("application/pdf");
    file.data = QByteArray("%PDF-1.7\n1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n2 0 obj\n<< /Type /Pages /Kids [ ] /Count 0 >>\nendobj\n");
    file.size = file.data.size();
    file.carried = true;
    pin.attachment = file;
    Fdf carrying;
    carrying.annotations = {pin};
    const QByteArray withPdf = QByteArray::fromStdString(pdf::ops::FdfCodec::write(carrying));
    REQUIRE(withPdf.contains("<< /Type /Catalog /Pages 2 0 R >>"));
    const core::Result<Fdf> pinned = pdf::ops::FdfCodec::read(withPdf);
    REQUIRE(pinned);
    REQUIRE(pinned.value().annotations.size() == 1);
    CHECK(pinned.value().annotations[0].nm == QStringLiteral("pinned"));
    REQUIRE(pinned.value().annotations[0].attachment);
    CHECK(pinned.value().annotations[0].attachment->data == file.data);
}
