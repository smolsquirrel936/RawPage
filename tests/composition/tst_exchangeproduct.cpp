// tst_exchangeproduct -- Stage 6 T13 through the PRODUCT: comments imported, exported, migrated and pasted in the
// product's own composition over the shipped engine, each SAVED and the file reopened fresh
// (`Stage6-Annotations-Testing-Guide.md` §5.16 and §5.13's A-81; D6-20, D6-21, D6-22).
//
//   * A-94 through the product: `corpus/exchange/threads.xfdf` imported into `compare/report-v2.pdf` -- ONE command --
//     saved, and the file reopened fresh holding every comment the label names; WHAT THE SCREEN SHOWS IS WHAT SAVE
//     WROTE, the live page and the file 0 bytes apart (guide §3.2).
//   * A-96 through the product: an export of what the Comments pane lists, filtered by author, read back.
//   * A-98 and A-99 through the product: `comment.migrate` from `annots-report-v1.pdf` into `report-v2.pdf` -- the
//     text read and aligned off the UI thread, ONE command -- saved, reopened fresh, and every comment graded on
//     §3.5 against `labels/annots-migration.json`; the unplaceable three in the report and in no page of the file.
//   * A-81 through the product: three annotations copied from `annots-every-type.pdf` and pasted into
//     `text-a4.pdf`, saved, reopened fresh: every field as copied but the name and the page.
//
// WHAT THIS DOES NOT COVER (convention 19): the file dialogs and the report dialog a person sees (offscreen, the
// answers are a case's and the report is read from the window), and other readers -- M-04 (Acrobat) and M-10.

#include <catch2/catch_test_macros.hpp>

#include "AnnotProduct.h"
#include "core/cmd/ActionRegistry.h"
#include "core/doc/AnnotationStore.h"
#include "pdf/DocumentBackendImpl.h"
#include "ui/panes/CommentsPane.h"
#include "ui/shell/CommentMigration.h"
#include "ui/shell/MainWindow.h"

#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>

#include <cmath>
#include <iostream>
#include <map>
#include <set>

using namespace rawpage;
using annot_product::Product;
using core::Annotation;
using core::AnnotationId;
using core::AnnotationSubtype;

namespace {

core::RasterImage rasterOf(core::IDocumentBackend& backend, core::DocumentHandle handle, int page, double scale)
{
    const core::PdfSize size = backend.pages(handle)->at(page)->displaySize();
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = page;
    request.deviceScale = scale;
    request.revision = core::Revision::initial();
    request.flags = core::render::RenderFlags::Annotations;
    request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil(size.width * scale)),
                                          static_cast<int>(std::ceil(size.height * scale))};
    auto image = backend.rasterizer(handle)->raster(request);
    REQUIRE(image);
    return image.value();
}

QJsonObject label(const char* name)
{
    QFile file(test_oracle::corpusPath(std::string("labels/") + name));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool within(const std::vector<double>& got, const QJsonArray& want, double tolerance)
{
    if (static_cast<qsizetype>(got.size()) != want.size()) return false;
    for (qsizetype i = 0; i < want.size(); ++i) {
        if (std::fabs(got[static_cast<std::size_t>(i)] - want[i].toDouble()) > tolerance) return false;
    }
    return true;
}

QString scratchCopy(Product& product, const char* relative)
{
    const QString source = test_oracle::corpusPath(relative);
    const QString target = QDir(product.scratch.path()).filePath(QStringLiteral("source-") + QFileInfo(source).fileName());
    QFile::remove(target);
    REQUIRE(QFile::copy(source, target));
    return target;
}

struct Fresh {
    pdf::DocumentBackendImpl backend;
    core::DocumentHandle handle;
    const core::AnnotationStore* store = nullptr;
    ~Fresh()
    {
        if (handle.isValid()) (void)backend.close(handle);
    }
};

std::unique_ptr<Fresh> saveAndReopen(Product& product)
{
    REQUIRE(product.root->services().actions.dispatch(core::ActionId::FileSave, core::KeyContext::Viewer));
    product.settle();
    auto fresh = std::make_unique<Fresh>();
    auto opened = fresh->backend.open(product.openedPath, nullptr);
    REQUIRE(opened);
    fresh->handle = opened.value();
    fresh->store = fresh->backend.annotations(opened.value());
    REQUIRE(fresh->store != nullptr);
    REQUIRE(fresh->store->status());
    return fresh;
}

const Annotation* named(const core::AnnotationStore& store, const QString& name)
{
    for (const Annotation& a : store.all()) {
        if (a.nm == name) return &a;
    }
    return nullptr;
}

} // namespace

