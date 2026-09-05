// A fixture, not product code. It carries FOUR references and only ONE of them is a defect, which
// is what makes this fixture a check on the rule's PRECISION as well as on its existence.
//
//   error.font.missingFace   -- a declared family, no catalog entry.  MUST be reported.
//   error.font.noSubstitute  -- a declared family, defined below.     Must NOT be reported.
//   error.repair.            -- a NAMESPACE reference (trailing dot). Must NOT be looked up.
//   settings.ini             -- a dotted literal that is a FILE NAME. Must NOT be looked up, and
//                               this is the half that made a family list necessary: the scanner's
//                               notion of a catalog key is generous on purpose, so a blocking rule
//                               over every dotted literal fails the build on the filesystem.
QString describe(int which)
{
    if (which == 0) return lookup(QLatin1String("error.font.missingFace"));
    if (which == 1) return lookup(QLatin1String("error.font.noSubstitute"));
    if (which == 2) return lookup(QStringLiteral("error.repair.") + kindName(which));
    return readConfig(QLatin1String("settings.ini"));
}
