// core/cmd/RecoveryJournal -- implementation. Stage 3 T9. The header states what is on disk and why.

#include "core/cmd/RecoveryJournal.h"

#include "core/Log.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/CommandRegistry.h"
#include "core/doc/Document.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QStandardPaths>

#include <atomic>
#include <mutex>

namespace rawpage::core {
namespace {

constexpr qint64 kMaxJournalBytes = 256LL * 1024 * 1024;   // a journal larger than this is not a session's edits
constexpr qint64 kMaxMetaBytes = 1024 * 1024;
// D6-23 (Stage 6 T11): a string longer than this goes to a side file. A side file larger than the cap is not one
// RawPage wrote -- the largest payload a command carries, `kMaxAttachmentBytes`, is 342 MiB as base64.
constexpr qsizetype kSideFileThreshold = 64 * 1024;
constexpr qint64 kMaxSideFileBytes = 360LL * 1024 * 1024;
const char* const kFormatLine = "{\"format\":1}";

std::mutex& observerMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::function<void(RecoveryJournal::Phase)>& observer()
{
    static std::function<void(RecoveryJournal::Phase)> instance;
    return instance;
}

void notePhase(RecoveryJournal::Phase phase)
{
    std::function<void(RecoveryJournal::Phase)> copy;
    {
        std::lock_guard<std::mutex> lock(observerMutex());
        copy = observer();
    }
    if (copy)
        copy(phase);
}

QJsonObject catalogJson(const CatalogText& text)
{
    return QJsonObject{{QStringLiteral("key"), QLatin1String(text.key)},
                       {QStringLiteral("args"), QJsonArray::fromStringList(text.arguments)}};
}

QByteArray compact(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// THE ONE WHOLE-FILE WRITE: `meta.json` and the truncated journal both go through it. A temporary file,
// flushed, then replaced over the target -- so a kill leaves the previous file or the new one.
Result<QByteArray> readCapped(const QString& path, qint64 cap, const char* tooLargeKey);

bool writeWholeFile(platform::IDurableFiles& durableFiles, const QString& path, const QByteArray& bytes)
{
    const QString temporary = path + QStringLiteral(".tmp");
    {
        QFile file(temporary);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))   // layer-lint: allow-filesystem-write(the recovery journal's meta.json and its truncation, D3-11 -- a temporary file replaced over the target)
            return false;
        if (file.write(bytes) != bytes.size() || !file.flush())
            return false;
    }
    if (!durableFiles.syncToDisk(temporary) || !durableFiles.replaceFile(path, temporary)) {
        durableFiles.removeFile(temporary);
        return false;
    }
    return true;
}

// ---- D6-23: payloads over 64 KiB in side files ------------------------------------------------------
// A journal line stays a line: every STRING in a serialized command longer than `kSideFileThreshold` -- an image
// stamp's pixels, an attachment's bytes, as base64 -- is written to `<sha256 of the string>.side` beside the journal,
// through the ONE whole-file write above (so the waiver census stays at fifteen), flushed and in place BEFORE the line
// naming it is appended, and the line carries `{"sideFile": "<hash>"}` in its place. Replay puts each back after
// checking the file's hash is its name. Commands never know: `serialize()` and `deserialize()` see whole strings.
// No command's JSON holds an object whose only key is `sideFile`, so the marker cannot be mistaken for one.

QString sideFileName(const QString& hash)
{
    return hash + QStringLiteral(".side");
}

bool isSideFileHash(const QString& text)
{
    if (text.size() != 64) return false;
    return std::all_of(text.begin(), text.end(), [](QChar c) {
        return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
    });
}

// `value` with each long string moved to a side file in `directory`; false when a side file could not be written.
bool externalize(QJsonValue& value, const QDir& directory, platform::IDurableFiles& durableFiles)
{
    if (value.isString()) {
        const QString text = value.toString();
        if (text.size() <= kSideFileThreshold) return true;
        const QByteArray bytes = text.toUtf8();
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        const QString path = directory.filePath(sideFileName(hash));
        // Named by its content: one already there IS this string, and is not written twice.
        if (!QFileInfo::exists(path) && !writeWholeFile(durableFiles, path, bytes)) return false;
        value = QJsonObject{{QStringLiteral("sideFile"), hash}};
        return true;
    }
    if (value.isArray()) {
        QJsonArray array = value.toArray();
        for (qsizetype i = 0; i < array.size(); ++i) {
            QJsonValue item = array.at(i);
            if (!externalize(item, directory, durableFiles)) return false;
            array.replace(i, item);
        }
        value = array;
        return true;
    }
    if (value.isObject()) {
        QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            QJsonValue item = it.value();
            if (!externalize(item, directory, durableFiles)) return false;
            it.value() = item;
        }
        value = object;
    }
    return true;
}

