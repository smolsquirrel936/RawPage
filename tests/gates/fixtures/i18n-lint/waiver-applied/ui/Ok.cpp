// NEGATIVE CONTROL for the escape hatch: the inline waiver must actually waive, or the hatch is
// documentation rather than behaviour.
#include "ui/Fake.h"
void build(QLabel* label) {
    label->setText("Internal build id, never shown to a user");   // i18n-lint: allow-literal(a developer-only diagnostic surface, removed at Stage 15)
}
