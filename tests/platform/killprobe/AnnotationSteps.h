// tests/platform/killprobe/AnnotationSteps -- the annotation commands `rawpage_killprobe annotations` and
// `annotsaves` run, one per step, and that `tst_recoverykill` runs again to know what any number of them left.
// Stage 6 T15, guide case A-107 (`Stage6-Annotations-Testing-Guide.md` §5.18), decisions D6-23 and D6-4.
//
// DETERMINISTIC BY CONSTRUCTION, because the harness's expectation IS a second run of them: every name, date,
// colour and byte below is a function of the step's number alone, so step k built in the probe and step k built
// in the harness are the same command. A random name (`core::newAnnotationName`) would make the replayed model
// differ from the harness's for no reason the case is about.
//
// THE CYCLE, five steps, every kind of annotation edit a recovery must carry and one large payload. Steps count
// from 1, as the probe's acknowledgements do; with c = (k - 1) % 5:
//   c == 0   AddAnnotation      an INK stroke of 40 samples, `/NM` kill-ink-<k>
//   c == 1   AddAnnotation      a HIGHLIGHT of two quadrilaterals, `/NM` kill-hl-<k>
//   c == 2   MoveAnnotation     kill-ink-<k-2> moved by (3, -2)
//   c == 3   DeleteAnnotation   kill-hl-<k-2>
//   c == 4   AddAttachment      payload-<k>.bin, 200 KiB -- past D6-23's 64 KiB, so a SIDE FILE
// So step 1 is an ink, step 3 its move, step 5 the first payload.

#ifndef RAWPAGE_TESTS_PLATFORM_KILLPROBE_ANNOTATIONSTEPS_H
#define RAWPAGE_TESTS_PLATFORM_KILLPROBE_ANNOTATIONSTEPS_H

#include "core/cmd/Command.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/cmd/commands/AttachmentCommands.h"
#include "core/doc/Annotation.h"
#include "core/doc/Attachments.h"
#include "core/doc/Document.h"
#include "core/doc/DrawnShapes.h"
#include "core/doc/PageTree.h"

#include <QByteArray>
#include <QString>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace rawpage::tests::killsteps {

inline constexpr std::int64_t kNow = 1791504000;      // 2026-10-09, every step's date
inline constexpr int kCycle = 5;
inline constexpr int kPayloadBytes = 200 * 1024;      // past D6-23's 64 KiB threshold

inline bool isPayloadStep(int k) noexcept { return k >= 1 && (k - 1) % kCycle == 4; }

// 200 KiB a payload step, from a linear congruential sequence seeded by the step -- the same bytes every run.
inline QByteArray payload(int k)
{
    QByteArray bytes(kPayloadBytes, '\0');
    std::uint32_t state = 2166136261u ^ static_cast<std::uint32_t>(k);
    for (char& byte : bytes) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<char>(state >> 24);
    }
    return bytes;
}

inline QString inkName(int k) { return QStringLiteral("kill-ink-%1").arg(k); }
inline QString highlightName(int k) { return QStringLiteral("kill-hl-%1").arg(k); }

// An annotation named `nm` -- `newAnnotation`, its random name replaced so both runs make the same one.
inline core::Annotation named(core::AnnotationSubtype subtype, int page, const QString& nm, int k)
{
    core::Annotation a = core::newAnnotation(subtype, page, QStringLiteral("kill probe"), kNow + k);
    a.nm = nm;
    a.id = core::AnnotationId::ofCreated(nm);
    return a;
}

// Step `k` (1-based) of the cycle, for `document` -- whose page count places it.
inline std::unique_ptr<core::Command> step(int k, const core::Document& document)
{
    const int pages = std::max(1, document.pages().count());
    const int lap = (k - 1) / kCycle;
    const int page = lap % pages;
    const double row = 700.0 - 30.0 * (lap % 20);
    switch ((k - 1) % kCycle) {
    case 0: {
        core::Annotation ink = named(core::AnnotationSubtype::Ink, page, inkName(k), k);
        std::vector<double> stroke;
        for (int s = 0; s < 40; ++s) {
            stroke.push_back(80.0 + 4.0 * s);
            stroke.push_back(row + 6.0 * std::sin(s * 0.5));
        }
        ink.inkList = {std::move(stroke)};
        ink.color = {0.1, 0.2, 0.8};
        ink.borderWidth = 1.5;
        return std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{*core::fittedShape(std::move(ink))});
    }
    case 1: {
        core::Annotation highlight = named(core::AnnotationSubtype::Highlight, page, highlightName(k), k);
        highlight.quadPoints = {300, row + 10, 420, row + 10, 300, row, 420, row,
                                300, row - 4, 380, row - 4, 300, row - 14, 380, row - 14};
        highlight.rect = *core::markupRect(highlight.quadPoints);
        highlight.color = {1.0, 0.9, 0.0};
        return std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{highlight});
    }
    case 2:
        return std::make_unique<core::MoveAnnotationCommand>(
            std::vector<core::AnnotationId>{core::AnnotationId::ofCreated(inkName(k - 2))}, 3.0, -2.0, kNow + k);
    case 3:
        return std::make_unique<core::DeleteAnnotationCommand>(
            std::vector<core::AnnotationId>{core::AnnotationId::ofCreated(highlightName(k - 2))});
    default: {
        const QString key = QStringLiteral("payload-%1.bin").arg(k);
        return std::make_unique<core::AddAttachmentCommand>(std::vector<core::DocumentAttachment>{
            core::DocumentAttachment{key, core::newEmbeddedFile(key, payload(k), QString(), kNow + k)}});
    }
    }
}

} // namespace rawpage::tests::killsteps

#endif // RAWPAGE_TESTS_PLATFORM_KILLPROBE_ANNOTATIONSTEPS_H
