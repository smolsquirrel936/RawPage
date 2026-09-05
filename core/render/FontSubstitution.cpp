#include "core/render/FontSubstitution.h"

#include <QRegularExpression>

namespace rawpage::core::render {
namespace {

// The language hint is BCP-47 and arrives from the document, so it can be anything: "zh", "zh-TW",
// "zh-Hant-TW", "cmn-Hant", or nothing at all. Only the script and region matter here, and Han
// disambiguation is the one place where guessing wrong is visible to a reader rather than subtle:
// the same code point is drawn with different strokes in Simplified and Traditional Chinese, and
// with different ones again in Japanese. A CJK document rendered in the wrong regional face is not
// "slightly off" -- it is what a native reader immediately calls broken.
enum class HanRegion { None, SimplifiedChinese, TraditionalChinese, Japanese, Korean };

HanRegion hanRegionFor(const QString& languageHint)
{
    const QString hint = languageHint.toLower();
    if (hint.isEmpty()) return HanRegion::None;

    if (hint.startsWith(QLatin1String("ja"))) return HanRegion::Japanese;
    if (hint.startsWith(QLatin1String("ko"))) return HanRegion::Korean;

    if (hint.startsWith(QLatin1String("zh")) || hint.startsWith(QLatin1String("cmn"))
        || hint.startsWith(QLatin1String("yue"))) {
        // Script subtag first, because it is explicit where the region is a proxy. "zh-Hant" with
        // no region is Traditional and says nothing about Taiwan or Hong Kong.
        if (hint.contains(QLatin1String("hant"))) return HanRegion::TraditionalChinese;
        if (hint.contains(QLatin1String("hans"))) return HanRegion::SimplifiedChinese;
        if (hint.contains(QLatin1String("tw")) || hint.contains(QLatin1String("hk"))
            || hint.contains(QLatin1String("mo"))) {
            return HanRegion::TraditionalChinese;
        }
        // Bare "zh" is Simplified: it is what the overwhelming majority of producers emit for
        // Simplified content and what the IANA registry's own suppress-script rules imply.
        return HanRegion::SimplifiedChinese;
    }

    return HanRegion::None;
}

void appendUnique(QStringList& list, const QString& family)
{
    if (family.isEmpty()) return;
    if (list.contains(family, Qt::CaseInsensitive)) return;
    list.append(family);
}

// The CJK candidates, serif first or sans first according to the request. Each list runs
// modern-face first and then the face that shipped with older Windows, because a machine that has
// only the old one is exactly the machine a substitution has to survive.
void appendHanCandidates(QStringList& out, HanRegion region, bool serif)
{
    switch (region) {
    case HanRegion::TraditionalChinese:
        if (serif) {
            appendUnique(out, QStringLiteral("PMingLiU"));
            appendUnique(out, QStringLiteral("MingLiU"));
        }
        appendUnique(out, QStringLiteral("Microsoft JhengHei"));
        appendUnique(out, QStringLiteral("PMingLiU"));
        break;
    case HanRegion::SimplifiedChinese:
        if (serif) {
            appendUnique(out, QStringLiteral("SimSun"));
            appendUnique(out, QStringLiteral("NSimSun"));
        }
        appendUnique(out, QStringLiteral("Microsoft YaHei"));
        appendUnique(out, QStringLiteral("SimSun"));
        break;
    case HanRegion::Japanese:
        if (serif) {
            appendUnique(out, QStringLiteral("Yu Mincho"));
            appendUnique(out, QStringLiteral("MS Mincho"));
        }
        appendUnique(out, QStringLiteral("Yu Gothic"));
        appendUnique(out, QStringLiteral("MS Gothic"));
        break;
    case HanRegion::Korean:
        if (serif) appendUnique(out, QStringLiteral("Batang"));
        appendUnique(out, QStringLiteral("Malgun Gothic"));
        appendUnique(out, QStringLiteral("Gulim"));
        break;
    case HanRegion::None:
        break;
    }
}

} // namespace

bool isStandardFontName(const QString& normalizedFamily)
{
    // Five families, not fourteen names: the four Courier faces differ only by style and the style
    // is gone before this is called. `Arial`, `TimesNewRoman` and `CourierNew` are deliberately NOT
    // here -- they are real installed families that a resolver should match EXACTLY, and listing
    // them would decline a font we can serve perfectly.
    static const QStringList kStandard{
        QStringLiteral("Courier"), QStringLiteral("Helvetica"), QStringLiteral("Times"),
        QStringLiteral("Symbol"),  QStringLiteral("ZapfDingbats")};
    return kStandard.contains(normalizedFamily, Qt::CaseInsensitive);
}

QString lastResortFamily()
{
    return QStringLiteral("Arial");
}

QStringList substitutionCandidates(const platform::IFontResolver::Request& request)
{
    QStringList out;

    const HanRegion region = hanRegionFor(request.languageHint);

    // ORDER OF THE THREE TESTS IS THE POLICY, and it is not arbitrary.
    //
    // Symbolic first: a symbolic font's byte values are not characters at all, so serif-versus-sans
    // is meaningless for it and picking a text face by that route draws letters where the document
    // meant arrows. Fixed pitch second, because a monospaced serif face is still monospaced first --
    // code and tables break when the advance width changes, and they do not break when the serifs
    // go. Serif last, which is the ordinary case.
    if (request.symbolic) {
        appendUnique(out, QStringLiteral("Segoe UI Symbol"));
        appendUnique(out, QStringLiteral("Segoe UI Emoji"));
        // Wingdings rather than a text face, because falling through from a symbolic request to
        // Arial renders dingbat code points as Latin letters -- legible, and wrong in a way that
        // reads as our bug rather than as a missing font.
        appendUnique(out, QStringLiteral("Wingdings"));
    } else if (request.fixedPitch) {
        // CJK before the Latin monospaced faces: a Han glyph is not in Consolas, and a document
        // whose language hint says Chinese needs coverage more than it needs a fixed advance.
        appendHanCandidates(out, region, request.serif);
        appendUnique(out, QStringLiteral("Consolas"));
        appendUnique(out, QStringLiteral("Courier New"));
    } else {
        appendHanCandidates(out, region, request.serif);
        if (request.serif) {
            appendUnique(out, QStringLiteral("Times New Roman"));
            appendUnique(out, QStringLiteral("Georgia"));
        } else {
            appendUnique(out, QStringLiteral("Segoe UI"));
            appendUnique(out, QStringLiteral("Tahoma"));
        }
    }

    // Always present, always last. `lastResortFamily()` is named separately so a caller can report
    // "nothing better was available" as a distinct fact -- see the header.
    appendUnique(out, lastResortFamily());
    return out;
}

QString normalizeBaseFontName(const QString& baseFont)
{
    QString name = baseFont;
    if (name.startsWith(QLatin1Char('/'))) name.remove(0, 1);

    // The subset tag: exactly six capitals and a '+', per ISO 32000-1 9.6.4. Anchored and
    // length-exact rather than "anything before a plus", because a family name may legitimately
    // contain one -- "Foo+Bar" is not a subset of "Bar".
    static const QRegularExpression subsetTag(QStringLiteral("^[A-Z]{6}\\+"));
    name.remove(subsetTag);

    // "Arial,BoldItalic" -- the comma form. Everything after the comma is style.
    const qsizetype comma = name.indexOf(QLatin1Char(','));
    if (comma > 0) name.truncate(comma);

    // "Garamond-Bold" -- the hyphen form. Only the trailing style word goes; a hyphenated family
    // ("Frutiger-Next") keeps its hyphen because the part after it is not a style word.
    static const QRegularExpression styleSuffix(
        QStringLiteral("[-_ ]?(Bold|Italic|Oblique|Regular|Roman|Light|Medium|Semibold|Demibold|"
                       "Black|Heavy|Thin|Condensed|Narrow|BoldItalic|BoldOblique)+$"),
        QRegularExpression::CaseInsensitiveOption);
    // Applied repeatedly: "Garamond-BoldItalic" and "Garamond-Bold-Italic" are both real, and one
    // pass leaves the second with a trailing "-Bold".
    QString previous;
    while (previous != name) {
        previous = name;
        name.remove(styleSuffix);
    }

    // The PostScript suffixes real producers append: "TimesNewRomanPSMT", "ArialMT".
    static const QRegularExpression psSuffix(QStringLiteral("(PSMT|PS|MT)$"));
    name.remove(psSuffix);

    return name.trimmed();
}

} // namespace rawpage::core::render
