// tst_attachments (render.) -- the files a document carries, through the SHIPPED backend. Stage 6 T11, decision
// D6-17, guide cases A-82, A-84 and A-85's backend half (`Stage6-Annotations-Testing-Guide.md` §5.14).
//
// The engine is linked here: the listing is held to PDFium's own (the attachment search units, D5-12), an extract
// is written by `pdf/DocumentBackendImpl` through `pdf/ops/AttachmentFiles`, and an attached PDF is opened from its
// bytes by the engine. WHAT THIS DOES NOT COVER (convention 19): the pane, the question asked before a non-PDF
// opens, the session folder and the shell -- `tests/ui/tst_attachments.cpp` and the composition case.

#include <catch2/catch_test_macros.hpp>

#include "core/backend.h"
#include "core/doc/AnnotationStore.h"
#include "core/doc/Attachments.h"
#include "core/doc/DocumentId.h"
#include "core/doc/PageTree.h"
#include "core/doc/Permissions.h"
#include "core/text/SearchUnit.h"
#include "pdf/DocumentBackendImpl.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <map>
#include <set>
#include <string>

using namespace rawpage;
using core::AttachmentRef;

namespace {

QString corpusFile(const char* relative)
{
    return QStringLiteral(RAWPAGE_CORPUS_DIR "/") + QString::fromUtf8(relative);
}

QByteArray readAll(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

QJsonObject label()
{
    return QJsonDocument::fromJson(readAll(corpusFile("labels/annots-attachments.json"))).object();
}

std::string sha256(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

// Every file under `root`, by path, with its size and modification time.
std::map<QString, std::pair<qint64, qint64>> census(const QString& root)
{
    std::map<QString, std::pair<qint64, qint64>> out;
    if (root.isEmpty() || !QFileInfo(root).isDir()) return out;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        out[info.absoluteFilePath()] = {info.size(), info.lastModified().toMSecsSinceEpoch()};
    }
    return out;
}

// The files under `root` changed or added since `before` whose names hold one of `names` -- a copy of an
// attachment that landed where it should not have -- outside `except`, the scratch folder the extracts were asked for
// (which a `QTemporaryDir` puts under the temporary folder itself). Narrowed to the names on purpose: other suites run in parallel
// and write into the temporary folder, and their files are not this case's subject.
QStringList landed(const QString& root, const std::map<QString, std::pair<qint64, qint64>>& before, const QStringList& names,
                   const QString& except)
{
    QStringList out;
    const QString skipped = QFileInfo(except).absoluteFilePath() + QLatin1Char('/');
    for (const auto& [path, stamp] : census(root)) {
        if (!except.isEmpty() && path.startsWith(skipped, Qt::CaseInsensitive)) continue;   // the extracts themselves
        const auto was = before.find(path);
        if (was != before.end() && was->second == stamp) continue;
        for (const QString& name : names)
            if (QFileInfo(path).fileName().contains(QFileInfo(name).completeBaseName(), Qt::CaseInsensitive)) out << path;
    }
    return out;
}

} // namespace

TEST_CASE("the shipped backend lists what the label lists and agrees with the engine", "[render][attachments][A-82]")
{
    const QJsonObject wanted = label();
    pdf::DocumentBackendImpl backend;
    auto handle = backend.open(corpusFile("ordinary/attachments.pdf"), nullptr);
    REQUIRE(handle);
    const core::Attachments* attachments = backend.attachments(handle.value());
    REQUIRE(attachments != nullptr);
    REQUIRE(attachments->status());

    const QJsonArray files = wanted.value(QStringLiteral("documentFiles")).toArray();
    REQUIRE(attachments->size() == static_cast<std::size_t>(files.size()));
    std::vector<QString> listed;
    for (qsizetype i = 0; i < files.size(); ++i) {
        const QJsonObject want = files.at(i).toObject();
        const core::DocumentAttachment& got = attachments->files()[static_cast<std::size_t>(i)];
        INFO(got.key.toStdString());
        CHECK(got.file.name == want.value(QStringLiteral("name")).toString());
        CHECK(got.file.description == want.value(QStringLiteral("description")).toString());
        CHECK(got.file.size == want.value(QStringLiteral("size")).toInteger());
        CHECK(got.file.mime == want.value(QStringLiteral("mime")).toString());
        listed.push_back(got.file.name);
    }

    // THE ENGINE'S LIST, which the attachment search units are read from (D5-12): the same names, in the same order --
    // the model the pane shows is the file the engine reads, not a second opinion of it.
    std::vector<core::text::SearchUnit> units;
    REQUIRE(backend.searchUnits(handle.value(), core::text::kAttachmentsPart, units));
    std::vector<QString> engine;
    for (const core::text::SearchUnit& unit : units) {
        if (unit.field == core::text::SearchUnit::Field::AttachmentName) engine.push_back(unit.text);
    }
    CHECK(engine == listed);

    // The annotation's file is the annotation's.
    const core::AnnotationStore* store = backend.annotations(handle.value());
    REQUIRE(store != nullptr);
    int pins = 0;
    for (const core::Annotation& a : store->all()) {
        if (a.subtype != core::AnnotationSubtype::FileAttachment) continue;
        ++pins;
        REQUIRE(a.attachment);
        CHECK(a.attachment->name == QStringLiteral("measurements.csv"));
    }
    CHECK(pins == 1);
}

TEST_CASE("extract writes each attachment's bytes and nothing else", "[render][attachments][A-84]")
{
    const QJsonObject wanted = label();
    pdf::DocumentBackendImpl backend;
    auto handle = backend.open(corpusFile("ordinary/attachments.pdf"), nullptr);
    REQUIRE(handle);
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    // Every attachment, the pushpin's included.
    std::vector<std::pair<AttachmentRef, QJsonObject>> all;
    for (const QJsonValue file : wanted.value(QStringLiteral("documentFiles")).toArray())
        all.emplace_back(AttachmentRef::ofDocument(file.toObject().value(QStringLiteral("name")).toString()), file.toObject());
    const QJsonObject pin = wanted.value(QStringLiteral("pages")).toArray().at(0).toObject()
                                .value(QStringLiteral("annots")).toArray().at(0).toObject();
    all.emplace_back(AttachmentRef::ofAnnotation(core::AnnotationId::ofObject(pin.value(QStringLiteral("object")).toInteger())),
                     pin.value(QStringLiteral("attachment")).toObject());

    QStringList names;
    for (const auto& [ref, want] : all) names << want.value(QStringLiteral("name")).toString();
    const QString temp = QDir::tempPath();
    const QString profile = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("RawPage"));
    const auto tempBefore = census(temp);
    const auto profileBefore = census(profile);
    const auto scratchBefore = census(scratch.path());
    REQUIRE(scratchBefore.empty());

