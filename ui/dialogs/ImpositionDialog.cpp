// ui/dialogs/ImpositionDialog -- see the header.

#include "ui/dialogs/ImpositionDialog.h"

#include "core/settings/ProfileStore.h"
#include "ui/shell/Catalog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace rawpage::ui {
namespace {

bool aPersonCanAnswer()
{
    return QGuiApplication::platformName() != QLatin1String("offscreen");
}

ImpositionDialog::Answer& installed()
{
    static ImpositionDialog::Answer answer;
    return answer;
}

constexpr core::ImpositionOrder kOrders[] = {core::ImpositionOrder::Across, core::ImpositionOrder::Down};
constexpr core::ImpositionBinding kBindings[] = {core::ImpositionBinding::Left, core::ImpositionBinding::Right};
constexpr core::SheetOrientation kOrientations[] = {core::SheetOrientation::Auto, core::SheetOrientation::Portrait,
                                                    core::SheetOrientation::Landscape};
constexpr core::DuplexFlip kFlips[] = {core::DuplexFlip::OneSided, core::DuplexFlip::LongEdge, core::DuplexFlip::ShortEdge};

QDoubleSpinBox* points(QWidget* parent, const char* name, double low, double high, double step, int decimals = 1)
{
    auto* box = new QDoubleSpinBox(parent);
    box->setObjectName(QLatin1String(name));
    box->setRange(low, high);
    box->setSingleStep(step);
    box->setDecimals(decimals);
    return box;
}

} // namespace

const std::vector<ImpositionPaper>& impositionPapers()
{
    static const std::vector<ImpositionPaper> papers{
        {catalog::key::ImpositionPaperLetter, core::PdfSize{612.0, 792.0}},  {catalog::key::ImpositionPaperLegal, core::PdfSize{612.0, 1008.0}},
        {catalog::key::ImpositionPaperTabloid, core::PdfSize{792.0, 1224.0}}, {catalog::key::ImpositionPaperA3, core::PdfSize{842.0, 1191.0}},
        {catalog::key::ImpositionPaperA4, core::PdfSize{595.0, 842.0}},       {catalog::key::ImpositionPaperA5, core::PdfSize{420.0, 595.0}},
    };
    return papers;
}

// ==========================================================================================
// ImpositionPreview
// ==========================================================================================

ImpositionPreview::ImpositionPreview(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("impositionPreview"));
    setMinimumSize(220, 220);
}

void ImpositionPreview::show(std::optional<core::ImpositionPlan> plan, int side)
{
    plan_ = std::move(plan);
    side_ = side;
    update();
}

