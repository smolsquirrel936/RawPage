// tst_pagetree -- `core/doc/Page` and `core/doc/PageTree`, plus `core/render/RenderRequest`.
// Stage 2 T9.
//
// These three have no guide case of their own: `Page` and the read half of `PageTree` are brought
// forward by D2-5, and `RenderRequest` by T9's own need for the type `IPageRasterizer::raster()`
// takes. Their consumers are T10's `TileGrid` and `PagePresentation` and T11's `RenderCache`, none
// of which exists yet -- which is exactly why they are tested now. A vocabulary type with no test
// is one whose semantics get decided by its first accidental caller.
//
// HEADLESS BY CONSTRUCTION: all three are `core/`, so this runs on `portable-asan` where there is
// no display and no PDF engine. The same types are exercised against real documents in
// `tests/render/tst_pdfiumdoc.cpp`; these cases are about the rules, not about any file.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/Page.h"
#include "core/doc/PageTree.h"
#include "core/doc/AnnotationStore.h"
#include "core/doc/PageMap.h"
#include "core/render/RenderRequest.h"

#include <QString>

#include <optional>

#include <unordered_set>
#include <utility>
#include <vector>

using rawpage::core::Page;
using rawpage::core::PageRotation;
using rawpage::core::PageTree;
using rawpage::core::PdfPoint;
using rawpage::core::PdfRect;
using rawpage::core::PdfSize;
using rawpage::core::Revision;
using rawpage::core::render::RenderFlags;
using rawpage::core::render::RenderRequest;
using rawpage::core::render::TileRect;

namespace {

constexpr PdfRect kLetter{PdfPoint{0.0, 0.0}, PdfSize{612.0, 792.0}};
constexpr PdfRect kA4{PdfPoint{0.0, 0.0}, PdfSize{595.0, 842.0}};

} // namespace

// ==========================================================================================
// Page -- five boxes, and the one that matters is not /MediaBox
// ==========================================================================================

TEST_CASE("doc page resolves an absent box through the inheritance chain", "[core][doc]")
{
    // ISO 32000-1 14.11.2: CropBox defaults to MediaBox; Bleed, Trim and Art each default to
    // CropBox. A missing box is never ABSENT -- it is inherited -- and a reader that returned an
    // empty rect for one would lay out a zero-sized page.
    //
    // **What a viewer draws is the CropBox, not the MediaBox.** A print-ready file routinely has a
    // MediaBox with bleed and crop marks around a CropBox that is the actual page, and laying out
    // MediaBoxes puts a grey margin round every page and reports the wrong size in Properties.
    Page page(0, kLetter);

    CHECK(page.cropBox() == kLetter);
    CHECK(page.bleedBox() == kLetter);
    CHECK(page.trimBox() == kLetter);
    CHECK(page.artBox() == kLetter);
    CHECK_FALSE(page.hasExplicitCropBox());

    // An explicit CropBox becomes the default for the other three, not merely for itself.
    const PdfRect crop{PdfPoint{10.0, 10.0}, PdfSize{592.0, 772.0}};
    page.setCropBox(crop);
    CHECK(page.cropBox() == crop);
    CHECK(page.bleedBox() == crop);
    CHECK(page.trimBox() == crop);
    CHECK(page.artBox() == crop);
    CHECK(page.hasExplicitCropBox());

    // `hasExplicitCropBox()` is Stage 3's question rather than layout's: re-emitting an INHERITED
    // box as an explicit one changes the file for no reason, and byte-identical incremental save
    // is the property that would lose.
    CHECK(page.mediaBox() == kLetter);
}

TEST_CASE("doc page clips every box to the MediaBox", "[core][doc]")
{
    // The specification requires the intersection and REAL FILES VIOLATE IT: a CropBox larger than
    // its MediaBox is common enough that trusting it lays out pages of the wrong size against
    // files that open correctly everywhere else.
    Page page(0, kLetter);
    page.setCropBox(PdfRect{PdfPoint{-100.0, -100.0}, PdfSize{1000.0, 1000.0}});

    CHECK(page.cropBox().size.width == 612.0);
    CHECK(page.cropBox().size.height == 792.0);
    CHECK(page.cropBox().left() == 0.0);
    CHECK(page.cropBox().bottom() == 0.0);

    // A box that does not overlap at all gives an EMPTY rect, never a negative-sized one: a
    // negative size multiplies through a scale into a negative pixel count, which allocates
    // nothing and reads as a blank page rather than as an error.
    page.setCropBox(PdfRect{PdfPoint{5000.0, 5000.0}, PdfSize{10.0, 10.0}});
    CHECK(page.cropBox().size.width == 0.0);
    CHECK(page.cropBox().size.height == 0.0);
}

