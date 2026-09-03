// pdf/cos/XrefRepair -- implementation. Stage 2 T8. See the header for the repair-as-little-as-
// possible design and for where each of A-21's three forbidden outcomes is prevented.

#include "pdf/cos/XrefRepair.h"

#include "pdf/cos/Lexer.h"
#include "pdf/cos/Parser.h"
#include "pdf/cos/filters/Filter.h"

#include <algorithm>
#include <map>
#include <set>

namespace rawpage::pdf::cos {
namespace {

using core::RepairEntry;
using core::RepairKind;
using core::RepairReport;
using core::RepairSeverity;

// Does an object number and generation immediately precede `at`, where `at` is the offset of the
// `obj` keyword? Walks backwards over `<digits> <whitespace> <digits> <whitespace>`, which is what
// an object header is -- and it uses the LEXER's definition of a regular character rather than a
// second one, because `pdf/cos/Lexer.h` says in as many words that two definitions of "where a
// token can end" in one reader is a bug waiting for an unusual file.
bool headerBefore(std::string_view source, std::size_t at, long long& number, std::size_t& start)
{
    auto skipBackWhitespace = [&source](std::size_t cursor) {
        while (cursor > 0
               && Lexer::isWhitespace(static_cast<std::uint8_t>(source[cursor - 1]))) {
            --cursor;
        }
        return cursor;
    };
    auto skipBackDigits = [&source](std::size_t cursor, bool& any) {
        any = false;
        while (cursor > 0 && source[cursor - 1] >= '0' && source[cursor - 1] <= '9') {
            --cursor;
            any = true;
        }
        return cursor;
    };

    std::size_t cursor = skipBackWhitespace(at);
    if (cursor == at)
        return false;                       // `obj` must be preceded by whitespace

    bool any = false;
    const std::size_t generationEnd = cursor;
    cursor = skipBackDigits(cursor, any);
    if (!any)
        return false;
    const std::size_t generationStart = cursor;

    const std::size_t beforeGeneration = cursor;
    cursor = skipBackWhitespace(cursor);
    if (cursor == beforeGeneration)
        return false;

    cursor = skipBackDigits(cursor, any);
    if (!any)
        return false;

    // The number must not itself be preceded by a regular character, or `12abc 0 obj` would be
    // read as object 12 -- and a byte that happens to spell a digit inside a binary stream is
    // exactly what a full-file scan walks through.
    if (cursor > 0 && Lexer::isRegular(static_cast<std::uint8_t>(source[cursor - 1])))
        return false;

    number = 0;
    for (std::size_t index = cursor; index < generationStart; ++index) {
        if (source[index] < '0' || source[index] > '9')
            break;
        // Clamped rather than wrapped, for the reason `pdf/cos/Lexer` clamps: a wrapped object
        // number is negative, and a negative one indexes something.
        if (number > 100'000'000LL)
            break;
        number = number * 10 + (source[index] - '0');
    }
    (void)generationEnd;
    start = cursor;
    return true;
}

// Every packed object inside an object stream that the scan found. Separate from the main scan
// because it needs the FILTERS, and because a container that will not decode is a report line
// rather than a reason to stop.
void expandObjectStreams(std::string_view source,
                         const std::vector<std::pair<long long, std::size_t>>& headers,
                         std::map<long long, XrefEntry>& entries, RepairReport& report)
{
    Parser parser(source);
    for (const auto& [containerNumber, offset] : headers) {
        auto container = parser.parseIndirectObjectAt(offset);
        if (!container)
            continue;
        const CosObject& stream = container.value().object;
        if (!stream.isStream() || stream.get("Type").text() != "ObjStm")
            continue;

        auto payload = decodeStream(stream, source);
        if (!payload) {
            report.add(RepairEntry{RepairKind::ObjectStreamUnreadable, RepairSeverity::Degraded,
                                   containerNumber, offset, 0});
            continue;
        }
        const Bytes& bytes = payload.value();
        auto packed = Parser::parseObjectStream(
            std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
            stream.get("N").integerValue(), stream.get("First").integerValue());
        if (!packed) {
            report.add(RepairEntry{RepairKind::ObjectStreamUnreadable, RepairSeverity::Degraded,
                                   containerNumber, offset, 0});
            continue;
        }

        long long index = 0;
        for (const IndirectObject& item : packed.value()) {
            // A packed object only wins where the scan found nothing at top level: an object that
            // exists BOTH loose in the file and inside a container is almost always a later
            // incremental update of the packed one, and last-wins at top level is the newer.
            if (entries.find(item.number) == entries.end()) {
                XrefEntry entry;
                entry.type = XrefEntryType::InObjectStream;
                entry.field2 = containerNumber;
                entry.field3 = index;
                entries.emplace(item.number, entry);
            }
            ++index;
        }
    }
}

// Does this table actually work? The check that decides whether the file's own structures are used
// or replaced -- and it is deliberately about the ROOT rather than about every object, because a
// document whose catalog resolves is one a reader can navigate, and walking everything on open
// would make opening a 900-page file proportional to its size.
bool verify(XrefTable& table)
{
    if (table.noteCount(XrefNote::ObjectOffsetWrong) > 0)
        return false;
    const CosObject& root = table.trailer().get("Root");
    if (!root.isReference())
        return false;
    auto catalog = table.fetch(root.reference().number);
    if (!catalog)
        return false;
    return catalog.value().object.isDictionary();
}

void carryOverNotes(const XrefTable& table, RepairReport& report)
{
    // Everything the failed load already discovered. Carried rather than re-derived, so the report
    // can say what was WRONG as well as what was rebuilt -- T8's whole job is to make the second
    // answerable without losing the first.
    for (const XrefNoteRecord& record : table.notes()) {
        switch (record.note) {
        case XrefNote::StartxrefMissing:
            report.add({RepairKind::StartxrefMissing, RepairSeverity::Degraded, -1, record.offset, 0});
            break;
        case XrefNote::StartxrefOutOfRange:
        case XrefNote::StartxrefNotATable:
            report.add({RepairKind::StartxrefUnusable, RepairSeverity::Degraded, -1, record.offset,
                        record.detail});
            break;
        case XrefNote::PrevLoop:
            report.add({RepairKind::XrefPrevLoop, RepairSeverity::Degraded, -1, record.offset, 0});
            break;
        case XrefNote::PrevOutOfRange:
        case XrefNote::SubsectionMalformed:
        case XrefNote::EntryMalformed:
        case XrefNote::XrefStreamUndecodable:
        case XrefNote::XrefStreamFieldsBad:
            report.add({RepairKind::XrefSectionUnreadable, RepairSeverity::Degraded, -1,
                        record.offset, record.detail});
            break;
        case XrefNote::TrailerMissing:
            report.add({RepairKind::TrailerMissing, RepairSeverity::Degraded, -1, record.offset, 0});
            break;
        case XrefNote::SizeDisagrees:
            report.add({RepairKind::SizeDisagrees, RepairSeverity::Info, -1, record.offset,
                        record.detail});
            break;
        case XrefNote::ObjectStreamUnreadable:
            report.add({RepairKind::ObjectStreamUnreadable, RepairSeverity::Degraded,
                        record.detail, record.offset, 0});
            break;
        case XrefNote::ObjectOffsetWrong:
            report.add({RepairKind::ObjectOffsetWrong, RepairSeverity::Recovered, record.detail,
                        record.offset, 0});
            break;
        case XrefNote::None:
            break;
        }
    }
}

} // namespace

std::vector<std::pair<long long, std::size_t>> scanObjectHeaders(std::string_view source)
{
    // LAST DEFINITION WINS. An incremental update appends a new copy of an object, so the later
    // one is the newer -- which is the opposite of the rule `XrefTable` uses when walking the
    // /Prev chain, and for the same reason: there, "first seen" IS the newest section.
    //
    // `corpus/malformed/duplicate-object-number.pdf` is where the two rules visibly differ. Its
    // PROVENANCE row says the XREF's entry wins over the last one in the file -- and that is right
    // when there IS a usable xref, which is why this scan only runs when there is not.
    std::map<long long, std::size_t> latest;

    // A single forward pass. `find` over the whole buffer rather than lexing it, because a full-
    // file scan has to walk through binary stream data that no tokenizer should be asked to make
    // sense of -- the header shape is checked afterwards, backwards, from each candidate.
    for (std::size_t at = source.find("obj"); at != std::string_view::npos;
         at = source.find("obj", at + 3)) {
        // `obj` must be a whole token: `endobj` ends with it, and so does any name containing it.
        const std::size_t after = at + 3;
        if (after < source.size() && Lexer::isRegular(static_cast<std::uint8_t>(source[after])))
            continue;

        long long number = 0;
        std::size_t start = 0;
        if (!headerBefore(source, at, number, start))
            continue;
        latest[number] = start;
    }

    std::vector<std::pair<long long, std::size_t>> headers;
    headers.reserve(latest.size());
    for (const auto& [number, offset] : latest)
        headers.emplace_back(number, offset);
    return headers;
}

core::Result<OpenedFile> openWithRepair(std::string_view source)
{
    OpenedFile opened;

    // --- 0. Encryption, before anything tries to read a string ----------------------------
    // D2-6: the COS reader DECLINES an encrypted document BY NAME rather than half-parsing it.
    // Reading one at Stage 2 goes through PDFium's password callback instead; the
    // `StandardSecurityHandler` is Stage 10's.
    //
    // The refusal is `Unsupported`, not `ParseFailure`, and the distinction is the whole point:
    // this file is not damaged and the repair path must not run on it. Two parsers reaching
    // different conclusions about one protected file is a class of bug that is very hard to see,
    // and one of them declining on purpose is the honest arrangement until the handler exists.
    //
    // The check happens on a table that loaded, because `/Encrypt` is a trailer key -- the
    // cross-reference structure itself is never encrypted, which is what makes this knowable
    // before any string is touched.
    auto loaded = XrefTable::load(source);
    if (loaded && loaded.value().isEncrypted()) {
        return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.pdf.encrypted"),
                           QString::number(static_cast<qulonglong>(
                               loaded.value().trailer().get("Encrypt").reference().number))};
    }

