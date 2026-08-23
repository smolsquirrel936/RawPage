// I-20, transplanted. A marker QUOTED in prose must waive nothing -- and must not be counted in the
// census, because a census that includes documentation cannot show waivers creeping upward.
#include "ui/Fake.h"
void build(QLabel* label) {
    label->setText("Save the document");   // waive it with // i18n-lint: allow-literal(reason)
}
