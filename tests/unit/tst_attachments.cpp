// tst_attachments -- the files a document carries, below the widgets. Stage 6 T11, decision D6-17
// (`Stage6-Annotations-Plan.md` §3), guide cases A-82, A-83, A-84's headless half and A-86
// (`Stage6-Annotations-Testing-Guide.md` §5.14).
//
// WHAT THIS DOES NOT COVER, said where it matters (convention 19): every reading here is the FIRST-PARTY reader's,
// through `tests/unit/CosBackend.h` -- the engine is not linked. Whether PDFium lists the same files is
// `tests/render/tst_attachments.cpp`'s; whether the pane lists them, and the open, is the widget suites'.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "TinyPdf.h"
#include "core/cmd/CommandRegistry.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/cmd/commands/AttachmentCommands.h"
#include "core/doc/Attachments.h"
#include "core/doc/Stamps.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/AppearanceStreamBuilder.h"
#include "pdf/ops/EmbeddedFileReader.h"
#include "pdf/ops/RevisionWriter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>

#include <memory>
#include <set>
#include <string>

using namespace rawpage;
using core::AttachmentRef;
using core::DocumentAttachment;
using core::EmbeddedFile;

namespace {

constexpr std::int64_t kNow = 1791590400;   // 2026-10-09T00:00:00Z

QJsonObject label(const char* relative)
{
    return QJsonDocument::fromJson(tests::readAllBytes(tests::corpusFile(relative))).object();
}

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

QByteArray randomBytes(qsizetype size, quint32 seed)
{
    QByteArray out(size, Qt::Uninitialized);
    QRandomGenerator generator(seed);
    for (qsizetype i = 0; i < size; ++i) out[i] = static_cast<char>(generator.generate() & 0xFF);
    return out;
}

DocumentAttachment attached(const QString& key, const QByteArray& bytes, const QString& description = {})
{
    return DocumentAttachment{key, core::newEmbeddedFile(key, bytes, description, kNow)};
}

// The session's document saved as `name` in its folder, and that file opened fresh through the first-party backend.
struct Reopened {
    tests::CosBackend backend;
    core::DocumentHandle handle;
    const core::Attachments* attachments = nullptr;
};

std::unique_ptr<Reopened> saveAndReopen(tests::SaveSession& session, const QString& name)
{
    const QString target = session.directory.filePath(name);
    auto saved = session.coordinator->saveAs(*session.journal, target);
    if (!saved) UNSCOPED_INFO("save: " << keyOf(saved.error()) << " " << saved.error().context.toStdString());
    REQUIRE(saved);
    REQUIRE(saved.value().kind == core::SaveOutcome::Kind::Saved);
    auto reopened = std::make_unique<Reopened>();
    auto handle = reopened->backend.open(target, nullptr);
    REQUIRE(handle);
    reopened->handle = handle.value();
    reopened->attachments = reopened->backend.attachments(handle.value());
    REQUIRE(reopened->attachments != nullptr);
    REQUIRE(reopened->attachments->status());
    return reopened;
}

std::vector<QString> keysOf(const core::Attachments& attachments)
{
    std::vector<QString> keys;
    for (const DocumentAttachment& file : attachments.files()) keys.push_back(file.key);
    return keys;
}

bool emptyDelta(tests::SaveSession& session)
{
    auto delta = session.sink()->delta();
    REQUIRE(delta);
    return delta.value().isEmpty();
}

} // namespace

// ==========================================================================================
// The model (core/doc/Attachments)
// ==========================================================================================

TEST_CASE("an attached file is keyed by its name made unique", "[attachments][D6-17]")
{
    core::Attachments held = core::Attachments::fromReading({attached(QStringLiteral("readme.txt"), "a")});
    CHECK(held.uniqueKey(QStringLiteral("notes.txt")) == QStringLiteral("notes.txt"));
    CHECK(held.uniqueKey(QStringLiteral("readme.txt")) == QStringLiteral("readme (2).txt"));
    REQUIRE(held.insert(attached(QStringLiteral("readme (2).txt"), "b")));
    CHECK(held.uniqueKey(QStringLiteral("readme.txt")) == QStringLiteral("readme (3).txt"));
    CHECK(held.uniqueKey(QStringLiteral("Makefile")) == QStringLiteral("Makefile"));
    REQUIRE(held.insert(attached(QStringLiteral("Makefile"), "c")));
    CHECK(held.uniqueKey(QStringLiteral("Makefile")) == QStringLiteral("Makefile (2)"));

    // A key already held, an empty one, and any change to a store that was never read are refused, changing nothing.
    const QByteArray before = held.digest();
    CHECK_FALSE(held.insert(attached(QStringLiteral("Makefile"), "d")));
    CHECK_FALSE(held.insert(DocumentAttachment{QString(), core::newEmbeddedFile(QStringLiteral("x"), "x", {}, kNow)}));
    CHECK_FALSE(held.remove(QStringLiteral("nothing")));
    CHECK(held.digest() == before);
    core::Attachments unread;
    CHECK_FALSE(unread.status());
    CHECK_FALSE(unread.insert(attached(QStringLiteral("a"), "a")));
}

