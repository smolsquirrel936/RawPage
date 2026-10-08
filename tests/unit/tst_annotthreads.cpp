// tst_annotthreads -- Stage 6 T9 headless: the Comments pane below the widgets (`Stage6-Annotations-Testing-Guide.md`
// §5.12; A-67, A-68, A-69, A-71, A-72 and D6-11's store half; decisions D6-1, D6-12, D6-36).
//
//   * A-67: every grouping -- page, author, date, type, status, checkmark -- holds each top-level comment of
//     `annots-threads.pdf` exactly once, under the key the GENERATOR's label gives it, the groups in their order
//     and each group sorted; every filter -- author, type, status, checkmark, date range, text -- returns exactly
//     the set the label says, a thread found by the text of a reply under it.
//   * A-68: a reply and a reply to it, as `ReplyToAnnotationCommand`s, hang two deep; saved, `/IRT` names the
//     parent's OBJECT and no `/RT` is written (`/R` is the default).
//   * A-69: two review statuses are a history with their author and date, the latest the status; a checkmark is
//     each identity's own; saved, each is a hidden state reply with no appearance, read back the same.
//   * A-71: what a person deletes with a comment is the label's whole thread -- replies, states, group members,
//     pop-up -- and ONE `DeleteAnnotationCommand` takes it and one undo gives it back.
//   * A-72: sequence numbers run in reading order AS THE PAGE IS SHOWN, and only top-level comments have one.
//   * D6-11: the search units an open document's comments become are the store's, an unsaved one included.
//
// WHAT IT DOES NOT COVER (convention 19): the pane itself -- what it shows, its keys, its editor -- is
// `tests/ui/tst_commentspane`'s; the engine and the pixels are not involved in any of this.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/AnnotationStore.h"
#include "core/text/SearchEngine.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/PageOps.h"
#include "pdf/ops/RevisionWriter.h"
#include "tests/unit/TinyPdf.h"

#include <QDateTime>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <map>
#include <set>
#include <string>

using namespace rawpage;
using core::Annotation;
using core::AnnotationId;
using core::AnnotationStore;
using core::AnnotationSubtype;