void ImpositionPreview::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().window());
    if (!plan_ || plan_->sides.empty()) return;
    const core::ImpositionPlan& plan = *plan_;
    const core::ImposedSide& side = plan.sides[static_cast<std::size_t>(std::clamp(side_, 0, static_cast<int>(plan.sides.size()) - 1))];
    const double scale = std::min((width() - 16.0) / plan.sheet.width, (height() - 16.0) / plan.sheet.height);
    const double ox = (width() - plan.sheet.width * scale) / 2.0;
    const double oy = (height() - plan.sheet.height * scale) / 2.0;
    // Sheet space (y up) to the widget, through the side's half turn when it has one.
    const auto map = [&](const core::PdfRect& r) {
        const core::PdfRect s = core::onSheet(r, side, plan.sheet);
        return QRectF(ox + s.left() * scale, oy + (plan.sheet.height - s.top()) * scale, s.size.width * scale, s.size.height * scale);
    };
    const auto point = [&](core::PdfPoint p) {
        const core::PdfRect s = core::onSheet(core::PdfRect{p, core::PdfSize{0, 0}}, side, plan.sheet);
        return QPointF(ox + s.left() * scale, oy + (plan.sheet.height - s.bottom()) * scale);
    };
    painter.fillRect(QRectF(ox, oy, plan.sheet.width * scale, plan.sheet.height * scale), Qt::white);
    painter.setPen(QPen(Qt::darkGray, 1.0));
    painter.drawRect(QRectF(ox, oy, plan.sheet.width * scale, plan.sheet.height * scale));
    for (const core::ImposedCell& cell : side.cells) {
        painter.setPen(QPen(QColor(200, 200, 200), 1.0, Qt::DotLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(map(cell.cell));
        if (cell.placed.size.width <= 0.0) continue;
        const QRectF placed = map(cell.placed).intersected(map(cell.cell));
        painter.setPen(QPen(Qt::black, 1.0));
        painter.setBrush(cell.page < 0 ? QColor(235, 235, 235) : QColor(250, 250, 255));
        painter.drawRect(placed);
        if (cell.page < 0) continue;
        // The page's number, upside down on a turned side -- as the sheet will print it.
        painter.save();
        painter.translate(placed.center());
        if (side.turned) painter.rotate(180.0);
        QFont font = painter.font();
        font.setPixelSize(std::max(8, static_cast<int>(std::min(placed.width(), placed.height()) / 3.0)));
        painter.setFont(font);
        painter.drawText(QRectF(-placed.width() / 2.0, -placed.height() / 2.0, placed.width(), placed.height()), Qt::AlignCenter,
                         QString::number(cell.page + 1));
        painter.restore();
    }
    painter.setPen(QPen(Qt::black, 1.0));
    for (const core::ImposedLine& line : side.cropMarks) painter.drawLine(point(line.from), point(line.to));
    painter.setBrush(Qt::NoBrush);
    for (const core::PdfPoint& centre : plan.registration) {
        const double r = core::kRegistrationRadius * scale;
        painter.drawEllipse(point(centre), r, r);
    }
    if (plan.colourBar.size.width > 0.0) painter.fillRect(map(plan.colourBar), QColor(120, 120, 120));
}

// ==========================================================================================
// ImpositionDialog
// ==========================================================================================

ImpositionDialog::ImpositionDialog(const core::ImpositionSpec& initial, std::vector<core::PdfSize> shown, core::ProfileStore* profiles,
                                   const QString& profile, QWidget* parent)
    : QDialog(parent), shown_(std::move(shown)), profiles_(profiles)
{
    setObjectName(QStringLiteral("impositionDialog"));
    setWindowTitle(catalog::translate(initial.layout == core::ImpositionLayout::Booklet ? catalog::key::ImpositionBookletTitle
                                                                                        : catalog::key::ImpositionNUpTitle));
    setModal(true);
    auto* columns = new QHBoxLayout(this);
    columns->setContentsMargins(16, 16, 16, 16);
    columns->setSpacing(16);
    auto* column = new QVBoxLayout();
    column->setSpacing(10);
    columns->addLayout(column, 3);

    // --- Profiles ---
    auto* profileRow = new QHBoxLayout();
    profileRow->addWidget(new QLabel(catalog::translate(catalog::key::DecorateProfile), this));
    profile_ = new QComboBox(this);
    profile_->setObjectName(QStringLiteral("profileBox"));
    profileRow->addWidget(profile_, 1);
    profileName_ = new QLineEdit(this);
    profileName_->setObjectName(QStringLiteral("profileNameField"));
    profileName_->setPlaceholderText(catalog::translate(catalog::key::DecorateProfileName));
    profileName_->setMaxLength(core::ProfileStore::kMaxNameLength);
    profileRow->addWidget(profileName_, 1);
    saveProfile_ = new QPushButton(catalog::translate(catalog::key::DecorateProfileSave), this);
    saveProfile_->setObjectName(QStringLiteral("saveProfileButton"));
    profileRow->addWidget(saveProfile_);
    deleteProfile_ = new QPushButton(catalog::translate(catalog::key::DecorateProfileDelete), this);
    deleteProfile_->setObjectName(QStringLiteral("deleteProfileButton"));
    profileRow->addWidget(deleteProfile_);
    column->addLayout(profileRow);
    for (QWidget* w : std::initializer_list<QWidget*>{profile_, profileName_, saveProfile_, deleteProfile_}) w->setEnabled(profiles_ != nullptr);

    // --- The layout ---
    auto* layoutForm = new QFormLayout();
    layout_ = new QComboBox(this);
    layout_->setObjectName(QStringLiteral("layoutBox"));
    layout_->addItem(catalog::translate(catalog::key::ImpositionLayoutNUp));
    layout_->addItem(catalog::translate(catalog::key::ImpositionLayoutBooklet));
    layoutForm->addRow(catalog::translate(catalog::key::ImpositionLayout), layout_);
    column->addLayout(layoutForm);

    nUpGroup_ = new QGroupBox(catalog::translate(catalog::key::ImpositionLayoutNUp), this);
    auto* nUpForm = new QFormLayout(nUpGroup_);
    perSheet_ = new QComboBox(nUpGroup_);
    perSheet_->setObjectName(QStringLiteral("perSheetBox"));
    for (const int n : core::kNUpCounts) perSheet_->addItem(QString::number(n), n);
    nUpForm->addRow(catalog::translate(catalog::key::ImpositionPerSheet), perSheet_);
    order_ = new QComboBox(nUpGroup_);
    order_->setObjectName(QStringLiteral("orderBox"));
    order_->addItem(catalog::translate(catalog::key::ImpositionOrderAcross));
    order_->addItem(catalog::translate(catalog::key::ImpositionOrderDown));
    nUpForm->addRow(catalog::translate(catalog::key::ImpositionOrder), order_);
    reversed_ = new QCheckBox(catalog::translate(catalog::key::ImpositionReversed), nUpGroup_);
    reversed_->setObjectName(QStringLiteral("reversedBox"));
    nUpForm->addRow(QString(), reversed_);
    column->addWidget(nUpGroup_);

    bookletGroup_ = new QGroupBox(catalog::translate(catalog::key::ImpositionLayoutBooklet), this);
    auto* bookletForm = new QFormLayout(bookletGroup_);
    binding_ = new QComboBox(bookletGroup_);
    binding_->setObjectName(QStringLiteral("bindingBox"));
    binding_->addItem(catalog::translate(catalog::key::ImpositionBindingLeft));
    binding_->addItem(catalog::translate(catalog::key::ImpositionBindingRight));
    bookletForm->addRow(catalog::translate(catalog::key::ImpositionBinding), binding_);
    creep_ = points(bookletGroup_, "creepField", 0.0, core::kMaxCreep, 0.05, 2);
    bookletForm->addRow(catalog::translate(catalog::key::ImpositionCreep), creep_);
    column->addWidget(bookletGroup_);

    // --- The sheet ---
    auto* sheetGroup = new QGroupBox(catalog::translate(catalog::key::ImpositionSheet), this);
    auto* sheetForm = new QFormLayout(sheetGroup);
    paper_ = new QComboBox(sheetGroup);
    paper_->setObjectName(QStringLiteral("paperBox"));
    for (const ImpositionPaper& paper : impositionPapers()) paper_->addItem(catalog::translate(paper.key));
    paper_->addItem(catalog::translate(catalog::key::ImpositionPaperCustom));
    sheetForm->addRow(catalog::translate(catalog::key::ImpositionPaper), paper_);
    width_ = points(sheetGroup, "widthField", core::kMinSheetSide, core::kMaxSheetSide, 1.0);
    height_ = points(sheetGroup, "heightField", core::kMinSheetSide, core::kMaxSheetSide, 1.0);
    sheetForm->addRow(catalog::translate(catalog::key::ImpositionWidth), width_);
    sheetForm->addRow(catalog::translate(catalog::key::ImpositionHeight), height_);
    orientation_ = new QComboBox(sheetGroup);
    orientation_->setObjectName(QStringLiteral("orientationBox"));
    orientation_->addItem(catalog::translate(catalog::key::ImpositionOrientationAuto));
    orientation_->addItem(catalog::translate(catalog::key::ImpositionOrientationPortrait));
    orientation_->addItem(catalog::translate(catalog::key::ImpositionOrientationLandscape));
    sheetForm->addRow(catalog::translate(catalog::key::ImpositionOrientation), orientation_);
    margin_ = points(sheetGroup, "marginField", 0.0, core::kMaxImpositionMargin, 1.0);
    gutter_ = points(sheetGroup, "gutterField", 0.0, core::kMaxImpositionGutter, 1.0);
    sheetForm->addRow(catalog::translate(catalog::key::ImpositionMargin), margin_);
    sheetForm->addRow(catalog::translate(catalog::key::ImpositionGutter), gutter_);
    duplex_ = new QComboBox(sheetGroup);
    duplex_->setObjectName(QStringLiteral("duplexBox"));
    duplex_->addItem(catalog::translate(catalog::key::ImpositionDuplexOneSided));
    duplex_->addItem(catalog::translate(catalog::key::ImpositionDuplexLongEdge));
    duplex_->addItem(catalog::translate(catalog::key::ImpositionDuplexShortEdge));
    sheetForm->addRow(catalog::translate(catalog::key::ImpositionDuplex), duplex_);
    column->addWidget(sheetGroup);

    // --- The marks ---
    auto* marksGroup = new QGroupBox(catalog::translate(catalog::key::ImpositionMarks), this);
    auto* marksColumn = new QVBoxLayout(marksGroup);
    const auto mark = [&](QCheckBox*& box, QLatin1String key, const char* name) {
        box = new QCheckBox(catalog::translate(key), marksGroup);
        box->setObjectName(QLatin1String(name));
        marksColumn->addWidget(box);
    };
    mark(crop_, catalog::key::ImpositionMarksCrop, "cropBox");
    mark(registration_, catalog::key::ImpositionMarksRegistration, "registrationBox");
    mark(colourBar_, catalog::key::ImpositionMarksColourBar, "colourBarBox");
    mark(labels_, catalog::key::ImpositionMarksLabels, "labelsBox");
    mark(frames_, catalog::key::ImpositionMarksFrames, "framesBox");
    column->addWidget(marksGroup);

    refusal_ = new QLabel(this);
    refusal_->setObjectName(QStringLiteral("refusalLabel"));
    refusal_->setWordWrap(true);
    refusal_->setStyleSheet(QStringLiteral("color: #b00020;"));
    refusal_->hide();
    column->addWidget(refusal_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ImpositionDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ImpositionDialog::reject);
    column->addWidget(buttons);

    // --- The preview ---
    auto* previewColumn = new QVBoxLayout();
    previewColumn->addWidget(new QLabel(catalog::translate(catalog::key::ImpositionPreview), this));
    preview_ = new ImpositionPreview(this);
    previewColumn->addWidget(preview_, 1);
    auto* sideRow = new QHBoxLayout();
    sideRow->addWidget(new QLabel(catalog::translate(catalog::key::ImpositionPreviewSide), this));
    previewSide_ = new QSpinBox(this);
    previewSide_->setObjectName(QStringLiteral("previewSideField"));
    previewSide_->setMinimum(1);
    sideRow->addWidget(previewSide_, 1);
    previewColumn->addLayout(sideRow);
    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("summaryLabel"));
    summary_->setWordWrap(true);
    previewColumn->addWidget(summary_);
    columns->addLayout(previewColumn, 2);

    // Every field redraws the preview; the paper fills the size; a custom size is the paper's own.
    for (QComboBox* box : {layout_, perSheet_, order_, binding_, orientation_, duplex_})
        connect(box, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    for (QCheckBox* box : {reversed_, crop_, registration_, colourBar_, labels_, frames_})
        connect(box, &QCheckBox::toggled, this, [this] { refresh(); });
    for (QDoubleSpinBox* box : {creep_, width_, height_, margin_, gutter_})
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { refresh(); });
    connect(previewSide_, &QSpinBox::valueChanged, this, [this] { refresh(); });
    connect(paper_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const bool custom = index >= static_cast<int>(impositionPapers().size());
        width_->setEnabled(custom);
        height_->setEnabled(custom);
        if (!custom && !loading_) {
            loading_ = true;
            width_->setValue(impositionPapers()[static_cast<std::size_t>(index)].size.width);
            height_->setValue(impositionPapers()[static_cast<std::size_t>(index)].size.height);
            loading_ = false;
        }
        refresh();
    });

    // Profiles: chosen loads; save keeps the fields under the name typed; delete takes the chosen one away.
    connect(profile_, &QComboBox::activated, this, [this](int index) {
        const QString name = profile_->itemData(index).toString();
        if (!name.isEmpty()) loadProfile(name);
    });
    connect(profile_, &QComboBox::currentIndexChanged, this, [this](int index) { deleteProfile_->setEnabled(profiles_ != nullptr && index > 0); });
    connect(saveProfile_, &QPushButton::clicked, this, [this] {
        if (profiles_ == nullptr) return;
        const core::ImpositionSpec spec = specFromFields();
        if (const core::Status valid = core::validate(spec); !valid) {
            showRefusal(valid.error());
            return;
        }
        const QString name = profileName_->text().trimmed();
        if (const core::Status saved = profiles_->saveProfile(core::ProfileKind::Imposition, name, core::impositionToJson(spec)); !saved) {
            showRefusal(saved.error());
            return;
        }
        reloadProfiles(name);
    });
    connect(deleteProfile_, &QPushButton::clicked, this, [this] {
        const QString name = profile_->currentData().toString();
        if (profiles_ == nullptr || name.isEmpty()) return;
        if (profiles_->removeProfile(core::ProfileKind::Imposition, name)) reloadProfiles(QString());
    });

    loadSpec(initial);
    reloadProfiles(profile);
    refresh();
}

