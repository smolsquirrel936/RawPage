#include "pdf/ops/TransitionReader.h"

#include "pdf/cos/CosObject.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace rawpage::pdf::ops {
namespace {

using cos::CosObject;

// A PDF name, lowercased, for matching against `core::transitionStyleFromName`. ASCII only and
// deliberately so: every `/S` value ISO 32000-1 12.4.4 defines is ASCII, and `std::tolower` under a
// non-C locale is where a Turkish 'I' turns a name into something that matches nothing.
std::string lowerAscii(const std::string& text)
{
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
    return out;
}

// A name written as `/H`, or nothing. Returned as the file's own spelling rather than normalized,
// for the reason `core::PageTransition` gives: only two styles use these and the controller is
// what decides what they mean.
QString nameOf(const CosObject& dictionary, std::string_view key)
{
    const CosObject* value = dictionary.find(key);
    if (value == nullptr || !value->isName()) return QString();
    return QString::fromStdString(value->text());
}

// A POSITIVE, FINITE number of seconds, or nothing. A negative or absurd duration is refused here
// rather than carried: `/Dur -1` would make an auto-advance fire continuously, and a caller that
// had to range-check every field would eventually miss one.
std::optional<double> secondsOf(const CosObject& dictionary, std::string_view key)
{
    const CosObject* value = dictionary.find(key);
    if (value == nullptr || !value->isNumber()) return std::nullopt;
    const double seconds = value->numberValue(-1.0);

    // 24 hours. A bound rather than an expectation -- what it excludes is the NaN and the 1e300
    // that a malformed real produces, either of which would be a timer that never fires or one
    // that fires immediately.
    if (!(seconds >= 0.0) || seconds > 86400.0) return std::nullopt;
    return seconds;
}

} // namespace

void TransitionReader::read(cos::XrefTable& table, const PageWalk& walk, core::PageTransitions& out)
{
    out.setPageCount(static_cast<int>(walk.pages.size()));

    // --- The catalog's `/PageMode` ------------------------------------------------------------
    // Read through the trailer's `/Root` exactly as `PageOps::walk` does. A missing or malformed
    // catalog is not an error here: the model simply reports that the document does not ask to open
    // full screen, which is true of every document that cannot say so.
    const CosObject& root = table.trailer().get("Root");
    if (root.isReference()) {
        auto catalog = table.fetch(root.reference().number);
        if (catalog && catalog.value().object.isDictionary()) {
            const CosObject* mode = catalog.value().object.find("PageMode");
            if (mode != nullptr && mode->isName())
                out.setOpensFullScreen(lowerAscii(mode->text()) == "fullscreen");
        }
    }

    // --- Per page -----------------------------------------------------------------------------
    for (std::size_t index = 0; index < walk.pages.size(); ++index) {
        auto fetched = table.fetch(walk.pages[index].number);
        if (!fetched || !fetched.value().object.isDictionary()) continue;
        const CosObject& page = fetched.value().object;
        const int pageIndex = static_cast<int>(index);

        // `/Dur` FIRST, and it is a key on the PAGE rather than inside `/Trans` -- how long the
        // page is SHOWN, not how long the animation takes. The two are one letter apart in the
        // specification and confusing them makes a one-second fade into a one-second slide.
        if (const std::optional<double> shown = secondsOf(page, "Dur"))
            out.setTiming(pageIndex, core::PageTiming{*shown});

        // `/Trans` may be written inline or as an indirect reference; both are ordinary.
        const CosObject* transRef = page.find("Trans");
        if (transRef == nullptr) continue;

        const CosObject* trans = transRef;
        std::optional<cos::IndirectObject> resolved;
        if (transRef->isReference()) {
            auto indirect = table.fetch(transRef->reference().number);
            if (!indirect) continue;
            resolved = std::move(indirect).value();
            trans = &resolved->object;
        }
        if (!trans->isDictionary()) continue;

        core::PageTransition transition;

        // NO `/S` MEANS `/R`, which ISO 32000-1 12.4.4 states as the default and which is a cut.
        // The raw name stays EMPTY in that case -- `PageTransition::rawStyle`'s comment says so --
        // because the file did not name a style and inventing "R" here would be reporting a word
        // the document does not contain.
        const CosObject* style = trans->find("S");
        if (style != nullptr && style->isName()) {
            transition.rawStyle = QString::fromStdString(style->text());
            const std::optional<core::TransitionStyle> drawable =
                core::transitionStyleFromName(lowerAscii(style->text()));

            // A STYLE WE CANNOT DRAW IS `None` AND SAYS SO. Approximating `/Split` with a wipe
            // would be presenting an animation the author did not ask for; dropping the name would
            // leave nobody able to answer why the page did not move.
            transition.style = drawable.value_or(core::TransitionStyle::None);
            transition.supported = drawable.has_value();
        }

        transition.durationSeconds = secondsOf(*trans, "D");
        transition.dimension = nameOf(*trans, "Dm");
        transition.motion = nameOf(*trans, "M");

        // `/Di` IS A NUMBER OR THE NAME `/None`, which is the one field in this dictionary whose
        // type is genuinely either. Carried as text in both forms so the model has one field
        // rather than a number and a flag that can disagree.
        if (const CosObject* di = trans->find("Di")) {
            if (di->isNumber()) transition.direction = QString::number(di->numberValue());
            else if (di->isName()) transition.direction = QString::fromStdString(di->text());
        }

        out.setTransition(pageIndex, transition);
    }
}

} // namespace rawpage::pdf::ops
