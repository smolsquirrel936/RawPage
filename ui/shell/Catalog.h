// Resolving a catalog key to text -- the one place ui/ turns `shell.window.title` into words.
//
// WHY THIS FILE EXISTS AT ALL, when Qt already has tr().
//
// `QObject::tr()` derives the translation CONTEXT from the enclosing class name and takes the
// English sentence as the key. Neither half fits this project (T6):
//
//   * The key is the source string here, not the English sentence. `core/Result.h` forbids
//     user-facing English in `core/` and `ActionId.def` carries one `action.*` key per action, so
//     there is no English in the sources for lupdate to extract and `en` is a real catalog.
//   * The context therefore has to be the key's own namespace -- `action`, `error`, `shell` --
//     and not whichever class happens to contain the call. A label built by `RibbonTabBuilder`
//     and the same label shown by `ToolFinder` must resolve identically, and with tr() they would
//     land in two different contexts and need two catalog entries.
//
// So: the context is the part of the key before the first dot, and the source string is the whole
// key. That convention is what `i18n/rawpage_*.ts` is organised by (`<name>action</name>`,
// `<name>error</name>`, `<name>shell</name>`), and it is what makes a key resolvable from anywhere.
//
// `tools/i18n-lint` sees these calls as wrappers (its `wrappers` list matches `translate(`), and a
// key-shaped literal is not reported as an unwrapped literal in any case -- but the reason a key
// is safe is that it IS a key, not that the linter has a special case for it.

#ifndef RAWPAGE_UI_SHELL_CATALOG_H
#define RAWPAGE_UI_SHELL_CATALOG_H

#include "core/cmd/ActionId.h"
#include "core/cmd/KeyContext.h"
#include "core/settings/SettingsSchema.h"

#include <QString>
#include <QStringList>

namespace rawpage::ui::catalog {

// Resolve any catalog key. `key` must contain a dot; a key with none is a programming error and is
// returned unchanged so the mistake is visible on screen rather than swallowed.
QString translate(const QString& key);

// The label for an action, from `ActionId.def`'s fourth column. Every ActionId has one -- that is
// `i18n-lint`'s `action-label` rule, so this cannot return a key for a well-formed build.
QString translateAction(core::ActionId id);

// Resolve a `core::Error`'s message key and interpolate its untranslated context into the `%1` the
// catalog entry provides. This is the boundary Result.h describes: core/ carries a key plus a file
// name, ui/ turns the pair into a sentence in the user's language.
QString translateError(const QString& messageKey, const QString& context);

// ---------------------------------------------------------------------------------------
// Keys that are BUILT from a table rather than written down -- T14
// ---------------------------------------------------------------------------------------
// A Preferences page names a settings group, and the Shortcuts page names a KeyContext. Neither
// has a catalog key of its own anywhere: the key is derived from the enum's name, so that adding a
// context or a group does not need a second list of names to be updated in step (D1-6, again).
//
// They live here rather than at their two call sites for the reason `ShellKeys.def` gives about
// spelling a key by hand: `dumpStrings()` below has to emit every key the shell can show, and a
// derivation written twice is a derivation that can differ -- at which point A-72 would compare a
// key nothing renders against a rendered key nothing dumps.
QString settingsGroupKey(core::SettingsGroup group);
QString keyContextKey(core::KeyContext context);

// Every key the shell can put on screen -- `ui/shell/ShellKeys.def` plus every `action.*` label --
// as "key<TAB>text", sorted. Printed by `rawpage.exe --dump-ui-strings` (guide case A-72), which
// asks the mechanical question -- did a catalog load -- and cannot answer the editorial one, which
// is manual case M-06.
QStringList dumpStrings();

// The shell's own keys, as constants generated from `ui/shell/ShellKeys.def`. Spelling one by hand
// at a call site is how a key drifts out of the dump A-72 compares.
namespace key {
#define RAWPAGE_SHELL_STRING(Enumerator, Key) inline constexpr QLatin1String Enumerator{Key};
#include "ui/shell/ShellKeys.def"
} // namespace key

} // namespace rawpage::ui::catalog

#endif // RAWPAGE_UI_SHELL_CATALOG_H