namespace {

constexpr std::int64_t kNow = 1791590400;   // 2026-10-10T00:00:00Z

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

QByteArray corpusBytes(const QString& relative)
{
    QFile file(QStringLiteral(RAWPAGE_CORPUS_DIR "/") + relative);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

AnnotationStore storeOf(const QString& relative)
{
    const auto reading = tests::readAnnotations(corpusBytes(relative).toStdString());
    REQUIRE(reading);
    return AnnotationStore::fromReading(*reading);
}

// WHAT THE GENERATOR WROTE about `annots-threads.pdf` (`corpus/labels/annots-threads.json`) -- the oracle
// (rule 7), never the reader.
struct Label {
    struct Item {
        int page = -1;
        QString subtype;
        QString author;
        std::int64_t utc = 0;
        long long popup = -1;
    };
    std::map<long long, Item> items;                          // every annotation, by object
    std::set<long long> topLevel;                             // the label's roots and the four controls
    std::map<long long, std::vector<long long>> replies;      // /RT /R children, state replies excluded
    std::map<long long, std::vector<std::pair<QString, QString>>> review;   // (author, state), oldest first
    std::map<long long, std::vector<std::pair<QString, QString>>> marked;
    std::map<long long, std::vector<long long>> groups;       // a group's members
    std::map<long long, std::vector<long long>> states;       // every state reply's object, by parent

    QString status(long long object) const
    {
        const auto found = review.find(object);
        return found == review.end() || found->second.empty() ? QStringLiteral("None") : found->second.back().second;
    }
    bool markedBy(long long object, const QString& who) const
    {
        const auto found = marked.find(object);
        if (found == marked.end()) return false;
        std::optional<QString> latest;
        for (const auto& [author, state] : found->second)
            if (author == who) latest = state;
        return latest && *latest == QLatin1String("Marked");
    }
};

Label threadsLabel()
{
    const QJsonObject json = QJsonDocument::fromJson(corpusBytes(QStringLiteral("labels/annots-threads.json"))).object();
    Label out;
    for (const QJsonValue page : json.value(QStringLiteral("pages")).toArray()) {
        const int index = page.toObject().value(QStringLiteral("index")).toInt();
        for (const QJsonValue value : page.toObject().value(QStringLiteral("annots")).toArray()) {
            const QJsonObject a = value.toObject();
            Label::Item item;
            item.page = index;
            item.subtype = a.value(QStringLiteral("subtype")).toString();
            item.author = a.value(QStringLiteral("author")).toString();
            item.utc = QDateTime::fromString(a.value(QStringLiteral("createdUtc")).toString(), Qt::ISODate).toSecsSinceEpoch();
            if (a.value(QStringLiteral("popup")).isDouble()) item.popup = a.value(QStringLiteral("popup")).toInteger();
            out.items.emplace(a.value(QStringLiteral("object")).toInteger(), item);
        }
    }
    for (const QJsonValue value : json.value(QStringLiteral("topLevel")).toArray()) out.topLevel.insert(value.toInteger());
    const QJsonObject controls = json.value(QStringLiteral("controls")).toObject();
    for (const QJsonValue value : controls.value(QStringLiteral("cycle")).toArray()) out.topLevel.insert(value.toInteger());
    for (const QJsonValue value : controls.value(QStringLiteral("missingParent")).toArray()) out.topLevel.insert(value.toInteger());
    out.topLevel.insert(controls.value(QStringLiteral("crossPage")).toInteger());
    const QJsonObject replies = json.value(QStringLiteral("replies")).toObject();
    for (auto it = replies.begin(); it != replies.end(); ++it)
        for (const QJsonValue child : it.value().toArray()) out.replies[it.key().toLongLong()].push_back(child.toInteger());
    for (const auto& [key, into] : {std::pair{"reviewHistory", &out.review}, std::pair{"markedHistory", &out.marked}}) {
        const QJsonObject histories = json.value(QLatin1String(key)).toObject();
        for (auto it = histories.begin(); it != histories.end(); ++it) {
            for (const QJsonValue change : it.value().toArray()) {
                const QJsonObject c = change.toObject();
                (*into)[it.key().toLongLong()].emplace_back(c.value(QStringLiteral("author")).toString(),
                                                            c.value(QStringLiteral("state")).toString());
                out.states[it.key().toLongLong()].push_back(c.value(QStringLiteral("object")).toInteger());
            }
        }
    }
    for (const QJsonValue value : json.value(QStringLiteral("groups")).toArray()) {
        const QJsonObject g = value.toObject();
        for (const QJsonValue member : g.value(QStringLiteral("members")).toArray())
            out.groups[g.value(QStringLiteral("parent")).toInteger()].push_back(member.toInteger());
    }
    return out;
}

std::set<long long> objectsOf(const AnnotationStore& store, const std::vector<std::size_t>& indices)
{
    std::set<long long> out;
    for (const std::size_t index : indices) out.insert(store.at(index).id.object);
    return out;
}

std::set<long long> where(const Label& label, const std::function<bool(long long, const Label::Item&)>& keep)
{
    std::set<long long> out;
    for (const long long object : label.topLevel)
        if (keep(object, label.items.at(object))) out.insert(object);
    return out;
}

const QString kAna = QStringLiteral("Ana Lima");
const QString kBen = QStringLiteral("Ben Okafor");
const QString kZoe = QStringLiteral("Zoë Müller");

} // namespace

// ==========================================================================================
// A-67 -- group, sort and filter are the label's
// ==========================================================================================

TEST_CASE("every grouping holds each comment once under the key the label gives it", "[annotthreads][A-67]")
{
    const AnnotationStore store = storeOf(QStringLiteral("ordinary/annots-threads.pdf"));
    const Label label = threadsLabel();
    REQUIRE(label.topLevel.size() == 8);   // four roots and the four controls (A-21)

    // The key the label says each top-level comment belongs under, per grouping.
    const auto expectedKey = [&](AnnotationStore::GroupKey group, long long object) -> QString {
        const Label::Item& item = label.items.at(object);
        switch (group) {
        case AnnotationStore::GroupKey::None: return QString();
        case AnnotationStore::GroupKey::Page: return QString::number(item.page);
        case AnnotationStore::GroupKey::Author: return item.author;
        case AnnotationStore::GroupKey::Date:
            // The UTC day by arithmetic, not `QTimeZone::UTC` -- Qt 6.5's, and the portable suite builds on 6.4 (convention 24).
            return QDate::fromJulianDay(2440588 + item.utc / 86400).toString(Qt::ISODate);
        case AnnotationStore::GroupKey::Type: return item.subtype;
        case AnnotationStore::GroupKey::ReviewStatus: return label.status(object);
        case AnnotationStore::GroupKey::Checkmark:
            return label.markedBy(object, kAna) ? QStringLiteral("Marked") : QStringLiteral("Unmarked");
        }
        return QString();
    };

    AnnotationStore::Filter filter;
    filter.identity = kAna;
    using G = AnnotationStore::GroupKey;
    for (const G group : {G::None, G::Page, G::Author, G::Date, G::Type, G::ReviewStatus, G::Checkmark}) {
        for (const AnnotationStore::SortKey sort : {AnnotationStore::SortKey::Page, AnnotationStore::SortKey::Date,
                                                     AnnotationStore::SortKey::Author, AnnotationStore::SortKey::Subtype}) {
            INFO("group " << static_cast<int>(group) << ", sort " << static_cast<int>(sort));
            const std::vector<AnnotationStore::Group> groups = store.grouped(filter, group, sort);
            std::multiset<long long> seen;
            std::set<QString> keys;
            for (const AnnotationStore::Group& g : groups) {
                CHECK(keys.insert(g.key).second);   // one group per key
                CHECK_FALSE(g.items.empty());
                for (std::size_t i = 0; i < g.items.size(); ++i) {
                    const long long object = store.at(g.items[i]).id.object;
                    seen.insert(object);
                    CHECK(g.key == expectedKey(group, object));
                    if (i == 0) continue;
                    // Each group sorted by the key asked for.
                    const Label::Item& a = label.items.at(store.at(g.items[i - 1]).id.object);
                    const Label::Item& b = label.items.at(object);
                    switch (sort) {
                    case AnnotationStore::SortKey::Page: CHECK(a.page <= b.page); break;
                    case AnnotationStore::SortKey::Date: CHECK(a.utc <= b.utc); break;
                    case AnnotationStore::SortKey::Author: CHECK(QString::compare(a.author, b.author, Qt::CaseInsensitive) <= 0); break;
                    case AnnotationStore::SortKey::Subtype:
                        CHECK(static_cast<int>(core::annotationSubtypeFromName(a.subtype.toStdString()))
                              <= static_cast<int>(core::annotationSubtypeFromName(b.subtype.toStdString())));
                        break;
                    }
                }
            }
            CHECK(std::set<long long>(seen.begin(), seen.end()) == label.topLevel);
            CHECK(seen.size() == label.topLevel.size());   // each exactly once

            // THE GROUPS' ORDER (the header's): pages ascending, authors alphabetically, days newest first, the
            // statuses in Table 172's order with None last, Marked before Unmarked.
            for (std::size_t i = 1; i < groups.size(); ++i) {
                const QString& a = groups[i - 1].key;
                const QString& b = groups[i].key;
                switch (group) {
                case G::Page: CHECK(a.toInt() < b.toInt()); break;
                case G::Author: CHECK(QString::compare(a, b, Qt::CaseInsensitive) < 0); break;
                case G::Date: CHECK(a > b); break;
                case G::Type: CHECK(a < b); break;
                case G::ReviewStatus: {
                    const QStringList order{QStringLiteral("Accepted"), QStringLiteral("Rejected"), QStringLiteral("Cancelled"),
                                            QStringLiteral("Completed"), QStringLiteral("None")};
                    CHECK(order.indexOf(a) < order.indexOf(b));
                    break;
                }
                case G::Checkmark: CHECK(a == QLatin1String("Marked")); break;
                case G::None: break;
                }
            }
        }
    }
    // The none-grouping is ONE group; the page grouping two (both pages have comments); the label's statuses.
    CHECK(store.grouped(filter, G::None, AnnotationStore::SortKey::Page).size() == 1);
    CHECK(store.grouped(filter, G::Page, AnnotationStore::SortKey::Page).size() == 2);
    CHECK(label.status(6) == QLatin1String("Completed"));
    CHECK(label.status(15) == QLatin1String("Cancelled"));
}

TEST_CASE("a calendar day is the host's, by the offset it gives", "[annotthreads][A-67]")
{
    // Note 24 was written at 2026-09-16T02:00:00Z: the 16th in UTC, the 15th five hours west of it.
    const AnnotationStore store = storeOf(QStringLiteral("ordinary/annots-threads.pdf"));
    const auto dayOf = [&](int offset) {
        for (const AnnotationStore::Group& g : store.grouped({}, AnnotationStore::GroupKey::Date, AnnotationStore::SortKey::Page, offset)) {
            for (const std::size_t index : g.items)
                if (store.at(index).id.object == 24) return g.key;
        }
        return QString();
    };
    CHECK(dayOf(0) == QStringLiteral("2026-09-16"));
    CHECK(dayOf(-5 * 3600) == QStringLiteral("2026-09-15"));
    CHECK(dayOf(8 * 3600) == QStringLiteral("2026-09-16"));
}

TEST_CASE("every filter returns exactly the label's set", "[annotthreads][A-67]")
{
    const AnnotationStore store = storeOf(QStringLiteral("ordinary/annots-threads.pdf"));
    const Label label = threadsLabel();
    const auto run = [&](const AnnotationStore::Filter& filter) {
        return objectsOf(store, store.query(filter, AnnotationStore::SortKey::Page));
    };

    AnnotationStore::Filter byAuthor;
    byAuthor.authors = {kBen};
    CHECK(run(byAuthor) == where(label, [](long long, const Label::Item& i) { return i.author == kBen; }));
    byAuthor.authors = {kBen, kZoe};
    CHECK(run(byAuthor) == where(label, [](long long, const Label::Item& i) { return i.author == kBen || i.author == kZoe; }));

    AnnotationStore::Filter byType;
    byType.subtypes = {AnnotationSubtype::Text};
    CHECK(run(byType) == where(label, [](long long, const Label::Item& i) { return i.subtype == QLatin1String("Text"); }));
    byType.subtypes = {AnnotationSubtype::Highlight, AnnotationSubtype::Caret};
    CHECK(run(byType) == std::set<long long>{15, 20});

    for (const QString& status : {QStringLiteral("Completed"), QStringLiteral("Cancelled"), QStringLiteral("None"),
                                 QStringLiteral("Accepted")}) {
        INFO(status.toStdString());
        AnnotationStore::Filter byStatus;
        byStatus.statuses = {status};
        CHECK(run(byStatus) == where(label, [&](long long object, const Label::Item&) { return label.status(object) == status; }));
    }
    // ACCEPTED was a status once (Ben's, on 6) and is not now: the filter asks for the CURRENT one.
    AnnotationStore::Filter accepted;
    accepted.statuses = {QStringLiteral("Accepted")};
    CHECK(run(accepted).empty());

    // THE CHECKMARK IS PERSONAL: Ana's is on 6; Zoë's latest on 15 is Unmarked; Ben never marked.
    for (const QString& who : {kAna, kZoe, kBen}) {
        INFO(who.toStdString());
        AnnotationStore::Filter checked;
        checked.identity = who;
        checked.checked = true;
        CHECK(run(checked) == where(label, [&](long long object, const Label::Item&) { return label.markedBy(object, who); }));
        checked.checked = false;
        CHECK(run(checked) == where(label, [&](long long object, const Label::Item&) { return !label.markedBy(object, who); }));
    }
    AnnotationStore::Filter anaChecked;
    anaChecked.identity = kAna;
    anaChecked.checked = true;
    CHECK(run(anaChecked) == std::set<long long>{6});

    // A DATE RANGE, both ends inclusive, on the instant each is dated by.
    const std::int64_t from = QDateTime::fromString(QStringLiteral("2026-09-02T07:30:00Z"), Qt::ISODate).toSecsSinceEpoch();
    const std::int64_t to = QDateTime::fromString(QStringLiteral("2026-09-14T09:05:00Z"), Qt::ISODate).toSecsSinceEpoch();
    AnnotationStore::Filter range;
    range.from = from;
    range.to = to;
    CHECK(run(range) == where(label, [&](long long, const Label::Item& i) { return i.utc >= from && i.utc <= to; }));
    CHECK(run(range).count(15) == 1);   // on the first second
    CHECK(run(range).count(23) == 1);   // on the last

    // TEXT, in a comment or in any reply under it: "last week" is only in Ben's reply to note 6.
    AnnotationStore::Filter text;
    text.text = QStringLiteral("LAST WEEK");
    CHECK(run(text) == std::set<long long>{6});
    text.text = QStringLiteral("Table 4");   // Ana's reply to the highlight
    CHECK(run(text) == std::set<long long>{15});
    // A STATE REPLY'S text is a status, not a comment: "set by" is in every one of them and finds nothing.
    text.text = QStringLiteral("set by");
    CHECK(run(text).empty());

    // ALL OF THEM AT ONCE narrow together.
    AnnotationStore::Filter both;
    both.authors = {kBen};
    both.subtypes = {AnnotationSubtype::Text};
    CHECK(run(both) == where(label, [](long long, const Label::Item& i) {
              return i.author == kBen && i.subtype == QLatin1String("Text");
          }));
}

// ==========================================================================================
// A-71 -- a thread is what is deleted with its comment, and one undo gives it back
// ==========================================================================================

TEST_CASE("a comment's thread is the label's replies states group and pop-up", "[annotthreads][A-71]")
{
    const AnnotationStore store = storeOf(QStringLiteral("ordinary/annots-threads.pdf"));
    const Label label = threadsLabel();
    // The label's thread of `object`: it, its replies at any depth, the states and group members of each, and the
    // pop-up of each.
    const auto threadFromLabel = [&](long long root) {
        std::set<long long> out;
        std::vector<long long> pending{root};
        while (!pending.empty()) {
            const long long at = pending.back();
            pending.pop_back();
            // A child the label's controls show TOP-LEVEL -- the reply from page 2 to note 6 (D6-12) -- is an item
            // of its own, not part of the thread it names.
            if (at != root && label.topLevel.count(at) != 0) continue;
            if (!out.insert(at).second) continue;
            for (const auto* map : {&label.replies, &label.states, &label.groups}) {
                if (const auto found = map->find(at); found != map->end())
                    pending.insert(pending.end(), found->second.begin(), found->second.end());
            }
            if (label.items.at(at).popup >= 0) out.insert(label.items.at(at).popup);
        }
        return out;
    };
    for (const long long root : {6LL, 15LL, 20LL, 26LL}) {
        INFO("thread of " << root);
        const auto index = store.findObject(root);
        REQUIRE(index);
        CHECK(objectsOf(store, store.threadOf(*index)) == threadFromLabel(root));
    }
    CHECK(objectsOf(store, store.threadOf(*store.findObject(6))).size() == 9);   // note, pop-up, 3 replies, 4 states
}

TEST_CASE("deleting a comment with its thread is one step and one undo restores it", "[annotthreads][A-71][D6-1]")
{
    auto session = tests::openCopy("ordinary/annots-threads.pdf");
    REQUIRE(session);
    core::CommandJournal& journal = *session->journal;
    const AnnotationStore& store = journal.document().annotations();
    const QByteArray before = journal.document().modelHash();
    const std::size_t count = store.size();

    std::vector<AnnotationId> ids;
    for (const std::size_t index : store.threadOf(*store.findObject(6))) ids.push_back(store.at(index).id);
    REQUIRE(ids.size() == 9);
    REQUIRE(journal.run(std::make_unique<core::DeleteAnnotationCommand>(ids)));
    CHECK(store.size() == count - 9);
    CHECK_FALSE(store.findObject(6));
    CHECK_FALSE(store.findObject(13));   // a state reply went with it
    CHECK(journal.undoName()->key == QLatin1String("command.annot.deleteMany"));

    // EVERYTHING the list shows, as one command -- what *Delete all* builds.
    std::vector<AnnotationId> all;
    for (const std::size_t top : store.topLevel())
        for (const std::size_t index : store.threadOf(top)) all.push_back(store.at(index).id);
    REQUIRE(journal.run(std::make_unique<core::DeleteAnnotationCommand>(all)));
    CHECK(store.topLevel().empty());

    REQUIRE(journal.undo());
    REQUIRE(journal.undo());
    CHECK(store.size() == count);
    CHECK(journal.document().modelHash() == before);
}

// ==========================================================================================
// A-68 -- replies make threads, and are saved as ISO's
// ==========================================================================================

TEST_CASE("a reply and a reply to it hang two deep and are saved naming their parents", "[annotthreads][A-68]")
{
    auto session = tests::openCopy("ordinary/annots-threads.pdf");
    REQUIRE(session);
    core::CommandJournal& journal = *session->journal;
    const AnnotationStore& store = journal.document().annotations();
    const core::PdfRect page{core::PdfPoint{0, 0}, core::PdfSize{612, 792}};

    // To the HIGHLIGHT (15): a note-sized icon in the right margin at its height (D6-36).
    const Annotation highlight = store.at(*store.findObject(15));
    Annotation first = core::newReply(highlight, page, QStringLiteral("Which table?"), {1, 1, 0}, kBen, kNow);
    CHECK(first.rect.left() == 612 - 52);
    CHECK(first.rect.bottom() == highlight.rect.bottom() - 4);
    REQUIRE(journal.run(std::make_unique<core::ReplyToAnnotationCommand>(std::vector<Annotation>{first})));
    CHECK(journal.undoName()->key == QLatin1String("command.annot.reply"));
    // ... and to the reply: a reply to a NOTE sits on the note's own icon.
    Annotation second = core::newReply(first, page, QStringLiteral("Table 4."), {1, 1, 0}, kZoe, kNow + 60);
    CHECK(second.rect == first.rect);
    REQUIRE(journal.run(std::make_unique<core::ReplyToAnnotationCommand>(std::vector<Annotation>{second})));

    const std::size_t hl = *store.findObject(15);
    const std::size_t one = *store.find(first.id);
    const std::size_t two = *store.find(second.id);
    CHECK(store.parentOf(one) == hl);
    CHECK(store.parentOf(two) == one);   // two deep
    const std::vector<std::size_t> replies = store.replies(hl);
    CHECK(std::find(replies.begin(), replies.end(), one) != replies.end());
    CHECK(store.replies(one) == std::vector<std::size_t>{two});
    CHECK(store.topLevel().size() == 8);   // neither is a new item of the list
    CHECK(store.threadOf(hl).size() == 7);   // 15, Ana's reply, three states, and the two replies

    // SAVED AND READ BACK (rule 1): /IRT the object each parent was written at; no /RT written for /R.
    auto saved = session->coordinator->save(journal, session->path);
    REQUIRE(saved);
    REQUIRE(saved.value().kind == core::SaveOutcome::Kind::Saved);
    const auto reading = tests::readAnnotations(tests::readAllBytes(session->path).toStdString());
    REQUIRE(reading);
    const AnnotationStore back = AnnotationStore::fromReading(*reading);
    const Annotation* r1 = nullptr;
    const Annotation* r2 = nullptr;
    for (const Annotation& a : back.all()) {
        if (a.nm == first.nm) r1 = &a;
        if (a.nm == second.nm) r2 = &a;
    }
    REQUIRE(r1 != nullptr);
    REQUIRE(r2 != nullptr);
    REQUIRE(r1->inReplyTo);
    CHECK(*r1->inReplyTo == AnnotationId::ofObject(15));
    REQUIRE(r2->inReplyTo);
    CHECK(*r2->inReplyTo == r1->id);   // the object the first was written at
    CHECK(r2->replyType == core::ReplyType::Reply);
    CHECK(r1->author == kBen);
    CHECK(r2->contents == QStringLiteral("Table 4."));
    CHECK(r1->hasAppearance);   // a reply is a note, drawn as one
    CHECK(back.parentOf(*back.find(r2->id)) == back.find(r1->id));
    QByteArray bytes = tests::readAllBytes(session->path);
    CHECK_FALSE(bytes.mid(session->openedBytes.size()).contains("/RT"));
}

TEST_CASE("a reply is refused where its parent cannot be answered", "[annotthreads][A-68][D6-27][D6-36]")
{
    const core::PdfRect page{core::PdfPoint{0, 0}, core::PdfSize{612, 792}};
    // READ-ONLY refuses every interaction but selection (D6-27), by key.
    {
        auto session = tests::openCopy("ordinary/annots-flags.pdf");
        REQUIRE(session);
        const AnnotationStore& store = session->journal->document().annotations();
        std::optional<Annotation> readOnly;
        for (const Annotation& a : store.all())
            if (a.hasFlag(core::AnnotationFlag::ReadOnly) && a.subtype != AnnotationSubtype::Popup) readOnly = a;
        REQUIRE(readOnly);
        const core::Status refused = session->journal->run(std::make_unique<core::ReplyToAnnotationCommand>(
            std::vector<Annotation>{core::newReply(*readOnly, page, QStringLiteral("x"), {1, 1, 0}, kAna, kNow)}));
        REQUIRE_FALSE(refused);
        CHECK(keyOf(refused.error()) == "error.edit.annotationReadOnly");
        // A LOCKED one may be answered: a reply changes nothing of it.
        std::optional<Annotation> locked;
        for (const Annotation& a : store.all())
            if (a.flags & core::AnnotationFlag::Locked && !a.hasFlag(core::AnnotationFlag::ReadOnly)
                && a.subtype != AnnotationSubtype::Popup)
                locked = a;
        REQUIRE(locked);
        CHECK(session->journal->run(std::make_unique<core::ReplyToAnnotationCommand>(
            std::vector<Annotation>{core::newReply(*locked, page, QStringLiteral("x"), {1, 1, 0}, kAna, kNow)})));
    }
    // A DIRECT DICTIONARY is no object for /IRT to name (D6-36), said by key.
    {
        auto session = tests::openCopy("ordinary/annots-direct.pdf");
        REQUIRE(session);
        const AnnotationStore& store = session->journal->document().annotations();
        std::optional<Annotation> direct;
        for (const Annotation& a : store.all())
            if (a.id.kind == AnnotationId::Kind::Direct) direct = a;
        REQUIRE(direct);
        const core::Status refused = session->journal->run(std::make_unique<core::ReplyToAnnotationCommand>(
            std::vector<Annotation>{core::newReply(*direct, page, QStringLiteral("x"), {1, 1, 0}, kAna, kNow)}));
        REQUIRE_FALSE(refused);
        CHECK(keyOf(refused.error()) == "error.edit.annotationDirect");
    }
    // A reply placed on ANOTHER PAGE than its parent is not one the store could hang there: refused, nothing made.
    {
        auto session = tests::openCopy("ordinary/annots-threads.pdf");
        REQUIRE(session);
        const AnnotationStore& store = session->journal->document().annotations();
        const QByteArray hash = session->journal->document().modelHash();
        Annotation stray = core::newReply(store.at(*store.findObject(6)), page, QStringLiteral("x"), {1, 1, 0}, kAna, kNow);
        stray.page = 1;
        CHECK_FALSE(session->journal->run(std::make_unique<core::ReplyToAnnotationCommand>(std::vector<Annotation>{stray})));
        CHECK(session->journal->document().modelHash() == hash);
        // And a state handed to the reply command, or a reply to the status command: neither is what it promises.
        Annotation state = core::newStateReply(store.at(*store.findObject(6)), QStringLiteral("Review"),
                                               QStringLiteral("Accepted"), kAna, kNow);
        CHECK_FALSE(session->journal->run(std::make_unique<core::ReplyToAnnotationCommand>(std::vector<Annotation>{state})));
        Annotation reply = core::newReply(store.at(*store.findObject(6)), page, QStringLiteral("x"), {1, 1, 0}, kAna, kNow);
        CHECK_FALSE(session->journal->run(std::make_unique<core::SetAnnotationStatusCommand>(std::vector<Annotation>{reply})));
        CHECK(session->journal->document().modelHash() == hash);
    }
}

// ==========================================================================================
// A-69 -- status, its history, and the personal checkmark
// ==========================================================================================

TEST_CASE("two statuses are a history and a checkmark is each identity's own", "[annotthreads][A-69]")
{
    auto session = tests::openCopy("ordinary/annots-threads.pdf");
    REQUIRE(session);
    core::CommandJournal& journal = *session->journal;
    const AnnotationStore& store = journal.document().annotations();
    const QString ada = QStringLiteral("Ada Reviewer");
    const auto setStatus = [&](long long object, const QString& model, const QString& state, const QString& who,
                               std::int64_t at) {
        const Annotation parent = store.at(*store.findObject(object));
        return journal.run(std::make_unique<core::SetAnnotationStatusCommand>(
            std::vector<Annotation>{core::newStateReply(parent, model, state, who, at)}));
    };

    // THE CARET (20) has no status: two, by Ada, in order.
    const std::size_t caret = *store.findObject(20);
    CHECK(store.reviewStatus(caret) == QLatin1String("None"));
    REQUIRE(setStatus(20, QStringLiteral("Review"), QStringLiteral("Accepted"), ada, kNow));
    CHECK(journal.undoName()->key == QLatin1String("command.annot.setStatus"));
    REQUIRE(setStatus(20, QStringLiteral("Review"), QStringLiteral("Completed"), ada, kNow + 120));
    const auto history = store.reviewHistory(*store.findObject(20));
    REQUIRE(history.size() == 2);
    CHECK(history[0].state == QLatin1String("Accepted"));
    CHECK(history[1].state == QLatin1String("Completed"));
    CHECK(history[0].author == ada);
    CHECK(history[1].author == ada);
    CHECK(history[0].date.utcSeconds == kNow);
    CHECK(history[1].date.utcSeconds == kNow + 120);
    CHECK(store.reviewStatus(*store.findObject(20)) == QLatin1String("Completed"));
    CHECK(store.topLevel().size() == 8);   // a status is not an item of the list

    // THE CHECKMARK: Ada's, then Ben's, then Ada takes hers off -- each independent of the other's.
    REQUIRE(setStatus(20, QStringLiteral("Marked"), QStringLiteral("Marked"), ada, kNow + 200));
    CHECK(journal.undoName()->key == QLatin1String("command.annot.checkmark"));
    CHECK(store.isMarkedBy(*store.findObject(20), ada));
    CHECK_FALSE(store.isMarkedBy(*store.findObject(20), kBen));
    REQUIRE(setStatus(20, QStringLiteral("Marked"), QStringLiteral("Marked"), kBen, kNow + 300));
    REQUIRE(setStatus(20, QStringLiteral("Marked"), QStringLiteral("Unmarked"), ada, kNow + 400));
    CHECK_FALSE(store.isMarkedBy(*store.findObject(20), ada));
    CHECK(store.isMarkedBy(*store.findObject(20), kBen));

    // SAVED AND READ BACK: five hidden state replies, no appearance, the same history.
    auto saved = session->coordinator->save(journal, session->path);
    REQUIRE(saved);
    REQUIRE(saved.value().kind == core::SaveOutcome::Kind::Saved);
    const auto reading = tests::readAnnotations(tests::readAllBytes(session->path).toStdString());
    REQUIRE(reading);
    const AnnotationStore back = AnnotationStore::fromReading(*reading);
    const std::size_t again = *back.findObject(20);
    REQUIRE(back.reviewHistory(again).size() == 2);
    CHECK(back.reviewHistory(again)[1].state == QLatin1String("Completed"));
    CHECK(back.reviewHistory(again)[1].author == ada);
    CHECK(back.reviewHistory(again)[1].date.utcSeconds == kNow + 120);
    CHECK_FALSE(back.isMarkedBy(again, ada));
    CHECK(back.isMarkedBy(again, kBen));
    int states = 0;
    for (const Annotation& a : back.all()) {
        if (!a.isStateReply() || (a.author != ada && a.author != kBen)) continue;
        if (!a.inReplyTo || !(*a.inReplyTo == AnnotationId::ofObject(20))) continue;
        ++states;
        CHECK(a.flags == 30);   // Hidden | Print | NoZoom | NoRotate, Acrobat's
        CHECK_FALSE(a.hasAppearance);
        CHECK(a.contents.isEmpty());
    }
    CHECK(states == 5);

    // Undone to the start, the model is the file's.
    for (int i = 0; i < 5; ++i) REQUIRE(journal.undo());
    CHECK(journal.document().annotations().reviewHistory(*journal.document().annotations().findObject(20)).empty());
}

// ==========================================================================================
// A-72 -- sequence numbers, in reading order as the page is shown
// ==========================================================================================

TEST_CASE("sequence numbers run in reading order as each page is shown", "[annotthreads][A-72]")
{
    // Three comments on page 0 -- top left, top right, lower left -- and one on page 1.
    AnnotationStore::Reading reading;
    reading.pages.resize(2);
    const auto add = [&](long long object, int page, double left, double bottom) {
        Annotation a;
        a.id = AnnotationId::ofObject(object);
        a.subtype = AnnotationSubtype::Square;
        a.page = page;
        a.rect = core::PdfRect{core::PdfPoint{left, bottom}, core::PdfSize{40, 20}};
        core::AnnotationListingEntry entry;
        entry.fact = core::AnnotationFact{true, a.subtype, a.rect};
        entry.annotation = reading.annotations.size();
        reading.pages[static_cast<std::size_t>(page)].push_back(entry);
        reading.annotations.push_back(a);
    };
    add(10, 0, 300, 700);   // listed first, but to the right
    add(11, 0, 100, 700);
    add(12, 0, 100, 400);
    add(13, 1, 50, 50);
    const AnnotationStore store = AnnotationStore::fromReading(std::move(reading));
    const auto numberOf = [&](const std::vector<int>& numbers, long long object) {
        return numbers.at(*store.findObject(object));
    };

    const std::vector<int> upright = store.sequenceNumbers({0, 0});
    CHECK(numberOf(upright, 11) == 1);
    CHECK(numberOf(upright, 10) == 2);
    CHECK(numberOf(upright, 12) == 3);
    CHECK(numberOf(upright, 13) == 4);
    // Page 0 shown turned a quarter clockwise: its left edge is the top, and its bottom the left.
    const std::vector<int> turned = store.sequenceNumbers({90});
    CHECK(numberOf(turned, 12) == 1);
    CHECK(numberOf(turned, 11) == 2);
    CHECK(numberOf(turned, 10) == 3);
    CHECK(numberOf(turned, 13) == 4);
    const std::vector<int> half = store.sequenceNumbers({180});
    CHECK(numberOf(half, 12) == 1);   // its foot is the top; then right to left
    CHECK(numberOf(half, 10) == 2);
    CHECK(numberOf(half, 11) == 3);

    // ONLY top-level comments: on the threads file, exactly the label's eight, 1 to 8.
    const AnnotationStore threads = storeOf(QStringLiteral("ordinary/annots-threads.pdf"));
    const Label label = threadsLabel();
    const std::vector<int> numbers = threads.sequenceNumbers({});
    std::set<int> used;
    for (std::size_t i = 0; i < numbers.size(); ++i) {
        if (numbers[i] == 0) continue;
        CHECK(label.topLevel.count(threads.at(i).id.object) == 1);
        used.insert(numbers[i]);
    }
    CHECK(used == std::set<int>{1, 2, 3, 4, 5, 6, 7, 8});
}

// ==========================================================================================
// D6-11 -- comments of an open document are the store's
// ==========================================================================================

TEST_CASE("an open document's comment units are its store's and an unsaved one is among them", "[annotthreads][D6-11][A-74]")
{
    auto session = tests::openCopy("ordinary/annots-threads.pdf");
    REQUIRE(session);
    core::CommandJournal& journal = *session->journal;
    const AnnotationStore& store = journal.document().annotations();

    const auto unitsOf = [&] { return core::text::SearchEngine::commentUnits(store); };
    const auto texts = [](const core::text::SearchEngine::CommentUnits& units) {
        std::vector<QString> out;
        for (const auto& [page, part] : units)
            for (const core::text::SearchUnit& unit : part->units) out.push_back(unit.text);
        return out;
    };
    // Every comment with text -- replies included -- and no state reply's.
    const auto before = unitsOf();
    const std::vector<QString> found = texts(*before);
    CHECK(std::find(found.begin(), found.end(), QStringLiteral("Updated last week from the source.")) != found.end());
    CHECK(std::none_of(found.begin(), found.end(), [](const QString& t) { return t.contains(QStringLiteral("set by")); }));
    for (const auto& [page, part] : *before) {
        CHECK(part->part == core::text::SearchEngine::kStoreCommentsPart);
        for (const core::text::SearchUnit& unit : part->units) {
            CHECK(unit.contentClass == core::text::ContentClass::Comments);
            CHECK(unit.page == page);
            // The locator NAMES THE ANNOTATION, so a hit can select it.
            const std::optional<AnnotationId> id = AnnotationId::fromString(unit.locator);
            REQUIRE(id);
            CHECK(store.find(*id));
        }
    }

    // A NOTE NOT SAVED: in the units at once, and found by the same matcher the find bar's run uses.
    Annotation note = core::newAnnotation(AnnotationSubtype::Text, 1, kAna, kNow);
    note.rect = core::PdfRect{core::PdfPoint{100, 100}, core::PdfSize{20, 20}};
    note.icon = QStringLiteral("Comment");
    note.contents = QStringLiteral("A marmoset was here");
    REQUIRE(journal.run(std::make_unique<core::AddAnnotationCommand>(std::vector<Annotation>{note})));
    const auto after = unitsOf();
    const auto part = after->find(1);
    REQUIRE(part != after->end());
    core::text::SearchQuery query;
    query.pattern = QStringLiteral("marmoset");
    auto compiled = core::text::SearchEngine::compile(query);
    REQUIRE(compiled);
    int hits = 0;
    for (std::size_t i = 0; i < part->second->units.size(); ++i) {
        std::shared_ptr<const core::text::SearchUnit> unit(part->second, &part->second->units[i]);
        for (const core::text::SearchHit& hit : core::text::SearchEngine::findInUnit(compiled.value(), unit)) {
            ++hits;
            CHECK(hit.unit->locator == note.id.toString());
        }
    }
    CHECK(hits == 1);
    // THE CONTROL: the file -- what the engine's reader would give -- has no marmoset.
    CHECK_FALSE(session->openedBytes.contains("marmoset"));
}

TEST_CASE("a pop-up is in its note's thread whichever of the two names the other", "[annotthreads][A-71]")
{
    // annots-threads.pdf's pop-ups are named BOTH ways (`/Popup` and `/Parent`), so its case cannot tell the two
    // paths apart -- planted P4 (the `/Popup` path removed) stayed green over it. Here each path stands alone.
    AnnotationStore::Reading reading;
    reading.pages.resize(1);
    const auto add = [&](long long object, AnnotationSubtype subtype) -> Annotation& {
        Annotation a;
        a.id = AnnotationId::ofObject(object);
        a.subtype = subtype;
        a.page = 0;
        a.rect = core::PdfRect{core::PdfPoint{10, 10}, core::PdfSize{20, 20}};
        core::AnnotationListingEntry entry;
        entry.fact = core::AnnotationFact{true, subtype, a.rect};
        entry.annotation = reading.annotations.size();
        reading.pages[0].push_back(entry);
        reading.annotations.push_back(a);
        return reading.annotations.back();
    };
    add(10, AnnotationSubtype::Text);
    add(11, AnnotationSubtype::Popup);   // named only by the note's /Popup
    add(20, AnnotationSubtype::Text);
    add(21, AnnotationSubtype::Popup);   // naming its note only by its own /Parent
    reading.annotations[0].popup = AnnotationId::ofObject(11);
    reading.annotations[3].parent = AnnotationId::ofObject(20);
    const AnnotationStore store = AnnotationStore::fromReading(std::move(reading));
    CHECK(objectsOf(store, store.threadOf(*store.findObject(10))) == std::set<long long>{10, 11});
    CHECK(objectsOf(store, store.threadOf(*store.findObject(20))) == std::set<long long>{20, 21});
}

// ==========================================================================================
// A-29 -- recovery replays replies and statuses to the same model and the same bytes
// ==========================================================================================

TEST_CASE("recovery replays replies statuses and checkmarks to the same model and the same bytes",
          "[annotthreads][A-29][recovery]")
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
    const AnnotationStore& store = journal.document().annotations();
    const core::PdfRect page{core::PdfPoint{0, 0}, core::PdfSize{612, 792}};
    // A reply to the caret, a reply to THAT reply (a created parent, named by its /NM), a status and two checkmarks,
    // and one undone -- every kind of answer, replayed against the file as opened (D6-4).
    const Annotation caret = store.at(*store.findObject(20));
    const Annotation first = core::newReply(caret, page, QStringLiteral("Why decision?"), {1, 1, 0}, kAna, kNow);
    const Annotation second = core::newReply(first, page, QStringLiteral("It reads better."), {1, 1, 0}, kBen, kNow + 1);
    REQUIRE(journal.run(std::make_unique<core::ReplyToAnnotationCommand>(std::vector<Annotation>{first})));
    REQUIRE(journal.run(std::make_unique<core::ReplyToAnnotationCommand>(std::vector<Annotation>{second})));
    REQUIRE(journal.run(std::make_unique<core::SetAnnotationStatusCommand>(std::vector<Annotation>{
        core::newStateReply(caret, QStringLiteral("Review"), QStringLiteral("Rejected"), kZoe, kNow + 2)})));
    REQUIRE(journal.run(std::make_unique<core::SetAnnotationStatusCommand>(std::vector<Annotation>{
        core::newStateReply(first, QStringLiteral("Marked"), QStringLiteral("Marked"), kAna, kNow + 3),
        core::newStateReply(caret, QStringLiteral("Marked"), QStringLiteral("Marked"), kAna, kNow + 3)})));
    REQUIRE(journal.run(std::make_unique<core::SetAnnotationStatusCommand>(std::vector<Annotation>{
        core::newStateReply(caret, QStringLiteral("Review"), QStringLiteral("Completed"), kZoe, kNow + 4)})));
    REQUIRE(journal.undo());

    const QByteArray expectedModel = journal.document().modelHash();
    auto expected = pdf::ops::RevisionWriter::compose(*session->sink(), false);
    REQUIRE(expected);
    recovery.reset();
    journal.setRecorder(nullptr);

    const auto candidates = core::RecoveryJournal::discover(root.path());
    REQUIRE(candidates.size() == 1);
    auto fresh = tests::openCopy("ordinary/annots-threads.pdf");
    REQUIRE(fresh);
    auto report = core::RecoveryJournal::replay(candidates.front().directory, *fresh->journal);
    REQUIRE(report);
    CHECK_FALSE(report.value().stoppedAtStep.has_value());
    CHECK(fresh->journal->document().modelHash() == expectedModel);
    const AnnotationStore& replayed = fresh->journal->document().annotations();
    CHECK(replayed.reviewStatus(*replayed.findObject(20)) == QLatin1String("Rejected"));
    CHECK(replayed.parentOf(*replayed.find(second.id)) == replayed.find(first.id));
    auto bytes = pdf::ops::RevisionWriter::compose(*fresh->sink(), false);
    REQUIRE(bytes);
    CHECK(tests::sha256(bytes.value().bytes) == tests::sha256(expected.value().bytes));
}