TEST_CASE("doc page normalizes rotation including negative and out-of-range values", "[core][doc]")
{
    // `/Rotate` is inheritable, a multiple of 90, and MAY BE NEGATIVE -- `-90` is a legal file and
    // means 270. C++'s `%` keeps the sign of the dividend, so a single modulo leaves -90 as -90,
    // which falls through to "no rotation" and displays a landscape page sideways.
    Page page(0, kLetter);

    page.setRotation(0);    CHECK(page.rotation() == PageRotation::None);
    page.setRotation(90);   CHECK(page.rotation() == PageRotation::Clockwise90);
    page.setRotation(180);  CHECK(page.rotation() == PageRotation::Half);
    page.setRotation(270);  CHECK(page.rotation() == PageRotation::Clockwise270);
    page.setRotation(-90);  CHECK(page.rotation() == PageRotation::Clockwise270);
    page.setRotation(-180); CHECK(page.rotation() == PageRotation::Half);
    page.setRotation(-270); CHECK(page.rotation() == PageRotation::Clockwise90);
    page.setRotation(450);  CHECK(page.rotation() == PageRotation::Clockwise90);
    page.setRotation(720);  CHECK(page.rotation() == PageRotation::None);
}

TEST_CASE("doc page transposes its display size on a quarter turn", "[core][doc]")
{
    // What layout and `TileGrid` want is the size AFTER rotation. A caller that scaled
    // `cropBox().size` itself gets every landscape page in a portrait document wrong -- and gets
    // it wrong in a way that looks like a layout bug rather than a rotation one.
    Page page(0, kLetter);

    CHECK(page.displaySize().width == 612.0);
    CHECK(page.displaySize().height == 792.0);

    page.setRotation(90);
    CHECK(page.displaySize().width == 792.0);
    CHECK(page.displaySize().height == 612.0);

    page.setRotation(180);
    CHECK(page.displaySize().width == 612.0);
    CHECK(page.displaySize().height == 792.0);

    page.setRotation(270);
    CHECK(page.displaySize().width == 792.0);

    // And it follows the CROP box, not the media box -- the one that is drawn.
    page.setRotation(0);
    page.setCropBox(PdfRect{PdfPoint{0.0, 0.0}, PdfSize{300.0, 400.0}});
    CHECK(page.displaySize().width == 300.0);
}

// ==========================================================================================
// PageTree -- the read half
// ==========================================================================================

TEST_CASE("doc pagetree renumbers its pages rather than trusting them", "[core][doc]")
{
    // Both the index a page CARRIES and its position are used -- `TileGrid` takes an index and a
    // `Page` -- so a page whose own index disagreed with where it sits would produce tiles
    // attributed to the wrong page. A caller building a tree from a filtered list cannot get this
    // wrong, because it is not asked to get it right.
    std::vector<Page> pages{Page(77, kA4), Page(3, kA4), Page(99, kA4)};
    const PageTree tree(std::move(pages));

    REQUIRE(tree.count() == 3);
    CHECK(tree.at(0)->index() == 0);
    CHECK(tree.at(1)->index() == 1);
    CHECK(tree.at(2)->index() == 2);

    // Range-checked in both directions, and nullptr rather than a throw: an out-of-range page is
    // an ordinary outcome when a document has just been replaced under a pane that has not
    // repainted yet.
    CHECK(tree.at(3) == nullptr);
    CHECK(tree.at(-1) == nullptr);
    CHECK(tree.displaySize(999).width == 0.0);
    CHECK(tree.rotation(999) == PageRotation::None);
}

