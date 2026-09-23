#include "core/settings/SessionState.h"

#include "core/Log.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace rawpage::core {
namespace {

// `LogSubsystem` has no `Session` value and this file does not add one: the enum is a short,
// stable list several censuses read, and session restore is a shell concern. `Ui` is the
// honest bucket -- the alternative, a new enumerator, would be a vocabulary change for one
// caller.
constexpr LogSubsystem kSubsystem = LogSubsystem::Ui;

// One place per key, so the reader and the writer cannot drift apart -- the defect a second copy of
// a field name always eventually produces.
constexpr QLatin1String kFormatKey("format");
constexpr QLatin1String kArrangementsKey("arrangements");
constexpr QLatin1String kSessionKey("session");
constexpr QLatin1String kFingerprintKey("fingerprint");
constexpr QLatin1String kWindowsKey("windows");
constexpr QLatin1String kDocumentsKey("documents");

QJsonObject rectToJson(const QRect& rect, bool maximized)
{
    QJsonObject object;
    object[QLatin1String("x")] = rect.x();
    object[QLatin1String("y")] = rect.y();
    object[QLatin1String("w")] = rect.width();
    object[QLatin1String("h")] = rect.height();
    object[QLatin1String("maximized")] = maximized;
    return object;
}

QRect rectFromJson(const QJsonObject& object)
{
    return QRect(object.value(QLatin1String("x")).toInt(), object.value(QLatin1String("y")).toInt(),
                 object.value(QLatin1String("w")).toInt(),
                 object.value(QLatin1String("h")).toInt());
}

QJsonObject documentIdToJson(const DocumentId& id)
{
    QJsonObject object;
    object[QLatin1String("path")] = id.path;
    object[QLatin1String("size")] = static_cast<double>(id.size);
    object[QLatin1String("modified")] = static_cast<double>(id.modifiedMsecs);
    object[QLatin1String("hash")] = QString::fromLatin1(id.contentHash.toHex());
    return object;
}

DocumentId documentIdFromJson(const QJsonObject& object)
{
    DocumentId id;
    id.path = object.value(QLatin1String("path")).toString();
    id.size = static_cast<qint64>(object.value(QLatin1String("size")).toDouble(-1));
    id.modifiedMsecs = static_cast<qint64>(object.value(QLatin1String("modified")).toDouble(0));
    id.contentHash =
        QByteArray::fromHex(object.value(QLatin1String("hash")).toString().toLatin1());
    return id;
}

QJsonObject guideToJson(const Guide& guide)
{
    QJsonObject object;
    object[QLatin1String("o")] =
        guide.orientation == Guide::Orientation::Horizontal ? QLatin1String("h") : QLatin1String("v");
    object[QLatin1String("pos")] = guide.position;
    object[QLatin1String("page")] = guide.pageIndex;
    object[QLatin1String("locked")] = guide.locked;
    return object;
}

Guide guideFromJson(const QJsonObject& object)
{
    Guide guide;
    guide.orientation = object.value(QLatin1String("o")).toString() == QLatin1String("v")
                            ? Guide::Orientation::Vertical
                            : Guide::Orientation::Horizontal;
    guide.position = object.value(QLatin1String("pos")).toDouble();
    guide.pageIndex = object.value(QLatin1String("page")).toInt();
    guide.locked = object.value(QLatin1String("locked")).toBool();
    return guide;
}

QJsonObject documentToJson(const SessionDocument& document)
{
    QJsonObject object;
    object[QLatin1String("path")] = document.path;
    object[QLatin1String("id")] = documentIdToJson(document.id);
    // The NAME, not the enumerator's number: `layoutModeName` is what the settings file already
    // holds for `pageDisplay/layoutMode`, and a session that stored an integer would break the day
    // somebody inserted a value into the middle of the enum.
    object[QLatin1String("layout")] =
        QString::fromLatin1(render::layoutModeName(document.layoutMode));
    object[QLatin1String("fit")] = QString::fromLatin1(render::fitModeName(document.fit));
    object[QLatin1String("zoom")] = document.zoom;
    object[QLatin1String("page")] = document.pageIndex;
    object[QLatin1String("scrollX")] = document.scrollX;
    object[QLatin1String("scrollY")] = document.scrollY;
    if (!document.splitState.isEmpty()) object[QLatin1String("split")] = document.splitState;

    QJsonArray guides;
    for (const Guide& guide : document.guides) guides.append(guideToJson(guide));
    if (!guides.isEmpty()) object[QLatin1String("guides")] = guides;
    return object;
}

SessionDocument documentFromJson(const QJsonObject& object)
{
    SessionDocument document;
    document.path = object.value(QLatin1String("path")).toString();
    document.id = documentIdFromJson(object.value(QLatin1String("id")).toObject());

    // An unknown token keeps the default rather than failing the load: a session file is a
    // convenience, and refusing to restore three documents because one enum spelling moved would
    // trade a small wrong value for a large absent one.
    const QByteArray layout = object.value(QLatin1String("layout")).toString().toLatin1();
    if (const std::optional<render::LayoutMode> mode =
            render::layoutModeFromName(std::string_view(layout.constData(),
                                                        static_cast<std::size_t>(layout.size()))))
        document.layoutMode = *mode;
    const QByteArray fit = object.value(QLatin1String("fit")).toString().toLatin1();
    if (const std::optional<render::FitMode> mode =
            render::fitModeFromName(std::string_view(fit.constData(),
                                                     static_cast<std::size_t>(fit.size()))))
        document.fit = *mode;

    document.zoom = object.value(QLatin1String("zoom")).toDouble(1.0);
    document.pageIndex = object.value(QLatin1String("page")).toInt();
    document.scrollX = object.value(QLatin1String("scrollX")).toDouble();
    document.scrollY = object.value(QLatin1String("scrollY")).toDouble();
    document.splitState = object.value(QLatin1String("split")).toString();
    for (const QJsonValue& guide : object.value(QLatin1String("guides")).toArray())
        document.guides.push_back(guideFromJson(guide.toObject()));
    return document;
}

QJsonObject windowToJson(const SessionWindow& window)
{
    QJsonObject object = rectToJson(window.geometry, window.maximized);
    object[QLatin1String("active")] = window.activeDocument;
    if (!window.paneLayout.isEmpty()) object[QLatin1String("panes")] = window.paneLayout;
    QJsonArray documents;
    for (const SessionDocument& document : window.documents)
        documents.append(documentToJson(document));
    object[kDocumentsKey] = documents;
    return object;
}

SessionWindow windowFromJson(const QJsonObject& object)
{
    SessionWindow window;
    window.geometry = rectFromJson(object);
    window.maximized = object.value(QLatin1String("maximized")).toBool();
    window.activeDocument = object.value(QLatin1String("active")).toInt();
    window.paneLayout = object.value(QLatin1String("panes")).toString();
    for (const QJsonValue& document : object.value(kDocumentsKey).toArray())
        window.documents.push_back(documentFromJson(document.toObject()));
    return window;
}

// The area of the intersection, as a `long long` because two 4K screens multiply past `int`.
long long intersectionArea(const QRect& a, const QRect& b)
{
    const QRect overlap = a.intersected(b);
    if (overlap.isEmpty()) return 0;
    return static_cast<long long>(overlap.width()) * static_cast<long long>(overlap.height());
}

} // namespace

