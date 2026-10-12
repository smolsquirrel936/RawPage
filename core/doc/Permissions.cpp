// core/doc/Permissions -- implementation. Stage 3 T4. See the header for what is refused and why.

#include "core/doc/Permissions.h"

#include "core/doc/PageTree.h"

#include <algorithm>
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
    case Operation::Annotate:
    case Operation::Attach:
        if (editable_)
            return Status{};
        return Error{ErrorCode::PermissionDenied, refusalKey_, refusalContext_};
    }
    return Error{ErrorCode::PermissionDenied, refusalKey_, refusalContext_};
}

namespace {

bool closeEnough(double a, double b)
{
    const double allowed = std::max(Permissions::kRectTolerance,
                                    Permissions::kRectRelativeTolerance * std::max(std::fabs(a), std::fabs(b)));
    return std::fabs(a - b) <= allowed;   // false for NaN, which is the answer wanted
}

bool sameEntry(const AnnotationFact& engine, const AnnotationFact& cos)
{
    if (engine.dictionary != cos.dictionary) return false;
    if (!engine.dictionary) return true;
    if (engine.subtype != cos.subtype) return false;
    if (engine.rect.has_value() != cos.rect.has_value()) return false;
    if (!engine.rect) return true;
    const PdfRect& a = *engine.rect;
    const PdfRect& b = *cos.rect;
    return closeEnough(a.left(), b.left()) && closeEnough(a.bottom(), b.bottom())
           && closeEnough(a.right(), b.right()) && closeEnough(a.top(), b.top());
}

} // namespace

void Permissions::assessAnnotations(const std::vector<std::vector<AnnotationFact>>& engine,
                                    const std::vector<std::vector<AnnotationFact>>& cos)
{
    disagreeing_.clear();
    const std::size_t pages = std::max(engine.size(), cos.size());
    static const std::vector<AnnotationFact> none;
    for (std::size_t page = 0; page < pages; ++page) {
        const std::vector<AnnotationFact>& theirs = page < engine.size() ? engine[page] : none;
        const std::vector<AnnotationFact>& ours = page < cos.size() ? cos[page] : none;
        bool same = theirs.size() == ours.size();
        for (std::size_t entry = 0; same && entry < theirs.size(); ++entry)
            same = sameEntry(theirs[entry], ours[entry]);
        if (!same) disagreeing_.push_back(static_cast<int>(page));
    }
}

Status Permissions::mayAnnotate(int page) const
{
    if (Status document = may(Operation::Annotate); !document) return document;
    if (std::find(disagreeing_.begin(), disagreeing_.end(), page) != disagreeing_.end()) {
        // Context is the 1-based page number a person would look for, as for the page-tree refusals.
        return Error{ErrorCode::PermissionDenied, QLatin1String("error.edit.annotationsDisagree"),
                     QString::number(page + 1)};
    }
    return Status{};
}

} // namespace rawpage::core