TEST_CASE("the files are kept in the order of the bytes their keys are written as", "[attachments][D6-17]")
{
    core::Attachments held = core::Attachments::fromReading({});
    for (const char* key : {"b.txt", "a.txt", "Z.txt", "c.txt"}) REQUIRE(held.insert(attached(QString::fromLatin1(key), "x")));
    // ASCII sorts by its bytes -- upper case before lower -- and a key outside ASCII is UTF-16BE behind FE FF, so after them.
    REQUIRE(held.insert(attached(QStringLiteral("\u00E9t\u00E9.txt"), "y")));
    CHECK(keysOf(held) == std::vector<QString>{QStringLiteral("Z.txt"), QStringLiteral("a.txt"), QStringLiteral("b.txt"),
                                               QStringLiteral("c.txt"), QStringLiteral("\u00E9t\u00E9.txt")});
    CHECK(core::nameTreeKeyBytes(QStringLiteral("a.txt")) == QByteArray("a.txt"));
    CHECK(core::nameTreeKeyBytes(QStringLiteral("\u00E9")) == QByteArray("\xFE\xFF\x00\xE9", 4));
    // Removed and put back, a file goes where it was.
    auto removed = held.remove(QStringLiteral("b.txt"));
    REQUIRE(removed);
    REQUIRE(held.insert(removed.value()));
    CHECK(keysOf(held)[2] == QStringLiteral("b.txt"));
}

TEST_CASE("an executable is told by its last extension as Windows reads the name", "[attachments][D6-17]")
{
    for (const char* name : {"setup.exe", "SETUP.EXE", "report.pdf.exe", "setup.exe.", "setup.exe  ", "run.bat", "x.cmd",
                             "a.vbs", "a.js", "a.ps1", "a.msi", "a.lnk", "a.scr", "a.hta", "a.jar", "folder/setup.exe",
                             "folder\\setup.com"}) {
        INFO(name);
        CHECK(core::isExecutableName(QString::fromLatin1(name)));
    }
    for (const char* name : {"readme.txt", "appendix.pdf", "setup.exe.txt", "exe", "Makefile", "measurements.csv",
                             "photo.jpeg", "archive.zip", ".exe-notes"}) {
        INFO(name);
        CHECK_FALSE(core::isExecutableName(QString::fromLatin1(name)));
    }
    // The corpus's own: the label names which of its files are executable by name.
    const QJsonObject wanted = label("labels/annots-attachments.json");
    std::set<QString> executable;
    for (const QJsonValue name : wanted.value(QStringLiteral("executableNames")).toArray()) executable.insert(name.toString());
    for (const QJsonValue file : wanted.value(QStringLiteral("documentFiles")).toArray()) {
        const QString name = file.toObject().value(QStringLiteral("name")).toString();
        INFO(name.toStdString());
        CHECK(core::isExecutableName(name) == (executable.count(name) != 0));
    }
}

TEST_CASE("an attachment's name on disk is one file in one folder", "[attachments][D6-17]")
{
    CHECK(core::safeFileName(QStringLiteral("readme.txt")) == QStringLiteral("readme.txt"));
    CHECK(core::safeFileName(QStringLiteral("..\\..\\Windows\\win.ini")) == QStringLiteral("win.ini"));
    CHECK(core::safeFileName(QStringLiteral("../../etc/passwd")) == QStringLiteral("passwd"));
    CHECK(core::safeFileName(QStringLiteral("a:b*c?d\"e<f>g|h")) == QStringLiteral("a_b_c_d_e_f_g_h"));
    CHECK(core::safeFileName(QStringLiteral("setup.exe. . ")) == QStringLiteral("setup.exe"));
    CHECK(core::safeFileName(QStringLiteral("CON.txt")) == QStringLiteral("_CON.txt"));
    CHECK(core::safeFileName(QStringLiteral("lpt1")) == QStringLiteral("_lpt1"));
    CHECK(core::safeFileName(QStringLiteral("COM10.txt")) == QStringLiteral("COM10.txt"));
    CHECK(core::safeFileName(QStringLiteral("tab\there")) == QStringLiteral("tab_here"));
    CHECK(core::safeFileName(QString()) == QStringLiteral("attachment"));
    CHECK(core::safeFileName(QStringLiteral("...")) == QStringLiteral("attachment"));
    const QString longName = QString(300, QLatin1Char('a')) + QStringLiteral(".csv");
    CHECK(core::safeFileName(longName).size() == 200);
    CHECK(core::safeFileName(longName).endsWith(QStringLiteral(".csv")));
}

TEST_CASE("an embedded file round-trips through its JSON and a hostile one is refused", "[attachments][D6-17]")
{
    const EmbeddedFile file = core::newEmbeddedFile(QStringLiteral("C:/work/Zo\u00EB.csv"), QByteArray("a,b\n1,2\n"),
                                                    QStringLiteral("t\u00E4ble"), kNow);
    CHECK(file.name == QStringLiteral("Zo\u00EB.csv"));   // the folder dropped
    CHECK(file.mime == QStringLiteral("text/csv"));
    CHECK(file.size == 8);
    CHECK(file.carried);
    CHECK(file.checksum == QCryptographicHash::hash(QByteArray("a,b\n1,2\n"), QCryptographicHash::Md5));
    CHECK(file.created.utcSeconds == kNow);
    CHECK(file.modified == file.created);
    auto back = EmbeddedFile::fromJson(file.toJson());
    REQUIRE(back);
    CHECK(back.value() == file);

    // An empty file is a file: carried, and zero long.
    const EmbeddedFile empty = core::newEmbeddedFile(QStringLiteral("empty.bin"), QByteArray(), QString(), kNow);
    auto emptyBack = EmbeddedFile::fromJson(empty.toJson());
    REQUIRE(emptyBack);
    CHECK(emptyBack.value().carried);
    CHECK(emptyBack.value().data.isEmpty());

    // A file READ from a document carries no bytes, and says so.
    EmbeddedFile read = file;
    read.carried = false;
    read.data.clear();
    auto readBack = EmbeddedFile::fromJson(read.toJson());
    REQUIRE(readBack);
    CHECK_FALSE(readBack.value().carried);
    CHECK(readBack.value() == read);
}