    // --- 1. The file's own structures, first ---------------------------------------------
    if (loaded && verify(loaded.value())) {
        opened.table = std::move(loaded).value();
        opened.usedOriginalXref = true;
        // An Info-level finding is still worth reporting even on a healthy file: a `/Length` that
        // lied and was recovered exactly is the commonest thing in any real PDF, and Stage 3 wants
        // to know before it rewrites the stream. It does NOT make the document "repaired".
        if (opened.table.noteCount(XrefNote::SizeDisagrees) > 0) {
            opened.report.add({RepairKind::SizeDisagrees, RepairSeverity::Info, -1, 0,
                               opened.table.largestObjectNumber()});
        }
        opened.report.setObjectsTotal(opened.table.entryCount());
        return opened;
    }

    // --- 2. Scan --------------------------------------------------------------------------
    opened.usedOriginalXref = false;
    if (loaded)
        carryOverNotes(loaded.value(), opened.report);

    const std::vector<std::pair<long long, std::size_t>> headers = scanObjectHeaders(source);
    if (headers.empty()) {
        // Nothing to work with at all. This is the ONE outcome that is a failure rather than a
        // report: there is no document here, and inventing an empty one would be worse than
        // saying so. `corpus/malformed/junk-before-header.pdf` does NOT land here -- it has
        // objects, just not where the header says.
        return core::Error{core::ErrorCode::ParseFailure, QLatin1String("error.pdf.noObjects"),
                           QString::number(static_cast<qulonglong>(source.size()))};
    }