TEST_CASE("doc pagetree finds a label without confusing it with an index", "[core][doc]")
{
    // ISO 32000-1 12.4.2: a document may number its front matter i, ii and then restart at 1, so
    // **it has two pages that both call themselves "1"**. A Go To Page box has to decide what the
    // user meant, and this class answers the two questions separately rather than guessing.
    std::vector<Page> pages;
    for (const char* label : {"i", "ii", "1", "2"}) {
        Page page(0, kA4);
        page.setLabel(QString::fromLatin1(label));
        pages.push_back(std::move(page));
    }
    const PageTree tree(std::move(pages));

    CHECK(tree.indexOfLabel(QStringLiteral("i")) == 0);
    CHECK(tree.indexOfLabel(QStringLiteral("1")) == 2);   // NOT index 1
    CHECK(tree.indexOfLabel(QStringLiteral("nope")) == -1);
    CHECK(tree.indexOfLabel(QString()) == -1);

    // The label is not the index in disguise, and this is the assertion that says so.
    CHECK(tree.at(2)->label() == QStringLiteral("1"));
    CHECK(tree.at(1)->label() != QStringLiteral("1"));
}

TEST_CASE("doc pagetree measures widest and tallest independently", "[core][doc]")
{
    // A document with one very wide page and one very tall page needs a viewport that fits BOTH
    // dimensions. Returning whichever page has the greatest area would clip the other, and
    // clipping is what a continuous scroll view cannot recover from without relaying out
    // mid-scroll. `corpus/ordinary/mixed-page-sizes.pdf` is the fixture this rule exists for.
    std::vector<Page> pages{
        Page(0, PdfRect{PdfPoint{0.0, 0.0}, PdfSize{1000.0, 100.0}}),   // very wide
        Page(0, PdfRect{PdfPoint{0.0, 0.0}, PdfSize{100.0, 1000.0}}),   // very tall
    };
    const PageTree tree(std::move(pages));

    CHECK(tree.largestDisplaySize().width == 1000.0);
    CHECK(tree.largestDisplaySize().height == 1000.0);

    // An empty tree is zero rather than undefined.
    CHECK(PageTree{}.largestDisplaySize().width == 0.0);
    CHECK(PageTree{}.count() == 0);
    CHECK(PageTree{}.isEmpty());
}

// ==========================================================================================
// RenderRequest -- its equality IS the cache key (D2-12)
// ==========================================================================================

namespace {

RenderRequest validRequest()
{
    RenderRequest request;
    request.document = rawpage::core::DocumentHandle{7};
    request.pageIndex = 3;
    request.tile = TileRect{0, 0, 256, 256};
    request.deviceScale = 2.0;
    request.revision = Revision::initial();
    return request;
}

} // namespace

TEST_CASE("render request refuses to be valid without a real revision", "[core][render]")
{
    // **D2-12's load-bearing rule**: the `Revision` is MANDATORY rather than defaulted, because a
    // defaulted one makes "a stale tile shown after a Stage 3 mutation" the DEFAULT behaviour.
    // `core::Revision`'s default-constructed value is deliberately invalid, and this is where that
    // becomes a refusal rather than a wrong pixel.
    CHECK(validRequest().isValid());

    RenderRequest noRevision = validRequest();
    noRevision.revision = Revision{};
    CHECK_FALSE(noRevision.isValid());

    // The other four, each of which has a silent failure mode: a zero scale divides, an empty tile
    // allocates nothing and returns a valid-looking empty image, and an invalid handle names no
    // document at all.
    RenderRequest noDocument = validRequest();
    noDocument.document = rawpage::core::DocumentHandle{};
    CHECK_FALSE(noDocument.isValid());

    RenderRequest negativePage = validRequest();
    negativePage.pageIndex = -1;
    CHECK_FALSE(negativePage.isValid());

    RenderRequest emptyTile = validRequest();
    emptyTile.tile = TileRect{0, 0, 0, 64};
    CHECK_FALSE(emptyTile.isValid());

    RenderRequest zeroScale = validRequest();
    zeroScale.deviceScale = 0.0;
    CHECK_FALSE(zeroScale.isValid());
}

TEST_CASE("render request equality covers every field that changes the pixels", "[core][render]")
{
    // If a field can change what is drawn and is NOT part of equality, the cache serves the wrong
    // picture. If it cannot change what is drawn and IS part of equality, the cache holds two
    // entries for one picture. Every field is checked in one direction here, which is the only way
    // to notice a new field that was added to the struct and not to `operator==`.
    const RenderRequest base = validRequest();
    CHECK(base == validRequest());

    auto differing = [&base](auto&& mutate) {
        RenderRequest other = base;
        mutate(other);
        return other != base;
    };

    CHECK(differing([](RenderRequest& r) { r.document = rawpage::core::DocumentHandle{8}; }));
    CHECK(differing([](RenderRequest& r) { r.pageIndex = 4; }));
    CHECK(differing([](RenderRequest& r) { r.tile.x = 1; }));
    CHECK(differing([](RenderRequest& r) { r.tile.width = 128; }));
    CHECK(differing([](RenderRequest& r) { r.deviceScale = 2.5; }));
    CHECK(differing([](RenderRequest& r) { r.revision = r.revision.next(); }));
    CHECK(differing([](RenderRequest& r) { r.flags = RenderFlags::Grayscale; }));
    CHECK(differing([](RenderRequest& r) { r.hiddenLayers = {12}; }));
}

