// tst_fontresolver -- `platform/win/WinFontResolver`, the OS half of guide case A-66. Stage 2 T16.
//
// WHAT THIS SUITE OWNS: whether a family is installed on THIS machine and which file backs it.
// That is the whole of what `platform/win/` is allowed to decide -- the ordered list of families to
// try is `core/render/FontSubstitution`'s, and `tests/unit/tst_fontsubstitution.cpp` asserts it
// with no display and no Windows at all.
//
// WHAT IT DOES NOT COVER (convention 19): **anything PDFium does with the answer.** No engine is
// linked here. Whether the resolved face actually reaches the rasterizer, whether the base-14
// decline fires, and whether a second tile re-resolves are `tests/render/tst_fontbridge.cpp`'s,
// which is the only suite that can hold all three libraries at once.
//
// -------------------------------------------------------------------------------------------
// THIS SUITE ASSERTS AGAINST A REAL MACHINE, AND SAYS SO RATHER THAN PRETENDING OTHERWISE.
//
// Every case below depends on what is installed on the box it runs on. Arial and Times New Roman
// have shipped with every Windows since the nineties and the cases lean on exactly that; anything
// narrower -- a particular CJK face, a particular file name -- is asserted as a SHAPE (non-empty, a
// readable path, a different answer) rather than as a value, because a Windows N edition or a
// stripped container is a legitimate machine and a suite that fails there is reporting the wrong
// thing. Case A-62's rule for the battery, applied to fonts.

#include <catch2/catch_test_macros.hpp>

#include "core/render/FontSubstitution.h"
#include "platform/win/WinFontResolver.h"

#include <QFile>
#include <QFileInfo>

using rawpage::core::ErrorCode;
using rawpage::platform::FontMatch;
using rawpage::platform::IFontResolver;
using rawpage::platform::win::WinFontResolver;

namespace {

IFontResolver::Request requestFor(const QString& family)
{
    IFontResolver::Request request;
    request.family = family;
    return request;
}

} // namespace

TEST_CASE("a family that is installed resolves EXACTLY and names a readable file",
          "[platform][font]")
{
    const WinFontResolver resolver;

    const auto match = resolver.resolve(requestFor(QStringLiteral("Arial")));
    REQUIRE(match);

    // `exact` is the field the whole boundary turns on: a document rendered in a substitute looks
    // fine and is not what its author saw, and this is the only place that distinction is made.
    CHECK(match.value().exact);
    CHECK(match.value().family == QLatin1String("Arial"));

    // THE FILE PATH IS THE LOAD-BEARING FIELD, which is why DirectWrite is used rather than GDI:
    // PDFium's font hook is handed BYTES, so a match that cannot name a readable file is a match
    // nobody can use. Asserted by opening it, not by looking at the string.
    INFO("resolved to: " << match.value().filePath.toStdString());
    REQUIRE_FALSE(match.value().filePath.isEmpty());
    CHECK(QFileInfo(match.value().filePath).isFile());
    QFile file(match.value().filePath);
    CHECK(file.open(QIODevice::ReadOnly));
    CHECK(file.size() > 1024);   // an SFNT with a table directory is never smaller than this
}

TEST_CASE("the family name comes back from the collection, not echoed from the request",
          "[platform][font]")
{
    const WinFontResolver resolver;

    // DirectWrite matches case-insensitively, and the name a report shows a person should be the
    // one the system uses. A resolver that echoed the request would put "times new roman" in a log
    // line and in a future font-substitution dialog.
    const auto match = resolver.resolve(requestFor(QStringLiteral("times new roman")));
    REQUIRE(match);
    CHECK(match.value().exact);
    CHECK(match.value().family == QLatin1String("Times New Roman"));
}

