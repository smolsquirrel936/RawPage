// NEGATIVE CONTROLS. Every one of these must stay legal, or the rule is "flag everything" and the
// first person to hit it waives it permanently.
#include "ui/Fake.h"

void build(QLabel* label, QWidget* window, QString name) {
    // Properly wrapped -- the whole point of the rule.
    label->setText(tr("Save the document"));
    window->setWindowTitle(QCoreApplication::translate("shell", "shell.window.title"));

    // An OBJECT NAME is an internal identifier and must never be translated.
    label->setObjectName("saveLabel");

    // A catalog KEY passed to a sink is already the translated path, not a sentence.
    label->setText(resolve("action.file.save"));

    // Punctuation and formatting carry no words.
    label->setText(", ");
    label->setText("%1");

    // A mention in prose:  label->setText("Not a real call");
    label->setText(tr("Page %1").arg(name));
}
