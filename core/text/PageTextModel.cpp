#include "core/text/PageTextModel.h"

#include <algorithm>

namespace rawpage::core::text {

namespace {

// The part of a font name that says its style: after a subset tag (`ABCDEF+`) and after the last
// `-` or `,` -- `Helvetica-BoldOblique`, `Arial,Bold`. Empty for a name with no style part, so a
// family whose NAME contains "Bold" (a face called `Boldface`) is not mistaken for one.
QString styleOf(const QString& name)
{
    QString base = name;
    const qsizetype plus = base.indexOf(QLatin1Char('+'));
    if (plus == 6) base = base.mid(plus + 1);
    const qsizetype cut = std::max(base.lastIndexOf(QLatin1Char('-')), base.lastIndexOf(QLatin1Char(',')));
    return cut < 0 ? QString() : base.mid(cut + 1);
}

} // namespace

bool FontIdentity::isBold() const
{
    if (weight > 0) return weight >= 600;
    if ((flags & (1 << 18)) != 0) return true;   // ISO 32000-1 Table 123: ForceBold, bit 19
    const QString style = styleOf(name);
    return style.contains(QLatin1String("Bold"), Qt::CaseInsensitive)
           || style.contains(QLatin1String("Black"), Qt::CaseInsensitive)
           || style.contains(QLatin1String("Heavy"), Qt::CaseInsensitive);
}

bool FontIdentity::isItalic() const
{
    if ((flags & (1 << 6)) != 0) return true;    // Table 123: Italic, bit 7
    const QString style = styleOf(name);
    return style.contains(QLatin1String("Italic"), Qt::CaseInsensitive)
           || style.contains(QLatin1String("Oblique"), Qt::CaseInsensitive);
}

int PageTextModel::addFont(const FontIdentity& font)
{
    // A linear search: a page names a handful of fonts, and a map keyed on three fields would be
    // more code than the loop it replaces.
    const auto found = std::find(fonts_.begin(), fonts_.end(), font);
    if (found != fonts_.end()) return static_cast<int>(found - fonts_.begin());
    fonts_.push_back(font);
    return static_cast<int>(fonts_.size()) - 1;
}

void PageTextModel::append(TextChar character)
{
    // THE HEADER'S RULE, ENFORCED WHERE EVERY PRODUCER PASSES. An unmapped glyph's codepoint is the
    // engine's guess -- usually its character CODE -- and a guess stored here would reach a
    // clipboard as a character the document does not contain.
    if (character.isUnmapped()) character.codepoint = kReplacementCharacter;
    if (character.font < -1 || character.font >= static_cast<int>(fonts_.size()))
        character.font = -1;
    chars_.push_back(character);
}

const FontIdentity* PageTextModel::fontOf(const TextChar& character) const noexcept
{
    if (character.font < 0 || character.font >= static_cast<int>(fonts_.size())) return nullptr;
    return &fonts_[static_cast<std::size_t>(character.font)];
}

std::size_t PageTextModel::shownCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        chars_.begin(), chars_.end(), [](const TextChar& c) { return !c.isGenerated(); }));
}

std::size_t PageTextModel::unmappedCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(chars_.begin(), chars_.end(), [](const TextChar& c) {
        return !c.isGenerated() && c.isUnmapped();
    }));
}

bool PageTextModel::hasExtractableText() const noexcept
{
    return std::any_of(chars_.begin(), chars_.end(), [](const TextChar& c) {
        return !c.isGenerated() && !c.isUnmapped();
    });
}

QString PageTextModel::text(Whitespace whitespace) const
{
    QString out;
    out.reserve(static_cast<qsizetype>(chars_.size()));
    for (const TextChar& c : chars_) {
        if (whitespace == Whitespace::ShownOnly && c.isGenerated()) continue;
        // `char32_t` through `fromUcs4`'s one-character form: a scalar above U+FFFF becomes the
        // surrogate pair a QString holds, which appending the value as a QChar would truncate.
        const char32_t scalar = c.codepoint;
        out.append(QString::fromUcs4(&scalar, 1));
    }
    return out;
}

} // namespace rawpage::core::text
