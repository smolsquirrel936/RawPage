// core/doc/PaperSize -- the named page sizes a blank page is made at, and the token `documents/defaultPageSize` stores.
// Stage 7 T8 (`Stage7-Assembly-Plan.md` D7-22; guide A-45).
//
// A HEADER OF ITS OWN for `core/doc/SaveMode.h`'s reason: the settings table names this enum, and `SettingsSchema.h` is
// included by every settings reader.
//
// SIZES IN PDF POINTS, portrait, as ISO 216 and ANSI/ASME Y14.1 give them rounded to the point: A4 595 x 842, Letter
// 612 x 792, Legal 612 x 1008, A3 842 x 1191, A5 420 x 595. A4 BY DEFAULT -- not a guess at the person's locale, which
// a setting that changed meaning with the region it was read in would be; the Insert Pages dialog offers the others
// and remembers nothing a person did not choose here.

#ifndef RAWPAGE_CORE_DOC_PAPERSIZE_H
#define RAWPAGE_CORE_DOC_PAPERSIZE_H

#include "core/Geometry.h"

#include <array>
#include <optional>
#include <string_view>

namespace rawpage::core {

enum class PaperSize { A4, Letter, Legal, A3, A5 };

inline constexpr std::array<PaperSize, 5> kPaperSizes{PaperSize::A4, PaperSize::Letter, PaperSize::Legal, PaperSize::A3,
                                                      PaperSize::A5};

inline constexpr std::string_view paperSizeName(PaperSize size) noexcept
{
    switch (size) {
    case PaperSize::Letter: return "letter";
    case PaperSize::Legal:  return "legal";
    case PaperSize::A3:     return "a3";
    case PaperSize::A5:     return "a5";
    case PaperSize::A4:     break;
    }
    return "a4";
}

inline std::optional<PaperSize> paperSizeFromName(std::string_view name) noexcept
{
    for (const PaperSize size : kPaperSizes)
        if (paperSizeName(size) == name) return size;
    return std::nullopt;
}

inline constexpr PdfSize paperSizePoints(PaperSize size) noexcept
{
    switch (size) {
    case PaperSize::Letter: return PdfSize{612.0, 792.0};
    case PaperSize::Legal:  return PdfSize{612.0, 1008.0};
    case PaperSize::A3:     return PdfSize{842.0, 1191.0};
    case PaperSize::A5:     return PdfSize{420.0, 595.0};
    case PaperSize::A4:     break;
    }
    return PdfSize{595.0, 842.0};
}

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_PAPERSIZE_H
