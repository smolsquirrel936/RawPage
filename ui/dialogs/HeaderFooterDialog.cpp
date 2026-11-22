// ui/dialogs/HeaderFooterDialog -- see the header.

#include "ui/dialogs/HeaderFooterDialog.h"

#include "core/doc/BatesAllocator.h"
#include "core/settings/ProfileStore.h"
#include "ui/shell/Catalog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
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

HeaderFooterDialog::Answer& installedHeaderFooter()
{
    static HeaderFooterDialog::Answer answer;
    return answer;
}

BatesDialog::Answer& installedBates()
{
    static BatesDialog::Answer answer;
    return answer;
}

constexpr const char* kAreaNames[core::kHeaderFooterAreas] = {"headerLeft", "headerCentre", "headerRight",
                                                              "footerLeft", "footerCentre", "footerRight"};
constexpr const char* kFields[] = {"page", "count", "label", "date", "time", "file", "path", "bates"};

QLabel* refusalLine(QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setObjectName(QStringLiteral("refusal"));
    label->setWordWrap(true);
    label->hide();
    return label;
}

QDoubleSpinBox* pointsField(QWidget* parent, const QString& name, double low, double high, double step)
{
    auto* field = new QDoubleSpinBox(parent);
    field->setObjectName(name);
    field->setRange(low, high);
    field->setDecimals(1);
    field->setSingleStep(step);
    return field;
}

QDialogButtonBox* okCancel(QDialog* dialog)
{
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    return buttons;
}

// The first page `spec` reaches -- the preview's -- or the document's first.
std::vector<int> reached(const core::HeaderFooterSpec& spec, const core::PageTree& pages)
{
    core::Result<std::vector<int>> at = core::headerFooterPages(spec, pages);
    return at ? std::move(at).value() : std::vector<int>{};
}

} // namespace

// ==========================================================================================
// The preview
// ==========================================================================================

HeaderFooterPreview::HeaderFooterPreview(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("headerFooterPreview"));
    setMinimumSize(180, 220);
}

void HeaderFooterPreview::show(const core::Page& page, std::optional<core::PageDecoration> decoration)
{
    shown_ = page.displaySize();
    decoration_ = std::move(decoration);
    update();
}

void HeaderFooterPreview::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const QRectF area = QRectF(rect()).adjusted(6, 6, -6, -6);
    if (shown_.width <= 0 || shown_.height <= 0 || area.isEmpty()) return;
    const double scale = std::min(area.width() / shown_.width, area.height() / shown_.height);
    const QRectF page(area.center().x() - shown_.width * scale / 2, area.center().y() - shown_.height * scale / 2,
                      shown_.width * scale, shown_.height * scale);
    painter.fillRect(page, palette().color(QPalette::Base));
    painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    painter.drawRect(page);
    if (!decoration_) return;
    // Shown space -> the widget: y up becomes y down.
    const auto at = [&](double x, double y) { return QPointF(page.left() + x * scale, page.bottom() - y * scale); };
    // THE CONTENT'S BOX: the page as shown, through the wrapper -- smaller than the page only with *shrink*.
    if (decoration_->wrap) {
        const auto& m = decoration_->wrap->matrix;
        const QPointF a = at(m[4], m[5]);
        const QPointF b = at(m[4] + m[0] * shown_.width, m[5] + m[3] * shown_.height);
        painter.setPen(QPen(palette().color(QPalette::Mid), 1, Qt::DashLine));
        painter.setBrush(palette().color(QPalette::AlternateBase));
        painter.drawRect(QRectF(a, b).normalized());
        painter.setBrush(Qt::NoBrush);
    }
    for (const core::DecorationLine& line : decoration_->lines) {
        const QColor colour = QColor::fromRgbF(static_cast<float>(line.colour[0]), static_cast<float>(line.colour[1]),
                                               static_cast<float>(line.colour[2]));
        painter.setPen(colour);
        QFont font(QStringLiteral("Helvetica"));
        font.setStyleHint(QFont::SansSerif);
        font.setPixelSize(std::max(1, static_cast<int>(std::lround(line.size * scale))));
        painter.setFont(font);
        double x = line.matrix[4];
        const double y = line.matrix[5];
        for (const core::TypedRun& run : line.runs) {
            painter.drawText(at(x, y), run.text);
            x += run.width;   // the page's own advances, not the screen font's
        }
        if (line.underline) {
            const double thickness = std::max(0.25, line.size / 20.0);
            painter.fillRect(QRectF(at(line.matrix[4], y - line.size * 0.1), at(line.matrix[4] + line.width, y - line.size * 0.1 - thickness))
                                 .normalized(),
                             colour);
        }
    }
}

