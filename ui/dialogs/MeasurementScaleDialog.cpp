// ui/dialogs/MeasurementScaleDialog -- implementation. Stage 6 T12. See the header for the two ways to say a scale.

#include "ui/dialogs/MeasurementScaleDialog.h"

#include "ui/shell/Catalog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace rawpage::ui {
namespace {

// The units a length ON THE PAGE is given in -- a sheet is measured in points, inches, millimetres or centimetres.
constexpr core::MeasureUnit kPaperUnits[] = {core::MeasureUnit::Points, core::MeasureUnit::Inches,
                                             core::MeasureUnit::Millimetres, core::MeasureUnit::Centimetres};

// A unit's SYMBOL, as the ruler shows its own: never translated (`core/Units.h`).
QString symbol(core::MeasureUnit unit)
{
    return QLatin1String(core::measureUnitToken(unit).data());
}

QDoubleSpinBox* lengthBox(QWidget* parent, const char* name)
{
    auto* box = new QDoubleSpinBox(parent);
    box->setObjectName(QLatin1String(name));
    box->setDecimals(4);
    box->setRange(0.0, 1.0e9);
    box->setValue(1.0);
    return box;
}

void selectUnit(QComboBox* box, core::MeasureUnit unit)
{
    const int at = box->findData(static_cast<int>(unit));
    if (at >= 0) box->setCurrentIndex(at);
}

core::MeasureUnit unitOf(const QComboBox* box)
{
    return static_cast<core::MeasureUnit>(box->currentData().toInt());
}

} // namespace

MeasurementScaleDialog::MeasurementScaleDialog(const core::MeasureScale& current, QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("measurementScaleDialog"));
    setWindowTitle(catalog::translate(catalog::key::MeasureDialogTitle));
    build(current);
}

MeasurementScaleDialog::MeasurementScaleDialog(const core::MeasureScale& current, double measuredPoints, QWidget* parent)
    : QDialog(parent), measuredPoints_(measuredPoints)
{
    setObjectName(QStringLiteral("measurementScaleDialog"));
    setWindowTitle(catalog::translate(catalog::key::MeasureDialogCalibrateTitle));
    build(current);
}

void MeasurementScaleDialog::build(const core::MeasureScale& current)
{
    auto* column = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    column->addLayout(form);

    paperUnit_ = new QComboBox(this);
    paperUnit_->setObjectName(QStringLiteral("measurePaperUnit"));
    for (const core::MeasureUnit unit : kPaperUnits) paperUnit_->addItem(symbol(unit), static_cast<int>(unit));
    selectUnit(paperUnit_, core::paperUnitFor(current.unit));
    realUnit_ = new QComboBox(this);
    realUnit_->setObjectName(QStringLiteral("measureRealUnit"));
    for (int i = 0; i < core::kMeasureUnitCount; ++i) {
        const auto unit = static_cast<core::MeasureUnit>(i);
        realUnit_->addItem(symbol(unit), i);
    }
    selectUnit(realUnit_, current.unit);

    paperLength_ = lengthBox(this, "measurePaperLength");
    realLength_ = lengthBox(this, "measureRealLength");
    if (measuredPoints_) {
        // CALIBRATION: what was measured, said in the paper unit -- shown, not edited -- and what it is.
        const core::MeasureUnit paper = core::paperUnitFor(current.unit);
        paperLength_->setValue(*measuredPoints_ / core::pointsPerMeasureUnit(paper));
        paperLength_->setReadOnly(true);
        paperUnit_->setEnabled(false);
        realLength_->setValue(current.toUnits(*measuredPoints_));
        auto* measured = new QLabel(catalog::translate(catalog::key::MeasureDialogMeasured)
                                        .arg(core::formatMeasureNumber(paperLength_->value(), 4) + QLatin1Char(' ') + symbol(paper)),
                                    this);
        measured->setObjectName(QStringLiteral("measureMeasured"));
        column->insertWidget(0, measured);
    } else {
        // The current scale in its own two lengths: one paper unit, and what it stands for.
        const core::MeasureUnit paper = core::paperUnitFor(current.unit);
        realLength_->setValue(current.ratio * core::pointsPerMeasureUnit(paper) / core::pointsPerMeasureUnit(current.unit));
    }

    auto* paperRow = new QHBoxLayout;
    paperRow->addWidget(paperLength_, 1);
    paperRow->addWidget(paperUnit_);
    auto* realRow = new QHBoxLayout;
    realRow->addWidget(realLength_, 1);
    realRow->addWidget(realUnit_);
    form->addRow(catalog::translate(catalog::key::MeasureDialogPaper), paperRow);
    form->addRow(catalog::translate(measuredPoints_ ? catalog::key::MeasureDialogKnown : catalog::key::MeasureDialogReal),
                 realRow);

    precision_ = new QSpinBox(this);
    precision_->setObjectName(QStringLiteral("measurePrecision"));
    precision_->setRange(0, core::kMaxMeasurePrecision);
    precision_->setValue(current.precision);
    form->addRow(catalog::translate(catalog::key::MeasureDialogPrecision), precision_);

    preview_ = new QLabel(this);
    preview_->setObjectName(QStringLiteral("measureScalePreview"));
    column->addWidget(preview_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    column->addWidget(buttons_);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    for (QDoubleSpinBox* box : {paperLength_, realLength_})
        connect(box, &QDoubleSpinBox::valueChanged, this, &MeasurementScaleDialog::refresh);
    for (QComboBox* box : {paperUnit_, realUnit_})
        connect(box, &QComboBox::currentIndexChanged, this, &MeasurementScaleDialog::refresh);
    connect(precision_, &QSpinBox::valueChanged, this, &MeasurementScaleDialog::refresh);
    refresh();
}

std::optional<core::MeasureScale> MeasurementScaleDialog::chosen() const
{
    core::MeasureScale scale;
    scale.unit = unitOf(realUnit_);
    scale.precision = precision_->value();
    if (measuredPoints_) {
        const core::Result<double> ratio = core::calibratedRatio(*measuredPoints_, realLength_->value(), scale.unit);
        if (!ratio) return std::nullopt;
        scale.ratio = ratio.value();
    } else {
        const double paper = paperLength_->value() * core::pointsPerMeasureUnit(unitOf(paperUnit_));
        const double real = realLength_->value() * core::pointsPerMeasureUnit(scale.unit);
        if (!(paper > 0.0) || !(real > 0.0)) return std::nullopt;
        scale.ratio = real / paper;
    }
    if (!scale.isValid()) return std::nullopt;
    return scale;
}

void MeasurementScaleDialog::refresh()
{
    const std::optional<core::MeasureScale> scale = chosen();
    preview_->setText(scale ? catalog::translate(catalog::key::MeasureDialogScale).arg(core::scaleString(*scale))
                            : catalog::translate(catalog::key::MeasureDialogInvalid));
    if (QPushButton* ok = buttons_->button(QDialogButtonBox::Ok)) ok->setEnabled(scale.has_value());
}

} // namespace rawpage::ui
