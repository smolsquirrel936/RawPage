#include "ui/shell/RibbonTabBuilder.h"

#include "core/Log.h"
#include "core/cmd/ActionRegistry.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/RawPageAction.h"
#include "ui/shell/Ribbon.h"

#include <QGridLayout>
#include <QToolButton>
#include <QWidget>

namespace rawpage::ui {
namespace {

using core::LogSubsystem;

// A group's controls, laid out the way a ribbon lays them out: `Large` buttons occupy the group's
// full height in their own column, `Small` buttons stack three to a column. Three rather than two
// because that is what the reference (P-4) does at this body height, and mixing the two conventions
// inside one ribbon is the sort of thing R-1 is judged against by eye.
constexpr int kSmallRowsPerColumn = 3;

QToolButton* makeButton(RawPageAction* action, ribbon::ControlSize size, QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::TabFocus);   // keyboard operability, retained

    if (size == ribbon::ControlSize::Large) {
        // Text UNDER icon is the ribbon idiom even with no icon to put above it. Stage 1 ships no
        // icon set -- see the deviation recorded for T10 -- so a large button is currently a tall
        // text button, which is honest about what exists rather than a placeholder glyph that
        // implies an icon is coming from somewhere.
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setFixedHeight(kRibbonBodyHeight - kRibbonTitleHeight - kRibbonGroupMargin - 6);
        button->setMinimumWidth(56);
    } else {
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setFixedHeight((kRibbonBodyHeight - kRibbonTitleHeight - kRibbonGroupMargin - 6)
                               / kSmallRowsPerColumn);
    }
    return button;
}

} // namespace

RibbonTabBuilder::RibbonTabBuilder(Ribbon& ribbon, ActionFactory& actions)
    : ribbon_(ribbon), actions_(actions)
{
}

QStringList RibbonTabBuilder::build(const ribbon::ParseResult& parsed)
{
    QStringList problems;
    for (const ribbon::Diagnostic& diagnostic : parsed.diagnostics)
        problems << diagnostic.formatted(ribbon::shippedPath());

    for (const ribbon::Tab& tab : parsed.spec.tabs) {
        auto* page = new QWidget;
        page->setObjectName(tab.id);

        // The custom layout, not a QHBoxLayout. See Ribbon.h: collapse changes what a child IS, and
        // no stock layout can express that.
        auto* pageLayout = new RibbonPageLayout(page);

        for (const ribbon::Group& group : tab.groups) {
            auto* widget = new RibbonGroup(catalog::translate(group.titleKey), page);
            widget->setObjectName(group.id);
            widget->setCollapsePriority(group.collapsePriority);

            auto* content = new QWidget(widget);
            auto* grid    = new QGridLayout(content);
            grid->setContentsMargins(0, 0, 0, 0);
            grid->setSpacing(2);

            int column   = 0;
            int smallRow = 0;
            for (const ribbon::Control& control : group.controls) {
                // THE SINGLE PLACEMENT CALL. Note what is absent: no `if (id == ActionId::FileOpen)`
                // anywhere. The button is built from the id, the label comes from ActionId.def
                // through the catalog, and the enabled state comes from the registry -- so a command
                // that Stage 7 registers becomes live here with no edit to this file.
                RawPageAction* action = actions_.create(control.id, content);
                QToolButton*   button = makeButton(action, control.size, content);

                // T11: right-clicking a ribbon control offers to put it on the Quick Access
                // Toolbar. Which control was clicked is the command's parameter -- the ribbon
                // reports it, `MainWindow` routes it, and the QAT holds it. See
                // QuickAccessToolbar.h on why a parameterized command needs that at all (O11).
                ribbon_.attachControl(button, control.id);

                if (control.size == ribbon::ControlSize::Large) {
                    if (smallRow != 0) { ++column; smallRow = 0; }
                    grid->addWidget(button, 0, column, kSmallRowsPerColumn, 1);
                    ++column;
                } else {
                    grid->addWidget(button, smallRow, column);
                    if (++smallRow == kSmallRowsPerColumn) { smallRow = 0; ++column; }
                }
            }

            widget->setContent(content);
            pageLayout->addWidget(widget);
        }

        ribbon_.addPage(catalog::translate(tab.titleKey), page, tab.contextual);

        // The FIRST tab-order pass, before any resize has happened. Without it the chain is
        // whatever creation order produced, and the first collapse is what would otherwise
        // establish it -- which is I-05 arriving one layout later instead of never.
        pageLayout->restoreTabOrder();
    }

    if (!problems.isEmpty()) {
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui)
            << "ribbon resource has" << problems.size()
            << "problem(s); the ribbon is thinner than it should be --" << problems.first();
    } else {
        RAWPAGE_LOG_INFO(LogSubsystem::Ui)
            << "ribbon built from" << ribbon::shippedPath() << "--" << ribbon_.pageCount()
            << "tabs," << parsed.spec.controlCount() << "controls";
    }

    return problems;
}

} // namespace rawpage::ui