TEST_CASE("a pushpin and a paper clip are drawn and an icon this build does not draw is refused by name",
          "[attachments][appearance][D6-17]")
{
    core::Annotation pin = core::newFileAttachment(0, core::PdfRect{core::PdfPoint{100, 640}, core::PdfSize{16, 24}},
                                                   core::newEmbeddedFile(QStringLiteral("a.txt"), "a", {}, kNow),
                                                   {0.13, 0.4, 0.86}, QStringLiteral("Ana"), kNow);
    CHECK(pin.icon == QStringLiteral("PushPin"));
    CHECK(pin.contents == QStringLiteral("a.txt"));
    REQUIRE(pdf::ops::AppearanceStreamBuilder::normal(pin));
    pin.icon = QStringLiteral("Paperclip");
    REQUIRE(pdf::ops::AppearanceStreamBuilder::normal(pin));
    for (const char* icon : {"Graph", "Tag", "Bogus"}) {
        INFO(icon);
        pin.icon = QString::fromLatin1(icon);
        auto refused = pdf::ops::AppearanceStreamBuilder::normal(pin);
        REQUIRE_FALSE(refused);
        CHECK(keyOf(refused.error()) == "error.pdf.appearanceUnsupported");
    }
}

// ==========================================================================================
// A-82 / A-84 -- the first-party reading, against the label
// ==========================================================================================

TEST_CASE("the reading lists every attachment the label lists with its name description and size",
          "[attachments][A-82][A-84]")
{
    const QJsonObject wanted = label("labels/annots-attachments.json");
    tests::CosBackend backend;
    auto handle = backend.open(tests::corpusFile("ordinary/attachments.pdf"), nullptr);
    REQUIRE(handle);
    const core::Attachments* attachments = backend.attachments(handle.value());
    REQUIRE(attachments != nullptr);
    REQUIRE(attachments->status());

    const QJsonArray files = wanted.value(QStringLiteral("documentFiles")).toArray();
    REQUIRE(attachments->size() == static_cast<std::size_t>(files.size()));
    for (qsizetype i = 0; i < files.size(); ++i) {
        const QJsonObject want = files.at(i).toObject();
        const DocumentAttachment& got = attachments->files()[static_cast<std::size_t>(i)];
        INFO(want.value(QStringLiteral("name")).toString().toStdString());
        CHECK(got.key == want.value(QStringLiteral("name")).toString());
        CHECK(got.file.name == want.value(QStringLiteral("name")).toString());
        CHECK(got.file.description == want.value(QStringLiteral("description")).toString());
        CHECK(got.file.mime == want.value(QStringLiteral("mime")).toString());
        CHECK(got.file.size == want.value(QStringLiteral("size")).toInteger());
        CHECK(got.file.modified.raw == want.value(QStringLiteral("modified")).toString());
        CHECK(got.file.checksum.size() == 16);
        CHECK_FALSE(got.file.carried);   // read, never copied into the model
        // A-84's bytes: each file's decoded stream is the file the generator embedded.
        auto bytes = backend.attachmentData(handle.value(), AttachmentRef::ofDocument(got.key));
        REQUIRE(bytes);
        CHECK(tests::sha256(bytes.value()) == want.value(QStringLiteral("sha256")).toString().toStdString());
        CHECK(QCryptographicHash::hash(bytes.value(), QCryptographicHash::Md5) == got.file.checksum);
    }

    // The FileAttachment annotation's file, likewise.
    const QJsonObject annotation = wanted.value(QStringLiteral("pages")).toArray().at(0).toObject()
                                       .value(QStringLiteral("annots")).toArray().at(0).toObject();
    const QJsonObject want = annotation.value(QStringLiteral("attachment")).toObject();
    const core::AnnotationStore* store = backend.annotations(handle.value());
    REQUIRE(store != nullptr);
    const std::optional<std::size_t> at = store->findObject(annotation.value(QStringLiteral("object")).toInteger());
    REQUIRE(at);
    const core::Annotation& pin = store->at(*at);
    REQUIRE(pin.attachment);
    CHECK(pin.attachment->name == want.value(QStringLiteral("name")).toString());
    CHECK(pin.attachment->description == want.value(QStringLiteral("description")).toString());
    CHECK(pin.attachment->mime == want.value(QStringLiteral("mime")).toString());
    CHECK(pin.attachment->size == want.value(QStringLiteral("size")).toInteger());
    auto bytes = backend.attachmentData(handle.value(), AttachmentRef::ofAnnotation(pin.id));
    REQUIRE(bytes);
    CHECK(tests::sha256(bytes.value()) == want.value(QStringLiteral("sha256")).toString().toStdString());

    // THE CONTROLS: a key the document does not hold, and the label's control file, which holds none.
    auto missing = backend.attachmentData(handle.value(), AttachmentRef::ofDocument(QStringLiteral("nothing.txt")));
    REQUIRE_FALSE(missing);
    CHECK(keyOf(missing.error()) == "error.attachment.unknown");
    auto control = backend.open(tests::corpusFile("ordinary/text-a4.pdf"), nullptr);
    REQUIRE(control);
    REQUIRE(backend.attachments(control.value()) != nullptr);
    CHECK(backend.attachments(control.value())->status());
    CHECK(backend.attachments(control.value())->size() == 0);
}

