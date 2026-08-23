#include "app/SingleInstance.h"

#include "core/Log.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

#include <utility>

namespace rawpage::app {
namespace {

using core::ErrorCode;
using core::LogSubsystem;

} // namespace

SingleInstance::SingleInstance(platform::ISingleInstanceBus* bus, QString scopeId)
    : bus_(bus), scopeId_(std::move(scopeId))
{
}

SingleInstance::~SingleInstance()
{
    release();
}

QString SingleInstance::scopeIdForExecutable(const QString& executablePath)
{
    // The DIRECTORY, not the file: an installation is a directory, and a build that renames the
    // executable within one is still that installation.
    //
    // Cleaned to Qt's own spelling -- forward slashes, no redundant separators -- so that the same
    // directory reached two ways is one identity. Then case-folded, because Windows paths are
    // case-insensitive and "C:/Program Files/RawPage" and "c:/program files/rawpage" are the same
    // install. toCaseFolded rather than toLower: toLower is locale-dependent, and in a Turkish
    // locale it maps 'I' to a dotless i, which would give one install two identities depending on
    // who logged in.
    const QString directory = QDir::cleanPath(QFileInfo(executablePath).absolutePath());

    const QByteArray digest = QCryptographicHash::hash(
        directory.toCaseFolded().toUtf8(), QCryptographicHash::Sha256);

    // 16 hex characters of SHA-256. This is a namespacing token, not a security boundary: the cost
    // of a collision is two installs sharing an instance, and 64 bits makes that not happen.
    return QStringLiteral("RawPage-") + QString::fromLatin1(digest.left(8).toHex());
}

core::Result<SingleInstance::Role> SingleInstance::claim(bool enabled)
{
    if (!enabled) {
        RAWPAGE_LOG_INFO(LogSubsystem::Ui)
            << "single instance: disabled by general/singleInstance; this process runs standalone";
        role_ = Role::Disabled;
        return role_;
    }

    if (bus_ == nullptr) {
        // Not an error. `platform::Platform` leaves an unimplemented interface null by design, and
        // a build without a bus is a build where every launch is its own instance -- which is a
        // usable product, unlike one that refuses to start.
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "single instance: this platform bundle implements no ISingleInstanceBus;"
            << "every launch will be its own instance";
        role_ = Role::Disabled;
        return role_;
    }

    const core::Result<bool> claimed = bus_->claim(scopeId_);
    if (!claimed) {
        // A failed claim is reported, never swallowed into "assume primary". Two primaries both
        // holding a document open is a data-loss shape, and the symptom appears at save time.
        role_ = Role::Disabled;
        return claimed.error();
    }

    role_ = claimed.value() ? Role::Primary : Role::Secondary;
    RAWPAGE_LOG_INFO(LogSubsystem::Ui)
        << "single instance: scope" << scopeId_
        << (role_ == Role::Primary ? "-- primary" : "-- secondary, forwarding to the running instance");
    return role_;
}

core::Status SingleInstance::listen(platform::ISingleInstanceBus::OpenFilesHandler handler)
{
    if (role_ != Role::Primary || bus_ == nullptr) {
        return core::Error{ErrorCode::Unavailable, QLatin1String("error.instance.notPrimary")};
    }
    return bus_->listen(std::move(handler));
}

core::Status SingleInstance::forward(const QStringList& files)
{
    if (role_ != Role::Secondary || bus_ == nullptr) {
        return core::Error{ErrorCode::Unavailable, QLatin1String("error.instance.notSecondary")};
    }
    return bus_->forward(files);
}

void SingleInstance::release()
{
    if (bus_ != nullptr) bus_->release();
    role_ = Role::Disabled;
}

} // namespace rawpage::app
