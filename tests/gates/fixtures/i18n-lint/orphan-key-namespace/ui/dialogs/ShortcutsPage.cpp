// A fixture, not product code. It references a catalog NAMESPACE rather than a key: the whole key
// is assembled at runtime from a table, exactly as ui/dialogs/ShortcutsPage.cpp assembles
// "prefs.context." + keyContextName(context). Nothing here spells prefs.context.Viewer, and the
// orphan-key rule must still not call that entry an orphan.
QString contextLabel(int context)
{
    return translate(QStringLiteral("prefs.context.") + nameOf(context));
}
