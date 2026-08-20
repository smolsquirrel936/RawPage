// Length -- one strong type for a physical measurement, stored in PDF points.
//
// Features.md 1.3 offers the user five units: points, inches, millimetres, centimetres,
// picas. Storing a bare double and remembering which unit it is in is how a ruler ends up
// reading 25.4 instead of 1. Length stores points and converts explicitly at the edges.
//
// PDF points are the canonical unit because they are the document's own: 1 pt = 1/72 inch,
// exactly, by definition of the PDF user space. Every other unit here is an exact rational
// multiple of it, so the conversions are exact up to floating point and round-trip cleanly.

#ifndef RAWPAGE_CORE_UNITS_H
#define RAWPAGE_CORE_UNITS_H

#include <QString>

#include <cmath>

namespace rawpage::core {

enum class Unit {
    Points,
    Inches,
    Millimetres,
    Centimetres,
    Picas,
};

class Length {
public:
    constexpr Length() = default;

    // Named constructors only. `Length(3)` would be ambiguous about what 3 means, which is
    // the entire problem this type exists to solve.
    static constexpr Length fromPoints(double v) { return Length(v); }
    static constexpr Length fromInches(double v) { return Length(v * 72.0); }
    static constexpr Length fromMillimetres(double v) { return Length(v * (72.0 / 25.4)); }
    static constexpr Length fromCentimetres(double v) { return Length(v * (720.0 / 25.4)); }
    static constexpr Length fromPicas(double v) { return Length(v * 12.0); }

    static constexpr Length from(double v, Unit unit) {
        switch (unit) {
        case Unit::Points:      return fromPoints(v);
        case Unit::Inches:      return fromInches(v);
        case Unit::Millimetres: return fromMillimetres(v);
        case Unit::Centimetres: return fromCentimetres(v);
        case Unit::Picas:       return fromPicas(v);
        }
        return Length();
    }

    constexpr double points() const { return points_; }
    constexpr double inches() const { return points_ / 72.0; }
    constexpr double millimetres() const { return points_ * (25.4 / 72.0); }
    constexpr double centimetres() const { return points_ * (25.4 / 720.0); }
    constexpr double picas() const { return points_ / 12.0; }

    constexpr double in(Unit unit) const {
        switch (unit) {
        case Unit::Points:      return points();
        case Unit::Inches:      return inches();
        case Unit::Millimetres: return millimetres();
        case Unit::Centimetres: return centimetres();
        case Unit::Picas:       return picas();
        }
        return 0.0;
    }

    constexpr Length operator+(Length other) const { return Length(points_ + other.points_); }
    constexpr Length operator-(Length other) const { return Length(points_ - other.points_); }
    constexpr Length operator*(double factor) const { return Length(points_ * factor); }
    constexpr Length operator/(double divisor) const { return Length(points_ / divisor); }
    constexpr Length operator-() const { return Length(-points_); }

    constexpr bool operator==(const Length&) const = default;
    constexpr auto operator<=>(const Length&) const = default;

    // Comparison with a tolerance, because these are measurements. Exact == on a value
    // that has been through a unit round-trip is a test that fails for no useful reason.
    bool nearlyEquals(Length other, double tolerancePoints = 1e-9) const {
        return std::abs(points_ - other.points_) <= tolerancePoints;
    }

private:
    explicit constexpr Length(double points) : points_(points) {}
    double points_ = 0.0;
};

constexpr const char* unitSuffix(Unit unit) {
    switch (unit) {
    case Unit::Points:      return "pt";
    case Unit::Inches:      return "in";
    case Unit::Millimetres: return "mm";
    case Unit::Centimetres: return "cm";
    case Unit::Picas:       return "pc";
    }
    return "";
}

// Formatting is for rulers, measurement readouts, and property dialogs. The suffix is a
// unit symbol, not a translatable word -- "mm" is "mm" in every locale Features.md 9.1
// lists, and treating it as a string to translate would invite someone to translate it
// wrongly.
inline QString formatLength(Length length, Unit unit, int decimals = 2) {
    return QString::number(length.in(unit), 'f', decimals) + QLatin1String(unitSuffix(unit));
}

// Parses "12", "12mm", "12 mm", "1.5in". A bare number takes `defaultUnit`, which is what
// a user typing into a ruler field expects. Returns false rather than guessing on garbage.
inline bool parseLength(const QString& text, Unit defaultUnit, Length& out) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return false;

    static const struct { const char* suffix; Unit unit; } kSuffixes[] = {
        {"pt", Unit::Points}, {"in", Unit::Inches}, {"mm", Unit::Millimetres},
        {"cm", Unit::Centimetres}, {"pc", Unit::Picas},
    };

    for (const auto& entry : kSuffixes) {
        const QLatin1String suffix(entry.suffix);
        if (trimmed.endsWith(suffix, Qt::CaseInsensitive)) {
            bool ok = false;
            const double value = trimmed.left(trimmed.size() - suffix.size()).trimmed().toDouble(&ok);
            if (!ok) return false;
            out = Length::from(value, entry.unit);
            return true;
        }
    }

    bool ok = false;
    const double value = trimmed.toDouble(&ok);
    if (!ok) return false;
    out = Length::from(value, defaultUnit);
    return true;
}

} // namespace rawpage::core

#endif // RAWPAGE_CORE_UNITS_H