QString SessionState::defaultFilePath()
{
    // Beside `settings.ini`, and for the reasons `Settings::defaultFilePath()` states at length:
    // `AppDataLocation` is already `%APPDATA%\<org>\<app>` because `app/` leaves the organization
    // unset, and `GenericConfigLocation` is `%LOCALAPPDATA%` on Windows -- a session that did not
    // roam would be missing on the user's second machine.
    //
    // JSON rather than the INI `Settings` uses, because this is a tree -- windows holding tabs
    // holding guides -- and an INI flattens a tree into keys with indices in their names, which is
    // a format nobody can hand-edit and every reader parses slightly differently.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/session.json");
}

SessionState::SessionState(QString filePath) : filePath_(std::move(filePath)) {}

// -------------------------------------------------------------------------------------------
// The fingerprint
// -------------------------------------------------------------------------------------------

QString SessionState::fingerprintOf(const std::vector<platform::MonitorInfo>& monitors)
{
    // Sorted by `id`, so the order the OS happened to enumerate them in is not part of the
    // identity -- the same desk unplugged and replugged must hash the same.
    std::vector<const platform::MonitorInfo*> sorted;
    sorted.reserve(monitors.size());
    for (const platform::MonitorInfo& monitor : monitors) sorted.push_back(&monitor);
    std::sort(sorted.begin(), sorted.end(),
              [](const platform::MonitorInfo* a, const platform::MonitorInfo* b) {
                  return a->id < b->id;
              });

    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const platform::MonitorInfo* monitor : sorted) {
        // `scaleFactor` to four decimals rather than as raw bytes: a double's exact bits are not
        // something to hash across builds, and 150 % is 1.5 on every machine that reports it.
        const QString row = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                                .arg(monitor->id)
                                .arg(monitor->x)
                                .arg(monitor->y)
                                .arg(monitor->width)
                                .arg(monitor->height)
                                .arg(QString::number(monitor->scaleFactor, 'f', 4))
                                .arg(monitor->primary ? 1 : 0);
        hash.addData(row.toUtf8());
        hash.addData(QByteArrayLiteral("\n"));
    }
    // Sixteen hex digits. It names an arrangement in a file a person may read; the full 64 would be
    // noise, and a collision here costs a clamped window rather than anything worse.
    return QString::fromLatin1(hash.result().toHex().left(16));
}