TEST_CASE("render request hashes in agreement with its equality", "[core][render]")
{
    // A hash that disagreed with equality produces a cache that MISSES SILENTLY -- D2-12's named
    // failure whose only symptom is that scrolling is slow, and which no test of the cache itself
    // would catch. The hash is supplied beside `operator==` for exactly this reason, and this case
    // is what keeps them in step.
    const std::hash<RenderRequest> hash;
    const RenderRequest base = validRequest();

    // Equal requests hash equally. Two separately-built copies rather than one, so a hash that
    // cached its own answer inside the object would not pass.
    CHECK(hash(base) == hash(validRequest()));

    // And it is usable as a real key.
    std::unordered_set<RenderRequest> keys;
    keys.insert(base);
    keys.insert(validRequest());
    REQUIRE(keys.size() == 1);

    RenderRequest other = base;
    other.tile.y = 256;
    keys.insert(other);
    CHECK(keys.size() == 2);

    // Different revisions are different keys, which is the whole reason a `Revision` is in here:
    // after a Stage 3 mutation the same tile of the same page must not be served from before it.
    RenderRequest newer = base;
    newer.revision = base.revision.next();
    keys.insert(newer);
    CHECK(keys.size() == 3);
}

TEST_CASE("render request carries no pointer, so its key is by value", "[core][render]")
{
    // D2-12's first consequence, asserted structurally: a single pointer member makes the key
    // identity-based, and the cache then misses on every lookup -- nothing fails, nothing logs,
    // and the only symptom is slowness no test can see.
    //
    // `DocumentHandle` is a `std::uint64_t` in a struct, not a pointer to anything, which is what
    // lets two documents share one cache without their tiles colliding (open decision O7). A
    // trivially-copyable check on the handle is what makes that a compile-time fact.
    STATIC_REQUIRE(std::is_trivially_copyable_v<rawpage::core::DocumentHandle>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<TileRect>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Revision>);

    // A copy is a genuine copy: nothing is shared with the original, so a request cached under one
    // key cannot be mutated through another.
    RenderRequest copy = validRequest();
    copy.tile.x = 99;
    CHECK(validRequest().tile.x == 0);
}

// ==========================================================================================
// Stage 7 T2 -- insert, move, remove and duplicate, as one operation over a PageMap (D7-3), guide A-17
// ==========================================================================================
//
// The PAGE TREE and the ANNOTATION STORE's listings are the two parts of the model a page command changes
// besides the per-page refusals; each is rebuilt by the same `PageMap`, and each is undone by the map's
// inverse with what the first took away. Graded by NAME -- each page carries its label as a name -- so a
// case reads "the page called c is now third", never "the index arithmetic agreed with itself".

namespace {

PageTree namedTree(int count)
{
    std::vector<Page> pages;
    for (int index = 0; index < count; ++index) {
        Page page(index, index % 2 == 0 ? kLetter : kA4);
        page.setLabel(QString(QChar(QLatin1Char('a').unicode() + index)));
        page.setRotation(index % 4 * 90);
        pages.push_back(page);
    }
    return PageTree(std::move(pages));
}

QString namesOf(const PageTree& tree)
{
    QString out;
    for (const Page& page : tree.pages()) out += page.label();
    return out;
}

bool indicesAgree(const PageTree& tree)
{
    for (int index = 0; index < tree.count(); ++index) {
        if (tree.at(index)->index() != index) return false;
    }
    return true;
}

} // namespace

