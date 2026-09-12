// How far apart two flat colours look. Stage 2 issue I-29.
//
// CIE76 delta E, through sRGB -> linear -> XYZ (D65) -> L*a*b*. The standard formulation, and the
// crude one on purpose: CIEDE2000 is more faithful near neutrals, but every question asked of this
// helper is "clearly different or not" against a floor several times the just-noticeable
// difference (~2.3), and at that distance the two formulas agree about the answer.
//
// WHY NOT A CONTRAST RATIO. `tst_theme`'s `contrastRatio` compares LUMINANCE, which is the right
// question for text on a background and the wrong one for a background beside a background: a
// pale amber tint and a pale grey have nearly the same luminance and are nonetheless obvious apart,
// because they differ in hue.
//
// WHY NOT `!=`. The first draft of the banner case asserted `fill != desk`, and its negative
// control -- the old banner, painted from `AlternateBase` -- PASSED it: the old fill measured
// #f7f7f7 against a #f3f3f3 desk, unequal and about 1.3 apart, which is the invisible grey the
// user reported. Exact inequality proves only that SOME bit differs (convention 7).

#ifndef RAWPAGE_TESTS_UI_COLORDISTANCE_H
#define RAWPAGE_TESTS_UI_COLORDISTANCE_H

#include <QColor>

#include <array>
#include <cmath>

namespace rawpage::uitest {

inline double deltaE76(const QColor& a, const QColor& b)
{
    const auto lab = [](const QColor& color) {
        const auto linear = [](double value) {
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        const QColor rgb = color.toRgb();
        const double r = linear(rgb.redF());
        const double g = linear(rgb.greenF());
        const double bl = linear(rgb.blueF());
        const double x = (0.4124 * r + 0.3576 * g + 0.1805 * bl) / 0.95047;
        const double y = (0.2126 * r + 0.7152 * g + 0.0722 * bl) / 1.00000;
        const double z = (0.0193 * r + 0.1192 * g + 0.9505 * bl) / 1.08883;
        const auto f = [](double v) {
            return v > 216.0 / 24389.0 ? std::cbrt(v) : (24389.0 / 27.0 * v + 16.0) / 116.0;
        };
        return std::array<double, 3>{116.0 * f(y) - 16.0, 500.0 * (f(x) - f(y)),
                                     200.0 * (f(y) - f(z))};
    };
    const auto p = lab(a);
    const auto q = lab(b);
    return std::sqrt((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1])
                     + (p[2] - q[2]) * (p[2] - q[2]));
}

// Two flat areas meeting at a hard edge read as DIFFERENT SURFACES at a glance, rather than on
// inspection, from about here. A floor with room, not a target; the shipped tokens clear it by
// more than double (ThemeManager.cpp says by how much).
inline constexpr double kDistinctSurfaceDeltaE = 10.0;

} // namespace rawpage::uitest

#endif // RAWPAGE_TESTS_UI_COLORDISTANCE_H
