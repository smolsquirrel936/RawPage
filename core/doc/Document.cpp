// core/doc/Document -- implementation. Stage 3 T4. See the header for what the model holds.

#include "core/doc/Document.h"

namespace rawpage::core {

Status Document::hydrate(const IDocumentBackend& backend, DocumentHandle handle)
{
    const PageTree* pages = backend.pages(handle);
    if (pages == nullptr) {
        return Error{ErrorCode::NotFound, QLatin1String("error.open.notFound"),
                     QString::number(static_cast<qulonglong>(handle.id))};
    }

    // Everything is read before anything is assigned, so a failure above leaves the document as it
    // was -- the same "partial application is a bug" rule a command lives under.
    const RepairReport* repair = backend.repairReport(handle);
    const Permissions* permissions = backend.permissions(handle);
    const DocumentId* id = backend.documentId(handle);

    handle_ = handle;
    pages_ = *pages;
    repair_ = repair ? *repair : RepairReport{};
    permissions_ = permissions ? *permissions : Permissions{};
    id_ = id ? *id : DocumentId{};
    revision_ = Revision::initial();
    return Status{};
}

} // namespace rawpage::core
