// app/RecoveryPrompt -- at launch, the documents a crashed session did not save, offered once. Stage 3 T11,
// decision D3-11 (`Stage3-Mutation-Plan.md` §3); flow A's `RecoveryPrompt::checkAndOffer()` in
// `Architecture-FileMap.md` §5.
//
// WHAT IT DOES, IN ORDER, AND NOTHING ELSE -- it is `app/`, so it wires and decides no domain rule:
//
//   1. `core::RecoveryJournal::discover` under the recovery root. A directory whose lock is held belongs to a
//      running session and is never listed, so a second instance never offers the first one's documents.
//   2. A journal with NO OPERATIONS is removed without asking. It is a document that was open, or saved, when
//      the process ended with nothing unsaved; offering it would be a question with nothing behind it.
//   3. The rest become `ui::RecoveryDialog` offers -- into the file, into a new file, or not recoverable -- and
//      the dialog answers one disposition each. **Nobody to ask** (convention 16): nothing is recovered and
//      nothing discarded; every journal waits for a launch with a person in front of it, and the log says how
//      many.
//   4. Recover is `ui::MainWindow::recoverDocument`, which replays and reports; Discard is
//      `RecoveryJournal::discardDirectory`; Decide later leaves the directory for the next launch.
//
// WHY IT RUNS BEFORE THE COMMAND LINE'S FILES ARE OPENED (`app/main.cpp`): a file passed on the command line
// that is also a recovery candidate would otherwise open clean first, and the recovery would then be refused
// as already open. Recovered first, the later open finds the tab and focuses it.
//
// WHAT IS NOT HERE: which directory is the root. `app::Services::recoveryRoot` carries it -- empty meaning
// `RecoveryJournal::defaultRoot()` -- and `documents/recoveryDir` (Stage 3 T12) is what sets it.

#ifndef RAWPAGE_APP_RECOVERYPROMPT_H
#define RAWPAGE_APP_RECOVERYPROMPT_H

#include "app/Services.h"
#include "core/cmd/RecoveryJournal.h"
#include "ui/dialogs/RecoveryDialog.h"

namespace rawpage::ui {
class MainWindow;
}

namespace rawpage::app {

class RecoveryPrompt {
public:
    struct Outcome {
        int found = 0;          // directories discovery listed
        int empty = 0;          // removed without asking: no operations
        int offered = 0;        // put in front of the dialog (or its twin)
        int recovered = 0;
        int discarded = 0;
        int kept = 0;           // decided later, refused, or nobody to ask
    };

    static Outcome checkAndOffer(ui::MainWindow& window, const Services& services);

    // How a discovered journal is described to the dialog. Public so a case can read it without a dialog.
    static ui::RecoveryDialog::Offer offerFor(const core::RecoveryCandidate& candidate);
};

} // namespace rawpage::app

#endif // RAWPAGE_APP_RECOVERYPROMPT_H