// ==========================================================================================
// HeaderFooterDialog
// ==========================================================================================

HeaderFooterDialog::HeaderFooterDialog(Mode mode, const core::PageTree& pages, core::FieldValues values,
                                       const core::HeaderFooterSpec& initial, core::ProfileStore* profiles, const QString& profile,
                                       QWidget* parent)
    : QDialog(parent), pages_(pages), values_(std::move(values)), han_(initial.han), bates_(initial.bates), profiles_(profiles)
{
    setObjectName(QStringLiteral("headerFooterDialog"));
    setWindowTitle(catalog::translate(mode == Mode::Add ? catalog::key::DecorateHeaderFooterAddTitle
                                                        : catalog::key::DecorateHeaderFooterUpdateTitle));
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
    const bool store = profiles_ != nullptr;
    for (QWidget* w : std::initializer_list<QWidget*>{profile_, profileName_, saveProfile_, deleteProfile_}) w->setEnabled(store);

    // --- The six areas ---
    auto* grid = new QGridLayout();
    grid->addWidget(new QLabel(catalog::translate(catalog::key::DecorateLeft), this), 0, 1);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::DecorateCentre), this), 0, 2);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::DecorateRight), this), 0, 3);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::DecorateHeader), this), 1, 0);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::DecorateFooter), this), 2, 0);
    for (int i = 0; i < core::kHeaderFooterAreas; ++i) {
        auto* field = new QLineEdit(this);
        field->setObjectName(QLatin1String(kAreaNames[i]) + QStringLiteral("Field"));
        field->setMaxLength(core::kMaxAreaLength);
        areas_[static_cast<std::size_t>(i)] = field;
        grid->addWidget(field, 1 + i / 3, 1 + i % 3);
        connect(field, &QLineEdit::textChanged, this, &HeaderFooterDialog::refreshPreview);
        // The field `Insert` types into is the area that last HAD THE FOCUS -- which moves to the button when it is
        // pressed. Followed by focus, not by the cursor: clicking into an empty field moves no cursor.
        field->installEventFilter(this);
    }
    lastArea_ = areas_[static_cast<std::size_t>(core::HeaderFooterArea::FooterCentre)];
    column->addLayout(grid);

    auto* fieldRow = new QHBoxLayout();
    fieldRow->addWidget(new QLabel(catalog::translate(catalog::key::DecorateInsertField), this));
    field_ = new QComboBox(this);
    field_->setObjectName(QStringLiteral("fieldBox"));
    for (const char* name : kFields)
        field_->addItem(catalog::translate(QStringLiteral("decorate.field.") + QLatin1String(name)),
                        QStringLiteral("<<") + QLatin1String(name) + QStringLiteral(">>"));
    fieldRow->addWidget(field_, 1);
    insert_ = new QPushButton(catalog::translate(catalog::key::DecorateInsert), this);
    insert_->setObjectName(QStringLiteral("insertButton"));
    fieldRow->addWidget(insert_);
    column->addLayout(fieldRow);
    connect(insert_, &QPushButton::clicked, this, [this] {
        if (lastArea_ == nullptr) return;
        lastArea_->insert(field_->currentData().toString());
        lastArea_->setFocus();
    });

    // --- Style ---
    auto* style = new QHBoxLayout();
    style->addWidget(new QLabel(catalog::translate(catalog::key::DecorateSize), this));
    size_ = pointsField(this, QStringLiteral("sizeField"), core::kMinDecorationSize, core::kMaxDecorationSize, 1.0);
    style->addWidget(size_);
    colourButton_ = new QPushButton(catalog::translate(catalog::key::DecorateColour), this);
    colourButton_->setObjectName(QStringLiteral("colourButton"));
    style->addWidget(colourButton_);
    underline_ = new QCheckBox(catalog::translate(catalog::key::DecorateUnderline), this);
    underline_->setObjectName(QStringLiteral("underlineBox"));
    style->addWidget(underline_);
    style->addStretch(1);
    column->addLayout(style);
    connect(colourButton_, &QPushButton::clicked, this, [this] {
        const QColor chosen = QColorDialog::getColor(colour_, this);
        if (chosen.isValid()) setColour(chosen);
    });

    // --- Margins and shrink ---
    auto* marginsBox = new QGroupBox(catalog::translate(catalog::key::DecorateMargins), this);
    auto* margins = new QGridLayout(marginsBox);
    const QLatin1String edgeKeys[4] = {catalog::key::DecorateTop, catalog::key::DecorateBottom, catalog::key::DecorateLeft,
                               catalog::key::DecorateRight};
    const char* edgeNames[4] = {"topField", "bottomField", "leftField", "rightField"};
    for (int i = 0; i < 4; ++i) {
        margins->addWidget(new QLabel(catalog::translate(edgeKeys[i]), marginsBox), i / 2, (i % 2) * 2);
        margins_[static_cast<std::size_t>(i)] = pointsField(marginsBox, QLatin1String(edgeNames[i]), 0.0, core::kMaxDecorationMargin, 1.0);
        margins->addWidget(margins_[static_cast<std::size_t>(i)], i / 2, (i % 2) * 2 + 1);
    }
    column->addWidget(marginsBox);
    shrink_ = new QCheckBox(catalog::translate(catalog::key::DecorateShrink), this);
    shrink_->setObjectName(QStringLiteral("shrinkBox"));
    column->addWidget(shrink_);

    // --- Pages ---
    auto* pagesRow = new QHBoxLayout();
    pagesRow->addWidget(new QLabel(catalog::translate(catalog::key::DecoratePages), this));
    range_ = new QLineEdit(this);
    range_->setObjectName(QStringLiteral("rangeField"));
    range_->setPlaceholderText(QStringLiteral("1-%1").arg(pages_.count()));
    pagesRow->addWidget(range_, 1);
    parity_ = new QComboBox(this);
    parity_->setObjectName(QStringLiteral("parityBox"));
    for (const auto& [parity, name] : {std::pair{core::PageParity::Any, "all"}, std::pair{core::PageParity::Odd, "odd"},
                                       std::pair{core::PageParity::Even, "even"}})
        parity_->addItem(catalog::translate(QStringLiteral("decorate.parity.") + QLatin1String(name)), static_cast<int>(parity));
    pagesRow->addWidget(parity_);
    column->addLayout(pagesRow);

    refusal_ = refusalLine(this);
    column->addWidget(refusal_);
    column->addStretch(1);
    column->addWidget(okCancel(this));

    // --- The preview ---
    auto* previewColumn = new QVBoxLayout();
    previewColumn->addWidget(new QLabel(catalog::translate(catalog::key::DecoratePreview), this));
    preview_ = new HeaderFooterPreview(this);
    previewColumn->addWidget(preview_, 1);
    auto* pageRow = new QHBoxLayout();
    pageRow->addWidget(new QLabel(catalog::translate(catalog::key::DecoratePreviewPage), this));
    previewPage_ = new QSpinBox(this);
    previewPage_->setObjectName(QStringLiteral("previewPageField"));
    previewPage_->setRange(1, std::max(1, pages_.count()));
    pageRow->addWidget(previewPage_);
    pageRow->addStretch(1);
    previewColumn->addLayout(pageRow);
    columns->addLayout(previewColumn, 2);

    for (QDoubleSpinBox* field : {size_, margins_[0], margins_[1], margins_[2], margins_[3]})
        connect(field, &QDoubleSpinBox::valueChanged, this, &HeaderFooterDialog::refreshPreview);
    for (QCheckBox* box : {underline_, shrink_}) connect(box, &QCheckBox::toggled, this, &HeaderFooterDialog::refreshPreview);
    connect(range_, &QLineEdit::textChanged, this, &HeaderFooterDialog::refreshPreview);
    connect(parity_, &QComboBox::currentIndexChanged, this, &HeaderFooterDialog::refreshPreview);
    connect(previewPage_, &QSpinBox::valueChanged, this, &HeaderFooterDialog::refreshPreview);

    // Profiles: chosen loads; save keeps the fields under the name typed; delete takes the chosen one away.
    connect(profile_, &QComboBox::activated, this, [this](int index) {
        const QString name = profile_->itemData(index).toString();
        if (!name.isEmpty()) loadProfile(name);
    });
    connect(saveProfile_, &QPushButton::clicked, this, [this] {
        if (profiles_ == nullptr) return;
        const QString name = profileName_->text().trimmed();
        const core::Status saved = profiles_->saveProfile(core::ProfileKind::HeaderFooter, name, core::specToJson(specFromFields()));
        if (!saved) {
            refusal_->setText(catalog::translateError(QString(saved.error().messageKey), saved.error().context));
            refusal_->show();
            return;
        }
        reloadProfiles(name);
    });
    // Once, here: a `Qt::UniqueConnection` on a lambda is a debug-only fail-fast (CLAUDE.md, S4 guide I-09) -- found by this
    // dialog's first debug run, connecting this in `reloadProfiles`.
    connect(profile_, &QComboBox::currentIndexChanged, this, [this](int index) {
        deleteProfile_->setEnabled(profiles_ != nullptr && index > 0);
    });
    connect(deleteProfile_, &QPushButton::clicked, this, [this] {
        const QString name = profile_->currentData().toString();
        if (profiles_ == nullptr || name.isEmpty()) return;
        if (profiles_->removeProfile(core::ProfileKind::HeaderFooter, name)) reloadProfiles(QString());
    });

    loadSpec(initial);
    reloadProfiles(profile);
    refreshPreview();
}