// `value` with each `{"sideFile": hash}` put back from `directory`; the error names the side file that is missing,
// too large, or whose content is not the hash it is named by.
Status internalize(QJsonValue& value, const QDir& directory)
{
    if (value.isArray()) {
        QJsonArray array = value.toArray();
        for (qsizetype i = 0; i < array.size(); ++i) {
            QJsonValue item = array.at(i);
            if (Status in = internalize(item, directory); !in) return in;
            array.replace(i, item);
        }
        value = array;
        return Status{};
    }
    if (!value.isObject()) return Status{};
    QJsonObject object = value.toObject();
    if (object.size() == 1 && object.contains(QStringLiteral("sideFile"))) {
        const QString hash = object.value(QStringLiteral("sideFile")).toString();
        if (!isSideFileHash(hash))
            return Error{ErrorCode::ParseFailure, QLatin1String("error.recovery.sideFile"), hash.left(80)};
        auto bytes = readCapped(directory.filePath(sideFileName(hash)), kMaxSideFileBytes, "error.recovery.sideFile");
        if (!bytes) return Error{ErrorCode::NotFound, QLatin1String("error.recovery.sideFile"), hash};
        if (QString::fromLatin1(QCryptographicHash::hash(bytes.value(), QCryptographicHash::Sha256).toHex()) != hash)
            return Error{ErrorCode::Corrupted, QLatin1String("error.recovery.sideFile"), hash};
        value = QString::fromUtf8(bytes.value());
        return Status{};
    }
    for (auto it = object.begin(); it != object.end(); ++it) {
        QJsonValue item = it.value();
        if (Status in = internalize(item, directory); !in) return in;
        it.value() = item;
    }
    value = object;
    return Status{};
}

// Every side file in `directory`, by name.
QStringList sideFilesIn(const QString& directory)
{
    return QDir(directory).entryList(QStringList{QStringLiteral("*.side"), QStringLiteral("*.side.tmp")}, QDir::Files);
}

// THE ONE APPEND: the journal is opened for appending when it is created and again after a save truncates
// it, and both go through here so the waiver census counts the operation once.
bool openForAppend(QFile& journal)
{
    return journal.open(QIODevice::ReadWrite | QIODevice::Append);   // layer-lint: allow-filesystem-write(the recovery journal's append, one flushed line per operation, D3-11)
}

// Undo of a step from before the last save, which the rebuilt history no longer holds: its commands'
// inverses, run as a new step. Never serialized -- replay runs with no recorder attached, and the next
// journal is seeded with the original undo line instead.
class InverseCommand final : public Command {
public:
    explicit InverseCommand(std::unique_ptr<Command> inner) : inner_(std::move(inner)) {}
    QLatin1String typeId() const override { return QLatin1String("recovery.inverse"); }
    CatalogText name() const override { return inner_->name(); }
    Scope scope() const override { return inner_->scope(); }
    Operation operation() const override { return inner_->operation(); }
    bool apply(Document& document, MutationSink& sink) override { return inner_->revert(document, sink); }
    bool revert(Document& document, MutationSink& sink) override { return inner_->apply(document, sink); }
    QJsonObject serialize() const override { return {}; }

private:
    std::unique_ptr<Command> inner_;
};

