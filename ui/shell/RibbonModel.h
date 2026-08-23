// The declarative ribbon layout, parsed -- decision D1-8 (Stage1-AppShell-Plan.md §3).
//
// ---------------------------------------------------------------------------------------------
// WHY THIS IS A SEPARATE FILE FROM RibbonTabBuilder, AND WHY IT NAMES NO WIDGET.
//
// D1-8 requires `res/ribbon.json` to be validated AT BUILD TIME: an unknown action id, a duplicate
// placement, or an empty group must fail the build rather than yield a silently missing button. So
// there are two consumers of this schema -- `tools/ribbon-validate`, which runs before the link,
// and `ui/shell/RibbonTabBuilder`, which runs at startup.
//
// Two consumers is two chances to disagree, and a validator that accepts a file the runtime rejects
// is worse than no validator: it certifies the thing it failed to check. So there is exactly ONE
// parser, and the validator COMPILES THIS TRANSLATION UNIT IN rather than reimplementing it -- the
// same technique `tests/unit` already uses for `app/CommandLine.cpp`, and for the same reason. That
// is only possible because nothing here names a widget: this is a pure
// `QByteArray -> RibbonSpec + diagnostics` function over QtCore and `core/cmd/ActionId.h`.
//
// The consequence to preserve: if you add a field here, both the validator and the builder see it
// on the next build, and neither can be forgotten.
// ---------------------------------------------------------------------------------------------

#ifndef RAWPAGE_UI_SHELL_RIBBONMODEL_H
#define RAWPAGE_UI_SHELL_RIBBONMODEL_H

#include "core/cmd/ActionId.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <vector>

namespace rawpage::ui::ribbon {

// The schema version this build understands. D1-16 asks every vocabulary this stage creates to
// carry a version from its first write, and a layout resource is one: Stage 11's UI customization
// serializes placements that refer to it. A file declaring a version we do not know is refused
// outright rather than read optimistically -- unlike `settings.ini`, whose newer-file rule is to
// preserve what it cannot understand, because a half-understood settings file still runs and a
// half-understood ribbon is a window with buttons missing and no way to tell.
inline constexpr int kSchemaVersion = 1;

// How much room a control asks for. `Large` is the tall text-under-icon button a ribbon puts at the
// head of a group; `Small` is one row of a two- or three-row stack.
enum class ControlSize { Large, Small };

struct Control {
    core::ActionId id{};
    QString        actionStringId;   // kept for diagnostics: the enum has no printable name
    ControlSize    size = ControlSize::Small;
};

struct Group {
    QString              id;
    QString              titleKey;
    // Higher collapses FIRST. A ribbon collapses its least important group before its most
    // important one, so this has to be data rather than child order -- and it is deliberately NOT
    // the same thing as visual order, which is why I-05 exists (see Ribbon.h).
    int                  collapsePriority = 0;
    std::vector<Control> controls;
};

struct Tab {
    QString            id;
    QString            titleKey;
    // A contextual tab is declared here and shown by `Ribbon::setContextualVisible`. Stage 1 proves
    // the mechanism (criterion R-4); the content of each contextual tab is per-feature and belongs
    // to Stages 5-13.
    bool               contextual = false;
    std::vector<Group> groups;
};

struct Spec {
    int              schemaVersion = 0;
    std::vector<Tab> tabs;

    // Every `ribbon.*` catalog key the spec names, tabs and groups, in file order. `Catalog`'s
    // string dump asks for this so that guide case A-72 covers the ribbon: a dump that omitted the
    // tab and group names would keep passing while covering less of the screen, which is exactly
    // the failure ShellKeys.def exists to prevent for the shell's own strings.
    QStringList catalogKeys() const;

    int controlCount() const;
};

// One diagnostic. `line` is 1-based and points into the file as written -- A-51 requires the build
// failure to name the id AND the line, because "unknown action id" without a location in a
// four-hundred-line resource is a search, not a diagnostic.
struct Diagnostic {
    int     line = 0;
    QString message;

    QString formatted(const QString& fileName) const;
};

struct ParseResult {
    Spec                    spec;
    std::vector<Diagnostic> diagnostics;

    bool ok() const { return diagnostics.empty(); }
};

// Parse and validate in one pass. `bytes` is the file as read; it is kept only to resolve offsets
// to line numbers, and nothing is retained after the call.
//
// EVERY error is collected rather than the first one returned. A validator that stops at the first
// unknown id makes fixing a stale resource a sequence of build-edit-build cycles, one per typo,
// which is how a mechanism people are meant to use every day becomes one they route around.
ParseResult parse(const QByteArray& bytes);

// The spec this build ships, loaded from `ribbon.json` beside the executable. Cached after the
// first call: it is read once during composition, and `Catalog::dumpStrings()` asks for it again.
//
// NOT a Qt resource. The `.qm` catalogs made the same choice at T6 for the same reason -- data that
// Stage 15 may want to ship replaceable is data that lives beside the executable. The build-time
// validation is what makes that safe: the file in the tree is proven before it is copied, and the
// runtime parse is THIS function, so an edited copy fails with the same diagnostics rather than
// with a silently thinner ribbon.
const ParseResult& shipped();

// Where `shipped()` looks. Exposed so the widget suite can assert the file was found rather than
// silently testing an empty ribbon, which is the shape of green-but-vacuous this project keeps
// finding in its own harness.
QString shippedPath();

} // namespace rawpage::ui::ribbon

#endif // RAWPAGE_UI_SHELL_RIBBONMODEL_H
