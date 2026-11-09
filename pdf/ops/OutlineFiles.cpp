#include "pdf/ops/OutlineFiles.h"

#include "pdf/ops/AttachmentFiles.h"

#include <QFile>
#include <QFileInfo>

namespace rawpage::pdf::ops {

core::Result<std::vector<core::OutlineItem>> OutlineFiles::read(const QString& path, int pageCount, const QString& key)
{
    const QFileInfo info(path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.outline.importUnreadable"), info.fileName()};
    const QByteArray bytes = file.read(core::kMaxOutlineExchangeBytes + 1);
    if (bytes.size() > core::kMaxOutlineExchangeBytes)
        return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.outline.importUnreadable"), info.fileName()};
    return core::readOutlineExchange(bytes, pageCount, key);
}

core::Status OutlineFiles::write(const std::vector<core::OutlineItem>& items, core::OutlineFormat format, const QString& path,
                                 bool overwrite)
{
    const core::Status written = AttachmentFiles::write(core::writeOutlineExchange(items, format), path, overwrite);
    if (written) return written;
    if (written.error().messageKey == QLatin1String("error.attachment.exists"))
        return core::Error{written.error().code, QLatin1String("error.outline.exportExists"), written.error().context};
    return core::Error{written.error().code, QLatin1String("error.outline.exportFailed"), written.error().context};
}

} // namespace rawpage::pdf::ops