// ==========================================================================================
// A-83 -- adding and deleting are commands, saved and undone exactly
// ==========================================================================================

TEST_CASE("an attached and a deleted file are saved and read back and each undo is exact",
          "[attachments][A-83][D6-17]")
{
    auto session = tests::openCopy("ordinary/attachments.pdf");
    REQUIRE(session);
    core::CommandJournal& journal = *session->journal;
    const QByteArray original = journal.document().modelHash();
    const QByteArray added = randomBytes(70000, 11);   // larger than one side-file threshold, for the line below

    // Attached, and before a save undone: nothing to write.
    REQUIRE(journal.run(std::make_unique<core::AddAttachmentCommand>(
        std::vector<DocumentAttachment>{attached(QStringLiteral("added.bin"), added, QStringLiteral("random"))})));
    CHECK_FALSE(emptyDelta(*session));
    REQUIRE(journal.undo());
    CHECK(journal.document().modelHash() == original);
    CHECK(emptyDelta(*session));
    REQUIRE(journal.redo());

    // Deleted -- the executable-named one -- and the two saved.
    REQUIRE(journal.run(std::make_unique<core::DeleteAttachmentCommand>(std::vector<QString>{QStringLiteral("setup.exe")})));
    auto first = saveAndReopen(*session, QStringLiteral("saved-1.pdf"));
    CHECK(keysOf(*first->attachments) == std::vector<QString>{QStringLiteral("added.bin"), QStringLiteral("appendix.pdf"),
                                                              QStringLiteral("readme.txt")});
    const auto at = first->attachments->find(QStringLiteral("added.bin"));
    REQUIRE(at);
    const EmbeddedFile& file = first->attachments->files()[*at].file;
    CHECK(file.description == QStringLiteral("random"));
    CHECK(file.size == added.size());
    CHECK(file.mime == QStringLiteral("application/octet-stream"));
    CHECK(file.checksum == QCryptographicHash::hash(added, QCryptographicHash::Md5));
    auto bytes = first->backend.attachmentData(first->handle, AttachmentRef::ofDocument(QStringLiteral("added.bin")));
    REQUIRE(bytes);
    CHECK(bytes.value() == added);
    // The files the session did not touch read back as they were.
    auto readme = first->backend.attachmentData(first->handle, AttachmentRef::ofDocument(QStringLiteral("readme.txt")));
    REQUIRE(readme);
    CHECK(readme.value() == QByteArray("Notes for reviewers: the appendix holds the raw tables.\n"));

    // AFTER THE SAVE, the delete undone: the very file specification listed again -- the original's object -- and saved.
    REQUIRE(journal.undo());
    CHECK(journal.document().attachments().find(QStringLiteral("setup.exe")).has_value());
    auto second = saveAndReopen(*session, QStringLiteral("saved-2.pdf"));
    CHECK(keysOf(*second->attachments) == std::vector<QString>{QStringLiteral("added.bin"), QStringLiteral("appendix.pdf"),
                                                               QStringLiteral("readme.txt"), QStringLiteral("setup.exe")});
    tests::CosBackend originalBackend;
    auto originalHandle = originalBackend.open(tests::corpusFile("ordinary/attachments.pdf"), nullptr);
    REQUIRE(originalHandle);
    auto setupNow = second->backend.attachmentData(second->handle, AttachmentRef::ofDocument(QStringLiteral("setup.exe")));
    auto setupThen = originalBackend.attachmentData(originalHandle.value(), AttachmentRef::ofDocument(QStringLiteral("setup.exe")));
    REQUIRE(setupNow);
    REQUIRE(setupThen);
    CHECK(setupNow.value() == setupThen.value());
    {
        // The pair listed is the pair the file held: the same reference to the same specification object.
        QFile saved(session->directory.filePath(QStringLiteral("saved-2.pdf")));
        REQUIRE(saved.open(QIODevice::ReadOnly));
        const QByteArray bytesSaved = saved.readAll();
        auto table = pdf::cos::openWithRepair(std::string_view(bytesSaved.constData(), static_cast<std::size_t>(bytesSaved.size())));
        REQUIRE(table);
        auto originalTable = pdf::cos::openWithRepair(std::string_view(session->openedBytes.constData(),
                                                                       static_cast<std::size_t>(session->openedBytes.size())));
        REQUIRE(originalTable);
        const auto valueOf = [](pdf::cos::XrefTable& t, const char* key) {
            for (const pdf::ops::NameTreeEntry& entry : pdf::ops::EmbeddedFileReader::entries(t))
                if (entry.key.isString() && entry.key.text() == key) return entry.value;
            return pdf::cos::CosObject::makeNull();
        };
        CHECK(valueOf(table.value().table, "setup.exe") == valueOf(originalTable.value().table, "setup.exe"));
        CHECK(valueOf(table.value().table, "setup.exe").isReference());
    }

    // And the attach undone after the saves: the file is gone from the next one.
    REQUIRE(journal.undo());
    CHECK(journal.document().modelHash() == original);
    auto third = saveAndReopen(*session, QStringLiteral("saved-3.pdf"));
    CHECK(keysOf(*third->attachments) == std::vector<QString>{QStringLiteral("appendix.pdf"), QStringLiteral("readme.txt"),
                                                              QStringLiteral("setup.exe")});
}

