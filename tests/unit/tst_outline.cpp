// `core/doc/Outline` and `core/doc/Destinations` -- the VOCABULARY, headless. Stage 4 T4.
//
// WHAT THIS FILE IS AND IS NOT. These two models are read by the PDFium facade and are pure data
// here, so this suite builds them by hand and asserts the arithmetic no engine is involved in:
// counting a tree, measuring its depth, finding a name, counting what did not resolve. **The
// claims about real documents -- that `outline-labels.pdf` has two levels, that `ghost-page`
// resolves to nothing -- are `tests/render/tst_docmodels.cpp`'s**, because they are claims about a
// READER and this target links no engine (`rawpage_assert_headless`). Neither half is sufficient:
// one proves the shape is computed correctly over data nobody read, the other proves the data is
// what a file actually says.

#include "core/doc/Destinations.h"
#include "core/doc/Outline.h"

#include <catch2/catch_test_macros.hpp>

using rawpage::core::Destination;
using rawpage::core::Destinations;
using rawpage::core::NamedDestination;
using rawpage::core::Outline;
using rawpage::core::OutlineItem;

namespace {

OutlineItem itemAt(const QString& title, int page)
{
    OutlineItem item;
    item.title = title;
    item.destination.pageIndex = page;
    item.destination.fit = Destination::Fit::Fit;
    return item;
}

} // namespace

TEST_CASE("an empty outline is empty rather than absent", "[outline]")
{
    // A document with no outline is ORDINARY -- most are. The pane shows its empty state; nothing
    // here is an error (guide case A-27).
    const Outline outline;
    REQUIRE(outline.isEmpty());
    REQUIRE(outline.nodeCount() == 0);
    REQUIRE(outline.depth() == 0);
}

TEST_CASE("an outline counts every node at every depth", "[outline]")
{
    Outline outline;
    OutlineItem chapter = itemAt(QStringLiteral("1 Introduction"), 0);
    chapter.children.push_back(itemAt(QStringLiteral("1.1 Scope"), 1));
    chapter.children.push_back(itemAt(QStringLiteral("1.2 Terms"), 2));
    chapter.children.front().children.push_back(itemAt(QStringLiteral("1.1.1 Deeper"), 3));
    outline.roots().push_back(std::move(chapter));
    outline.roots().push_back(itemAt(QStringLiteral("2 The end"), 9));

    // Five nodes over three levels: a count that stopped at the roots would say two, and one that
    // stopped at the children would say four.
    REQUIRE_FALSE(outline.isEmpty());
    REQUIRE(outline.nodeCount() == 5);
    REQUIRE(outline.depth() == 3);
}

TEST_CASE("a flat outline is one level deep", "[outline]")
{
    // The control for the depth arithmetic: a depth that counted ROOTS rather than LEVELS would
    // report three here.
    Outline outline;
    outline.roots().push_back(itemAt(QStringLiteral("a"), 0));
    outline.roots().push_back(itemAt(QStringLiteral("b"), 1));
    outline.roots().push_back(itemAt(QStringLiteral("c"), 2));
    REQUIRE(outline.depth() == 1);
    REQUIRE(outline.nodeCount() == 3);
}

TEST_CASE("an unresolved destination is a value rather than an absence", "[destinations]")
{
    Destination nowhere;
    REQUIRE(nowhere.pageIndex == -1);
    REQUIRE_FALSE(nowhere.isResolved());
    REQUIRE(nowhere.fit == Destination::Fit::Unknown);

    Destination somewhere;
    somewhere.pageIndex = 0;
    REQUIRE(somewhere.isResolved());   // page 0 is a page: a bare `pageIndex > 0` test would miss it
}

TEST_CASE("the destination table finds by exact name and counts what did not resolve",
          "[destinations]")
{
    Destinations destinations;
    REQUIRE(destinations.isEmpty());

    Destination first;
    first.pageIndex = 0;
    first.fit = Destination::Fit::XYZ;
    first.hasX = true;
    first.hasY = true;
    first.x = 72.0;
    first.y = 720.0;
    destinations.add(NamedDestination{QStringLiteral("chapter-one"), first});

    Destination second;
    second.pageIndex = 4;
    second.fit = Destination::Fit::Fit;
    destinations.add(NamedDestination{QStringLiteral("appendix"), second});

    // The dangling one stays in the table. A pane that omitted it would tell the user their
    // document has two destinations when it has three, and the missing one is the one they were
    // looking for (D4-3).
    destinations.add(NamedDestination{QStringLiteral("ghost-page"), Destination{}});

    REQUIRE(destinations.size() == 3);
    REQUIRE(destinations.unresolvedCount() == 1);

    const NamedDestination* found = destinations.find(QStringLiteral("chapter-one"));
    REQUIRE(found != nullptr);
    REQUIRE(found->destination.pageIndex == 0);
    REQUIRE(found->destination.hasX);
    REQUIRE(found->destination.x == 72.0);
    REQUIRE_FALSE(found->destination.hasZoom);

    // PDF destination names are byte strings and are CASE-SENSITIVE: folding them would make two
    // distinct destinations one.
    REQUIRE(destinations.find(QStringLiteral("Chapter-One")) == nullptr);
    REQUIRE(destinations.find(QStringLiteral("nothing-by-this-name")) == nullptr);
}

TEST_CASE("nothing is unresolved when every destination resolves", "[destinations]")
{
    // The control: a count that returned the table's size would pass the case above.
    Destinations destinations;
    Destination resolved;
    resolved.pageIndex = 2;
    destinations.add(NamedDestination{QStringLiteral("one"), resolved});
    destinations.add(NamedDestination{QStringLiteral("two"), resolved});
    REQUIRE(destinations.unresolvedCount() == 0);
}

TEST_CASE("every fit mode has a stable name", "[destinations]")
{
    // The tokens a log line or a future file format would hold: a surface cannot display a value
    // the enum lacks, which is the same arrangement `layoutModeName` and `fitModeName` use.
    REQUIRE(std::string(rawpage::core::destinationFitName(Destination::Fit::XYZ)) == "XYZ");
    REQUIRE(std::string(rawpage::core::destinationFitName(Destination::Fit::Fit)) == "Fit");
    REQUIRE(std::string(rawpage::core::destinationFitName(Destination::Fit::FitH)) == "FitH");
    REQUIRE(std::string(rawpage::core::destinationFitName(Destination::Fit::FitBV)) == "FitBV");
    REQUIRE(std::string(rawpage::core::destinationFitName(Destination::Fit::Unknown)) == "Unknown");
}
