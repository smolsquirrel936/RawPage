#include "app/RecoveryPrompt.h"

#include "core/Log.h"
#include "core/settings/Settings.h"
#include "platform/platform.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/MainWindow.h"

#include <QFileInfo>

namespace rawpage::app {

using core::LogSubsystem;

ui::RecoveryDialog::Offer RecoveryPrompt::offerFor(const core::RecoveryCandidate& candidate)
{
    using State = ui::RecoveryDialog::Offer::State;
    ui::RecoveryDialog::Offer offer;
    const QFileInfo source(candidate.meta.sourcePath);
    offer.sourcePath = candidate.meta.sourcePath;
    offer.documentName = source.fileName();
    if (candidate.problemKey.size() != 0) {
        offer.state = State::Damaged;
        offer.problem = ui::catalog::translateError(QString(candidate.problemKey), QFileInfo(candidate.directory).fileName());
        if (offer.documentName.isEmpty())
            offer.documentName = QFileInfo(candidate.directory).fileName();   // a meta too damaged to name its source
    } else if (!source.exists() || !source.isFile()) {
        offer.state = State::SourceMissing;
    } else if (candidate.meta.untitled) {
        offer.state = State::Untitled;
    } else {
        offer.state = candidate.sourceMatches ? State::IntoFile : State::SourceChanged;
    }
    return offer;
}

RecoveryPrompt::Outcome RecoveryPrompt::checkAndOffer(ui::MainWindow& window, const Services& services)
{
    Outcome outcome;
    // Where `documents/recoveryDir` says (Stage 3 T12), as `DocumentWindow` writes -- and only there: journals
    // left under a directory the setting no longer names are not offered until it names it again.
    const QString root = core::RecoveryJournal::resolveRoot(services.settings.get(core::keys::documents::recoveryDir),
                                                            services.recoveryRoot);
    platform::IDurableFiles* durableFiles = services.platform.durableFiles.get();

    std::vector<core::RecoveryCandidate> candidates;
    for (core::RecoveryCandidate& candidate : core::RecoveryJournal::discover(root)) {
        ++outcome.found;
        if (candidate.problemKey.size() == 0 && candidate.operationLines == 0) {
            // NOTHING UNSAVED TO OFFER. Removed quietly; if it cannot be removed it stays, and is not offered.
            if (core::RecoveryJournal::discardDirectory(candidate.directory, durableFiles)) {
                ++outcome.empty;
            } else {
                ++outcome.kept;
            }
            continue;
        }
        candidates.push_back(std::move(candidate));
    }
    if (candidates.empty()) {
        // Braced: RAWPAGE_LOG expands to an if/else (`app/CompositionRoot.cpp` says the same).
        if (outcome.found > 0) {
            RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "recovery:" << outcome.found << "journal(s) under" << root
                                              << "held nothing unsaved;" << outcome.empty << "removed";
        }
        return outcome;
    }

    std::vector<ui::RecoveryDialog::Offer> offers;
    offers.reserve(candidates.size());
    for (const core::RecoveryCandidate& candidate : candidates)
        offers.push_back(offerFor(candidate));

    if (!ui::RecoveryDialog::canAsk()) {
        // CONVENTION 16: disabled where there is nobody, never guessed. Recovering would open documents nobody
        // chose; discarding would lose work nobody gave up.
        outcome.kept += static_cast<int>(candidates.size());
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "recovery:" << static_cast<int>(candidates.size())
                                             << "document(s) with unsaved changes kept under" << root
                                             << "-- nobody to ask";
        return outcome;
    }

    outcome.offered = static_cast<int>(candidates.size());
    const std::optional<std::vector<ui::RecoveryDisposition>> answer = ui::RecoveryDialog::ask(&window, offers);

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const core::RecoveryCandidate& candidate = candidates[i];
        const ui::RecoveryDisposition disposition =
            answer && i < answer->size() ? (*answer)[i] : ui::RecoveryDisposition::Later;

        switch (disposition) {
        case ui::RecoveryDisposition::Recover: {
            if (!ui::RecoveryDialog::canRecover(offers[i])) {
                ++outcome.kept;                         // the dialog offers no Recover here; a twin that says it anyway is refused
                break;
            }
            const core::Result<ui::DocumentWindow*> recovered = window.recoverDocument(candidate);
            if (recovered) {
                ++outcome.recovered;
            } else {
                ++outcome.kept;
                RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "recovery: kept" << candidate.directory << "--"
                                                     << recovered.error().messageKey << recovered.error().context;
            }
            break;
        }
        case ui::RecoveryDisposition::Discard:
            if (const core::Status discarded = core::RecoveryJournal::discardDirectory(candidate.directory, durableFiles)) {
                ++outcome.discarded;
                RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "recovery: discarded" << candidate.directory;
            } else {
                ++outcome.kept;
                RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "recovery: could not discard" << candidate.directory << "--"
                                                     << discarded.error().messageKey;
            }
            break;
        case ui::RecoveryDisposition::Later:
            ++outcome.kept;
            break;
        }
    }

    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "recovery: offered" << outcome.offered << "-- recovered" << outcome.recovered
                                      << "discarded" << outcome.discarded << "kept" << outcome.kept;
    return outcome;
}

} // namespace rawpage::app
