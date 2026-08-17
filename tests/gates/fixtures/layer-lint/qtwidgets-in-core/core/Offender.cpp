// LL-6: core/ must build with no display. A widget include breaks that by definition.
#include <QtWidgets/QWidget>
namespace recto::core { void offend() {} }