// -------------------------------------------------------------------------------------------
// The clamp
// -------------------------------------------------------------------------------------------

QRect SessionState::clampInto(const QRect& window, const std::vector<QRect>& screens)
{
    if (screens.empty() || window.isEmpty()) return window;

    // REACHABLE IS THE FIRST QUESTION, and answering it first is what makes the control -- a window
    // already on screen is not moved by a single pixel -- a branch rather than an accident of the
    // arithmetic. It is also what leaves a window straddling two adjacent monitors alone: that
    // window is fully visible and belongs to nobody's idea of a defect.
    const QRect titleStrip(window.x(), window.y(), window.width(),
                           std::min(kTitleBarHeight, window.height()));
    const int wanted = std::min(kMinimumVisibleWidth, window.width());
    for (const QRect& screen : screens) {
        const QRect grabbable = titleStrip.intersected(screen);
        if (!grabbable.isEmpty() && grabbable.width() >= wanted) return window;
    }

    // Otherwise put it wholly onto the screen it already overlaps most, or the first screen when it
    // overlaps none -- which is the undocked case, where the geometry names a monitor that is gone.
    const QRect* best = &screens.front();
    long long bestArea = 0;
    for (const QRect& screen : screens) {
        const long long area = intersectionArea(window, screen);
        if (area > bestArea) {
            bestArea = area;
            best = &screen;
        }
    }

    QRect out = window;
    out.setWidth(std::min(out.width(), best->width()));
    out.setHeight(std::min(out.height(), best->height()));
    if (out.right() > best->right()) out.moveRight(best->right());
    if (out.bottom() > best->bottom()) out.moveBottom(best->bottom());
    if (out.left() < best->left()) out.moveLeft(best->left());
    if (out.top() < best->top()) out.moveTop(best->top());
    return out;
}

// -------------------------------------------------------------------------------------------
// Capture
// -------------------------------------------------------------------------------------------