bool HeaderFooterDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn) {
        for (QLineEdit* area : areas_) {
            if (watched == area) lastArea_ = area;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void HeaderFooterDialog::setColour(const QColor& colour)
{
    colour_ = colour.isValid() ? colour.toRgb() : QColor(Qt::black);
    QPixmap swatch(12, 12);
    swatch.fill(colour_);
    colourButton_->setIcon(QIcon(swatch));
    refreshPreview();
}

void HeaderFooterDialog::loadSpec(const core::HeaderFooterSpec& spec)
{
    loading_ = true;
    for (int i = 0; i < core::kHeaderFooterAreas; ++i) areas_[static_cast<std::size_t>(i)]->setText(spec.areas[static_cast<std::size_t>(i)]);
    size_->setValue(spec.size);
    underline_->setChecked(spec.underline);
    margins_[0]->setValue(spec.top);
    margins_[1]->setValue(spec.bottom);
    margins_[2]->setValue(spec.left);
    margins_[3]->setValue(spec.right);
    shrink_->setChecked(spec.shrink);
    range_->setText(spec.range);
    parity_->setCurrentIndex(std::max(0, parity_->findData(static_cast<int>(spec.parity))));
    bates_ = spec.bates;
    loading_ = false;
    setColour(QColor::fromRgbF(static_cast<float>(spec.colour[0]), static_cast<float>(spec.colour[1]), static_cast<float>(spec.colour[2])));
}

core::HeaderFooterSpec HeaderFooterDialog::specFromFields() const
{
    core::HeaderFooterSpec spec;
    for (int i = 0; i < core::kHeaderFooterAreas; ++i) spec.areas[static_cast<std::size_t>(i)] = areas_[static_cast<std::size_t>(i)]->text();
    spec.size = size_->value();
    spec.colour = {colour_.redF(), colour_.greenF(), colour_.blueF()};
    spec.underline = underline_->isChecked();
    spec.top = margins_[0]->value();
    spec.bottom = margins_[1]->value();
    spec.left = margins_[2]->value();
    spec.right = margins_[3]->value();
    spec.shrink = shrink_->isChecked();
    spec.range = range_->text().trimmed();
    spec.parity = static_cast<core::PageParity>(parity_->currentData().toInt());
    spec.han = han_;
    spec.bates = bates_;
    return spec;
}

core::Result<HeaderFooterDialog::Choice> HeaderFooterDialog::choice() const
{
    const core::HeaderFooterSpec spec = specFromFields();
    if (core::Status valid = core::validate(spec); !valid) return valid.error();
    if (auto at = core::headerFooterPages(spec, pages_); !at) return at.error();
    // Every page the spec reaches laid out -- a field that expands to a character no face can show is refused HERE, with
    // the dialog still open, rather than by the command.
    for (const int page : reached(spec, pages_)) {
        core::FieldPage field{page, pages_.count(), pages_.at(page)->label(), std::nullopt};
        if (core::usesBates(spec)) field.bates = spec.bates.start;
        if (auto laid = core::layoutHeaderFooter(spec, core::DecorationKind::HeaderFooter, *pages_.at(page), values_, field); !laid)
            return laid.error();
    }
    return Choice{spec, profile_->currentData().toString()};
}

void HeaderFooterDialog::refreshPreview()
{
    if (loading_ || preview_ == nullptr) return;
    const core::HeaderFooterSpec spec = specFromFields();
    const int index = std::clamp(previewPage_->value() - 1, 0, std::max(0, pages_.count() - 1));
    const core::Page* page = pages_.at(index);
    if (page == nullptr) return;
    // THE PAGE AS SHOWN, unturned: the layout's lines are then in the preview's own coordinates.
    core::Page shown(0, core::PdfRect{core::PdfPoint{0, 0}, page->displaySize()});
    const std::vector<int> at = reached(spec, pages_);
    const auto position = std::find(at.begin(), at.end(), index);
    core::FieldPage field{index, pages_.count(), page->label(), std::nullopt};
    if (core::usesBates(spec)) field.bates = spec.bates.start + (position != at.end() ? position - at.begin() : 0);
    auto laid = core::layoutHeaderFooter(spec, core::DecorationKind::HeaderFooter, shown, values_, field);
    if (!laid) {
        refusal_->setText(catalog::translateError(QString(laid.error().messageKey), laid.error().context));
        refusal_->show();
        preview_->show(shown, std::nullopt);
        return;
    }
    refusal_->hide();
    // A page the spec does not reach is previewed bare: what it will look like.
    preview_->show(shown, position != at.end() ? std::optional<core::PageDecoration>(std::move(laid).value()) : std::nullopt);
}

void HeaderFooterDialog::reloadProfiles(const QString& select)
{
    profile_->clear();
    profile_->addItem(catalog::translate(catalog::key::DecorateProfileNone), QString());
    if (profiles_ == nullptr) return;
    for (const QString& name : profiles_->profileNames(core::ProfileKind::HeaderFooter)) profile_->addItem(name, name);
    const int at = profile_->findData(select);
    profile_->setCurrentIndex(at < 0 ? 0 : at);
    deleteProfile_->setEnabled(at > 0);
}

void HeaderFooterDialog::loadProfile(const QString& name)
{
    if (profiles_ == nullptr) return;
    core::Result<QJsonObject> json = profiles_->profile(core::ProfileKind::HeaderFooter, name);
    core::Result<core::HeaderFooterSpec> spec = json ? core::specFromJson(json.value()) : core::Result<core::HeaderFooterSpec>(json.error());
    if (!spec) {
        refusal_->setText(catalog::translateError(QString(spec.error().messageKey), spec.error().context));
        refusal_->show();
        return;
    }
    core::HeaderFooterSpec loaded = std::move(spec).value();
    loaded.han = han_;   // the person's language, not the profile maker's
    loadSpec(loaded);
    profileName_->setText(name);
    refreshPreview();
}

void HeaderFooterDialog::accept()
{
    const core::Result<Choice> chosen = choice();
    if (!chosen) {
        refusal_->setText(catalog::translateError(QString(chosen.error().messageKey), chosen.error().context));
        refusal_->show();
        return;
    }
    QDialog::accept();
}

bool HeaderFooterDialog::canAsk()
{
    return static_cast<bool>(installedHeaderFooter()) || aPersonCanAnswer();
}

std::optional<HeaderFooterDialog::Choice> HeaderFooterDialog::ask(QWidget* parent, Mode mode, const core::PageTree& pages,
                                                                  const core::FieldValues& values, const core::HeaderFooterSpec& initial,
                                                                  core::ProfileStore* profiles, const QString& profile)
{
    if (installedHeaderFooter()) return installedHeaderFooter()(mode, initial);
    if (!aPersonCanAnswer()) return std::nullopt;
    HeaderFooterDialog dialog(mode, pages, values, initial, profiles, profile, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    core::Result<Choice> chosen = dialog.choice();
    if (!chosen) return std::nullopt;
    return std::move(chosen).value();
}

void HeaderFooterDialog::setAnswerForTesting(Answer answer)
{
    installedHeaderFooter() = std::move(answer);
}

// ==========================================================================================
// BatesDialog
// ==========================================================================================

BatesDialog::BatesDialog(std::vector<Document> documents, std::size_t current, const core::BatesFormat& format, QWidget* parent)
    : QDialog(parent), documents_(std::move(documents))
{
    setObjectName(QStringLiteral("batesDialog"));
    setWindowTitle(catalog::translate(catalog::key::DecorateBatesTitle));
    setModal(true);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(10);

    column->addWidget(new QLabel(catalog::translate(catalog::key::DecorateBatesDocuments), this));
    auto* listRow = new QHBoxLayout();
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("documentList"));
    for (std::size_t i = 0; i < documents_.size(); ++i) {
        auto* item = new QListWidgetItem(catalog::translate(catalog::key::DecorateBatesPages)
                                             .arg(documents_[i].name)
                                             .arg(documents_[i].pages),
                                         list_);
        item->setData(Qt::UserRole, static_cast<qulonglong>(i));
        item->setFlags(documents_[i].editable ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable) : Qt::NoItemFlags);
        item->setCheckState(documents_[i].editable && i == current ? Qt::Checked : Qt::Unchecked);
    }
    if (list_->count() > 0) list_->setCurrentRow(static_cast<int>(std::min(current, documents_.size() - 1)));
    listRow->addWidget(list_, 1);
    auto* buttons = new QVBoxLayout();
    up_ = new QPushButton(catalog::translate(catalog::key::DecorateBatesUp), this);
    up_->setObjectName(QStringLiteral("upButton"));
    down_ = new QPushButton(catalog::translate(catalog::key::DecorateBatesDown), this);
    down_->setObjectName(QStringLiteral("downButton"));
    buttons->addWidget(up_);
    buttons->addWidget(down_);
    buttons->addStretch(1);
    listRow->addLayout(buttons);
    column->addLayout(listRow);
    connect(up_, &QPushButton::clicked, this, [this] { move(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { move(1); });

    auto* grid = new QGridLayout();
    grid->addWidget(new QLabel(catalog::translate(catalog::key::DecorateBatesPrefix), this), 0, 0);
    prefix_ = new QLineEdit(format.prefix, this);
    prefix_->setObjectName(QStringLiteral("prefixField"));
    prefix_->setMaxLength(core::kMaxBatesAffix);
    grid->addWidget(prefix_, 0, 1);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::DecorateBatesSuffix), this), 0, 2);
    suffix_ = new QLineEdit(format.suffix, this);
    suffix_->setObjectName(QStringLiteral("suffixField"));
    suffix_->setMaxLength(core::kMaxBatesAffix);
    grid->addWidget(suffix_, 0, 3);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::DecorateBatesStart), this), 1, 0);
    start_ = new QSpinBox(this);
    start_->setObjectName(QStringLiteral("startField"));
    start_->setRange(0, 2147483647);
    start_->setValue(static_cast<int>(std::clamp<long long>(format.start, 0, 2147483647)));
    grid->addWidget(start_, 1, 1);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::DecorateBatesDigits), this), 1, 2);
    digits_ = new QSpinBox(this);
    digits_->setObjectName(QStringLiteral("digitsField"));
    digits_->setRange(core::kMinBatesDigits, core::kMaxBatesDigits);
    digits_->setValue(std::clamp(format.digits, core::kMinBatesDigits, core::kMaxBatesDigits));
    grid->addWidget(digits_, 1, 3);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::DecorateBatesPosition), this), 2, 0);
    position_ = new QComboBox(this);
    position_->setObjectName(QStringLiteral("positionBox"));
    for (int i = 0; i < core::kHeaderFooterAreas; ++i)
        position_->addItem(catalog::translate(QStringLiteral("decorate.position.") + QLatin1String(kAreaNames[i])), i);
    position_->setCurrentIndex(static_cast<int>(core::HeaderFooterArea::FooterRight));
    grid->addWidget(position_, 2, 1);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::DecorateSize), this), 2, 2);
    size_ = pointsField(this, QStringLiteral("sizeField"), core::kMinDecorationSize, core::kMaxDecorationSize, 1.0);
    size_->setValue(10.0);
    grid->addWidget(size_, 2, 3);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::DecorateMargins), this), 3, 0, 1, 2);
    edge_ = pointsField(this, QStringLiteral("edgeField"), 0.0, core::kMaxDecorationMargin, 1.0);
    edge_->setValue(24.0);
    side_ = pointsField(this, QStringLiteral("sideField"), 0.0, core::kMaxDecorationMargin, 1.0);
    side_->setValue(36.0);
    grid->addWidget(edge_, 3, 2);
    grid->addWidget(side_, 3, 3);
    column->addLayout(grid);

    sample_ = new QLabel(this);
    sample_->setObjectName(QStringLiteral("sampleLabel"));
    column->addWidget(sample_);
    refusal_ = refusalLine(this);
    column->addWidget(refusal_);
    column->addWidget(okCancel(this));
    for (QLineEdit* field : {prefix_, suffix_}) connect(field, &QLineEdit::textChanged, this, &BatesDialog::refreshSample);
    for (QSpinBox* field : {start_, digits_}) connect(field, &QSpinBox::valueChanged, this, &BatesDialog::refreshSample);
    refreshSample();
}

