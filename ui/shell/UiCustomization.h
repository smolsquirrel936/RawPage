// What the user changed about the UI, as data -- decision D1-16 applied to the third of the three
// vocabularies Stage 1 writes to disk.
//
// ---------------------------------------------------------------------------------------------
// THE THREE VOCABULARIES, AND WHY THIS ONE NEEDS ITS OWN VERSION STAMP.
//
// Stage 1 creates three things a later build reads back: settings keys, `ActionId` strings, and
// this. The first two are versioned where they live -- `meta/schemaVersion` stamps `settings.ini`,
// and `ActionId.def`'s alias rows keep a renamed id resolving forever. A UI customization stored
// only in `settings.ini` would inherit that and need nothing more.
//
// It is not stored only there. `Features.md` §1.1 says a customization can be EXPORTED to a file
// and imported on another machine, which is how a team deploys a house layout -- and a file that
// leaves this machine is a file that arrives at a build with a different idea of what the format
// is. So the serialized form carries its own `version`, and the two directions are not symmetric:
//
//   * an OLDER version is migrated forward and re-stamped;
//   * a NEWER version is read for the parts we understand and its unknown members are CARRIED
//     THROUGH UNCHANGED (`UiCustomizationData::extras`), so that round-tripping a file through an
//     older build does not silently delete the newer build's customizations. That is the half of
//     D1-16 people forget, and the moment it matters is a rollback -- exactly when a user can
//     least afford to lose their layout.
//
// ---------------------------------------------------------------------------------------------
// WHY THERE IS NO `exportTo(path)` HERE.
//
// `tools/layer-lint` permits a filesystem write in `pdf/` and `core/settings/` and nowhere else,
// and that is not an inconvenience to be waived: a `ui/` class that writes files is a class that
// will one day write a document. This class converts between the in-memory shape and BYTES, and
// whoever has a path hands those bytes to something allowed to write them. The file picker itself
// is dialog work and arrives with T14.
//
// ---------------------------------------------------------------------------------------------
// COMMANDS ARE STORED AS STRING IDS, NEVER AS ENUM VALUES.
//
// `ActionId`'s numbering changes every time a row is inserted into `ActionId.def`; the string id is
// the permanent public name (D1-16), and `core::actionFromStringId` resolves a retired one through
// the alias table. That is what makes a QAT customization written in Stage 7 still open the right
// command in Stage 15 -- and it is why `resolveCommands` reports what it DROPPED rather than
// quietly returning a shorter list.

#ifndef RAWPAGE_UI_SHELL_UICUSTOMIZATION_H
#define RAWPAGE_UI_SHELL_UICUSTOMIZATION_H

#include "core/Result.h"
#include "core/cmd/ActionId.h"
#include "core/cmd/KeyBindingStore.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <vector>

namespace rawpage::core {
class Settings;
}

namespace rawpage::ui {

// Bumped only when the serialized SHAPE changes, never for a new value inside an existing field.
inline constexpr int kUiCustomizationVersion = 1;

struct UiCustomizationData {
    int         version = kUiCustomizationVersion;
    QStringList qatCommands;                  // action string ids, in toolbar order
    bool        qatBelowRibbon = false;

    std::vector<core::KeyBindingStore::OverrideEntry> shortcuts;

    // Members a NEWER build wrote and this one does not understand. Re-emitted verbatim by
    // `serialize`, which is what makes the downgrade path lossless rather than merely non-fatal.
    QJsonObject extras;
};

class UiCustomization {
public:
    // The live customization, read from where it actually lives: the QAT from `Settings`, the
    // shortcut overrides from the store. Nothing here caches -- a cached copy is a copy that
    // disagrees with the toolbar the moment somebody drags a button.
    static UiCustomizationData fromSettings(const core::Settings& settings,
                                            const core::KeyBindingStore& bindings);

    // The inverse. Returns how many entries were dropped as unresolvable, so a caller can report a
    // partially-understood import instead of implying a clean one.
    struct ApplyReport {
        int droppedCommands = 0;
        int droppedShortcuts = 0;
        bool migrated = false;
    };
    static ApplyReport applyToSettings(const UiCustomizationData& data, core::Settings& settings,
                                       core::KeyBindingStore& bindings);

    static QByteArray serialize(const UiCustomizationData& data);

    // A malformed blob is an `Error`, not an assertion and not an empty customization: an import
    // that silently produces "no customizations" is indistinguishable from a successful reset, and
    // the user who just picked the wrong file needs to be told which it was.
    static core::Result<UiCustomizationData> parse(const QByteArray& bytes);

    // String ids -> ids, through the alias table. `dropped` collects the ids that resolved to
    // nothing; pass nullptr only when a caller genuinely cannot report them.
    static std::vector<core::ActionId> resolveCommands(const QStringList& stringIds,
                                                       QStringList* dropped = nullptr);
    static QStringList toStringIds(const std::vector<core::ActionId>& commands);
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_UICUSTOMIZATION_H