void SessionState::capture(const Session& session)
{
    session_ = session;

    Arrangement arrangement;
    arrangement.fingerprint = session.fingerprint;
    for (const SessionWindow& window : session.windows)
        arrangement.windows.push_back(WindowGeometry{window.geometry, window.maximized});

    // Most-recent-first, and an arrangement seen again MOVES rather than duplicating -- the same
    // shape `RecentFiles::note` has, and for the same reason.
    const auto existing = std::find_if(arrangements_.begin(), arrangements_.end(),
                                       [&](const Arrangement& candidate) {
                                           return candidate.fingerprint == session.fingerprint;
                                       });
    if (existing != arrangements_.end()) arrangements_.erase(existing);
    arrangements_.insert(arrangements_.begin(), std::move(arrangement));
    if (arrangements_.size() > kMaxArrangements) arrangements_.resize(kMaxArrangements);
}

// -------------------------------------------------------------------------------------------
// Restore
// -------------------------------------------------------------------------------------------

SessionState::Restored SessionState::restore(const QString& fingerprint,
                                             const std::vector<QRect>& screens,
                                             const FileExists& exists) const
{
    Restored restored;
    restored.windows = session_.windows;

    // The geometry for THIS arrangement, else the most recent one we have. A fingerprint we have
    // never seen is the ordinary case -- it is what plugging into a projector looks like.
    const auto match = std::find_if(
        arrangements_.begin(), arrangements_.end(),
        [&](const Arrangement& candidate) { return candidate.fingerprint == fingerprint; });
    const Arrangement* geometry = nullptr;
    if (match != arrangements_.end()) {
        geometry = &*match;
    } else if (!arrangements_.empty()) {
        geometry = &arrangements_.front();
        restored.usedFallbackArrangement = true;
    }
    if (geometry != nullptr) {
        const std::size_t count = std::min(restored.windows.size(), geometry->windows.size());
        for (std::size_t at = 0; at < count; ++at) {
            restored.windows[at].geometry = geometry->windows[at].rect;
            restored.windows[at].maximized = geometry->windows[at].maximized;
        }
    }

    // Then the clamp, on EVERY window and not only the ones that came from a fallback: an
    // arrangement we recognize can still have had a monitor moved within it, and the clamp is the
    // backstop for exactly that.
    for (SessionWindow& window : restored.windows) {
        const QRect clamped = clampInto(window.geometry, screens);
        if (clamped != window.geometry) restored.clamped = true;
        window.geometry = clamped;
    }

    // A document whose file is gone is dropped and named once. Not an error: a file that moved.
    const FileExists present =
        exists ? exists : FileExists([](const QString& path) { return QFileInfo::exists(path); });
    for (SessionWindow& window : restored.windows) {
        std::vector<SessionDocument> kept;
        kept.reserve(window.documents.size());
        for (SessionDocument& document : window.documents) {
            if (document.path.isEmpty() || present(document.path)) {
                kept.push_back(std::move(document));
                continue;
            }
            restored.droppedPaths.push_back(document.path);
            RAWPAGE_LOG_INFO(kSubsystem)
                << "session: not restoring a document that is no longer there: " << document.path;
        }
        // The active tab is an INDEX into a list that just got shorter. Clamping it here rather
        // than in the shell keeps "which tab is active" answerable from the data alone.
        if (window.activeDocument >= static_cast<int>(kept.size()))
            window.activeDocument = kept.empty() ? 0 : static_cast<int>(kept.size()) - 1;
        window.documents = std::move(kept);
    }
    return restored;
}

// -------------------------------------------------------------------------------------------
// The file
// -------------------------------------------------------------------------------------------