void BatesDialog::move(int step)
{
    const int row = list_->currentRow();
    const int to = row + step;
    if (row < 0 || to < 0 || to >= list_->count()) return;
    QListWidgetItem* item = list_->takeItem(row);
    list_->insertItem(to, item);
    list_->setCurrentRow(to);
}

void BatesDialog::refreshSample()
{
    sample_->setText(catalog::translate(catalog::key::DecorateBatesSample)
                         .arg(core::formatBates(core::BatesFormat{prefix_->text(), suffix_->text(), digits_->value(), start_->value()},
                                                start_->value())));
}

core::Result<BatesDialog::Choice> BatesDialog::choice() const
{
    Choice out;
    long long pages = 0;
    for (int row = 0; row < list_->count(); ++row) {
        const QListWidgetItem* item = list_->item(row);
        if (item->checkState() != Qt::Checked) continue;
        const auto index = static_cast<std::size_t>(item->data(Qt::UserRole).toULongLong());
        if (index >= documents_.size() || !documents_[index].editable) continue;
        out.order.push_back(index);
        pages += documents_[index].pages;
    }
    if (out.order.empty()) return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.decorate.noDocuments")};
    core::HeaderFooterSpec& spec = out.spec;
    spec.areas[static_cast<std::size_t>(position_->currentData().toInt())] = QStringLiteral("<<bates>>");
    spec.size = size_->value();
    spec.top = spec.bottom = edge_->value();
    spec.left = spec.right = side_->value();
    spec.bates = core::BatesFormat{prefix_->text(), suffix_->text(), digits_->value(), start_->value()};
    if (core::Status valid = core::validate(spec); !valid) return valid.error();
    if (spec.bates.start + pages - 1 > core::largestBates(spec.bates.digits))
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.decorate.batesOverflow"),
                           documents_[out.order.back()].name};
    return out;
}

