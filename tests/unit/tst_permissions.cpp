// tst_permissions -- the pre-flight: which documents this build may edit. Stage 3 T4, guide case
// A-36's planted half (`Stage3-Mutation-Testing-Guide.md` §5.6), decision D3-7.
//
// WHAT THIS SUITE IS TRYING TO CATCH: an edit allowed where the two readers disagree about which page
// is which, a refusal without a name, and -- the control -- an agreement refused.
//
// WHAT IT DOES NOT COVER (convention 19): the REAL readings. Every disagreement here is planted into
// a hand-built page tree and fact list; that the shipped backend feeds `assess()` PDFium's tree and
// `pdf/ops/PageOps`' walk -- and so refuses the encrypted corpus and `malformed/pages-loop.pdf` -- is
// `tests/render/tst_backendedit.cpp`.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/PageTree.h"
#include "core/doc/Permissions.h"

#include <set>
#include <string>

using namespace rawpage::core;

namespace {

PageTree treeOf(int pages, int rotation = 0)
{
    std::vector<Page> list;
    for (int index = 0; index < pages; ++index) {
        Page page(index, PdfRect{PdfPoint{0.0, 0.0}, PdfSize{595.276, 841.89}});
        page.setRotation(rotation);
        list.push_back(page);
    }
    return PageTree(std::move(list));
}

std::vector<CosPageFacts> factsOf(int pages, int rotation = 0)
{
    return std::vector<CosPageFacts>(static_cast<std::size_t>(pages),
                                     CosPageFacts{rotation, PdfSize{595.276, 841.89}});
}

std::string keyOf(const Permissions& permissions)
{
    return std::string(permissions.refusalKey().data(),
                       static_cast<std::size_t>(permissions.refusalKey().size()));
}

} // namespace

TEST_CASE("permissions allow editing when both readers agree", "[doc][permissions]")
{
    // THE CONTROL, first: without it, every refusal below is equally satisfied by a predicate that
    // refuses everything.
    const Permissions permissions = Permissions::assess(treeOf(3, 90), CosReading::Read, factsOf(3, 90));
    CHECK(permissions.canEditPages());
    CHECK(permissions.may(Operation::EditPages));
}

TEST_CASE("permissions refuse by name where this build cannot write", "[doc][permissions]")
{
    // GUIDE CASE A-36, planted. Each refusal its own key; all PermissionDenied.
    struct Planted {
        const char* description;
        Permissions permissions;
        const char* key;
    };
    std::vector<CosPageFacts> rotated = factsOf(3);
    rotated[1].rotation = 90;
    std::vector<CosPageFacts> resized = factsOf(3);
    resized[2].drawnSize = PdfSize{612.0, 792.0};

    const std::vector<Planted> planted = {
        {"encrypted", Permissions::assess(treeOf(3), CosReading::Encrypted, {}), "error.edit.encrypted"},
        {"no page map", Permissions::assess(treeOf(3), CosReading::NoPageMap, {}), "error.edit.noPageMap"},
        {"page count", Permissions::assess(treeOf(2), CosReading::Read, factsOf(1)),
         "error.edit.pageCountDisagrees"},
        {"rotation", Permissions::assess(treeOf(3), CosReading::Read, rotated),
         "error.edit.pageRotationDisagrees"},
        {"size", Permissions::assess(treeOf(3), CosReading::Read, resized), "error.edit.pageSizeDisagrees"},
    };

    std::set<std::string> keys;
    for (const Planted& item : planted) {
        INFO("planted: " << item.description);
        CHECK_FALSE(item.permissions.canEditPages());
        CHECK(keyOf(item.permissions) == item.key);
        auto refusal = item.permissions.may(Operation::EditPages);
        REQUIRE_FALSE(refusal);
        CHECK(refusal.error().code == ErrorCode::PermissionDenied);
        CHECK(std::string(refusal.error().messageKey.data()) == item.key);
        keys.insert(item.key);
    }
    CHECK(keys.size() == planted.size());
}

TEST_CASE("permissions name the page a disagreement is on", "[doc][permissions]")
{
    std::vector<CosPageFacts> rotated = factsOf(5);
    rotated[3].rotation = 180;
    const Permissions permissions = Permissions::assess(treeOf(5), CosReading::Read, rotated);
    CHECK(permissions.refusalContext() == QStringLiteral("4"));   // 1-based, as a person counts

    const Permissions counted = Permissions::assess(treeOf(2), CosReading::Read, factsOf(1));
    CHECK(counted.refusalContext() == QStringLiteral("2/1"));
}