bool sameStep(const std::vector<const Command*>& history, const std::vector<std::unique_ptr<Command>>& line)
{
    if (history.size() != line.size())
        return false;
    for (std::size_t i = 0; i < history.size(); ++i)
        if (compact(history[i]->serialize()) != compact(line[i]->serialize()))
            return false;
    return true;
}

std::optional<CatalogText> catalogFrom(const QJsonValue& value)
{
    if (!value.isObject())
        return std::nullopt;
    const QJsonObject object = value.toObject();
    if (!object.value(QStringLiteral("key")).isString() || !object.value(QStringLiteral("args")).isArray())
        return std::nullopt;
    // The key must outlive the step: a QLatin1String views its bytes, so keep one interned copy per key.
    static std::mutex internMutex;
    static std::vector<std::unique_ptr<QByteArray>> interned;
    const QByteArray key = object.value(QStringLiteral("key")).toString().toLatin1().left(128);
    std::lock_guard<std::mutex> lock(internMutex);
    const QByteArray* stored = nullptr;
    for (const auto& existing : interned)
        if (*existing == key) { stored = existing.get(); break; }
    if (stored == nullptr) {
        if (interned.size() >= 4096)
            return std::nullopt;               // a hostile journal naming endless distinct keys (A-70)
        interned.push_back(std::make_unique<QByteArray>(key));
        stored = interned.back().get();
    }
    CatalogText text;
    text.key = QLatin1String(stored->constData(), stored->size());
    for (const QJsonValue& argument : object.value(QStringLiteral("args")).toArray())
        text.arguments.append(argument.toString().left(256));
    return text;
}

Result<QByteArray> readCapped(const QString& path, qint64 cap, const char* tooLargeKey)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Error{ErrorCode::NotFound, QLatin1String("error.recovery.unreadable"), path};
    if (file.size() > cap)
        return Error{ErrorCode::ParseFailure, QLatin1String(tooLargeKey), path};
    return file.readAll();
}

} // namespace

// ---------------------------------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------------------------------

RecoveryJournal::RecoveryJournal(QString directory, platform::IDurableFiles* durableFiles)
    : directory_(std::move(directory)), durableFiles_(durableFiles)
{
}

RecoveryJournal::~RecoveryJournal()
{
    journal_.close();
}

std::unique_ptr<RecoveryJournal> RecoveryJournal::open(const QString& root, const QString& sourcePath, const DocumentId& id,
                                                        platform::IDurableFiles* durableFiles, const QByteArray& seedLines,
                                                        bool untitled, const QString& seedDirectory)
{
    // NEVER A NAME ALREADY ON DISK (Stage 3 T11): a reused process id with a matching counter would otherwise
    // overwrite a crashed session's journal before anyone was asked about it. Bounded, so a directory full of
    // hostile names cannot keep this loop going.
    static std::atomic<int> counter{0};
    QString directory;
    for (int attempt = 0; attempt < 1000; ++attempt) {
        directory = QDir(root).filePath(QStringLiteral("%1-%2-%3")
                                            .arg(id.key())
                                            .arg(QCoreApplication::applicationPid())
                                            .arg(counter.fetch_add(1)));
        if (!QFileInfo::exists(directory))
            break;
    }
    std::unique_ptr<RecoveryJournal> journal(new RecoveryJournal(directory, durableFiles));
    journal->untitled_ = untitled;
    if (durableFiles == nullptr) {
        journal->degrade("error.recovery.noDurableFiles", root);
        return journal;
    }
    if (!QDir().mkpath(directory)) {
        journal->degrade("error.recovery.unavailable", directory);
        return journal;
    }
    journal->lock_ = std::make_unique<QLockFile>(QDir(directory).filePath(QStringLiteral("lock")));
    journal->lock_->setStaleLockTime(0);   // stale only when its process is gone, never by age
    if (!journal->lock_->tryLock(0)) {
        journal->degrade("error.recovery.unavailable", directory);
        return journal;
    }
    if (!journal->writeMeta(sourcePath, id)) {
        journal->degrade("error.recovery.unavailable", directory);
        return journal;
    }
    // D6-23: the seeded lines' side files come with them -- they live in the crashed session's directory -- each
    // through the same whole-file write, and before the lines that name them.
    if (!seedDirectory.isEmpty() && !seedLines.isEmpty()) {
        for (const QString& name : QDir(seedDirectory).entryList(QStringList{QStringLiteral("*.side")}, QDir::Files)) {
            if (!seedLines.contains(name.left(64).toLatin1())) continue;   // named by no seeded line
            auto bytes = readCapped(QDir(seedDirectory).filePath(name), kMaxSideFileBytes, "error.recovery.sideFile");
            if (!bytes || !writeWholeFile(*durableFiles, QDir(directory).filePath(name), bytes.value())) {
                journal->degrade("error.recovery.unavailable", directory);
                return journal;
            }
        }
    }
    const QString journalPath = QDir(directory).filePath(QStringLiteral("journal.ndjson"));
    if (!writeWholeFile(*durableFiles, journalPath, QByteArray(kFormatLine) + '\n' + seedLines)) {
        journal->degrade("error.recovery.unavailable", directory);
        return journal;
    }
    journal->journal_.setFileName(journalPath);
    if (!openForAppend(journal->journal_))
        journal->degrade("error.recovery.unavailable", journalPath);
    return journal;
}