core::ImpositionSpec ImpositionDialog::specFromFields() const
{
    core::ImpositionSpec spec;
    spec.layout = layout_->currentIndex() == 1 ? core::ImpositionLayout::Booklet : core::ImpositionLayout::NUp;
    spec.perSheet = perSheet_->currentData().toInt();
    spec.order = kOrders[std::clamp(order_->currentIndex(), 0, 1)];
    spec.reversed = reversed_->isChecked();
    spec.binding = kBindings[std::clamp(binding_->currentIndex(), 0, 1)];
    spec.creep = creep_->value();
    spec.sheet = core::PdfSize{width_->value(), height_->value()};
    spec.orientation = kOrientations[std::clamp(orientation_->currentIndex(), 0, 2)];
    spec.margin = margin_->value();
    spec.gutter = gutter_->value();
    spec.duplex = kFlips[std::clamp(duplex_->currentIndex(), 0, 2)];
    spec.cropMarks = crop_->isChecked();
    spec.registrationMarks = registration_->isChecked();
    spec.colourBar = colourBar_->isChecked();
    spec.sheetLabels = labels_->isChecked();
    spec.frames = frames_->isChecked();
    return spec;
}

void ImpositionDialog::loadSpec(const core::ImpositionSpec& spec)
{
    loading_ = true;
    layout_->setCurrentIndex(spec.layout == core::ImpositionLayout::Booklet ? 1 : 0);
    const int perSheet = perSheet_->findData(spec.perSheet);
    perSheet_->setCurrentIndex(perSheet < 0 ? 0 : perSheet);
    order_->setCurrentIndex(spec.order == core::ImpositionOrder::Down ? 1 : 0);
    reversed_->setChecked(spec.reversed);
    binding_->setCurrentIndex(spec.binding == core::ImpositionBinding::Right ? 1 : 0);
    creep_->setValue(spec.creep);
    // The paper whose size it is, either way round -- else a custom size.
    int paper = static_cast<int>(impositionPapers().size());
    for (std::size_t at = 0; at < impositionPapers().size(); ++at) {
        const core::PdfSize& size = impositionPapers()[at].size;
        const bool same = (std::fabs(size.width - spec.sheet.width) < 0.5 && std::fabs(size.height - spec.sheet.height) < 0.5)
                          || (std::fabs(size.width - spec.sheet.height) < 0.5 && std::fabs(size.height - spec.sheet.width) < 0.5);
        if (same) {
            paper = static_cast<int>(at);
            break;
        }
    }
    paper_->setCurrentIndex(paper);
    width_->setValue(spec.sheet.width);
    height_->setValue(spec.sheet.height);
    const bool custom = paper >= static_cast<int>(impositionPapers().size());
    width_->setEnabled(custom);
    height_->setEnabled(custom);
    orientation_->setCurrentIndex(spec.orientation == core::SheetOrientation::Portrait ? 1
                                  : spec.orientation == core::SheetOrientation::Landscape ? 2
                                                                                         : 0);
    margin_->setValue(spec.margin);
    gutter_->setValue(spec.gutter);
    duplex_->setCurrentIndex(spec.duplex == core::DuplexFlip::LongEdge ? 1 : spec.duplex == core::DuplexFlip::ShortEdge ? 2 : 0);
    crop_->setChecked(spec.cropMarks);
    registration_->setChecked(spec.registrationMarks);
    colourBar_->setChecked(spec.colourBar);
    labels_->setChecked(spec.sheetLabels);
    frames_->setChecked(spec.frames);
    loading_ = false;
    refresh();
}

