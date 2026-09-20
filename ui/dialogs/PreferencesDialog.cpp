#include "ui/dialogs/PreferencesDialog.h"

#include "core/Log.h"
#include "ui/dialogs/SettingsPage.h"
#include "ui/dialogs/ShortcutsPage.h"
#include "ui/shell/Catalog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace rawpage::ui {
namespace {

using core::LogSubsystem;
using core::SettingsGroup;

// The page's own name in the list on the left. The KEY is derived by `catalog::settingsGroupKey`
// rather than built here, so that `--dump-ui-strings` and this list cannot spell it differently --
// see that function's comment. A page added to `shippedPages()` needs one catalog entry and no
// second name anywhere.
QString groupLabel(SettingsGroup group)
{
    return catalog::translate(catalog::settingsGroupKey(group));
}

} // namespace

const std::vector<SettingsGroup>& PreferencesDialog::shippedPages()
{
    // THE ONE PLACE A PAGE IS ADDED. Everything else -- the list entry, the stacked widget, the
    // rows on the page -- follows from this list and from `SettingsKeys.def`. See the header for
    // why these six and not the other 20.
    static const std::vector<SettingsGroup> pages{
        SettingsGroup::General,

        // S3 T12. `documents/recoveryDir` and `documents/warnOnRepairedSave` (D3-9) beside Stage 1's
        // `recentCount` and `recentPinned`, which had been declared in this group with no page since
        // Stage 1. Placed second, where `SettingsGroup` and Features.md §9.1 put it. Six of 26 groups ship.
        SettingsGroup::Documents,

        // S2 T15. Two rows in this list and sixteen in `SettingsKeys.def` are the WHOLE of the
        // Preferences work for them -- `SettingsPage` builds one typed editor per declared key
        // (convention 17), so there is no widget code per setting and no second list saying which
        // key belongs to which page. Five of 26 groups shipped then.
        SettingsGroup::PageDisplay,

        // S4 T8. The `UnitsAndGuides` group has existed in `SettingsGroup` since Stage 1 with no
        // keys in it; the eleven `units/*` rows fill it, and this ONE LINE is the whole of the
        // page (convention 17). **Seven of 26 groups ship.** Placed where `SettingsGroup` and
        // `Features.md` §9.1 put it: beside the other page-appearance pages rather than at the end.
        SettingsGroup::UnitsAndGuides,

        SettingsGroup::PerformanceAndCache,

        SettingsGroup::Logging,
        SettingsGroup::Shortcuts,
    };
    return pages;
}

PreferencesDialog::PreferencesDialog(const app::Services& services, QWidget* parent)
    : QDialog(parent), services_(services)
{
    setObjectName(QStringLiteral("preferencesDialog"));
    setWindowTitle(catalog::translate(catalog::key::PrefsWindowTitle));

    auto* column = new QVBoxLayout(this);

    auto* body = new QHBoxLayout;
    pageList_ = new QListWidget(this);
    pageList_->setObjectName(QStringLiteral("preferencesPageList"));
    body->addWidget(pageList_);

    pageStack_ = new QStackedWidget(this);
    pageStack_->setObjectName(QStringLiteral("preferencesPageStack"));
    body->addWidget(pageStack_, 1);
    column->addLayout(body, 1);

    for (const SettingsGroup group : shippedPages()) {
        pageList_->addItem(groupLabel(group));
        order_.push_back(group);

        // Shortcuts is the one page that is not a list of settings keys: its subject is
        // `KeyBindingStore`, and the `shortcuts/<context>/<action>` family is declared as a FAMILY
        // rather than as rows (D1-10), so a generated page would have found nothing to show.
        if (group == SettingsGroup::Shortcuts) {
            shortcuts_ = new ShortcutsPage(services_.settings, services_.keyBindings, this);
            pageStack_->addWidget(shortcuts_);
            continue;
        }

        auto* page = new SettingsPage(services_.settings, group, this);
        pages_.emplace(group, page);
        pageStack_->addWidget(page);
    }

    connect(pageList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) pageStack_->setCurrentIndex(row);
    });

    // --- The buttons ---------------------------------------------------------------------------
    // Close and Restore Defaults, and no OK / Cancel / Apply. See the header: there is nothing for
    // Apply to do when every change has already applied, and there is nothing Cancel could undo
    // without a transaction `core::Settings` deliberately does not have.
    auto* buttons = new QDialogButtonBox(this);
    buttons->setObjectName(QStringLiteral("preferencesButtons"));

    QPushButton* restore = buttons->addButton(
        catalog::translate(catalog::key::PrefsButtonRestoreDefaults),
        QDialogButtonBox::ResetRole);
    restore->setObjectName(QStringLiteral("preferencesRestoreDefaults"));

    QPushButton* close = buttons->addButton(
        catalog::translate(catalog::key::PrefsButtonClose), QDialogButtonBox::AcceptRole);
    close->setObjectName(QStringLiteral("preferencesClose"));

    connect(restore, &QPushButton::clicked, this, [this] { restoreCurrentPageDefaults(); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    column->addWidget(buttons);

    if (pageList_->count() > 0) pageList_->setCurrentRow(0);
}

PreferencesDialog::~PreferencesDialog() = default;

void PreferencesDialog::showPage(SettingsGroup group)
{
    for (std::size_t i = 0; i < order_.size(); ++i) {
        if (order_[i] != group) continue;
        pageList_->setCurrentRow(static_cast<int>(i));
        return;
    }
}

SettingsGroup PreferencesDialog::currentPage() const
{
    const int row = pageList_ ? pageList_->currentRow() : -1;
    if (row < 0 || row >= static_cast<int>(order_.size())) return SettingsGroup::General;
    return order_[static_cast<std::size_t>(row)];
}

SettingsPage* PreferencesDialog::settingsPage(SettingsGroup group) const
{
    const auto it = pages_.find(group);
    return it == pages_.end() ? nullptr : it->second;
}

void PreferencesDialog::restoreCurrentPageDefaults()
{
    // THE PAGE, not the whole preference set. §9.1 asks for both; the second one is a destructive
    // action that needs a confirmation, and a confirmation is a modal question this stage has no
    // reason to ask -- Stage 14 owns the full surface.
    const SettingsGroup group = currentPage();
    if (group == SettingsGroup::Shortcuts) {
        if (shortcuts_) shortcuts_->restoreDefaults();
        return;
    }
    if (SettingsPage* page = settingsPage(group)) page->restoreDefaults();
}

} // namespace rawpage::ui
