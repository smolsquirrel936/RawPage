// The spelling Qt's own documentation recommends for performance -- and the one a rule written
// against raw quotes alone would sail straight past.
#include "ui/Fake.h"
void build(QLabel* label) {
    label->setText(QStringLiteral("Cancel the operation"));
}