void ImpositionDialog::refresh()
{
    if (loading_) return;
    const core::ImpositionSpec spec = specFromFields();
    const bool booklet = spec.layout == core::ImpositionLayout::Booklet;
    nUpGroup_->setVisible(!booklet);
    bookletGroup_->setVisible(booklet);
    refusal_->hide();
    core::Result<core::ImpositionPlan> plan = core::planImposition(spec, shown_);
    if (!plan) {
        preview_->show(std::nullopt, 0);
        summary_->clear();
        showRefusal(plan.error());
        return;
    }
    const int sides = static_cast<int>(plan.value().sides.size());
    previewSide_->blockSignals(true);
    previewSide_->setMaximum(std::max(1, sides));
    previewSide_->blockSignals(false);
    summary_->setText(catalog::translate(catalog::key::ImpositionSummary)
                          .arg(static_cast<int>(shown_.size()))
                          .arg(plan.value().sheets)
                          .arg(sides)
                          .arg(plan.value().pagesPadded - static_cast<int>(shown_.size())));
    const int side = previewSide_->value() - 1;
    preview_->show(std::move(plan).value(), side);
}

void ImpositionDialog::showRefusal(const core::Error& error)
{
    refusal_->setText(catalog::translateError(QString(error.messageKey), error.context));
    refusal_->show();
}

