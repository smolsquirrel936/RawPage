// tst_objectallocator -- guide case A-23 (`Stage3-Mutation-Testing-Guide.md` §5.4). Stage 3 T2.
//
// WHAT THIS SUITE IS TRYING TO CATCH: an allocator that hands out a number the file already uses,
// or one off the free list. Decision D3-5 (`Stage3-Mutation-Plan.md` §3) is that an incremental
// update never reuses a number -- see `pdf/cos/ObjectAllocator.h` for why.
//
// WHAT IT DOES NOT COVER: that the WRITER uses the allocator for every number it adds. That is
// `tst_incrementalwriter`'s cross-reference-stream cases, which check the stream's own number.

#include <catch2/catch_test_macros.hpp>

#include "pdf/cos/ObjectAllocator.h"
#include "pdf/cos/XrefTable.h"

#include <filesystem>
#include <fstream>
#include <string>

using rawpage::pdf::cos::ObjectAllocator;
using rawpage::pdf::cos::XrefEntryType;
using rawpage::pdf::cos::XrefTable;

namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::filesystem::path corpus(std::string_view relative)
{
    return std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative;
}

} // namespace

TEST_CASE("object allocator starts at the file's own Size", "[cos][allocator]")
{
    const std::string file = readFile(corpus("ordinary/text-a4.pdf"));
    auto loaded = XrefTable::load(file);
    REQUIRE(loaded);
    const XrefTable& table = loaded.value();
    REQUIRE(table.declaredSize() == table.largestObjectNumber() + 1);

    ObjectAllocator allocator = ObjectAllocator::forTable(table);
    CHECK(allocator.firstNumber() == table.declaredSize());
    const long long first = allocator.allocate();
    const long long second = allocator.allocate();
    CHECK(first == table.declaredSize());
    CHECK(second == first + 1);
    CHECK(allocator.nextNumber() == second + 1);
    CHECK(table.find(first) == nullptr);
}

TEST_CASE("object allocator does not trust a Size smaller than the objects present", "[cos][allocator]")
{
    // `corpus/malformed/size-too-small.pdf` says `/Size 4` over a dozen objects. Trusting it would
    // hand out object 4, which the file already uses -- and the update would shadow a live object.
    const std::string file = readFile(corpus("malformed/size-too-small.pdf"));
    auto loaded = XrefTable::load(file);
    REQUIRE(loaded);
    const XrefTable& table = loaded.value();
    REQUIRE(table.declaredSize() == 4);
    REQUIRE(table.largestObjectNumber() > 4);

    ObjectAllocator allocator = ObjectAllocator::forTable(table);
    const long long number = allocator.allocate();
    CHECK(number == table.largestObjectNumber() + 1);
    CHECK(table.find(number) == nullptr);
}

TEST_CASE("object allocator never reuses a free-list entry below Size", "[cos][allocator]")
{
    // GUIDE CASE A-23. A table whose free list holds objects 2 and 3, below `/Size 6`. ISO 32000-1
    // 7.5.4 would allow reusing either with its generation incremented; D3-5 says no. Hand-built
    // because no corpus file has a real free entry below its `/Size` -- `free-list-broken.pdf`'s
    // head points at a LIVE object, which is a different fault. `load()` checks no object offset,
    // so the in-use entries need not point anywhere real for this case.
    const std::string file =
        "%PDF-1.7\n"
        "xref\n"
        "0 6\n"
        "0000000002 65535 f\r\n"
        "0000000100 00000 n\r\n"
        "0000000003 00001 f\r\n"
        "0000000000 00001 f\r\n"
        "0000000200 00000 n\r\n"
        "0000000300 00000 n\r\n"
        "trailer\n<< /Size 6 /Root 1 0 R >>\n"
        "startxref\n9\n%%EOF\n";
    auto loaded = XrefTable::load(file);
    REQUIRE(loaded);
    const XrefTable& table = loaded.value();

    // The fixture carries its fault (convention 22): two genuinely free entries below /Size.
    REQUIRE(table.find(2) != nullptr);
    REQUIRE(table.find(2)->type == XrefEntryType::Free);
    REQUIRE(table.find(3) != nullptr);
    REQUIRE(table.find(3)->type == XrefEntryType::Free);

    ObjectAllocator allocator = ObjectAllocator::forTable(table);
    for (int round = 0; round < 5; ++round) {
        const long long number = allocator.allocate();
        INFO("allocation " << round << " gave " << number);
        CHECK(number >= 6);
        CHECK(number != 2);
        CHECK(number != 3);
    }
}

TEST_CASE("object allocator never allocates object zero", "[cos][allocator]")
{
    // Object 0 is the free-list head in every PDF. A table with nothing in it must still start at 1.
    ObjectAllocator allocator(0);
    CHECK(allocator.allocate() == 1);
    ObjectAllocator negative(-5);
    CHECK(negative.allocate() == 1);
}
