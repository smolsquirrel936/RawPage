#include "ui/dialogs/RecoveryDialog.h"

#include "ui/shell/Catalog.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace rawpage::ui {
namespace {

RecoveryDialog::Answer& installedAnswer()
{
    static RecoveryDialog::Answer answer;
    return answer;
}

QString stateText(const RecoveryDialog::Offer& offer)
{
    using State = RecoveryDialog::Offer::State;
    switch (offer.state) {
    case State::IntoFile: return catalog::translate(catalog::key::RecoveryStateIntoFile);
    case State::SourceChanged: return catalog::translate(catalog::key::RecoveryStateSourceChanged);
    case State::Untitled: return catalog::translate(catalog::key::RecoveryStateUntitled);
    case State::SourceMissing: return catalog::translate(catalog::key::RecoveryStateSourceMissing);
    case State::Damaged: return catalog::translate(catalog::key::RecoveryStateDamaged).arg(offer.problem);
    }
    return QString();
}

} // namespace

bool RecoveryDialog::canRecover(const Offer& offer) noexcept
{
    return offer.state == Offer::State::IntoFile || offer.state == Offer::State::SourceChanged
           || offer.state == Offer::State::Untitled;
}

RecoveryDialog::RecoveryDialog(std::vector<Offer> offers, QWidget* parent) : QDialog(parent), offers_(std::move(offers))
{
    setObjectName(QStringLiteral("recoveryDialog"));
    setWindowTitle(catalog::translate(catalog::key::RecoveryTitle));
    setModal(true);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(12);

    auto* intro = new QLabel(catalog::translate(catalog::key::RecoveryIntro), this);
    intro->setWordWrap(true);
    column->addWidget(intro);

    // A SCROLL AREA, because a crash with a dozen documents open is a dozen rows and a dialog taller than the
    // screen hides its own buttons.
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* list = new QWidget(scroll);
    auto* listColumn = new QVBoxLayout(list);
    listColumn->setContentsMargins(0, 0, 0, 0);
    listColumn->setSpacing(14);

    for (const Offer& offer : offers_) {
        Row row;
        auto* box = new QFrame(list);
        box->setFrameShape(QFrame::StyledPanel);
        auto* boxColumn = new QVBoxLayout(box);

        auto* name = new QLabel(box);
        name->setTextFormat(Qt::PlainText);
        name->setText(offer.documentName);
        QFont bold = name->font();
        bold.setBold(true);
        name->setFont(bold);
        boxColumn->addWidget(name);

        auto* path = new QLabel(box);
        path->setTextFormat(Qt::PlainText);
        path->setText(offer.sourcePath);
        path->setWordWrap(true);
        boxColumn->addWidget(path);

        row.state = new QLabel(box);
        row.state->setTextFormat(Qt::PlainText);
        row.state->setText(stateText(offer));
        row.state->setWordWrap(true);
        boxColumn->addWidget(row.state);

        // THE BUTTON NAMES WHAT RECOVER WILL DO: into the file, or into a new one. A person choosing between
        // them should not have to read the sentence above to learn that their file will not be written.
        const bool intoFile = offer.state == Offer::State::IntoFile;
        auto* choices = new QHBoxLayout();
        auto* group = new QButtonGroup(box);
        row.recover = new QRadioButton(catalog::translate(intoFile ? catalog::key::RecoveryRecover
                                                                   : catalog::key::RecoveryRecoverToNewFile), box);
        row.discard = new QRadioButton(catalog::translate(catalog::key::RecoveryDiscard), box);
        row.later = new QRadioButton(catalog::translate(catalog::key::RecoveryLater), box);
        for (QRadioButton* button : {row.recover, row.discard, row.later}) {
            group->addButton(button);
            choices->addWidget(button);
        }
        choices->addStretch(1);
        boxColumn->addLayout(choices);

        // Nothing is thrown away by default: a recoverable row recovers, and a row that cannot be recovered waits.
        row.recover->setEnabled(canRecover(offer));
        (canRecover(offer) ? row.recover : row.later)->setChecked(true);

        listColumn->addWidget(box);
        rows_.push_back(row);
    }
    listColumn->addStretch(1);
    scroll->setWidget(list);
    column->addWidget(scroll, 1);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* proceed = buttons->addButton(catalog::translate(catalog::key::RecoveryContinue), QDialogButtonBox::AcceptRole);
    QPushButton* later = buttons->addButton(catalog::translate(catalog::key::RecoveryLaterAll), QDialogButtonBox::RejectRole);
    proceed->setDefault(true);
    connect(proceed, &QPushButton::clicked, this, &QDialog::accept);
    connect(later, &QPushButton::clicked, this, &QDialog::reject);
    column->addWidget(buttons);
}

std::vector<RecoveryDisposition> RecoveryDialog::dispositions() const
{
    std::vector<RecoveryDisposition> answer;
    answer.reserve(rows_.size());
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const Row& row = rows_[i];
        if (row.recover->isChecked() && canRecover(offers_[i]))
            answer.push_back(RecoveryDisposition::Recover);
        else if (row.discard->isChecked())
            answer.push_back(RecoveryDisposition::Discard);
        else
            answer.push_back(RecoveryDisposition::Later);
    }
    return answer;
}

bool RecoveryDialog::canAsk()
{
    return static_cast<bool>(installedAnswer())
           || QGuiApplication::platformName() != QLatin1String("offscreen");
}

std::optional<std::vector<RecoveryDisposition>> RecoveryDialog::ask(QWidget* parent, const std::vector<Offer>& offers)
{
    if (installedAnswer())
        return installedAnswer()(offers);
    if (!canAsk())
        return std::nullopt;
    RecoveryDialog dialog(offers, parent);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;      // Decide Later for all, the close box, Escape
    return dialog.dispositions();
}

void RecoveryDialog::setAnswerForTesting(Answer answer)
{
    installedAnswer() = std::move(answer);
}

} // namespace rawpage::ui