TEST_CASE("permissions do not compare a size the file never stated", "[doc][permissions]")
{
    // A page with no /MediaBox anywhere above it gets the ENGINE's default size, which the file never
    // stated; holding the first-party reading to it would refuse a legal file.
    std::vector<CosPageFacts> unstated = factsOf(2);
    unstated[0].drawnSize.reset();
    CHECK(Permissions::assess(treeOf(2), CosReading::Read, unstated).canEditPages());

    // And the tolerance is a tolerance, not a free pass: 0.005 agrees, 0.5 does not.
    std::vector<CosPageFacts> nearly = factsOf(1);
    nearly[0].drawnSize->width += 0.005;
    CHECK(Permissions::assess(treeOf(1), CosReading::Read, nearly).canEditPages());
    nearly[0].drawnSize->width += 0.5;
    CHECK_FALSE(Permissions::assess(treeOf(1), CosReading::Read, nearly).canEditPages());
}

TEST_CASE("permissions refuse until assessed", "[doc][permissions]")
{
    const Permissions unassessed;
    CHECK_FALSE(unassessed.canEditPages());
    auto refusal = unassessed.may(Operation::EditPages);
    REQUIRE_FALSE(refusal);
    CHECK(std::string(refusal.error().messageKey.data()) == "error.edit.notAssessed");
}

// --- Stage 6 T2 (D6-3): annotations, compared entry by entry --------------------------------------
// Guide case A-18's arithmetic half. Its engine half plants a missing ENTRY, which a comparison that
// counted and looked at nothing else would also catch; these hold each per-entry fact on its own --
// dictionary-ness, subtype, rectangle beyond the tolerance -- and the tolerance itself.

namespace {

AnnotationFact square(double x0, double y0, double x1, double y1)
{
    return AnnotationFact{true, AnnotationSubtype::Square, normalizedRect(x0, y0, x1, y1)};
}

} // namespace

TEST_CASE("permissions refuse annotations on the page whose entries differ in any compared fact", "[doc][permissions][A-18]")
{
    const std::vector<std::vector<AnnotationFact>> engine{{square(0, 0, 100, 50)}, {square(10, 10, 20, 20)}};
    const Permissions editable = Permissions::assess(treeOf(2), CosReading::Read, factsOf(2));

    // THE CONTROL: the same facts agree, and a difference inside the engine's float tolerance agrees.
    Permissions same = editable;
    same.assessAnnotations(engine, engine);
    CHECK(same.annotationDisagreements().empty());
    auto close = engine;
    close[1][0] = square(10.000004, 10, 20, 20.000004);
    same.assessAnnotations(engine, close);
    CHECK(same.annotationDisagreements().empty());

    struct Plant { const char* what; AnnotationFact fact; };
    for (const Plant& plant : {Plant{"subtype", AnnotationFact{true, AnnotationSubtype::Circle, normalizedRect(10, 10, 20, 20)}},
                               Plant{"rectangle", square(10, 10, 20, 20.5)},
                               Plant{"no rectangle", AnnotationFact{true, AnnotationSubtype::Square, std::nullopt}},
                               Plant{"not a dictionary", AnnotationFact{}}}) {
        auto cos = engine;
        cos[1][0] = plant.fact;
        Permissions permissions = editable;
        permissions.assessAnnotations(engine, cos);
        INFO(plant.what);
        CHECK(permissions.annotationDisagreements() == std::vector<int>{1});
        CHECK(permissions.mayAnnotate(0));
        const auto refused = permissions.mayAnnotate(1);
        REQUIRE_FALSE(refused);
        CHECK(std::string(refused.error().messageKey.data()) == "error.edit.annotationsDisagree");
        CHECK(refused.error().context == QStringLiteral("2"));
    }

    // Two entries that are both not dictionaries agree -- PDFium counts them too, and draws neither.
    Permissions bothEmpty = editable;
    bothEmpty.assessAnnotations({{AnnotationFact{}}}, {{AnnotationFact{}}});
    CHECK(bothEmpty.annotationDisagreements().empty());
}

TEST_CASE("permissions refuse every annotation where the document is refused", "[doc][permissions][A-18]")
{
    const Permissions encrypted = Permissions::assess(treeOf(1), CosReading::Encrypted, {});
    CHECK_FALSE(encrypted.may(Operation::Annotate));
    const auto refused = encrypted.mayAnnotate(0);
    REQUIRE_FALSE(refused);
    CHECK(std::string(refused.error().messageKey.data()) == "error.edit.encrypted");
}