    // --- 3. Keep what still verifies, replace what does not -------------------------------
    std::map<long long, XrefEntry> entries;
    bool startedFromScratch = true;
    if (loaded) {
        // The good half of a file whose damage is confined to its tail. Only entries whose offset
        // actually holds the object they name survive -- which is checked by `XrefTable::fetch`,
        // and is why this is not simply a copy.
        for (const auto& [number, entry] : loaded.value().entries()) {
            if (entry.type == XrefEntryType::Free)
                continue;
            auto object = loaded.value().fetch(number);
            if (object && object.value().number == number) {
                entries.emplace(number, entry);
                startedFromScratch = false;
            }
        }
    }

    Parser parser(source);
    std::size_t reconstructed = 0;
    for (const auto& [number, offset] : headers) {
        const auto existing = entries.find(number);
        if (existing != entries.end())
            continue;                       // the file's own table already places this one

        auto object = parser.parseIndirectObjectAt(offset);
        if (!object || object.value().number != number)
            continue;

        XrefEntry entry;
        entry.type = XrefEntryType::InFile;
        entry.field2 = static_cast<long long>(offset);
        entry.field3 = object.value().generation;
        entries.emplace(number, entry);
        ++reconstructed;
        opened.report.add({RepairKind::ObjectRecoveredByScan, RepairSeverity::Recovered, number,
                           offset, 0});
    }

