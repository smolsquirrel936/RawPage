// pdf/ops/AttachmentFiles -- implementation. Stage 6 T11. See the header.

#include "pdf/ops/AttachmentFiles.h"

#include "pdf/ops/EmbeddedFileReader.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

namespace rawpage::pdf::ops {

core::Result<QByteArray> AttachmentFiles::read(cos::XrefTable& table, const core::AttachmentRef& attachment)
{
    const auto notFound = [&attachment] {
        return core::Error{core::ErrorCode::NotFound, QLatin1String("error.attachment.unknown"),
                           attachment.kind == core::AttachmentRef::Kind::Document ? attachment.key
                                                                                 : attachment.annotation.toString()};
    };
    if (attachment.kind == core::AttachmentRef::Kind::Document) {
        for (const NameTreeEntry& entry : EmbeddedFileReader::entries(table)) {
            if (entry.key.isString() && EmbeddedFileReader::decodeTextString(entry.key.text()) == attachment.key)
                return EmbeddedFileReader::dataOf(table, &entry.value);
        }
        return notFound();
    }
    if (attachment.annotation.kind != core::AnnotationId::Kind::Object) return notFound();
    auto annotation = table.fetch(attachment.annotation.object);
    if (!annotation || !annotation.value().object.isDictionary()) return notFound();
    return EmbeddedFileReader::dataOf(table, annotation.value().object.find("FS"));
}

core::Status AttachmentFiles::write(const QByteArray& bytes, const QString& path, bool overwrite)
{
    const QFileInfo target(path);
    if (path.isEmpty() || target.fileName().isEmpty())
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.attachment.writeFailed"), path};
    if (target.exists() && !overwrite)
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.attachment.exists"), path};
    if (target.exists() && !target.isFile())
        return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.attachment.writeFailed"), path};
    if (!QDir().mkpath(target.absolutePath()))
        return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.attachment.writeFailed"), target.absolutePath()};

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.attachment.writeFailed"), path};
    }
    if (!file.commit())
        return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.attachment.writeFailed"), path};
    return core::Status{};
}

} // namespace rawpage::pdf::ops