TEST_CASE("an Acrobat-shaped XFDF imported through the product is saved as shown", "[exchangeproduct][A-94][A-34][D6-20]")
{
    Product product;
    product.open("compare/report-v2.pdf");
    const std::size_t before = product.document()->journal()->position();
    core::Result<core::MergeResult> merged = product.window->importCommentsFrom(test_oracle::corpusPath("exchange/threads.xfdf"));
    REQUIRE(merged);
    CHECK(product.document()->journal()->position() == before + 1);
    const int added = merged.value().count(core::MergeEntry::Outcome::Added);
    CHECK(added == static_cast<int>(merged.value().entries.size()));
    product.settle();

    auto fresh = saveAndReopen(product);
    const QJsonObject wanted = label("annots-threads.json");
    int present = 0;
    for (const QJsonValue page : wanted.value(QStringLiteral("pages")).toArray()) {
        for (const QJsonValue entry : page.toObject().value(QStringLiteral("annots")).toArray()) {
            const QString name = entry.toObject().value(QStringLiteral("nm")).toString();
            if (name.isEmpty()) continue;
            INFO(name.toStdString());
            const Annotation* a = named(*fresh->store, name);
            REQUIRE(a != nullptr);
            CHECK(a->contents == entry.toObject().value(QStringLiteral("contents")).toString());
            CHECK(a->author == entry.toObject().value(QStringLiteral("author")).toString());
            ++present;
        }
    }
    CHECK(present == added);

    // WHAT THE SCREEN SHOWS IS WHAT SAVE WROTE (guide §3.2): the live engine's page and the file reopened fresh.
    product.root->services().renderScheduler.waitForIdle(4000);
    core::IDocumentBackend& backend = product.root->services().backend;
    long long differing = 0;
    for (const int page : {0, 1}) {
        for (const double scale : {1.0, 2.0}) {
            const core::RasterImage live = rasterOf(backend, product.document()->handle(), page, scale);
            const core::RasterImage file = rasterOf(fresh->backend, fresh->handle, page, scale);
            REQUIRE(live.width == file.width);
            REQUIRE(live.height == file.height);
            for (qsizetype i = 0; i < live.pixels.size(); ++i) differing += live.pixels[i] != file.pixels[i] ? 1 : 0;
        }
    }
    std::cout << "A-94 through the product: " << present << " comments imported and saved; live against the file reopened fresh: "
              << differing << " differing bytes on two pages at 72 and 144 dpi\n";
    CHECK(differing == 0);
}

