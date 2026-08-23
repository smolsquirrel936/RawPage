// POSITIVE CONTROL for guide case A-41.
//
// Without this, "an undeclared key does not compile" is equally satisfied by a header that does not
// compile at all -- which is I-15's lesson in its smallest possible form. This file uses the schema
// exactly as product code will and MUST compile.

#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"

using namespace rawpage::core;

double probe(const Settings& settings)
{
    return settings.get(keys::general::uiFontScale);
}

void write(Settings& settings)
{
    settings.set(keys::general::theme, Theme::Dark);
    settings.set(keys::logging::retainFiles, 7);
    settings.setShortcutOverride(ShortcutKey::forAction(KeyContext::Viewer, ActionId::FileOpen),
                                 QStringLiteral("Ctrl+Shift+O"));
}
