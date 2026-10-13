// ui/dialogs/MeasurementScaleDialog -- sets the measurement scale: from two lengths, or by CALIBRATION from a length
// measured on the page. Stage 6 T12, decision D6-14 (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// IT COLLECTS; IT CHANGES NO DOCUMENT
// ===========================================================================================
// Every dialog collects parameters and constructs a command (`CLAUDE.md`) -- and this one constructs none, because
// a scale is not a document change: it is three preferences (`measuring/scaleRatio`, `measuring/displayUnit`,
// `measuring/precision`), which the caller writes with what `chosen()` returns. A measurement already made keeps the
// scale in its own `/Measure`; only the next one is made at the new scale (D6-14).
//
// ===========================================================================================
// TWO WAYS TO SAY ONE SCALE
// ===========================================================================================
// `annot.measurementScale`: a length ON THE PAGE in a paper unit (pt, in, mm, cm) and the length it STANDS FOR in
// any of the nine display units -- "1 in = 4 ft" -- reduced to the one ratio the key holds
// (`core::MeasureScale`), with the scale in words shown as it will be written into `/R`.
// `annot.measureCalibrate`: the length the distance tool measured, said in the current display unit's paper unit,
// and the length it IS -- `core::calibratedRatio`. Both say the decimals.

#ifndef RAWPAGE_UI_DIALOGS_MEASUREMENTSCALEDIALOG_H
#define RAWPAGE_UI_DIALOGS_MEASUREMENTSCALEDIALOG_H

#include "core/Measure.h"

#include <QDialog>

#include <optional>

class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace rawpage::ui {

class MeasurementScaleDialog : public QDialog {
    Q_OBJECT

public:
    // The scale from two lengths, starting from `current`.
    explicit MeasurementScaleDialog(const core::MeasureScale& current, QWidget* parent = nullptr);
    // CALIBRATION: `measuredPoints` were measured on the page; `current` gives the unit and decimals to start from.
    MeasurementScaleDialog(const core::MeasureScale& current, double measuredPoints, QWidget* parent = nullptr);

    bool isCalibrating() const noexcept { return measuredPoints_.has_value(); }

    // The scale the fields state, or nothing while they state none (a length of zero, a ratio out of range).
    std::optional<core::MeasureScale> chosen() const;

    // For the cases, which fill the fields as a person does.
    QDoubleSpinBox* paperLength() const noexcept { return paperLength_; }
    QComboBox* paperUnit() const noexcept { return paperUnit_; }
    QDoubleSpinBox* realLength() const noexcept { return realLength_; }
    QComboBox* realUnit() const noexcept { return realUnit_; }
    QSpinBox* precision() const noexcept { return precision_; }
    QLabel* preview() const noexcept { return preview_; }
    QDialogButtonBox* buttons() const noexcept { return buttons_; }

private:
    void build(const core::MeasureScale& current);
    void refresh();

    std::optional<double> measuredPoints_;
    QDoubleSpinBox* paperLength_ = nullptr;
    QComboBox* paperUnit_ = nullptr;
    QDoubleSpinBox* realLength_ = nullptr;
    QComboBox* realUnit_ = nullptr;
    QSpinBox* precision_ = nullptr;
    QLabel* preview_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_MEASUREMENTSCALEDIALOG_H