TEST_CASE("a file holding kinds this build cannot write imports the rest as one command", "[exchangeproduct][A-94][D6-20]")
{
    // UNTIL T14 `every-type.xfdf`'s redaction mark and sound were the two this build could not write. Since T14 it writes
    // both (the case below), so the refusal is MADE here: the same file with its note's icon `Key` and its pushpin's
    // `Graph` -- two icons the builder refuses by name (D6-33(h), D6-38(i)). Each is refused by the builder's own key
    // through the SHIPPED backend's `annotationWritable`, and the other eighteen are one command.
    Product product;
    product.open("compare/report-v2.pdf");
    QFile source(test_oracle::corpusPath("exchange/every-type.xfdf"));
    REQUIRE(source.open(QIODevice::ReadOnly));
    QByteArray bytes = source.readAll();
    source.close();
    REQUIRE(bytes.count("icon=\"Comment\"") == 1);
    REQUIRE(bytes.count("icon=\"PushPin\"") == 1);
    bytes.replace("icon=\"Comment\"", "icon=\"Key\"").replace("icon=\"PushPin\"", "icon=\"Graph\"");
    const QString path = QDir(product.scratch.path()).filePath(QStringLiteral("unwritable-icons.xfdf"));
    QFile out(path);
    REQUIRE(out.open(QIODevice::WriteOnly));
    REQUIRE(out.write(bytes) == bytes.size());
    out.close();

    const std::size_t before = product.document()->journal()->position();
    core::Result<core::MergeResult> merged = product.window->importCommentsFrom(path);
    REQUIRE(merged);
    CHECK(product.document()->journal()->position() == before + 1);
    CHECK(merged.value().count(core::MergeEntry::Outcome::Added) == 18);
    REQUIRE(merged.value().count(core::MergeEntry::Outcome::Refused) == 2);
    for (const core::MergeEntry& e : merged.value().entries) {
        if (e.outcome != core::MergeEntry::Outcome::Refused) continue;
        CHECK((e.subtype == AnnotationSubtype::Text || e.subtype == AnnotationSubtype::FileAttachment));
        REQUIRE(e.why);
        CHECK(QString(e.why->messageKey) == QLatin1String("error.pdf.appearanceUnsupported"));
    }
    // The report names both, in the person's words.
    REQUIRE(product.window->lastExchangeReport().has_value());
    int refused = 0;
    for (const ui::ExchangeReport::Row& row : product.window->lastExchangeReport()->rows) refused += row.reason.isEmpty() ? 0 : 1;
    CHECK(refused == 2);
}

TEST_CASE("since T14 an Acrobat-shaped exchange's redaction mark and sound import with their payloads", "[exchangeproduct][A-94][A-100][A-101]")
{
    // D6-40's revisit, paid at T14: `every-type.xfdf`'s twenty comments ALL arrive as one command -- the mark with its
    // overlay text and repeat, the sound with its samples byte for byte as the generator wrote them (its label's SHA-256)
    // -- and the saved file reads them back.
    Product product;
    product.open("compare/report-v2.pdf");
    core::Result<core::MergeResult> merged = product.window->importCommentsFrom(test_oracle::corpusPath("exchange/every-type.xfdf"));
    REQUIRE(merged);
    CHECK(merged.value().count(core::MergeEntry::Outcome::Added) == 20);
    CHECK(merged.value().count(core::MergeEntry::Outcome::Refused) == 0);

    const QJsonObject wanted = label("annots-every-type.json");
    QJsonObject wantSound, wantMark;
    for (const QJsonValue page : wanted.value(QStringLiteral("pages")).toArray()) {
        for (const QJsonValue entry : page.toObject().value(QStringLiteral("annots")).toArray()) {
            const QJsonObject a = entry.toObject();
            if (a.value(QStringLiteral("subtype")).toString() == QLatin1String("Sound")) wantSound = a;
            if (a.value(QStringLiteral("subtype")).toString() == QLatin1String("Redact")) wantMark = a;
        }
    }
    REQUIRE_FALSE(wantSound.isEmpty());
    REQUIRE_FALSE(wantMark.isEmpty());
    product.settle();
    auto fresh = saveAndReopen(product);
    const core::AnnotationStore* store = fresh->store;
    int sounds = 0, marks = 0;
    for (const Annotation& a : store->all()) {
        if (a.subtype == AnnotationSubtype::Sound) {
            ++sounds;
            REQUIRE(a.sound);
            const QJsonObject s = wantSound.value(QStringLiteral("sound")).toObject();
            CHECK(a.sound->rate == s.value(QStringLiteral("rate")).toInt());
            CHECK(a.sound->channels == s.value(QStringLiteral("channels")).toInt());
            CHECK(a.sound->bits == s.value(QStringLiteral("bits")).toInt());
            CHECK(a.sound->encoding == core::SoundEncoding::Signed);
            const core::Result<QByteArray> samples = fresh->backend.soundSamples(fresh->handle, a.id);
            REQUIRE(samples);
            CHECK(QString::fromLatin1(QCryptographicHash::hash(samples.value(), QCryptographicHash::Sha256).toHex())
                  == s.value(QStringLiteral("sha256")).toString());
        }
        if (a.subtype == AnnotationSubtype::Redact) {
            ++marks;
            CHECK(a.overlayText == wantMark.value(QStringLiteral("overlayText")).toString());
            CHECK(a.repeat == wantMark.value(QStringLiteral("repeat")).toBool());
            CHECK(a.quadPoints.size() == static_cast<std::size_t>(wantMark.value(QStringLiteral("quadPoints")).toArray().size()));
        }
    }
    CHECK(sounds == 1);
    CHECK(marks == 1);
}