SessionState::LoadReport SessionState::load()
{
    session_ = Session{};
    arrangements_.clear();

    LoadReport report;
    QFileInfo info(filePath_);
    if (!info.exists()) return report;   // no session yet is the first-launch case, not a fault
    report.fileExisted = true;
    if (info.isDir()) {
        report.reason = QStringLiteral("a directory is in the way of the session file");
        RAWPAGE_LOG_WARNING(kSubsystem) << "session: " << report.reason << ": " << filePath_;
        return report;
    }

    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly)) {
        report.reason = QStringLiteral("the session file could not be opened");
        RAWPAGE_LOG_WARNING(kSubsystem) << "session: " << report.reason << ": " << filePath_;
        return report;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError error{};
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject()) {
        report.reason = QStringLiteral("the session file is not readable JSON");
        RAWPAGE_LOG_WARNING(kSubsystem)
            << "session: " << report.reason << " (" << error.errorString() << "): " << filePath_;
        return report;
    }

    const QJsonObject root = json.object();
    const int format = root.value(kFormatKey).toInt(0);
    if (format <= 0 || format > kFormat) {
        // A format we do not know is NOT guessed at. A newer build's fields cannot be inferred from
        // here, and a half-understood session is worse than none: the user gets a clean start and
        // the file is replaced on the next clean quit.
        report.reason = QStringLiteral("the session file's format is %1, and this build writes %2")
                            .arg(format)
                            .arg(kFormat);
        RAWPAGE_LOG_WARNING(kSubsystem) << "session: " << report.reason;
        return report;
    }

    for (const QJsonValue& value : root.value(kArrangementsKey).toArray()) {
        const QJsonObject object = value.toObject();
        Arrangement arrangement;
        arrangement.fingerprint = object.value(kFingerprintKey).toString();
        for (const QJsonValue& window : object.value(kWindowsKey).toArray()) {
            const QJsonObject geometry = window.toObject();
            arrangement.windows.push_back(
                WindowGeometry{rectFromJson(geometry),
                               geometry.value(QLatin1String("maximized")).toBool()});
        }
        if (!arrangement.fingerprint.isEmpty()) arrangements_.push_back(std::move(arrangement));
    }
    // The bound is enforced on READ as well as on capture: a file edited by hand, or written by a
    // build whose bound was larger, must not make this one unbounded.
    if (arrangements_.size() > kMaxArrangements) arrangements_.resize(kMaxArrangements);

    const QJsonObject sessionObject = root.value(kSessionKey).toObject();
    session_.fingerprint = sessionObject.value(kFingerprintKey).toString();
    for (const QJsonValue& window : sessionObject.value(kWindowsKey).toArray())
        session_.windows.push_back(windowFromJson(window.toObject()));

    report.loaded = true;
    return report;
}

Result<void> SessionState::save() const
{
    QJsonObject root;
    root[kFormatKey] = kFormat;

    QJsonArray arrangements;
    for (const Arrangement& arrangement : arrangements_) {
        QJsonObject object;
        object[kFingerprintKey] = arrangement.fingerprint;
        QJsonArray windows;
        for (const WindowGeometry& window : arrangement.windows)
            windows.append(rectToJson(window.rect, window.maximized));
        object[kWindowsKey] = windows;
        arrangements.append(object);
    }
    root[kArrangementsKey] = arrangements;

    QJsonObject sessionObject;
    sessionObject[kFingerprintKey] = session_.fingerprint;
    QJsonArray windows;
    for (const SessionWindow& window : session_.windows) windows.append(windowToJson(window));
    sessionObject[kWindowsKey] = windows;
    root[kSessionKey] = sessionObject;

    // `QSaveFile`: the session is replaced atomically or not at all. A half-written file is the
    // one input `load()` treats as corrupt, and writing it over a good one on every quit would
    // make that path the ordinary one rather than the exceptional one.
    // No waiver marker: `core/settings/` carries the filesystem-write privilege as its own
    // `layer-lint` entry, so a waiver would waive nothing and would inflate the `--explain` census
    // that Stage 0's I-20 exists to keep honest -- the reasoning `Settings::flush` already carries.
    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return Error{ErrorCode::IoFailure, QLatin1String("error.session.write"), filePath_};
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return Error{ErrorCode::IoFailure, QLatin1String("error.session.write"), filePath_};
    return {};
}

} // namespace rawpage::core