void BatesDialog::accept()
{
    const core::Result<Choice> chosen = choice();
    if (!chosen) {
        refusal_->setText(catalog::translateError(QString(chosen.error().messageKey), chosen.error().context));
        refusal_->show();
        return;
    }
    QDialog::accept();
}

bool BatesDialog::canAsk()
{
    return static_cast<bool>(installedBates()) || aPersonCanAnswer();
}

std::optional<BatesDialog::Choice> BatesDialog::ask(QWidget* parent, std::vector<Document> documents, std::size_t current,
                                                    const core::BatesFormat& format)
{
    if (installedBates()) return installedBates()(documents, current);
    if (!aPersonCanAnswer()) return std::nullopt;
    BatesDialog dialog(std::move(documents), current, format, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    core::Result<Choice> chosen = dialog.choice();
    if (!chosen) return std::nullopt;
    return std::move(chosen).value();
}

void BatesDialog::setAnswerForTesting(Answer answer)
{
    installedBates() = std::move(answer);
}

QString batesReportText(const std::vector<BatesReportLine>& lines, const core::BatesFormat& format)
{
    QStringList out;
    for (const BatesReportLine& line : lines) {
        if (line.share)
            out << catalog::translate(catalog::key::DecorateBatesReportLine)
                       .arg(line.document, core::formatBates(format, line.share->first), core::formatBates(format, line.share->last));
        else
            out << catalog::translate(catalog::key::DecorateBatesReportRefused).arg(line.document, line.refusal);
    }
    return out.join(QLatin1Char('\n'));
}

} // namespace rawpage::ui
