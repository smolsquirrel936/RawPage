// A fixture, not product code: S5 guide I-08's control. The family is still enforced OUTSIDE the action
// table's id column:
//
//   compare.view.title    -- defined in the catalog.      Must NOT be reported.
//   compare.view.missing  -- in the family, defined by no catalog.  MUST be reported.
QString title() { return lookup(QLatin1String("compare.view.title")); }
QString missing() { return lookup(QLatin1String("compare.view.missing")); }
