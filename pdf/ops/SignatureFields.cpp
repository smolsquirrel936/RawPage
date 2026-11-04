// pdf/ops/SignatureFields -- implementation. Stage 7 T6. The header says what counts.

#include "pdf/ops/SignatureFields.h"

#include "pdf/cos/XrefTable.h"

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace rawpage::pdf::ops {
namespace {

// A value, resolved one level when it is a reference.
std::optional<cos::CosObject> resolved(cos::XrefTable& table, const cos::CosObject* value)
{
    if (value == nullptr) return std::nullopt;
    if (!value->isReference()) return *value;
    auto fetched = table.fetch(value->reference().number);
    if (!fetched) return std::nullopt;
    return std::move(fetched.value().object);
}

} // namespace

int SignatureFields::count(cos::XrefTable& table)
{
    const std::optional<cos::CosObject> catalog = resolved(table, table.trailer().find("Root"));
    if (!catalog || !catalog->isDictionary()) return 0;
    const std::optional<cos::CosObject> form = resolved(table, catalog->find("AcroForm"));
    if (!form || !form->isDictionary()) return 0;
    const std::optional<cos::CosObject> fields = resolved(table, form->find("Fields"));
    if (!fields || !fields->isArray()) return 0;

    // Depth first; each entry carries the field type its parent passed down.
    struct Pending {
        cos::CosObject value;
        std::string inheritedType;
    };
    std::vector<Pending> pending;
    for (const cos::CosObject& field : fields->items()) pending.push_back({field, {}});
    std::set<long long> visited;
    int signatures = 0;
    int seen = 0;
    while (!pending.empty() && seen < kMaxFields) {
        Pending next = std::move(pending.back());
        pending.pop_back();
        if (next.value.isReference() && !visited.insert(next.value.reference().number).second) continue;
        const std::optional<cos::CosObject> field = resolved(table, &next.value);
        if (!field || !field->isDictionary()) continue;
        ++seen;
        const cos::CosObject* type = field->find("FT");
        const std::string fieldType = type != nullptr && type->isName() ? type->text() : next.inheritedType;
        if (fieldType == "Sig") {
            const std::optional<cos::CosObject> value = resolved(table, field->find("V"));
            if (value && !value->isNull()) ++signatures;
        }
        if (const std::optional<cos::CosObject> kids = resolved(table, field->find("Kids")); kids && kids->isArray()) {
            for (const cos::CosObject& kid : kids->items()) pending.push_back({kid, fieldType});
        }
    }
    return signatures;
}

} // namespace rawpage::pdf::ops