    std::set<QString> written;
    for (const auto& [ref, want] : all) {
        const QString name = want.value(QStringLiteral("name")).toString();
        INFO(name.toStdString());
        const QString target = QDir(scratch.path()).filePath(QStringLiteral("out/") + name);   // a folder the extract makes
        REQUIRE(backend.extractAttachment(handle.value(), ref, target, false));
        CHECK(sha256(readAll(target)) == want.value(QStringLiteral("sha256")).toString().toStdString());
        written.insert(QFileInfo(target).absoluteFilePath());
        // A second extract to the same path is refused unless asked to replace, and the file stays as it was.
        auto again = backend.extractAttachment(handle.value(), ref, target, false);
        REQUIRE_FALSE(again);
        CHECK(keyOf(again.error()) == "error.attachment.exists");
        REQUIRE(backend.extractAttachment(handle.value(), ref, target, true));
        CHECK(sha256(readAll(target)) == want.value(QStringLiteral("sha256")).toString().toStdString());
    }

    // NOTHING ELSE: the scratch folder holds the four files and no temporary QSaveFile left behind; and no copy of an
    // attachment landed in the temporary folder or in RawPage's own (the bound is in `landed`).
    std::set<QString> after;
    for (const auto& [path, stamp] : census(scratch.path())) after.insert(path);
    CHECK(after == written);
    const QStringList strayTemp = landed(temp, tempBefore, names, scratch.path());
    const QStringList strayProfile = landed(profile, profileBefore, names, scratch.path());
    INFO("landed: " << strayTemp.join(QStringLiteral(", ")).toStdString() << " | " << strayProfile.join(QStringLiteral(", ")).toStdString());
    CHECK(strayTemp.isEmpty());
    CHECK(strayProfile.isEmpty());

    // THE CONTROL: an attachment the document does not hold writes nothing.
    const QString nowhere = QDir(scratch.path()).filePath(QStringLiteral("nothing.txt"));
    auto missing = backend.extractAttachment(handle.value(), AttachmentRef::ofDocument(QStringLiteral("nothing.txt")), nowhere, false);
    REQUIRE_FALSE(missing);
    CHECK(keyOf(missing.error()) == "error.attachment.unknown");
    CHECK_FALSE(QFileInfo::exists(nowhere));
}

TEST_CASE("an attached PDF opens from its bytes read-only and a Save As writes it unchanged", "[render][attachments][A-85]")
{
    pdf::DocumentBackendImpl backend;
    auto handle = backend.open(corpusFile("ordinary/attachments.pdf"), nullptr);
    REQUIRE(handle);
    auto bytes = backend.attachmentData(handle.value(), AttachmentRef::ofDocument(QStringLiteral("appendix.pdf")));
    REQUIRE(bytes);
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    auto copy = backend.openBytes(bytes.value(), QStringLiteral("appendix.pdf"), nullptr);
    REQUIRE(copy);
    REQUIRE(backend.pages(copy.value()) != nullptr);
    CHECK(backend.pages(copy.value())->count() == 1);
    // NO FILE STANDS BEHIND IT, and nothing was written to open it.
    REQUIRE(backend.documentId(copy.value()) != nullptr);
    CHECK(backend.documentId(copy.value())->path.isEmpty());
    CHECK(census(scratch.path()).empty());
    // NEVER EDITABLE, by key -- and its sink kept, so a Save As can write it.
    REQUIRE(backend.permissions(copy.value()) != nullptr);
    CHECK_FALSE(backend.permissions(copy.value())->canEditPages());
    CHECK(backend.permissions(copy.value())->refusalKey() == QLatin1String("error.edit.attachmentCopy"));
    REQUIRE(backend.mutationSink(copy.value()) != nullptr);
    const QString target = QDir(scratch.path()).filePath(QStringLiteral("appendix-saved.pdf"));
    REQUIRE(backend.writeRevision(copy.value(), target));
    CHECK(readAll(target) == bytes.value());   // a Save As of an unchanged document is a copy

    // THE CONTROL: bytes that are not a PDF are refused by the engine, by key.
    auto text = backend.attachmentData(handle.value(), AttachmentRef::ofDocument(QStringLiteral("readme.txt")));
    REQUIRE(text);
    auto refused = backend.openBytes(text.value(), QStringLiteral("readme.txt"), nullptr);
    REQUIRE_FALSE(refused);
    CHECK(keyOf(refused.error()) == "error.open.notPdf");
}