TEST_CASE("pagetree a move of a single page or a range at the start middle and end", "[core][doc][pagetree][A-17]")
{
    const struct {
        std::vector<int> pages;
        int to;
        const char* expected;
    } rows[] = {
        {{0}, 5, "bcdefa"}, {{5}, 0, "fabcde"}, {{2}, 3, "abdcef"},
        {{0, 1}, 2, "cdabef"}, {{3, 4, 5}, 0, "defabc"}, {{1, 4}, 3, "acdbef"},
    };
    for (const auto& row : rows) {
        PageTree tree = namedTree(6);
        const auto map = rawpage::core::PageMap::move(6, row.pages, row.to);
        REQUIRE(map);
        INFO("expected " << row.expected);
        REQUIRE(tree.apply(*map));
        CHECK(namesOf(tree) == QString::fromLatin1(row.expected));
        CHECK(indicesAgree(tree));
        // A moved page carries everything it is: its boxes, rotation and label went with it.
        const int b = tree.indexOfLabel(QStringLiteral("b"));
        CHECK(tree.at(b)->mediaBox() == kA4);
        CHECK(tree.at(b)->rotation() == PageRotation::Clockwise90);
        // And the inverse map puts it back.
        REQUIRE(tree.apply(map->inverse()));
        CHECK(namesOf(tree) == QStringLiteral("abcdef"));
    }
}

TEST_CASE("pagetree a removal and a duplication with their reverts", "[core][doc][pagetree][A-17]")
{
    PageTree tree = namedTree(6);
    const auto removal = rawpage::core::PageMap::removal(6, {0, 3, 5});
    REQUIRE(removal);
    const std::vector<Page> removed = tree.removedBy(*removal);
    REQUIRE(removed.size() == 3);
    REQUIRE(tree.apply(*removal));
    CHECK(namesOf(tree) == QStringLiteral("bce"));
    CHECK(indicesAgree(tree));
    REQUIRE(tree.apply(removal->inverse(), removed));
    CHECK(namesOf(tree) == QStringLiteral("abcdef"));
    CHECK(indicesAgree(tree));

    const auto duplication = rawpage::core::PageMap::duplication(6, {1, 5});
    REQUIRE(duplication);
    Page copyOfB = *tree.at(1);
    copyOfB.setLabel(QStringLiteral("B"));
    Page copyOfF = *tree.at(5);
    copyOfF.setLabel(QStringLiteral("F"));
    REQUIRE(tree.apply(*duplication, {copyOfB, copyOfF}));
    CHECK(namesOf(tree) == QStringLiteral("abBcdefF"));
    CHECK(indicesAgree(tree));
    REQUIRE(tree.apply(duplication->inverse(), {}));
    CHECK(namesOf(tree) == QStringLiteral("abcdef"));
}

TEST_CASE("pagetree refuses a removal of every page a wrong count and a wrong number of inserted pages", "[core][doc][pagetree][A-17]")
{
    // D7-21: a document with no pages is not a document. Each refusal changes NOTHING.
    PageTree tree = namedTree(3);
    CHECK_FALSE(tree.apply(*rawpage::core::PageMap::removal(3, {0, 1, 2})));
    CHECK_FALSE(tree.apply(rawpage::core::PageMap::identity(4)));
    CHECK_FALSE(tree.apply(*rawpage::core::PageMap::duplication(3, {1})));                 // no copy given
    CHECK_FALSE(tree.apply(*rawpage::core::PageMap::duplication(3, {1}), {Page(), Page()})); // two given
    CHECK(namesOf(tree) == QStringLiteral("abc"));
    CHECK(indicesAgree(tree));
    // THE CONTROL: the same calls with the right arguments are accepted.
    CHECK(tree.apply(*rawpage::core::PageMap::removal(3, {0, 1})));
    CHECK(namesOf(tree) == QStringLiteral("c"));
}

