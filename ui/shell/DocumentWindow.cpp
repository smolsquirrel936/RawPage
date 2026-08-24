#include "ui/shell/DocumentWindow.h"

#include "ui/shell/Catalog.h"

#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QVBoxLayout>

namespace rawpage::ui {

DocumentWindow::DocumentWindow(QString path, QWidget* parent)
    : QWidget(parent), path_(std::move(path))
{
    setObjectName(QStringLiteral("documentWindow"));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(48, 48, 48, 48);
    column->setSpacing(12);
    column->addStretch(1);

    auto* heading = new QLabel(title(), this);
    heading->setObjectName(QStringLiteral("documentHeading"));
    QFont headingFont = heading->font();
    headingFont.setPointSizeF(headingFont.pointSizeF() * 1.6);
    heading->setFont(headingFont);
    heading->setAlignment(Qt::AlignHCenter);
    column->addWidget(heading);

    // The path in full, under the name. A tab that says only "report.pdf" when three tabs say the
    // same is a tab strip the user cannot navigate -- and at this stage the path is also the whole
    // of what the product knows about the file, so showing it is showing the truth.
    auto* body = new QLabel(catalog::translate(catalog::key::DocumentPlaceholderBody).arg(path_), this);
    body->setObjectName(QStringLiteral("documentPlaceholderBody"));
    body->setWordWrap(true);
    body->setAlignment(Qt::AlignHCenter);
    column->addWidget(body);

    column->addStretch(2);
}

QString DocumentWindow::title() const
{
    const QFileInfo info(path_);
    const QString name = info.fileName();
    return name.isEmpty() ? catalog::translate(catalog::key::DocumentUntitled) : name;
}

} // namespace rawpage::ui