TEST_CASE("an export through the product carries what the filtered pane lists", "[exchangeproduct][A-96][D6-20]")
{
    Product product;
    product.open("ordinary/annots-threads.pdf");
    const QString out = QDir(product.scratch.path()).filePath(QStringLiteral("ben.fdf"));
    ui::MainWindow::setExchangeAnswersForTesting({{}, [out](const QString&) { return std::optional<QString>(out); }, {}});
    REQUIRE(product.root->services().actions.dispatch(core::ActionId::PaneComments, core::KeyContext::Global));
    ui::CommentsPane* pane = product.window->commentsPane();
    REQUIRE(pane != nullptr);
    pane->setAuthorFilter({QStringLiteral("Ben Okafor")});
    QCoreApplication::processEvents();
    REQUIRE(product.root->services().actions.dispatch(core::ActionId::CommentExport, core::KeyContext::Global));
    ui::MainWindow::setExchangeAnswersForTesting({});

    core::Result<core::Fdf> back = product.root->services().backend.readAnnotationExchange(out);
    REQUIRE(back);
    std::set<QString> got;
    for (const Annotation& a : back.value().annotations) {
        if (a.subtype != AnnotationSubtype::Popup) got.insert(a.id.nm);
    }
    core::AnnotationStore::Filter filter;
    filter.authors = {QStringLiteral("Ben Okafor")};
    std::set<QString> expected;
    for (const std::size_t item : product.store().query(filter, core::AnnotationStore::SortKey::Page)) {
        for (const std::size_t member : product.store().threadOf(item)) {
            if (product.store().at(member).subtype != AnnotationSubtype::Popup) expected.insert(core::exchangeNameOf(product.store().at(member)));
        }
    }
    CHECK_FALSE(expected.empty());
    CHECK(got == expected);
}