    // Objects that exist only inside a compressed container. Without this, a repaired PDF 1.5 file
    // loses its catalog and its whole page tree -- they are usually the packed ones.
    expandObjectStreams(source, headers, entries, opened.report);

    // --- 4. The trailer -------------------------------------------------------------------
    CosObject trailer = loaded ? loaded.value().trailer() : CosObject::makeDictionary({});

    // "Has a /Root" is not the same question as "has a /Root that resolves", and
    // `corpus/malformed/root-points-at-nothing.pdf` is the difference: its trailer names an object
    // that is not in the file. Measured -- that file reached this point having been repaired and
    // with an EMPTY report, which is the one combination A-21 forbids, because "repaired" with
    // nothing to show is a document a user is told nothing about.
    XrefTable probe = XrefTable::rebuilt(source, entries, CosObject::makeDictionary({}));
    const CosObject* root = trailer.find("Root");
    bool rootResolves = false;
    if (root != nullptr && root->isReference()) {
        auto catalog = probe.fetch(root->reference().number);
        rootResolves = catalog && catalog.value().object.isDictionary();
        if (!rootResolves) {
            opened.report.add({RepairKind::ObjectMissing, RepairSeverity::Degraded,
                               root->reference().number, 0, 0});
        }
    }

    if (!rootResolves) {
        // No usable trailer. The catalog can still be found by looking for what it IS rather than
        // by being told where it is -- a file can lose its trailer and remain entirely readable.
        opened.report.add({RepairKind::TrailerRootMissing, RepairSeverity::Degraded, -1, 0, 0});

        long long catalogNumber = -1;
        for (const auto& [number, entry] : entries) {
            (void)entry;
            auto object = probe.fetch(number);
            if (object && object.value().object.get("Type").text() == "Catalog") {
                catalogNumber = number;
                break;
            }
        }
        if (catalogNumber >= 0) {
            std::vector<CosObject::Entry> rebuilt;
            rebuilt.push_back({"Root", CosObject::makeReference(catalogNumber, 0)});
            rebuilt.push_back({"Size", CosObject::makeInteger(
                                           entries.empty() ? 0 : entries.rbegin()->first + 1)});
            trailer = CosObject::makeDictionary(std::move(rebuilt));
        } else {
            // No catalog anywhere. The table is still returned -- an object browser can use it,
            // and T8's criterion is a REPORT rather than a document -- but nothing can render.
            opened.report.add({RepairKind::TrailerMissing, RepairSeverity::Lost, -1, 0, 0});
        }
    }

    std::vector<XrefNoteRecord> carried;
    if (loaded)
        carried = loaded.value().notes();

    opened.table = XrefTable::rebuilt(source, std::move(entries), std::move(trailer),
                                      std::move(carried));
    opened.report.setObjectsTotal(opened.table.entryCount());

    // *"Reconstructed 3 objects of 4,000"* versus *"reconstructed the entire xref"* -- the
    // distinction §5.4 exists for, recorded explicitly rather than inferred from the ratio. A
    // two-object file with two bad offsets has the same ratio as a two-object file rebuilt from
    // nothing, and they are not the same risk.
    opened.report.setXrefRebuiltFromScratch(startedFromScratch && reconstructed > 0);
    return opened;
}

} // namespace rawpage::pdf::cos
