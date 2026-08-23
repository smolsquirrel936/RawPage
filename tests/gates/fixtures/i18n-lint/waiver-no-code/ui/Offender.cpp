// A waiver waives a violation ON ITS OWN LINE. A marker on a line with no code has nothing to
// waive, and the violation below it stands.
#include "ui/Fake.h"

// i18n-lint: allow-literal(this line has no code, so it silences nothing)
void build(QLabel* label) {
    label->setText("Save the document");
}