TEST_CASE("comments migrated through the product land where the label says and the rest are reported",
          "[exchangeproduct][A-98][A-99][D6-21]")
{
    Product product;
    product.open("compare/report-v2.pdf");
    const QString older = scratchCopy(product, "ordinary/annots-report-v1.pdf");
    ui::MainWindow::setExchangeAnswersForTesting({{}, {}, [older] { return std::optional<QString>(older); }});
    bool finished = false;
    QObject::connect(product.window.get(), &ui::MainWindow::migrationFinished, [&finished] { finished = true; });
    const std::size_t before = product.document()->journal()->position();
    REQUIRE(product.root->services().actions.dispatch(core::ActionId::CommentMigrate, core::KeyContext::Global));
    ui::MainWindow::setExchangeAnswersForTesting({});
    REQUIRE(product.window->migration() != nullptr);
    QElapsedTimer timer;
    timer.start();
    while (!finished && timer.elapsed() < 60000) product.settle();
    REQUIRE(finished);
    CHECK(product.document()->journal()->position() == before + 1);   // ONE import
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(product.window->migration() == nullptr);

    REQUIRE(product.window->lastExchangeReport().has_value());
    const ui::ExchangeReport& report = *product.window->lastExchangeReport();
    const QJsonObject wanted = label("annots-migration.json");
    const QJsonArray annotations = wanted.value(QStringLiteral("annotations")).toArray();
    CHECK(report.rows.size() == static_cast<std::size_t>(annotations.size()));

    auto fresh = saveAndReopen(product);
    int placedRight = 0;
    int placeable = 0;
    int reported = 0;
    int unplaceable = 0;
    for (const QJsonValue value : annotations) {
        const QJsonObject want = value.toObject();
        const QString name = want.value(QStringLiteral("nm")).toString();
        INFO(name.toStdString() << " " << want.value(QStringLiteral("why")).toString().toStdString());
        const Annotation* a = named(*fresh->store, name);
        const auto row = std::find_if(report.rows.begin(), report.rows.end(), [&name](const ui::ExchangeReport::Row& r) { return r.name == name; });
        REQUIRE(row != report.rows.end());
        if (want.value(QStringLiteral("placeable")).toBool()) {
            ++placeable;
            REQUIRE(a != nullptr);
            const bool page = a->page == want.value(QStringLiteral("expectedPage")).toInt();
            const bool where = want.contains(QStringLiteral("expectedQuadPoints"))
                                   ? within(a->quadPoints, want.value(QStringLiteral("expectedQuadPoints")).toArray(), 1.0)
                                   : within({a->rect.left(), a->rect.bottom(), a->rect.right(), a->rect.top()},
                                            want.value(QStringLiteral("expectedRect")).toArray(), 2.0);
            CHECK(page);
            CHECK(where);
            if (page && where) ++placedRight;
            CHECK(row->reason.isEmpty());
        } else {
            ++unplaceable;
            CHECK(a == nullptr);   // A-99: in no page of the file
            CHECK_FALSE(row->reason.isEmpty());
            if (a == nullptr && !row->reason.isEmpty()) ++reported;
        }
    }
    std::cout << "A-98 through the product: " << placedRight << " of " << placeable << " placeable comments where the label says; A-99: "
              << reported << " of " << unplaceable << " reported and in no page of the file\n";
    CHECK(placedRight == placeable);
    CHECK(reported == unplaceable);

    // ONE UNDO takes the migration back.
    REQUIRE(product.document()->journal()->undo());
    for (const QJsonValue value : annotations)
        CHECK(named(product.store(), value.toObject().value(QStringLiteral("nm")).toString()) == nullptr);
}

TEST_CASE("annotations pasted into another document through the product are saved as copied", "[exchangeproduct][A-81][D6-22]")
{
    Product product;
    product.open("ordinary/annots-every-type.pdf");
    // The highlight, the square and the note -- the note's pop-up goes with it.
    std::vector<AnnotationId> chosen;
    std::map<QString, Annotation> originals;
    for (const Annotation& a : product.store().all()) {
        if (a.subtype == AnnotationSubtype::Highlight || a.subtype == AnnotationSubtype::Square || a.subtype == AnnotationSubtype::Text) {
            chosen.push_back(a.id);
            originals.emplace(QString(core::annotationSubtypeName(a.subtype)), a);
        }
    }
    REQUIRE(chosen.size() == 3);
    product.overlay()->select(chosen);
    REQUIRE(product.root->services().actions.dispatch(core::ActionId::EditCopy, core::KeyContext::Viewer));
    REQUIRE(QGuiApplication::clipboard()->mimeData()->hasFormat(QLatin1String(ui::MainWindow::kAnnotationsMimeType)));

    product.open("ordinary/text-a4.pdf");
    const std::size_t before = product.document()->journal()->position();
    REQUIRE(product.root->services().actions.dispatch(core::ActionId::EditPaste, core::KeyContext::Viewer));
    CHECK(product.document()->journal()->position() == before + 1);
    product.settle();

    auto fresh = saveAndReopen(product);
    int compared = 0;
    for (const Annotation& a : fresh->store->all()) {
        if (a.subtype == AnnotationSubtype::Popup) continue;
        const auto original = originals.find(QString(core::annotationSubtypeName(a.subtype)));
        REQUIRE(original != originals.end());
        QJsonObject got = a.toJson();
        QJsonObject want = original->second.toJson();
        for (QJsonObject* json : {&got, &want})
            for (const char* key : {"id", "nm", "page", "inReplyTo", "popup", "parent", "hasAppearance"}) json->remove(QLatin1String(key));
        INFO(QJsonDocument(got).toJson().toStdString());
        CHECK(got == want);
        CHECK(a.nm != original->second.nm);
        CHECK(a.hasAppearance);   // drawn by our builder
        ++compared;
    }
    CHECK(compared == 3);
    QGuiApplication::clipboard()->clear();
}