TEST_CASE("a document with no attachment gains a tree and one that loses its last loses the key",
          "[attachments][A-83][D6-17]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    REQUIRE(session->journal->document().attachments().size() == 0);
    REQUIRE(session->journal->run(std::make_unique<core::AddAttachmentCommand>(
        std::vector<DocumentAttachment>{attached(QStringLiteral("only.txt"), QByteArray("only\n"))})));
    auto first = saveAndReopen(*session, QStringLiteral("one.pdf"));
    CHECK(keysOf(*first->attachments) == std::vector<QString>{QStringLiteral("only.txt")});

    REQUIRE(session->journal->run(std::make_unique<core::DeleteAttachmentCommand>(std::vector<QString>{QStringLiteral("only.txt")})));
    auto second = saveAndReopen(*session, QStringLiteral("none.pdf"));
    CHECK(second->attachments->size() == 0);
    QFile saved(session->directory.filePath(QStringLiteral("none.pdf")));
    REQUIRE(saved.open(QIODevice::ReadOnly));
    const QByteArray bytes = saved.readAll();
    auto table = pdf::cos::openWithRepair(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    REQUIRE(table);
    CHECK(pdf::ops::EmbeddedFileReader::entries(table.value().table).empty());
}

TEST_CASE("the save's verify refuses an attached file whose stream is not the bytes compressed when it was attached",
          "[attachments][A-83][verify]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    REQUIRE(session->journal->run(std::make_unique<core::AddAttachmentCommand>(
        std::vector<DocumentAttachment>{attached(QStringLiteral("checked.bin"), randomBytes(4096, 3))})));
    // One byte of the new embedded file's stream flipped after compose -- the writer's own reader still parses the file.
    pdf::ops::RevisionWriter::setComposeFaultForTesting([](QByteArray& bytes) {
        // The STREAM's `/Type /EmbeddedFile` -- not the names dictionary's `/EmbeddedFiles` key, written after it.
        qsizetype type = -1;
        for (qsizetype at = bytes.indexOf("/EmbeddedFile"); at >= 0; at = bytes.indexOf("/EmbeddedFile", at + 1)) {
            if (at + 13 < bytes.size() && bytes[at + 13] != 's') type = at;
        }
        REQUIRE(type > 0);
        // Inside its data for certain: twenty bytes before the `endstream` that closes it (4 KiB of random bytes
        // compress to more than that). Not after the first `stream`, which `application/octet-stream` holds.
        const qsizetype end = bytes.indexOf("endstream", type);
        REQUIRE(end > type + 40);
        bytes[end - 20] = static_cast<char>(bytes[end - 20] ^ 0x5A);
    });
    const QString target = session->directory.filePath(QStringLiteral("tampered.pdf"));
    auto saved = session->coordinator->saveAs(*session->journal, target);
    pdf::ops::RevisionWriter::setComposeFaultForTesting({});
    // Refused before anything replaced anything -- OUR bytes are wrong, so no other place is offered (D3-16) -- by
    // the verify's reason, naming the tree; and no file at the target.
    REQUIRE_FALSE(saved);
    CHECK(keyOf(saved.error()) == "error.save.verifyFailed");
    CHECK(saved.error().context == QStringLiteral("attachments"));
    CHECK_FALSE(QFileInfo::exists(target));
    // THE CONTROL: with no fault, the same save is written.
    auto clean = session->coordinator->saveAs(*session->journal, target);
    REQUIRE(clean);
    CHECK(clean.value().kind == core::SaveOutcome::Kind::Saved);
}

TEST_CASE("an attachment command is refused against what the document holds and changes nothing",
          "[attachments][A-83][refuse]")
{
    auto session = tests::openCopy("ordinary/attachments.pdf");
    REQUIRE(session);
    core::CommandJournal& journal = *session->journal;
    const QByteArray hash = journal.document().modelHash();
    // A key the document holds; a file with no bytes; a delete of a key it does not hold.
    CHECK_FALSE(journal.run(std::make_unique<core::AddAttachmentCommand>(
        std::vector<DocumentAttachment>{attached(QStringLiteral("readme.txt"), "x")})));
    DocumentAttachment empty = attached(QStringLiteral("new.txt"), "x");
    empty.file.carried = false;
    empty.file.data.clear();
    CHECK_FALSE(journal.run(std::make_unique<core::AddAttachmentCommand>(std::vector<DocumentAttachment>{empty})));
    CHECK_FALSE(journal.run(std::make_unique<core::DeleteAttachmentCommand>(std::vector<QString>{QStringLiteral("nothing.txt")})));
    CHECK(journal.document().modelHash() == hash);
    CHECK_FALSE(journal.canUndo());
    CHECK(emptyDelta(*session));
}