core::Result<ImpositionDialog::Choice> ImpositionDialog::choice() const
{
    const core::ImpositionSpec spec = specFromFields();
    core::Result<core::ImpositionPlan> plan = core::planImposition(spec, shown_);
    if (!plan) return plan.error();
    return Choice{spec, profile_->currentData().toString()};
}

void ImpositionDialog::reloadProfiles(const QString& select)
{
    profile_->clear();
    profile_->addItem(catalog::translate(catalog::key::DecorateProfileNone), QString());
    if (profiles_ == nullptr) return;
    for (const QString& name : profiles_->profileNames(core::ProfileKind::Imposition)) profile_->addItem(name, name);
    const int found = profile_->findData(select);
    profile_->setCurrentIndex(found < 0 ? 0 : found);
    deleteProfile_->setEnabled(found > 0);
}

void ImpositionDialog::loadProfile(const QString& name)
{
    if (profiles_ == nullptr) return;
    core::Result<QJsonObject> json = profiles_->profile(core::ProfileKind::Imposition, name);
    core::Result<core::ImpositionSpec> spec = json ? core::impositionFromJson(json.value()) : core::Result<core::ImpositionSpec>(json.error());
    if (!spec) {
        showRefusal(spec.error());
        return;
    }
    loadSpec(spec.value());
    profileName_->setText(name);
}

void ImpositionDialog::accept()
{
    core::Result<Choice> chosen = choice();
    if (!chosen) {
        showRefusal(chosen.error());
        return;
    }
    QDialog::accept();
}

bool ImpositionDialog::canAsk()
{
    return static_cast<bool>(installed()) || aPersonCanAnswer();
}

std::optional<ImpositionDialog::Choice> ImpositionDialog::ask(QWidget* parent, const core::ImpositionSpec& initial,
                                                              std::vector<core::PdfSize> shown, core::ProfileStore* profiles,
                                                              const QString& profile)
{
    if (installed()) return installed()(initial, shown);
    if (!aPersonCanAnswer()) return std::nullopt;
    ImpositionDialog dialog(initial, std::move(shown), profiles, profile, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    core::Result<Choice> chosen = dialog.choice();
    if (!chosen) return std::nullopt;
    return std::move(chosen).value();
}

void ImpositionDialog::setAnswerForTesting(Answer answer)
{
    installed() = std::move(answer);
}

} // namespace rawpage::ui