void RecoveryJournal::degrade(const char* key, const QString& context)
{
    if (!isRecoverable())
        return;                                // said once
    unrecoverableKey_ = QLatin1String(key);
    unrecoverableContext_ = context;
    journal_.close();
    RAWPAGE_LOG_WARNING(LogSubsystem::Save)
        << "recovery: this document's edits are kept in memory only --" << QLatin1String(key) << context;
}

bool RecoveryJournal::writeMeta(const QString& sourcePath, const DocumentId& id)
{
    QJsonObject meta{
        {QStringLiteral("format"), 1},
        {QStringLiteral("source"), sourcePath},
        {QStringLiteral("size"), static_cast<double>(id.size)},
        {QStringLiteral("modified"), static_cast<double>(id.modifiedMsecs)},
        {QStringLiteral("sha256"), QString::fromLatin1(id.contentHash.toHex())},
    };
    if (untitled_)
        meta.insert(QStringLiteral("untitled"), true);   // absent otherwise, so a titled meta is byte-identical to T9's
    return writeWholeFile(*durableFiles_, QDir(directory_).filePath(QStringLiteral("meta.json")), compact(meta));
}

void RecoveryJournal::appendLine(const QByteArray& line)
{
    if (!isRecoverable())
        return;
    notePhase(Phase::Writing);
    const QByteArray bytes = line + '\n';
    if (journal_.write(bytes) != bytes.size() || !journal_.flush()) {
        degrade("error.recovery.unavailable", journal_.fileName());
        return;
    }
    notePhase(Phase::Flushing);
    if (!durableFiles_->syncToDisk(journal_.fileName())) {
        degrade("error.recovery.unavailable", journal_.fileName());
        return;
    }
    notePhase(Phase::Flushed);
}

void RecoveryJournal::recordRun(const Command& command, const std::optional<CatalogText>& opensGroup)
{
    if (!isRecoverable())
        return;
    const QJsonObject serialized = command.serialize();
    if (serialized.isEmpty()) {
        degrade("error.recovery.notRecoverable", QLatin1String(command.typeId()));
        return;
    }
    // D6-23: its long strings in side files first, so the line never names a file that is not yet on disk.
    QJsonValue cmd = serialized;
    if (!externalize(cmd, QDir(directory_), *durableFiles_)) {
        degrade("error.recovery.unavailable", directory_);
        return;
    }
    if (opensGroup)
        appendLine(compact(QJsonObject{{QStringLiteral("op"), QStringLiteral("groupBegin")},
                                       {QStringLiteral("name"), catalogJson(*opensGroup)}}));
    appendLine(compact(QJsonObject{{QStringLiteral("op"), QStringLiteral("run")}, {QStringLiteral("cmd"), cmd}}));
}

void RecoveryJournal::recordGroupEnd()
{
    appendLine(compact(QJsonObject{{QStringLiteral("op"), QStringLiteral("groupEnd")}}));
}

