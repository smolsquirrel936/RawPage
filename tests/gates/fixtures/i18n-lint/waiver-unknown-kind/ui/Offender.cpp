// A marker naming a kind no rule declares waives nothing. Without this, a typo in the kind reads as
// a silenced rule instead of as a mistake.
#include "ui/Fake.h"
void build(QLabel* label) {
    label->setText("Save the document");   // i18n-lint: allow-sentence(a kind that does not exist)
}
