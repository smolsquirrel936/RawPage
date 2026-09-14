// core/doc/Permissions -- implementation. Stage 3 T4. See the header for what is refused and why.

#include "core/doc/Permissions.h"

#include "core/doc/PageTree.h"

#include <cmath>

namespace rawpage::core {

Permissions Permissions::refused(const char* key, QString context)
{
    Permissions permissions;
    permissions.editable_ = false;
    permissions.refusalKey_ = QLatin1String(key);
    permissions.refusalContext_ = std::move(context);
    return permissions;
}

Permissions Permissions::assess(const PageTree& engine, CosReading reading,
                                const std::vector<CosPageFacts>& cos)
{
    if (reading == CosReading::Encrypted)
        return refused("error.edit.encrypted");
    if (reading == CosReading::NoPageMap)
        return refused("error.edit.noPageMap");

    if (static_cast<std::size_t>(engine.count()) != cos.size()) {
        return refused("error.edit.pageCountDisagrees",
                       QStringLiteral("%1/%2").arg(engine.count()).arg(cos.size()));
    }

    for (int index = 0; index < engine.count(); ++index) {
        const CosPageFacts& ours = cos[static_cast<std::size_t>(index)];
        const Page* theirs = engine.at(index);
        // Context is the 1-based page number a person would look for, untranslated (core/Result.h).
        const QString page = QString::number(index + 1);

        if (static_cast<int>(theirs->rotation()) != ours.rotation)
            return refused("error.edit.pageRotationDisagrees", page);

        // A first-party reading with no /MediaBox anywhere cannot be compared on size -- the engine
        // substitutes a default the file never stated -- so only a stated size is held to agree.
        if (ours.mediaSize) {
            const PdfSize engineSize = theirs->mediaBox().size;
            if (std::fabs(std::fabs(ours.mediaSize->width) - std::fabs(engineSize.width)) > kSizeTolerance
                || std::fabs(std::fabs(ours.mediaSize->height) - std::fabs(engineSize.height))
                       > kSizeTolerance) {
                return refused("error.edit.pageSizeDisagrees", page);
            }
        }
    }

    Permissions permissions;
    permissions.editable_ = true;
    permissions.refusalKey_ = QLatin1String("");
    return permissions;
}

Status Permissions::may(Operation operation) const
{
    switch (operation) {
    case Operation::EditPages:
        if (editable_)
            return Status{};
        return Error{ErrorCode::PermissionDenied, refusalKey_, refusalContext_};
    }
    return Error{ErrorCode::PermissionDenied, refusalKey_, refusalContext_};
}

} // namespace rawpage::core
