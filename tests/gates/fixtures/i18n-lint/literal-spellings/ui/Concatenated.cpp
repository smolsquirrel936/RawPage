// Built by concatenation, which is also the shape that cannot be translated correctly even if
// someone did wrap the fragment: word order differs between languages.
#include "ui/Fake.h"
void build(QLabel* label, int page) {
    label->setText("Page " + QString::number(page));
}