TEST_CASE("a family nobody has is SUBSTITUTED, and the substitute is named", "[platform][font]")
{
    const WinFontResolver resolver;

    IFontResolver::Request request = requestFor(QStringLiteral("RawPageAbsentSerif"));
    request.serif = true;

    const auto match = resolver.resolve(request);
    REQUIRE(match);

    INFO("substitute: " << match.value().family.toStdString());
    CHECK_FALSE(match.value().exact);
    CHECK_FALSE(match.value().family.isEmpty());
    CHECK(match.value().family != request.family);
    CHECK(QFileInfo(match.value().filePath).isFile());

    // It came from the policy's list rather than from anywhere else. Without this the case would
    // pass over a resolver that returned any installed font it liked, which is precisely the
    // arrangement A-66 exists to rule out.
    CHECK(rawpage::core::render::substitutionCandidates(request).contains(match.value().family,
                                                                         Qt::CaseInsensitive));
}

TEST_CASE("a serif request and a sans request get different substitutes", "[platform][font]")
{
    const WinFontResolver resolver;

    IFontResolver::Request serif = requestFor(QStringLiteral("RawPageAbsentSerif"));
    serif.serif = true;
    IFontResolver::Request sans = requestFor(QStringLiteral("RawPageAbsentSans"));

    const auto serifMatch = resolver.resolve(serif);
    const auto sansMatch = resolver.resolve(sans);
    REQUIRE(serifMatch);
    REQUIRE(sansMatch);

    // The descriptor was read. This is the same assertion `tests/render` makes end to end, and it
    // is here as well because a failure in one and not the other localizes the defect to a layer.
    INFO("serif -> " << serifMatch.value().family.toStdString()
         << " ; sans -> " << sansMatch.value().family.toStdString());
    CHECK(serifMatch.value().family != sansMatch.value().family);
}

TEST_CASE("the weight and the slant reach DirectWrite", "[platform][font]")
{
    const WinFontResolver resolver;

    IFontResolver::Request regular = requestFor(QStringLiteral("Arial"));
    IFontResolver::Request bold = requestFor(QStringLiteral("Arial"));
    bold.weight = 700;
    IFontResolver::Request italic = requestFor(QStringLiteral("Arial"));
    italic.italic = true;

    const auto regularMatch = resolver.resolve(regular);
    const auto boldMatch = resolver.resolve(bold);
    const auto italicMatch = resolver.resolve(italic);
    REQUIRE(regularMatch);
    REQUIRE(boldMatch);
    REQUIRE(italicMatch);

    // All three are the same FAMILY and must be different FACES. Arial ships all three on every
    // Windows, so a resolver that ignored weight and style would return one path three times --
    // which renders a bold document in regular and looks like a content bug rather than a font one.
    CHECK(regularMatch.value().family == boldMatch.value().family);
    CHECK(boldMatch.value().filePath != regularMatch.value().filePath);
    CHECK(italicMatch.value().filePath != regularMatch.value().filePath);
}

TEST_CASE("installedFamilies answers the machine and is sorted", "[platform][font]")
{
    const WinFontResolver resolver;

    const auto families = resolver.installedFamilies();
    REQUIRE(families);

    // A count rather than a list: which families a machine has is not this project's business, and
    // an assertion naming one would fail on a legitimately different install. Any Windows has more
    // than a handful.
    INFO("installed families: " << families.value().size());
    CHECK(families.value().size() > 20);
    CHECK(families.value().contains(QStringLiteral("Arial"), Qt::CaseInsensitive));

    QStringList sorted = families.value();
    sorted.sort(Qt::CaseInsensitive);
    CHECK(sorted == families.value());
}

TEST_CASE("an empty requested family goes straight to the candidate list", "[platform][font]")
{
    const WinFontResolver resolver;

    // A /FontDescriptor with no usable name is a real thing in damaged files, and the answer is a
    // substitute rather than an error: the alternative is a page with no text on it.
    const auto match = resolver.resolve(requestFor(QString()));
    REQUIRE(match);
    CHECK_FALSE(match.value().exact);
    CHECK_FALSE(match.value().family.isEmpty());
}