namespace {

// `unserializable` names the command that serialized to nothing; empty with nullopt: a side file could not be written.
std::optional<QJsonArray> serializeStep(const std::vector<const Command*>& step, QString& unserializable, const QDir& directory,
                                        platform::IDurableFiles& durableFiles)
{
    QJsonArray commands;
    for (const Command* command : step) {
        QJsonValue serialized = command->serialize();
        if (serialized.toObject().isEmpty()) {
            unserializable = QLatin1String(command->typeId());
            return std::nullopt;
        }
        if (!externalize(serialized, directory, durableFiles)) return std::nullopt;   // D6-23
        commands.append(serialized);
    }
    return commands;
}

} // namespace

void RecoveryJournal::recordUndo(const CatalogText& stepName, const std::vector<const Command*>& step)
{
    if (!isRecoverable())
        return;
    QString unserializable;
    const auto commands = serializeStep(step, unserializable, QDir(directory_), *durableFiles_);
    if (!commands) {
        if (unserializable.isEmpty()) degrade("error.recovery.unavailable", directory_);
        else degrade("error.recovery.notRecoverable", unserializable);
        return;
    }
    appendLine(compact(QJsonObject{{QStringLiteral("op"), QStringLiteral("undo")},
                                   {QStringLiteral("name"), catalogJson(stepName)},
                                   {QStringLiteral("cmds"), *commands}}));
}

void RecoveryJournal::recordRedo(const CatalogText& stepName, const std::vector<const Command*>& step)
{
    if (!isRecoverable())
        return;
    QString unserializable;
    const auto commands = serializeStep(step, unserializable, QDir(directory_), *durableFiles_);
    if (!commands) {
        if (unserializable.isEmpty()) degrade("error.recovery.unavailable", directory_);
        else degrade("error.recovery.notRecoverable", unserializable);
        return;
    }
    appendLine(compact(QJsonObject{{QStringLiteral("op"), QStringLiteral("redo")},
                                   {QStringLiteral("name"), catalogJson(stepName)},
                                   {QStringLiteral("cmds"), *commands}}));
}

void RecoveryJournal::recordSaved(const Document& document, const QString& path)
{
    if (!isRecoverable())
        return;
    // meta.json first: if a kill lands between the two writes, the lines are replayed onto a file that
    // matches -- and the edits they carry are already in it. See the header's window. A saved document has a
    // file of its own, so it is no longer untitled.
    untitled_ = false;
    if (!writeMeta(path, document.id())) {
        degrade("error.recovery.unavailable", directory_);
        return;
    }
    journal_.close();
    if (!writeWholeFile(*durableFiles_, QDir(directory_).filePath(QStringLiteral("journal.ndjson")), QByteArray(kFormatLine) + '\n')
        || !openForAppend(journal_)) {
        degrade("error.recovery.unavailable", directory_);
        return;
    }
    // D6-23: no line names a side file any more -- the history before the save is the saved file.
    for (const QString& name : sideFilesIn(directory_))
        durableFiles_->removeFile(QDir(directory_).filePath(name));
}

void RecoveryJournal::discard()
{
    journal_.close();
    if (durableFiles_ != nullptr && !directory_.isEmpty()) {
        const QDir dir(directory_);
        for (const char* name : {"journal.ndjson", "meta.json", "journal.ndjson.tmp", "meta.json.tmp"})
            durableFiles_->removeFile(dir.filePath(QLatin1String(name)));
        for (const QString& name : sideFilesIn(directory_))   // D6-23
            durableFiles_->removeFile(dir.filePath(name));
    }
    if (lock_)
        lock_->unlock();
    QDir().rmdir(directory_);
    if (isRecoverable()) {
        unrecoverableKey_ = QLatin1String("error.recovery.discarded");
        unrecoverableContext_ = directory_;
    }
}

void RecoveryJournal::setPhaseObserverForTesting(std::function<void(Phase)> newObserver)
{
    std::lock_guard<std::mutex> lock(observerMutex());
    observer() = std::move(newObserver);
}

