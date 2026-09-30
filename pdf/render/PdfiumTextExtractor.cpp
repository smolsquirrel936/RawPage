#include "pdf/render/PdfiumTextExtractor.h"

#include "core/text/PageTextModel.h"
#include "pdf/render/PdfiumDocument.h"
#include "pdf/render/PdfiumInit.h"

#include <fpdf_text.h>
#include <fpdfview.h>

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rawpage::pdf::render {
namespace {

core::Error extractError(core::ErrorCode code, const char* key, int pageIndex)
{
    return core::Error{code, QLatin1String(key), QString::number(pageIndex)};
}

bool isHighSurrogate(unsigned int unit) noexcept { return unit >= 0xD800 && unit <= 0xDBFF; }
bool isLowSurrogate(unsigned int unit) noexcept { return unit >= 0xDC00 && unit <= 0xDFFF; }

core::PdfRect rectFrom(double left, double bottom, double right, double top)
{
    // Normalized rather than trusted, for `PdfiumDocument.cpp`'s reason: an inverted box is a
    // negative size, and a negative size multiplied through a scale is a highlight nobody sees.
    const double x0 = std::min(left, right);
    const double x1 = std::max(left, right);
    const double y0 = std::min(bottom, top);
    const double y1 = std::max(bottom, top);
    return core::PdfRect{core::PdfPoint{x0, y0}, core::PdfSize{x1 - x0, y1 - y0}};
}

core::PdfRect unite(const core::PdfRect& a, const core::PdfRect& b)
{
    return rectFrom(std::min(a.left(), b.left()), std::min(a.bottom(), b.bottom()),
                    std::max(a.right(), b.right()), std::max(a.top(), b.top()));
}

// The font that drew character `index`, as the engine names it. UTF-8 per `fpdf_text.h`, in the
// two-call shape: ask for the length (which includes the terminator), allocate, ask again.
core::text::FontIdentity fontIdentity(FPDF_TEXTPAGE page, int index)
{
    core::text::FontIdentity font;
    int flags = 0;
    const unsigned long length = FPDFText_GetFontInfo(page, index, nullptr, 0, &flags);
    if (length > 1) {
        std::vector<char> buffer(length);
        if (FPDFText_GetFontInfo(page, index, buffer.data(), length, &flags) == length)
            font.name = QString::fromUtf8(buffer.data(), static_cast<qsizetype>(length - 1));
    }
    font.flags = flags;
    // ZERO MEANS "COULD NOT SAY", measured at Stage 5 T2: the engine derives a weight from the
    // descriptor's /StemV, so a base-14 font with no descriptor -- `Helvetica-Bold` in every corpus
    // file -- reports 0, not 700. The model's "unknown" is -1, and `FontIdentity::isBold()` falls
    // back to the flags and the standard name for exactly this case.
    const int weight = FPDFText_GetFontWeight(page, index);
    font.weight = weight > 0 ? weight : -1;
    return font;
}

} // namespace

PdfiumTextExtractor::PdfiumTextExtractor(PdfiumDocument& document, core::DocumentHandle self)
    : document_(document), self_(self)
{
}

core::Status PdfiumTextExtractor::extractPage(core::DocumentHandle document, int pageIndex,
                                              core::text::PageTextModel& into)
{
    if (document != self_) {
        return core::Error{core::ErrorCode::InvalidArgument,
                           QLatin1String("error.backend.unknownDocument"),
                           QString::number(static_cast<qulonglong>(document.id))};
    }
    // The TREE's page count, not the engine's: `PdfiumDocument` drops a trailing run of pages the
    // engine claims and cannot load (`pages-count-wrong.pdf`'s /Count 97 over three kids), and an
    // extractor that answered for page 50 of that file would be describing a page the viewer says
    // does not exist.
    if (document_.pages().at(pageIndex) == nullptr)
        return extractError(core::ErrorCode::InvalidArgument, "error.render.noSuchPage", pageIndex);

    // BUILT ASIDE AND SWAPPED IN AT THE END, so a failure leaves `into` exactly as it was. Partial
    // application is a bug in this project (CLAUDE.md), and a half-filled model is a page that
    // appears to end in the middle of a sentence.
    core::text::PageTextModel model(pageIndex);

    {
        // EVERYTHING inside the lock, as the rasterizer does -- the page load, the text page, every
        // query and both closes. PDFium's sharing is process-global (I-05), so a text query racing a
        // render on another document is the same heap corruption as two renders.
        const std::lock_guard<EngineMutex> guard(engineLock());

        auto* engine = static_cast<FPDF_DOCUMENT>(document_.engineHandle());
        if (engine == nullptr)
            return extractError(core::ErrorCode::Unavailable, "error.render.documentClosed", pageIndex);

        FPDF_PAGE page = FPDF_LoadPage(engine, pageIndex);
        if (page == nullptr)
            return extractError(core::ErrorCode::ParseFailure, "error.text.extractFailed", pageIndex);
        FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
        if (textPage == nullptr) {
            FPDF_ClosePage(page);
            return extractError(core::ErrorCode::ParseFailure, "error.text.extractFailed", pageIndex);
        }

        const int count = FPDFText_CountChars(textPage);
        if (count < 0) {
            FPDFText_ClosePage(textPage);
            FPDF_ClosePage(page);
            return extractError(core::ErrorCode::ParseFailure, "error.text.extractFailed", pageIndex);
        }

        // ONE FONT LOOKUP PER TEXT OBJECT, not per character. Every character of a text object is
        // drawn in that object's font, and `FPDFText_GetFontInfo` is a two-call string API -- on a
        // dense page that is 12,000 calls to learn two names. Keyed on the object pointer, which is
        // stable for the life of this text page and never leaves this function.
        std::unordered_map<const void*, int> fontOfObject;

        for (int index = 0; index < count; ++index) {
            unsigned int unit = FPDFText_GetUnicode(textPage, index);

            core::text::TextChar character;
            std::uint8_t flags = 0;
            if (FPDFText_IsGenerated(textPage, index) == 1)
                flags |= static_cast<std::uint8_t>(core::text::CharFlag::Generated);
            if (FPDFText_IsHyphen(textPage, index) == 1)
                flags |= static_cast<std::uint8_t>(core::text::CharFlag::Hyphen);
            if (FPDFText_HasUnicodeMapError(textPage, index) == 1)
                flags |= static_cast<std::uint8_t>(core::text::CharFlag::Unmapped);

            FS_RECTF loose{};
            if (FPDFText_GetLooseCharBox(textPage, index, &loose))
                character.box = rectFrom(loose.left, loose.bottom, loose.right, loose.top);
            double x = 0.0;
            double y = 0.0;
            if (FPDFText_GetCharOrigin(textPage, index, &x, &y)) character.origin = core::PdfPoint{x, y};
            const float angle = FPDFText_GetCharAngle(textPage, index);
            character.angle = angle < 0.0F ? 0.0 : static_cast<double>(angle);
            character.fontSize = FPDFText_GetFontSize(textPage, index);

            if (FPDF_PAGEOBJECT object = FPDFText_GetTextObject(textPage, index)) {
                const auto known = fontOfObject.find(object);
                if (known != fontOfObject.end()) {
                    character.font = known->second;
                } else {
                    character.font = model.addFont(fontIdentity(textPage, index));
                    fontOfObject.emplace(object, character.font);
                }
            }

            // ONE ENTRY PER UNICODE SCALAR (D5-2). The engine's unit is UTF-16 on this platform, so
            // a character above U+FFFF arrives as two entries; they are joined here, the boxes
            // united, and a surrogate with no partner is a broken mapping and flagged as one.
            if (isHighSurrogate(unit) && index + 1 < count
                && isLowSurrogate(FPDFText_GetUnicode(textPage, index + 1))) {
                const unsigned int low = FPDFText_GetUnicode(textPage, index + 1);
                FS_RECTF second{};
                if (FPDFText_GetLooseCharBox(textPage, index + 1, &second)) {
                    character.box = unite(character.box, rectFrom(second.left, second.bottom,
                                                                  second.right, second.top));
                }
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                ++index;
            } else if (isHighSurrogate(unit) || isLowSurrogate(unit)) {
                flags |= static_cast<std::uint8_t>(core::text::CharFlag::Unmapped);
            }

            // A LINE-END HYPHEN ARRIVES AS U+0002, measured at Stage 5 T2 on `hyphenated.pdf`: the
            // engine flags the hyphen and REPLACES its codepoint with a control character that is
            // in no document. Stored as the hyphen-minus the content showed, because U+0002 in a
            // clipboard or a search string is text that is not there (the guide's new S1). The
            // bound: a line-end U+00AD or U+2010 in the content is recorded as U+002D too -- the
            // engine keeps no record of which it was.
            if (unit == 0x0002 && (flags & static_cast<std::uint8_t>(core::text::CharFlag::Hyphen)) != 0)
                unit = 0x002D;

            // ZERO is the engine's own "could not convert" (`fpdf_text.h`), and is unmapped by
            // definition whether or not the map-error flag says so.
            if (unit == 0) flags |= static_cast<std::uint8_t>(core::text::CharFlag::Unmapped);

            character.codepoint = static_cast<char32_t>(unit);
            character.flags = flags;
            model.append(character);
        }

        FPDFText_ClosePage(textPage);
        FPDF_ClosePage(page);
    }

    into = std::move(model);
    return {};
}

} // namespace rawpage::pdf::render
