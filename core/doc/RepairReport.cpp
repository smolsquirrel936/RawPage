#include "core/doc/RepairReport.h"

#include <QStringList>

#include <algorithm>

namespace rawpage::core {

void RepairReport::add(const RepairEntry& entry)
{
    entries_.push_back(entry);

    // An object is counted as RECONSTRUCTED only when it was actually rebuilt -- found by scanning
    // rather than through the table, or recovered from a wrong offset. A `StreamLengthRecovered`
    // is not a reconstruction of the OBJECT: the object was where the table said, and one field
    // inside it was wrong. Conflating the two inflates the numerator of A-22's ratio and makes
    // every ordinary real-world PDF look like a rebuild.
    if (entry.kind == RepairKind::ObjectRecoveredByScan
        || entry.kind == RepairKind::ObjectOffsetWrong) {
        ++objectsReconstructed_;
    }
}

bool RepairReport::requiresSaveAs() const noexcept
{
    // Anything worse than `Info`. A `/Length` that was recovered exactly is `Info` and is the
    // commonest finding in any real PDF -- forcing Save-As on it would put the prompt in front of
    // users constantly, which is how a warning stops being read.
    return worstSeverity() > RepairSeverity::Info;
}

RepairSeverity RepairReport::worstSeverity() const noexcept
{
    RepairSeverity worst = RepairSeverity::Info;
    for (const RepairEntry& entry : entries_)
        worst = std::max(worst, entry.severity);
    return worst;
}

std::size_t RepairReport::count(RepairKind kind) const noexcept
{
    std::size_t total = 0;
    for (const RepairEntry& entry : entries_) {
        if (entry.kind == kind)
            ++total;
    }
    return total;
}

QLatin1String RepairReport::summaryKey() const noexcept
{
    if (entries_.empty())
        return QLatin1String("error.repair.none");
    if (xrefRebuilt_)
        return QLatin1String("error.repair.wholeXref");
    if (objectsReconstructed_ > 0)
        return QLatin1String("error.repair.objects");
    return QLatin1String("error.repair.minor");
}

QStringList RepairReport::summaryArguments() const
{
    // Untranslated detail only -- numbers. `core/Result.h`'s rule: never a whole sentence, because
    // word order differs between languages and the catalog entry owns the sentence.
    QStringList arguments;
    if (entries_.empty())
        return arguments;
    if (xrefRebuilt_) {
        arguments << QString::number(static_cast<qulonglong>(objectsTotal_));
        return arguments;
    }
    if (objectsReconstructed_ > 0) {
        arguments << QString::number(static_cast<qulonglong>(objectsReconstructed_))
                  << QString::number(static_cast<qulonglong>(objectsTotal_));
        return arguments;
    }
    arguments << QString::number(static_cast<qulonglong>(entries_.size()));
    return arguments;
}

// No `default:` label, so a new enumerator is a -Wswitch error rather than a "?" in a report.
const char* repairKindName(RepairKind kind) noexcept
{
    switch (kind) {
    case RepairKind::None:                   return "None";
    case RepairKind::StartxrefMissing:       return "StartxrefMissing";
    case RepairKind::StartxrefUnusable:      return "StartxrefUnusable";
    case RepairKind::XrefSectionUnreadable:  return "XrefSectionUnreadable";
    case RepairKind::XrefPrevLoop:           return "XrefPrevLoop";
    case RepairKind::ObjectOffsetWrong:      return "ObjectOffsetWrong";
    case RepairKind::ObjectRecoveredByScan:  return "ObjectRecoveredByScan";
    case RepairKind::ObjectMissing:          return "ObjectMissing";
    case RepairKind::ObjectStreamUnreadable: return "ObjectStreamUnreadable";
    case RepairKind::StreamLengthRecovered:  return "StreamLengthRecovered";
    case RepairKind::StreamEndstreamMissing: return "StreamEndstreamMissing";
    case RepairKind::TrailerMissing:         return "TrailerMissing";
    case RepairKind::TrailerRootMissing:     return "TrailerRootMissing";
    case RepairKind::SizeDisagrees:          return "SizeDisagrees";
    }
    return "?";
}

const char* repairSeverityName(RepairSeverity severity) noexcept
{
    switch (severity) {
    case RepairSeverity::Info:      return "Info";
    case RepairSeverity::Recovered: return "Recovered";
    case RepairSeverity::Degraded:  return "Degraded";
    case RepairSeverity::Lost:      return "Lost";
    }
    return "?";
}

} // namespace rawpage::core