QString RecoveryJournal::resolveRoot(const QString& configured, const QString& fallback)
{
    const QString trimmed = configured.trimmed();
    if (!trimmed.isEmpty()) {
        if (QDir::isAbsolutePath(trimmed))
            return QDir::cleanPath(trimmed);
        RAWPAGE_LOG_WARNING(LogSubsystem::Save)
            << "recovery: documents/recoveryDir is not an absolute path and is ignored --" << trimmed;
    }
    return fallback.isEmpty() ? defaultRoot() : fallback;
}

QString RecoveryJournal::defaultRoot()
{
    // GenericDataLocation is %LOCALAPPDATA% on Windows, whatever the application and organization names
    // are set to -- so the path is the one Tech-Stack-Plan.md §5.4 names.
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("RawPage/recovery"));
}

// ---------------------------------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------------------------------

std::vector<RecoveryCandidate> RecoveryJournal::discover(const QString& root)
{
    std::vector<RecoveryCandidate> candidates;
    const QFileInfoList entries = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : entries) {
        const QDir dir(entry.absoluteFilePath());
        if (!QFileInfo::exists(dir.filePath(QStringLiteral("meta.json"))))
            continue;                                              // not a recovery directory

        // A LIVE owner's directory is never offered (A-65): the lock is held by a running process.
        QLockFile lock(dir.filePath(QStringLiteral("lock")));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0))
            continue;
        lock.unlock();

        RecoveryCandidate candidate;
        candidate.directory = entry.absoluteFilePath();
        const auto metaBytes = readCapped(dir.filePath(QStringLiteral("meta.json")), kMaxMetaBytes, "error.recovery.badMeta");
        const QJsonDocument metaJson = metaBytes ? QJsonDocument::fromJson(metaBytes.value()) : QJsonDocument();
        const QJsonObject meta = metaJson.object();
        if (!metaJson.isObject() || meta.value(QStringLiteral("format")).toInt() != 1
            || !meta.value(QStringLiteral("source")).isString() || !meta.value(QStringLiteral("size")).isDouble()
            || !meta.value(QStringLiteral("modified")).isDouble() || !meta.value(QStringLiteral("sha256")).isString()) {
            candidate.problemKey = QLatin1String("error.recovery.badMeta");
            candidates.push_back(candidate);
            continue;
        }
        candidate.meta.sourcePath = meta.value(QStringLiteral("source")).toString();
        candidate.meta.size = static_cast<qint64>(meta.value(QStringLiteral("size")).toDouble());
        candidate.meta.modifiedMsecs = static_cast<qint64>(meta.value(QStringLiteral("modified")).toDouble());
        candidate.meta.contentHash = QByteArray::fromHex(meta.value(QStringLiteral("sha256")).toString().toLatin1());
        candidate.meta.untitled = meta.value(QStringLiteral("untitled")).toBool(false);

        const auto journalBytes = readCapped(dir.filePath(QStringLiteral("journal.ndjson")), kMaxJournalBytes, "error.recovery.tooLarge");
        if (!journalBytes) {
            candidate.problemKey = journalBytes.error().messageKey;
        } else {
            const int newlines = static_cast<int>(journalBytes.value().count('\n'));
            candidate.operationLines = newlines > 0 ? newlines - 1 : 0;
        }

        const QFileInfo source(candidate.meta.sourcePath);
        if (source.exists() && source.isFile() && source.size() == candidate.meta.size
            && source.lastModified().toMSecsSinceEpoch() == candidate.meta.modifiedMsecs) {
            QFile file(candidate.meta.sourcePath);
            candidate.sourceMatches = file.open(QIODevice::ReadOnly)
                && QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256) == candidate.meta.contentHash;
        }
        candidates.push_back(candidate);
    }
    return candidates;
}