TEST_CASE("a pushpin and a sound copied from a file are pasted with their bytes", "[exchangeproduct][A-81][I-21][D6-22]")
{
    // S6 guide I-21: the model holds what a file attachment and a sound SAY, their bytes only for ones RawPage made -- so
    // a copy of ones READ from the file carried no bytes, and the paste was refused by key (`error.attachment.noData`,
    // `error.sound.noData`). The copy now reads them from the document first, as an export does.
    Product product;
    product.open("ordinary/annots-every-type.pdf");
    std::vector<AnnotationId> chosen;
    for (const Annotation& a : product.store().all())
        if (a.subtype == AnnotationSubtype::FileAttachment || a.subtype == AnnotationSubtype::Sound) chosen.push_back(a.id);
    REQUIRE(chosen.size() == 2);
    product.overlay()->select(chosen);
    REQUIRE(product.root->services().actions.dispatch(core::ActionId::EditCopy, core::KeyContext::Viewer));

    product.open("ordinary/text-a4.pdf");
    const std::size_t before = product.document()->journal()->position();
    REQUIRE(product.root->services().actions.dispatch(core::ActionId::EditPaste, core::KeyContext::Viewer));
    CHECK(product.document()->journal()->position() == before + 1);
    product.settle();

    auto fresh = saveAndReopen(product);
    const QJsonObject wanted = label("annots-every-type.json");
    QString soundSha, fileSha;
    for (const QJsonValue page : wanted.value(QStringLiteral("pages")).toArray()) {
        for (const QJsonValue entry : page.toObject().value(QStringLiteral("annots")).toArray()) {
            const QJsonObject a = entry.toObject();
            if (a.contains(QStringLiteral("sound"))) soundSha = a.value(QStringLiteral("sound")).toObject().value(QStringLiteral("sha256")).toString();
            if (a.contains(QStringLiteral("attachment")))
                fileSha = a.value(QStringLiteral("attachment")).toObject().value(QStringLiteral("sha256")).toString();
        }
    }
    REQUIRE_FALSE(soundSha.isEmpty());
    REQUIRE_FALSE(fileSha.isEmpty());
    int pasted = 0;
    for (const Annotation& a : fresh->store->all()) {
        if (a.subtype == AnnotationSubtype::Sound) {
            ++pasted;
            const core::Result<QByteArray> samples = fresh->backend.soundSamples(fresh->handle, a.id);
            REQUIRE(samples);
            CHECK(QString::fromLatin1(QCryptographicHash::hash(samples.value(), QCryptographicHash::Sha256).toHex()) == soundSha);
        }
        if (a.subtype == AnnotationSubtype::FileAttachment) {
            ++pasted;
            const core::Result<QByteArray> bytes = fresh->backend.attachmentData(fresh->handle, core::AttachmentRef::ofAnnotation(a.id));
            REQUIRE(bytes);
            CHECK(QString::fromLatin1(QCryptographicHash::hash(bytes.value(), QCryptographicHash::Sha256).toHex()) == fileSha);
        }
    }
    CHECK(pasted == 2);
    QGuiApplication::clipboard()->clear();
}