TEST_CASE("a pushpin made with a file is saved with the file as its FS", "[attachments][A-83][D6-17]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const QByteArray bytes("sample,length_mm\n1,12.5\n");
    core::Annotation pin = core::newFileAttachment(0, core::PdfRect{core::PdfPoint{100, 600}, core::PdfSize{16, 24}},
                                                   core::newEmbeddedFile(QStringLiteral("data.csv"), bytes, {}, kNow),
                                                   {0.13, 0.4, 0.86}, QStringLiteral("Ana"), kNow);
    REQUIRE(session->journal->run(std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{pin})));
    // Before the save, the bytes are the command's.
    auto unsaved = session->backend.attachmentData(session->handle, AttachmentRef::ofAnnotation(pin.id));
    REQUIRE(unsaved);
    CHECK(unsaved.value() == bytes);

    auto reopened = saveAndReopen(*session, QStringLiteral("pinned.pdf"));
    const core::AnnotationStore* store = reopened->backend.annotations(reopened->handle);
    REQUIRE(store != nullptr);
    std::optional<core::Annotation> read;
    for (const core::Annotation& a : store->all())
        if (a.subtype == core::AnnotationSubtype::FileAttachment) read = a;
    REQUIRE(read);
    CHECK(read->nm == pin.nm);
    REQUIRE(read->attachment);
    CHECK(read->attachment->name == QStringLiteral("data.csv"));
    CHECK(read->attachment->mime == QStringLiteral("text/csv"));
    CHECK(read->attachment->size == bytes.size());
    CHECK(read->hasAppearance);
    auto saved = reopened->backend.attachmentData(reopened->handle, AttachmentRef::ofAnnotation(read->id));
    REQUIRE(saved);
    CHECK(saved.value() == bytes);
    // And through the session's own sink after the save, by the created name the model still gives it.
    auto adopted = session->backend.attachmentData(session->handle, AttachmentRef::ofAnnotation(pin.id));
    REQUIRE(adopted);
    CHECK(adopted.value() == bytes);
}

TEST_CASE("a file read with a key or a name past the bound a created one keeps is deleted moved edited and replayed",
          "[attachments][I-18]")
{
    // S6 guide I-18: `kMaxAttachmentNameLength` bounds what a command CREATES -- a file RawPage attaches, its key and
    // its name. A file READ from the document is the document's, and a producer may name it at any length: a delete
    // of its key, and a move or an edit of its pushpin, must apply and their journal lines read back.
    const QString longKey = QString(2000, QLatin1Char('k')) + QStringLiteral(".txt");
    const QString longName = QString(2000, QLatin1Char('p')) + QStringLiteral(".txt");
    REQUIRE(longKey.size() > core::kMaxAttachmentNameLength);
    tests::TinyPdf pdf;
    const int catalog = pdf.reserve();
    const int pages = pdf.reserve();
    const int page = pdf.reserve();
    const auto ref = [](int number) { return std::to_string(number) + " 0 R"; };
    const int pinData = pdf.add("<< /Type /EmbeddedFile /Length 5 >>\nstream\nhello\nendstream");
    const int pinSpec = pdf.add("<< /Type /Filespec /F (" + longName.toStdString() + ") /EF << /F " + ref(pinData) + " >> >>");
    const int pin = pdf.add("<< /Type /Annot /Subtype /FileAttachment /Rect [ 100 600 116 624 ] /Name /PushPin /C [ 0 0 1 ] /P "
                            + ref(page) + " /FS " + ref(pinSpec) + " >>");
    const int docData = pdf.add("<< /Type /EmbeddedFile /Length 5 >>\nstream\nworld\nendstream");
    const int docSpec = pdf.add("<< /Type /Filespec /F (" + longKey.toStdString() + ") /EF << /F " + ref(docData) + " >> >>");
    pdf.set(page, "<< /Type /Page /Parent " + ref(pages) + " /MediaBox [ 0 0 612 792 ] /Annots [ " + ref(pin) + " ] >>");
    pdf.set(pages, "<< /Type /Pages /Kids [ " + ref(page) + " ] /Count 1 >>");
    pdf.set(catalog, "<< /Type /Catalog /Pages " + ref(pages) + " /Names << /EmbeddedFiles << /Names [ (" + longKey.toStdString()
                         + ") " + ref(docSpec) + " ] >> >> >>");
    auto session = tests::openBuilt(QStringLiteral("long-names.pdf"), pdf.bytes(catalog));
    REQUIRE(session);
    core::CommandJournal& journal = *session->journal;
    const core::AnnotationId pinId = core::AnnotationId::ofObject(pin);
    REQUIRE(journal.document().attachments().find(longKey));
    const auto readPin = [&journal, &pinId]() {
        const std::optional<std::size_t> at = journal.document().annotations().find(pinId);
        REQUIRE(at);
        return journal.document().annotations().at(*at);
    };
    REQUIRE(readPin().attachment);
    CHECK(readPin().attachment->name == longName);   // the file's own name, whole
    CHECK_FALSE(readPin().attachment->carried);
    const auto readsBack = [](const core::Command& command) {
        auto back = core::CommandRegistry::deserialize(command.serialize());
        if (!back) UNSCOPED_INFO(command.typeId().data() << ": " << keyOf(back.error()) << " " << back.error().context.toStdString());
        return back && back.value()->serialize() == command.serialize();
    };

    // The document's file deleted by its key, and undone.
    auto remove = std::make_unique<core::DeleteAttachmentCommand>(std::vector<QString>{longKey});
    CHECK(readsBack(*remove));
    REQUIRE(journal.run(std::move(remove)));
    CHECK_FALSE(journal.document().attachments().find(longKey));
    REQUIRE(journal.undo());
    CHECK(journal.document().attachments().find(longKey));

    // Its pushpin edited, and undone.
    core::Annotation edited = readPin();
    edited.contents = QStringLiteral("edited");
    auto edit = std::make_unique<core::EditAnnotationCommand>(std::vector<core::Annotation>{edited});
    CHECK(readsBack(*edit));
    REQUIRE(journal.run(std::move(edit)));
    CHECK(readPin().contents == QStringLiteral("edited"));
    REQUIRE(journal.undo());

    // ...and moved, and saved: the file's name read back whole, from the /FS the save kept.
    auto move = std::make_unique<core::MoveAnnotationCommand>(std::vector<core::AnnotationId>{pinId}, 10, -10, kNow);
    CHECK(readsBack(*move));
    REQUIRE(journal.run(std::move(move)));
    auto reopened = saveAndReopen(*session, QStringLiteral("moved.pdf"));
    const core::AnnotationStore* store = reopened->backend.annotations(reopened->handle);
    REQUIRE(store != nullptr);
    const std::optional<std::size_t> at = store->findObject(pin);
    REQUIRE(at);
    REQUIRE(store->at(*at).attachment);
    CHECK(store->at(*at).attachment->name == longName);
    CHECK(reopened->attachments->find(longKey));

    // THE CONTROL: the bound still binds what a command CREATES -- a pushpin carrying a file named past it.
    const core::Annotation created = core::newFileAttachment(
        0, core::PdfRect{core::PdfPoint{100, 600}, core::PdfSize{16, 24}},
        core::newEmbeddedFile(longName, QByteArray("abc"), {}, kNow), {0.13, 0.4, 0.86}, QStringLiteral("Ana"), kNow);
    CHECK_FALSE(core::CommandRegistry::deserialize(core::AddAnnotationCommand(std::vector<core::Annotation>{created}).serialize()));
}

