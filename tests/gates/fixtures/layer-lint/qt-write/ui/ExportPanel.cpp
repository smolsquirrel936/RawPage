// LL-4 in Qt. The write pattern list had QSaveFile but not this -- and QFile with
// QIODevice::WriteOnly is what a Qt developer writes first (I-15).
#include <QFile>
#include <QIODevice>

namespace recto::ui {
void exportNotes() {
    QFile out("notes.txt");
    out.open(QIODevice::WriteOnly);
    out.write("...");
}
}