Status RecoveryJournal::discardDirectory(const QString& directory, platform::IDurableFiles* durableFiles)
{
    if (durableFiles == nullptr)
        return Error{ErrorCode::Unavailable, QLatin1String("error.recovery.noDurableFiles"), directory};
    const QDir dir(directory);
    QLockFile lock(dir.filePath(QStringLiteral("lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return Error{ErrorCode::Unavailable, QLatin1String("error.recovery.live"), directory};
    for (const char* name : {"journal.ndjson", "meta.json", "journal.ndjson.tmp", "meta.json.tmp"}) {
        if (const Status removed = durableFiles->removeFile(dir.filePath(QLatin1String(name))); !removed)
            return removed;
    }
    for (const QString& name : sideFilesIn(directory)) {   // D6-23
        if (const Status removed = durableFiles->removeFile(dir.filePath(name)); !removed)
            return removed;
    }
    lock.unlock();
    QDir().rmdir(directory);
    return Status{};
}

// ---------------------------------------------------------------------------------------------------
// Replay
// ---------------------------------------------------------------------------------------------------

Result<ReplayReport> RecoveryJournal::replay(const QString& directory, CommandJournal& journal)
{
    if (journal.hasRecorder())
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.recovery.replayWhileRecording"), directory};
    auto bytes = readCapped(QDir(directory).filePath(QStringLiteral("journal.ndjson")), kMaxJournalBytes, "error.recovery.tooLarge");
    if (!bytes)
        return bytes.error();

    ReplayReport report;
    QList<QByteArray> lines = bytes.value().split('\n');
    // `split` leaves an empty last element after a final newline; anything else there is a torn line.
    if (!lines.isEmpty()) {
        if (!lines.last().isEmpty())
            report.tornTrailingLine = true;
        lines.removeLast();
    }
    if (lines.isEmpty() || QJsonDocument::fromJson(lines.first()).object().value(QStringLiteral("format")).toInt() != 1)
        return Error{ErrorCode::ParseFailure, QLatin1String("error.recovery.badJournal"), directory};

    int step = 0;
    bool inGroup = false;
    QByteArray pending;            // the lines of the step being applied
    auto stop = [&](int atStep, const QLatin1String& key, const QString& context) {
        if (inGroup) {
            journal.abandonOpenGroup();
            inGroup = false;
        }
        report.stoppedAtStep = atStep;
        report.stopKey = key;
        report.stopContext = context;
    };
    auto commandsFrom = [&directory](const QJsonValue& value, QLatin1String& key, QString& context) {
        std::vector<std::unique_ptr<Command>> commands;
        if (!value.isArray() || value.toArray().isEmpty()) {
            key = QLatin1String("error.recovery.malformedLine");
            context = QStringLiteral("cmds");
            return std::optional<std::vector<std::unique_ptr<Command>>>();
        }
        for (QJsonValue item : value.toArray()) {
            if (const Status in = internalize(item, QDir(directory)); !in) {   // D6-23
                key = in.error().messageKey;
                context = in.error().context;
                return std::optional<std::vector<std::unique_ptr<Command>>>();
            }
            auto command = CommandRegistry::deserialize(item.toObject());
            if (!command) {
                key = command.error().messageKey;
                context = command.error().context;
                return std::optional<std::vector<std::unique_ptr<Command>>>();
            }
            commands.push_back(std::move(command).value());
        }
        return std::optional<std::vector<std::unique_ptr<Command>>>(std::move(commands));
    };
    // Runs `commands` as ONE new step (a group when there are several).
    auto runAsStep = [&](std::vector<std::unique_ptr<Command>> commands, const CatalogText& name, bool inverse) -> std::optional<QString> {
        const bool grouped = commands.size() > 1;
        if (grouped) {
            journal.beginGroup(name);
            inGroup = true;
        }
        for (std::size_t i = 0; i < commands.size(); ++i) {
            std::unique_ptr<Command> command = inverse ? std::unique_ptr<Command>(new InverseCommand(std::move(commands[commands.size() - 1 - i])))
                                                       : std::move(commands[i]);
            if (const Status ran = journal.run(std::move(command)); !ran)
                return ran.error().context.isEmpty() ? QString::fromLatin1(ran.error().messageKey) : ran.error().context;
        }
        if (grouped) {
            journal.endGroup();
            inGroup = false;
        }
        return std::nullopt;
    };

    for (qsizetype index = 1; index < lines.size() && !report.stoppedAtStep; ++index) {
        const QByteArray& line = lines[index];
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        const QJsonObject op = document.object();
        const QString kind = op.value(QStringLiteral("op")).toString();
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            stop(inGroup ? step : step + 1, QLatin1String("error.recovery.malformedLine"), QStringLiteral("line %1").arg(index + 1));
            break;
        }

        if (kind == QLatin1String("groupBegin")) {
            const auto name = catalogFrom(op.value(QStringLiteral("name")));
            if (inGroup || !name) {
                stop(inGroup ? step : step + 1, QLatin1String("error.recovery.malformedLine"), QStringLiteral("line %1").arg(index + 1));
                break;
            }
            ++step;
            journal.beginGroup(*name);
            inGroup = true;
            pending = line + '\n';
        } else if (kind == QLatin1String("groupEnd")) {
            if (!inGroup) {
                stop(step + 1, QLatin1String("error.recovery.malformedLine"), QStringLiteral("line %1").arg(index + 1));
                break;
            }
            journal.endGroup();
            inGroup = false;
            report.appliedLines += pending + line + '\n';
            pending.clear();
            ++report.stepsApplied;
        } else if (kind == QLatin1String("run")) {
            if (!inGroup)
                ++step;
            QJsonValue cmd = op.value(QStringLiteral("cmd"));
            if (const Status in = internalize(cmd, QDir(directory)); !in) {   // D6-23
                stop(step, in.error().messageKey, in.error().context);
                break;
            }
            auto command = CommandRegistry::deserialize(cmd.toObject());
            if (!command) {
                stop(step, command.error().messageKey, command.error().context);
                break;
            }
            if (const Status ran = journal.run(std::move(command).value()); !ran) {
                stop(step, ran.error().messageKey, ran.error().context);
                break;
            }
            if (inGroup) {
                pending += line + '\n';
            } else {
                report.appliedLines += line + '\n';
                ++report.stepsApplied;
            }
        } else if (kind == QLatin1String("undo") || kind == QLatin1String("redo")) {
            if (inGroup) {
                stop(step, QLatin1String("error.recovery.malformedLine"), QStringLiteral("line %1").arg(index + 1));
                break;
            }
            ++step;
            QLatin1String key("");
            QString context;
            auto commands = commandsFrom(op.value(QStringLiteral("cmds")), key, context);
            const auto name = catalogFrom(op.value(QStringLiteral("name")));
            if (!commands || !name) {
                stop(step, commands ? QLatin1String("error.recovery.malformedLine") : key, commands ? QStringLiteral("line %1").arg(index + 1) : context);
                break;
            }
            std::optional<QString> failed;
            if (kind == QLatin1String("undo")) {
                const auto top = journal.undoStep();
                if (!top.empty() && sameStep(top, *commands)) {
                    if (!journal.undo())
                        failed = QStringLiteral("undo");
                } else if (top.empty()) {
                    failed = runAsStep(std::move(*commands), *name, /*inverse=*/true);   // a step from before the save
                } else {
                    stop(step, QLatin1String("error.recovery.historyMismatch"), QStringLiteral("line %1").arg(index + 1));
                    break;
                }
            } else {
                const auto next = journal.redoStep();
                if (!next.empty() && sameStep(next, *commands)) {
                    if (!journal.redo())
                        failed = QStringLiteral("redo");
                } else {
                    failed = runAsStep(std::move(*commands), *name, /*inverse=*/false);
                }
            }
            if (failed) {
                stop(step, QLatin1String("error.recovery.stepFailed"), *failed);
                break;
            }
            report.appliedLines += line + '\n';
            ++report.stepsApplied;
        } else {
            stop(inGroup ? step : step + 1, QLatin1String("error.recovery.malformedLine"), QStringLiteral("line %1").arg(index + 1));
            break;
        }
    }

    if (inGroup && !report.stoppedAtStep) {
        // A group the session never closed: it is not a step the user finished (D3-11).
        journal.abandonOpenGroup();
        report.droppedUnterminatedGroup = true;
    }
    return report;
}

} // namespace rawpage::core