// ==========================================================================================
// A-86 -- large payloads go to side files, through the existing write (D6-23)
// ==========================================================================================

TEST_CASE("a payload over 64 KiB is a side file named by its hash and replay puts it back",
          "[attachments][A-86][D6-23][recovery]")
{
    auto session = tests::openCopy("ordinary/attachments.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    auto recovery = core::RecoveryJournal::open(root.path(), session->path, session->journal->document().id(),
                                                &session->durableFiles);
    REQUIRE(recovery->isRecoverable());
    session->journal->setRecorder(recovery.get());

    // A 5 MB attachment, an image stamp whose pixels are past the threshold, and a small file that stays inline.
    const QByteArray big = randomBytes(5 * 1024 * 1024, 5);
    REQUIRE(session->journal->run(std::make_unique<core::AddAttachmentCommand>(
        std::vector<DocumentAttachment>{attached(QStringLiteral("big.bin"), big)})));
    core::Annotation stamp = core::newAnnotation(core::AnnotationSubtype::Stamp, 0, QStringLiteral("Ana"), kNow);
    stamp.rect = core::PdfRect{core::PdfPoint{100, 100}, core::PdfSize{120, 120}};
    stamp.intent = core::stampImageIntent();
    stamp.image.encoding = core::StampImage::Encoding::Flate;
    stamp.image.width = 160;
    stamp.image.height = 160;
    stamp.image.components = 3;
    stamp.image.data = qCompress(randomBytes(160 * 160 * 3, 9), 0).mid(4);   // stored, so it stays past 64 KiB
    REQUIRE(stamp.image.data.size() > 64 * 1024);
    REQUIRE(session->journal->run(std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{stamp})));
    REQUIRE(session->journal->run(std::make_unique<core::AddAttachmentCommand>(
        std::vector<DocumentAttachment>{attached(QStringLiteral("small.txt"), QByteArray("small\n"))})));
    const QByteArray expectedModel = session->journal->document().modelHash();

    // The journal: each long payload named by hash, and each side file its hash's content.
    const QDir directory(recovery->directory());
    QFile lines(directory.filePath(QStringLiteral("journal.ndjson")));
    REQUIRE(lines.open(QIODevice::ReadOnly));
    const QByteArray text = lines.readAll();
    lines.close();
    CHECK(text.size() < 64 * 1024);   // three commands, and not one payload inline but the small file's
    const QStringList sides = directory.entryList(QStringList{QStringLiteral("*.side")}, QDir::Files);
    REQUIRE(sides.size() == 2);
    for (const QString& name : sides) {
        INFO(name.toStdString());
        QFile side(directory.filePath(name));
        REQUIRE(side.open(QIODevice::ReadOnly));
        const QByteArray content = side.readAll();
        CHECK(QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex()) + QStringLiteral(".side") == name);
        CHECK(text.contains("{\"sideFile\":\"" + name.left(64).toLatin1() + "\"}"));
    }
    CHECK(text.contains(QByteArray("small.txt")));
    CHECK(text.contains(QByteArray(QByteArray("small\n").toBase64())));   // under the threshold: inline
    CHECK_FALSE(text.contains(big.toBase64().left(100)));

    // Replayed into the file as opened: the same model, the same bytes.
    recovery.reset();
    session->journal->setRecorder(nullptr);
    const auto candidates = core::RecoveryJournal::discover(root.path());
    REQUIRE(candidates.size() == 1);
    {
        auto fresh = tests::openCopy("ordinary/attachments.pdf");
        REQUIRE(fresh);
        auto report = core::RecoveryJournal::replay(candidates.front().directory, *fresh->journal);
        REQUIRE(report);
        CHECK_FALSE(report.value().stoppedAtStep.has_value());
        CHECK(report.value().stepsApplied == 3);
        CHECK(fresh->journal->document().modelHash() == expectedModel);
        auto replayed = fresh->backend.attachmentData(fresh->handle, AttachmentRef::ofDocument(QStringLiteral("big.bin")));
        REQUIRE(replayed);
        CHECK(replayed.value() == big);

        // And a journal seeded from that replay carries the side files with it.
        QTemporaryDir second;
        REQUIRE(second.isValid());
        auto seeded = core::RecoveryJournal::open(second.path(), fresh->path, fresh->journal->document().id(),
                                                  &fresh->durableFiles, report.value().appliedLines, false,
                                                  candidates.front().directory);
        REQUIRE(seeded->isRecoverable());
        CHECK(QDir(seeded->directory()).entryList(QStringList{QStringLiteral("*.side")}, QDir::Files).size() == 2);
        seeded->discard();
        CHECK(QDir(seeded->directory()).entryList(QStringList{QStringLiteral("*.side")}, QDir::Files).isEmpty());
    }

    // THE CONTROLS. A side file whose content is not its name's hash -- one byte changed -- and replay stops at its
    // step by name rather than attach bytes nobody wrote.
    {
        const QString changed = QDir(candidates.front().directory).filePath(sides.back());
        QFile side(changed);
        REQUIRE(side.open(QIODevice::ReadWrite));
        QByteArray content = side.readAll();
        const QByteArray original = content;
        content[0] = content[0] == 'A' ? 'B' : 'A';
        REQUIRE(side.seek(0));
        REQUIRE(side.write(content) == content.size());
        side.close();
        auto corrupt = tests::openCopy("ordinary/attachments.pdf");
        REQUIRE(corrupt);
        auto stoppedAtHash = core::RecoveryJournal::replay(candidates.front().directory, *corrupt->journal);
        REQUIRE(stoppedAtHash);
        REQUIRE(stoppedAtHash.value().stoppedAtStep.has_value());
        CHECK(std::string(stoppedAtHash.value().stopKey.data(), static_cast<std::size_t>(stoppedAtHash.value().stopKey.size()))
              == "error.recovery.sideFile");
        REQUIRE(side.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(side.write(original) == original.size());
    }
    // A side file gone: the same.
    REQUIRE(QFile::remove(QDir(candidates.front().directory).filePath(sides.front())));
    auto damaged = tests::openCopy("ordinary/attachments.pdf");
    REQUIRE(damaged);
    auto stopped = core::RecoveryJournal::replay(candidates.front().directory, *damaged->journal);
    REQUIRE(stopped);
    REQUIRE(stopped.value().stoppedAtStep.has_value());
    CHECK(std::string(stopped.value().stopKey.data(), static_cast<std::size_t>(stopped.value().stopKey.size())) == "error.recovery.sideFile");

    // And a discard takes the side files with the journal.
    REQUIRE(core::RecoveryJournal::discardDirectory(candidates.front().directory, &session->durableFiles));
    CHECK_FALSE(QDir(candidates.front().directory).exists());
}

