#include "ui/dialogs/LinkPropertiesDialog.h"

#include "ui/shell/Catalog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>

namespace rawpage::ui {
namespace {

bool aPersonCanAnswer()
{
    return QGuiApplication::platformName() != QLatin1String("offscreen");
}

LinkPropertiesDialog::Answer& installedProperties()
{
    static LinkPropertiesDialog::Answer answer;
    return answer;
}

DestinationNameDialog::Answer& installedName()
{
    static DestinationNameDialog::Answer answer;
    return answer;
}

LinkOpenDialog::Answer& installedOpen()
{
    static LinkOpenDialog::Answer answer;
    return answer;
}

// The target kinds, in the order the combo lists them -- `Features.md` §5.10's link actions.
enum TargetChoice {
    kKeep = 0,       // as the link has it now
    kPage,           // a page of this document, its view kept
    kCurrentView,    // the view in front
    kNamed,          // a named destination
    kWebLink,        // URI
    kRemotePage,     // GoToR: another PDF's page
    kOpenFile,       // Launch
    kScript,         // JavaScript -- written, never run (D7-12)
    kPrint,          // Named /Print
    kSubmit,         // SubmitForm
    kReset,          // ResetForm
    kNothing,        // no target
};

// ISO 32000-1 Table 166's styles, and Table 173's highlights, in the combos' order.
const std::array<const char*, 5> kStyles{{"S", "D", "U", "B", "I"}};
const std::array<const char*, 4> kHighlights{{"N", "I", "O", "P"}};
const std::array<std::array<double, 3>, 4> kColours{{{0.0, 0.0, 0.0}, {0.8, 0.0, 0.0}, {0.0, 0.5, 0.0}, {0.0, 0.0, 0.8}}};

} // namespace

// --- Choice ---------------------------------------------------------------------------------------------------------

core::Annotation LinkPropertiesDialog::Choice::appliedTo(core::Annotation link) const
{
    link.action = action;
    link.borderWidth = width;
    link.borderStyle = style;
    link.dashPattern = style == QLatin1String("D") ? std::vector<double>{3, 2} : std::vector<double>{};
    link.color = width > 0.0 ? colour : std::vector<double>{};
    link.highlight = highlight;
    return link;
}

LinkPropertiesDialog::Choice LinkPropertiesDialog::Choice::of(const core::Annotation& link)
{
    Choice out;
    out.action = link.action;
    out.width = link.borderWidth.value_or(0.0);
    out.style = link.borderStyle.isEmpty() ? QStringLiteral("S") : link.borderStyle;
    out.colour = link.color;
    out.highlight = link.highlight;
    return out;
}

// --- Properties ----------------------------------------------------------------------------------------------------

LinkPropertiesDialog::LinkPropertiesDialog(const core::Annotation& link, int pageCount, const QStringList& names,
                                           const core::Destination& currentView, QWidget* parent)
    : QDialog(parent), link_(link), currentView_(currentView)
{
    setObjectName(QStringLiteral("linkPropertiesDialog"));
    setWindowTitle(catalog::translate(catalog::key::LinksPropertiesTitle));
    setModal(true);
    auto* column = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    target_ = new QComboBox(this);
    target_->setObjectName(QStringLiteral("targetCombo"));
    for (const auto key : {catalog::key::LinksTargetKeep, catalog::key::LinksTargetPage, catalog::key::LinksTargetCurrentView,
                           catalog::key::LinksTargetNamed, catalog::key::LinksTargetWeb, catalog::key::LinksTargetRemote,
                           catalog::key::LinksTargetFile, catalog::key::LinksTargetScript, catalog::key::LinksTargetPrint,
                           catalog::key::LinksTargetSubmit, catalog::key::LinksTargetReset, catalog::key::LinksTargetNothing})
        target_->addItem(catalog::translate(key));
    form->addRow(catalog::translate(catalog::key::LinksTarget), target_);
    page_ = new QSpinBox(this);
    page_->setObjectName(QStringLiteral("targetPage"));
    page_->setRange(1, std::max(1, std::max(pageCount, 99999)));
    page_->setValue(link.action.kind == core::OutlineAction::Kind::GoTo && link.action.destination.isResolved()
                        ? link.action.destination.pageIndex + 1
                        : 1);
    if (link.action.kind == core::OutlineAction::Kind::GoToRemote) page_->setValue(link.action.destination.pageIndex + 1);
    form->addRow(catalog::translate(catalog::key::LinksTargetPageNumber), page_);
    named_ = new QComboBox(this);
    named_->setObjectName(QStringLiteral("targetNamed"));
    named_->addItems(names);
    if (link.action.kind == core::OutlineAction::Kind::GoToNamed) named_->setCurrentText(link.action.name);
    form->addRow(catalog::translate(catalog::key::LinksTargetDestination), named_);
    text_ = new QLineEdit(this);
    text_->setObjectName(QStringLiteral("targetText"));
    switch (link.action.kind) {
    case core::OutlineAction::Kind::Launch:
    case core::OutlineAction::Kind::GoToRemote: text_->setText(link.action.file); break;
    case core::OutlineAction::Kind::Uri:
    case core::OutlineAction::Kind::SubmitForm: text_->setText(link.action.uri); break;
    case core::OutlineAction::Kind::JavaScript: text_->setText(link.action.script); break;
    default: break;
    }
    form->addRow(catalog::translate(catalog::key::LinksTargetText), text_);

    visible_ = new QCheckBox(catalog::translate(catalog::key::LinksVisible), this);
    visible_->setObjectName(QStringLiteral("visibleCheck"));
    visible_->setChecked(link.borderWidth.value_or(0.0) > 0.0 && !link.color.empty());
    form->addRow(QString(), visible_);
    width_ = new QDoubleSpinBox(this);
    width_->setObjectName(QStringLiteral("widthSpin"));
    width_->setRange(0.5, 12.0);
    width_->setSingleStep(0.5);
    width_->setValue(std::clamp(link.borderWidth.value_or(1.0) > 0.0 ? link.borderWidth.value_or(1.0) : 1.0, 0.5, 12.0));
    form->addRow(catalog::translate(catalog::key::LinksWidth), width_);
    style_ = new QComboBox(this);
    style_->setObjectName(QStringLiteral("styleCombo"));
    for (const auto key : {catalog::key::LinksStyleSolid, catalog::key::LinksStyleDashed, catalog::key::LinksStyleUnderline,
                           catalog::key::LinksStyleBeveled, catalog::key::LinksStyleInset})
        style_->addItem(catalog::translate(key));
    for (std::size_t i = 0; i < kStyles.size(); ++i) {
        if (link.borderStyle == QLatin1String(kStyles[i])) style_->setCurrentIndex(static_cast<int>(i));
    }
    form->addRow(catalog::translate(catalog::key::LinksStyle), style_);
    colour_ = new QComboBox(this);
    colour_->setObjectName(QStringLiteral("colourCombo"));
    for (const auto key : {catalog::key::LinksColourBlack, catalog::key::LinksColourRed, catalog::key::LinksColourGreen,
                           catalog::key::LinksColourBlue})
        colour_->addItem(catalog::translate(key));
    colour_->setCurrentIndex(3);
    if (link.color.size() == 3) {
        for (std::size_t i = 0; i < kColours.size(); ++i) {
            if (std::fabs(kColours[i][0] - link.color[0]) < 0.02 && std::fabs(kColours[i][1] - link.color[1]) < 0.02
                && std::fabs(kColours[i][2] - link.color[2]) < 0.02)
                colour_->setCurrentIndex(static_cast<int>(i));
        }
    }
    form->addRow(catalog::translate(catalog::key::LinksColour), colour_);
    highlight_ = new QComboBox(this);
    highlight_->setObjectName(QStringLiteral("highlightCombo"));
    for (const auto key : {catalog::key::LinksHighlightNone, catalog::key::LinksHighlightInvert, catalog::key::LinksHighlightOutline,
                           catalog::key::LinksHighlightPush})
        highlight_->addItem(catalog::translate(key));
    highlight_->setCurrentIndex(1);   // ISO's default, invert
    for (std::size_t i = 0; i < kHighlights.size(); ++i) {
        if (link.highlight == QLatin1String(kHighlights[i])) highlight_->setCurrentIndex(static_cast<int>(i));
    }
    form->addRow(catalog::translate(catalog::key::LinksHighlight), highlight_);

    column->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
    connect(target_, &QComboBox::currentIndexChanged, this, &LinkPropertiesDialog::showFieldsFor);
    // A NEW link with no target yet starts at *a page* -- the commonest link -- and one being edited at *as it is*.
    target_->setCurrentIndex(link.action.kind == core::OutlineAction::Kind::None ? kPage : kKeep);
    showFieldsFor(target_->currentIndex());
}

void LinkPropertiesDialog::showFieldsFor(int kind)
{
    text_->setEnabled(kind == kWebLink || kind == kRemotePage || kind == kOpenFile || kind == kScript || kind == kSubmit);
    page_->setEnabled(kind == kPage || kind == kRemotePage);
    named_->setEnabled(kind == kNamed && named_->count() > 0);
}

LinkPropertiesDialog::Choice LinkPropertiesDialog::choice() const
{
    Choice out = Choice::of(link_);
    core::OutlineAction& a = out.action;
    const auto kindOnly = [](core::OutlineAction::Kind kind) {
        core::OutlineAction action;
        action.kind = kind;
        return action;
    };
    switch (target_->currentIndex()) {
    case kPage: {
        core::Destination view = link_.action.kind == core::OutlineAction::Kind::GoTo ? link_.action.destination : core::Destination{};
        if (view.fit == core::Destination::Fit::Unknown) view.fit = core::Destination::Fit::Fit;
        view.pageIndex = page_->value() - 1;
        a = core::OutlineAction::goTo(view);
        break;
    }
    case kCurrentView: a = core::OutlineAction::goTo(currentView_); break;
    case kNamed: a = kindOnly(core::OutlineAction::Kind::GoToNamed); a.name = named_->currentText(); break;
    case kWebLink: a = kindOnly(core::OutlineAction::Kind::Uri); a.uri = text_->text().trimmed(); break;
    case kRemotePage:
        a = kindOnly(core::OutlineAction::Kind::GoToRemote);
        a.file = text_->text().trimmed();
        a.destination.pageIndex = page_->value() - 1;
        a.destination.fit = core::Destination::Fit::Fit;
        break;
    case kOpenFile: a = kindOnly(core::OutlineAction::Kind::Launch); a.file = text_->text().trimmed(); break;
    case kScript: a = kindOnly(core::OutlineAction::Kind::JavaScript); a.script = text_->text(); break;
    case kPrint: a = kindOnly(core::OutlineAction::Kind::Named); a.name = QStringLiteral("Print"); break;
    case kSubmit: a = kindOnly(core::OutlineAction::Kind::SubmitForm); a.uri = text_->text().trimmed(); break;
    case kReset: a = kindOnly(core::OutlineAction::Kind::ResetForm); break;
    case kNothing: a = core::OutlineAction{}; break;
    default: break;   // kKeep
    }
    out.width = visible_->isChecked() ? width_->value() : 0.0;
    out.style = QLatin1String(kStyles[static_cast<std::size_t>(std::clamp(style_->currentIndex(), 0, 4))]);
    const std::array<double, 3>& rgb = kColours[static_cast<std::size_t>(std::clamp(colour_->currentIndex(), 0, 3))];
    out.colour = visible_->isChecked() ? std::vector<double>{rgb[0], rgb[1], rgb[2]} : std::vector<double>{};
    const int highlight = std::clamp(highlight_->currentIndex(), 0, 3);
    out.highlight = highlight == 1 ? QString() : QLatin1String(kHighlights[static_cast<std::size_t>(highlight)]);
    return out;
}

bool LinkPropertiesDialog::canAsk()
{
    return static_cast<bool>(installedProperties()) || aPersonCanAnswer();
}

std::optional<LinkPropertiesDialog::Choice> LinkPropertiesDialog::ask(QWidget* parent, const core::Annotation& link, int pageCount,
                                                                      const QStringList& names, const core::Destination& currentView)
{
    if (installedProperties()) return installedProperties()(link, pageCount, names, currentView);
    if (!aPersonCanAnswer()) return std::nullopt;
    LinkPropertiesDialog dialog(link, pageCount, names, currentView, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return dialog.choice();
}

void LinkPropertiesDialog::setAnswerForTesting(Answer answer)
{
    installedProperties() = std::move(answer);
}

// --- A destination's name ----------------------------------------------------------------------------------------

bool DestinationNameDialog::canAsk()
{
    return static_cast<bool>(installedName()) || aPersonCanAnswer();
}

std::optional<QString> DestinationNameDialog::ask(QWidget* parent, const QString& title, const QString& initial)
{
    if (installedName()) return installedName()(initial);
    if (!aPersonCanAnswer()) return std::nullopt;
    bool accepted = false;
    const QString name = QInputDialog::getText(parent, title, catalog::translate(catalog::key::LinksDestinationName),
                                               QLineEdit::Normal, initial, &accepted);
    if (!accepted || name.isEmpty()) return std::nullopt;
    return name;
}

void DestinationNameDialog::setAnswerForTesting(Answer answer)
{
    installedName() = std::move(answer);
}

// --- Before a link opens anything --------------------------------------------------------------------------------

bool LinkOpenDialog::ask(QWidget* parent, core::LinkFollow::Step step, const QString& what)
{
    if (installedOpen()) return installedOpen()(step, what);
    if (!aPersonCanAnswer()) return false;   // nobody to say yes: nothing is opened
    const auto question = [step]() {
        switch (step) {
        case core::LinkFollow::Step::OpenUri: return catalog::translate(catalog::key::LinksAskUri);
        case core::LinkFollow::Step::OpenRemote: return catalog::translate(catalog::key::LinksAskRemote);
        default: return catalog::translate(catalog::key::LinksAskFile);
        }
    }();
    QMessageBox box(QMessageBox::Question, catalog::translate(catalog::key::LinksAskTitle), question.arg(what),
                    QMessageBox::Yes | QMessageBox::No, parent);
    box.setDefaultButton(QMessageBox::No);   // opening something from someone else's document is never the default
    return box.exec() == QMessageBox::Yes;
}

void LinkOpenDialog::setAnswerForTesting(Answer answer)
{
    installedOpen() = std::move(answer);
}

// --- The verify's report ----------------------------------------------------------------------------------------

void LinkReportDialog::show(QWidget* parent, const QStringList& lines)
{
    if (!aPersonCanAnswer()) return;
    QMessageBox box(lines.isEmpty() ? QMessageBox::Information : QMessageBox::Warning, catalog::translate(catalog::key::LinksVerifyTitle),
                    lines.isEmpty() ? catalog::translate(catalog::key::LinksVerifyNone)
                                    : catalog::translate(catalog::key::LinksVerifyFound).arg(lines.size()),
                    QMessageBox::Ok, parent);
    if (!lines.isEmpty()) box.setDetailedText(lines.join(QLatin1Char('\n')));
    box.exec();
}

} // namespace rawpage::ui
