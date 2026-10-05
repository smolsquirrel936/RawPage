// tests/unit/SaveSession.h -- a document opened from a COPY of a corpus file, ready to edit and save.
// Stage 3 T8. Shared by `tst_savecoordinator.cpp` and `tests/roundtrip/tst_roundtrip.cpp`.
//
// Every session works on a copy in its own temporary directory, so no case can alter `corpus/` -- and
// so each case's "the original is unchanged" is about a file the case itself controls. The backend is
// `tests/unit/CosBackend.h` and the platform half is `tests/unit/FakeDurableFiles.h`; each file says
// what that leaves out.

#ifndef RAWPAGE_TESTS_UNIT_SAVESESSION_H
#define RAWPAGE_TESTS_UNIT_SAVESESSION_H

#include <catch2/catch_test_macros.hpp>

#include "CosBackend.h"
#include "FakeDurableFiles.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/SaveCoordinator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace rawpage::tests {

inline QString corpusFile(std::string_view relative)
{
    return QString::fromStdString((std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative).string());
}

inline QByteArray readAllBytes(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

// As HEX TEXT, and every "is it the same file" check compares these rather than two QByteArrays: a failed
// CHECK on two QByteArrays crashes Catch2's console reporter while it prints them (measured at T8), which
// turns a red case into an access violation that names no assertion.
inline std::string sha256(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

struct SaveSession {
    QTemporaryDir directory;
    QString path;             // the copy the document was opened from
    QByteArray openedBytes;   // what was on disk at open
    CosBackend backend;
    FakeDurableFiles durableFiles;
    core::DocumentHandle handle;
    std::unique_ptr<core::CommandJournal> journal;
    std::unique_ptr<core::SaveCoordinator> coordinator;

    pdf::ops::MutationSinkImpl* sink() { return backend.sinkFor(handle); }

    core::Status rotate(int page, int degrees = 90)
    {
        return journal->run(std::make_unique<core::RotatePageCommand>(page, degrees));
    }

    // The temporary files a save could have left beside the document.
    QStringList strayTemporaries() const
    {
        return QDir(directory.path()).entryList(QStringList{QStringLiteral(".*.rawpage-*.tmp")},
                                                QDir::Files | QDir::Hidden | QDir::System);
    }
};

inline std::unique_ptr<SaveSession> openSession(std::unique_ptr<SaveSession> session);

// Opens a copy of `relative`. Returns null -- after a REQUIRE on the copy -- when the first-party writer
// cannot open it for editing, so a corpus sweep can count and name those files.
inline std::unique_ptr<SaveSession> openCopy(std::string_view relative)
{
    auto session = std::make_unique<SaveSession>();
    REQUIRE(session->directory.isValid());
    const QString name = QString::fromStdString(std::filesystem::path(relative).filename().string());
    session->path = session->directory.filePath(name);
    REQUIRE(QFile::copy(corpusFile(relative), session->path));
    QFile::setPermissions(session->path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return openSession(std::move(session));
}

// Stage 6 T3: the same over bytes a case built (`tests/unit/TinyPdf.h`), written into the session's own
// directory as `name`. A planted reading or annotation disagreement is set on `prepare`'s backend first.
inline std::unique_ptr<SaveSession> openBuilt(const QString& name, const std::string& bytes,
                                              const std::function<void(CosBackend&)>& prepare = {})
{
    auto session = std::make_unique<SaveSession>();
    REQUIRE(session->directory.isValid());
    session->path = session->directory.filePath(name);
    {
        QFile file(session->path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write(bytes.data(), static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size()));
    }
    if (prepare) prepare(session->backend);
    return openSession(std::move(session));
}

inline std::unique_ptr<SaveSession> openSession(std::unique_ptr<SaveSession> session)
{
    session->openedBytes = readAllBytes(session->path);

    auto handle = session->backend.open(session->path, nullptr);
    if (!handle || session->backend.sinkFor(handle.value()) == nullptr)
        return nullptr;
    session->handle = handle.value();
    core::Document document;
    REQUIRE(document.hydrate(session->backend, session->handle));
    session->journal = std::make_unique<core::CommandJournal>(
        std::move(document), session->backend.mutationSink(session->handle));
    session->coordinator = std::make_unique<core::SaveCoordinator>(session->backend, &session->durableFiles);
    return session;
}

} // namespace rawpage::tests

#endif // RAWPAGE_TESTS_UNIT_SAVESESSION_H