// ==========================================================================================
// A-26's T11 clause -- a file's own pushpin, changed
// ==========================================================================================

TEST_CASE("the file's own pushpin recoloured is redrawn by our builder and moved carries its producer's appearance",
          "[attachments][A-26][D6-5][D6-6]")
{
    auto session = tests::openCopy("ordinary/attachments.pdf");
    REQUIRE(session);
    const core::AnnotationId pin = core::AnnotationId::ofObject(5);
    REQUIRE(session->journal->document().annotations().find(pin));

    // MOVED: what it draws has not changed, so the producer's appearance goes with its rectangle.
    REQUIRE(session->journal->run(std::make_unique<core::MoveAnnotationCommand>(std::vector<core::AnnotationId>{pin}, 10, -10, kNow)));
    REQUIRE(session->sink()->editOf(pin) != nullptr);
    CHECK_FALSE(session->sink()->editOf(pin)->redraw);
    REQUIRE(session->journal->undo());

    // RECOLOURED: since T11 our builder draws a pushpin, so it is redrawn rather than refused.
    core::AnnotationAppearance red;
    red.color = std::vector<double>{0.85, 0.1, 0.1};
    REQUIRE(session->journal->run(std::make_unique<core::SetAnnotationAppearanceCommand>(std::vector<core::AnnotationId>{pin}, red, kNow)));
    REQUIRE(session->sink()->editOf(pin) != nullptr);
    CHECK(session->sink()->editOf(pin)->redraw);
    auto reopened = saveAndReopen(*session, QStringLiteral("recoloured.pdf"));
    const core::AnnotationStore* store = reopened->backend.annotations(reopened->handle);
    REQUIRE(store != nullptr);
    const std::optional<std::size_t> at = store->findObject(5);
    REQUIRE(at);
    CHECK(store->at(*at).color == std::vector<double>{0.85, 0.1, 0.1});
    CHECK(store->at(*at).hasAppearance);
    // Its file is untouched by the edit: the /FS on disk is kept, value for value (D6-5).
    auto bytes = reopened->backend.attachmentData(reopened->handle, AttachmentRef::ofAnnotation(pin));
    REQUIRE(bytes);
    CHECK(tests::sha256(bytes.value()) == "9ff358efdcb7d61c9f4661540bf1e1342c7a9640f8161babfd3e7316afc569d7");
}