TEST_CASE("pagetree the annotation store's listings follow their pages and an undo is exact", "[core][doc][pagetree][annotations][A-17]")
{
    using rawpage::core::Annotation;
    using rawpage::core::AnnotationId;
    using rawpage::core::AnnotationListingEntry;
    using rawpage::core::AnnotationStore;
    using rawpage::core::PageMap;
    // Four pages: page 0 owns obj 10; page 1 owns obj 11 and a direct dictionary; page 2 lists obj 10 a SECOND
    // time (an annotation two pages list -- its owner is page 0); page 3 owns obj 13 and an entry that is no
    // dictionary at all.
    AnnotationStore::Reading reading;
    const auto make = [](AnnotationId id, int page) {
        Annotation a;
        a.id = id;
        a.page = page;
        a.subtype = rawpage::core::AnnotationSubtype::Square;
        return a;
    };
    reading.annotations = {make(AnnotationId::ofObject(10), 0), make(AnnotationId::ofObject(11), 1),
                           make(AnnotationId::ofDirect(1, 1), 1), make(AnnotationId::ofObject(13), 3)};
    const auto entry = [](std::optional<std::size_t> index) {
        AnnotationListingEntry e;
        e.fact.dictionary = index.has_value();
        e.annotation = index;
        return e;
    };
    reading.pages = {{entry(0)}, {entry(1), entry(2)}, {entry(0)}, {entry(3), entry(std::nullopt)}};
    AnnotationStore store = AnnotationStore::fromReading(reading);
    const auto ownerOf = [&store](AnnotationId id) { return store.at(*store.find(id)).page; };
    const QByteArray d10 = store.annotationDigest(*store.find(AnnotationId::ofObject(10)));

    SECTION("a move renumbers each owner and keeps every listing whole")
    {
        const PageMap map = *PageMap::move(4, {0}, 3);   // b c d a
        REQUIRE(store.applyPageMap(map));
        CHECK(ownerOf(AnnotationId::ofObject(10)) == 3);
        CHECK(ownerOf(AnnotationId::ofObject(11)) == 0);
        CHECK(ownerOf(AnnotationId::ofObject(13)) == 2);
        CHECK(store.listing(2).size() == 2);               // the old page 3, its non-dictionary entry with it
        CHECK_FALSE(store.listing(2)[1].annotation);
        // A direct dictionary keeps the NAME it was given at open (D6-4) -- names do not move.
        CHECK(store.find(AnnotationId::ofDirect(1, 1)));
        CHECK(store.annotationDigest(*store.find(AnnotationId::ofObject(10))) != d10);   // its page is in its digest
        REQUIRE(store.applyPageMap(map.inverse()));
        CHECK(ownerOf(AnnotationId::ofObject(10)) == 0);
        CHECK(store.annotationDigest(*store.find(AnnotationId::ofObject(10))) == d10);
    }
    SECTION("a removal: an annotation only the page listed leaves; one listed elsewhere changes owner; undo is exact")
    {
        const PageMap map = *PageMap::removal(4, {0, 3});
        std::vector<AnnotationStore::PageListing> removed = store.listingsRemovedBy(map);
        REQUIRE(removed.size() == 2);
        REQUIRE(store.applyPageMap(map));
        CHECK(store.size() == 3);                          // obj 13 went with page 3
        CHECK_FALSE(store.find(AnnotationId::ofObject(13)));
        CHECK(ownerOf(AnnotationId::ofObject(10)) == 1);   // page 2 lists it still: the first page that does
        REQUIRE(store.applyPageMap(map.inverse(), std::move(removed)));
        CHECK(store.size() == 4);
        CHECK(ownerOf(AnnotationId::ofObject(10)) == 0);   // given back to the page that came back
        CHECK(ownerOf(AnnotationId::ofObject(13)) == 3);
        CHECK(store.listing(3).size() == 2);
        CHECK(store.annotationDigest(*store.find(AnnotationId::ofObject(10))) == d10);
    }
    SECTION("an inserted listing adds the annotations it names and the inverse drops them")
    {
        const PageMap map = *PageMap::duplication(4, {1});
        AnnotationStore::PageListing copy;
        Annotation made = make(AnnotationId::ofCreated(QStringLiteral("copy-0")), 2);
        made.nm = QStringLiteral("copy-0");
        copy.entries.push_back({rawpage::core::AnnotationFact{true, made.subtype, std::nullopt}, made});
        REQUIRE(store.applyPageMap(map, {copy}));
        CHECK(store.size() == 5);
        CHECK(ownerOf(AnnotationId::ofCreated(QStringLiteral("copy-0"))) == 2);
        CHECK(ownerOf(AnnotationId::ofObject(13)) == 4);
        REQUIRE(store.applyPageMap(map.inverse()));
        CHECK(store.size() == 4);
        CHECK_FALSE(store.find(AnnotationId::ofCreated(QStringLiteral("copy-0"))));
    }
    SECTION("refused unchanged for a wrong count or a wrong number of listings")
    {
        CHECK_FALSE(store.applyPageMap(PageMap::identity(5)));
        CHECK_FALSE(store.applyPageMap(*PageMap::duplication(4, {0})));
        CHECK(store.pageCount() == 4);
        CHECK(store.size() == 4);
    }
}
