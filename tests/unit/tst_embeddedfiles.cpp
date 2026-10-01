// tst_embeddedfiles -- `pdf/ops/EmbeddedFileReader`, the first-party read of each embedded file's name and
// DESCRIPTION. Stage 5 T8, decision D5-12; the headless half of guide case A-76's attachment row
// (`Stage5-TextSearch-Testing-Guide.md` §5.10).
//
// THE ORACLE IS THE GENERATOR (rule 7): `corpus/make-text.py`'s `search_classes()` embeds `lemur.pdf` (no
// description) and `ocelot.txt` (description `narwhal`), and a name tree lists them in key order.
//
// WHAT IT DOES NOT COVER (convention 19): that the ENGINE enumerates the attachments in the same order, so
// the two lists join -- `tests/render/tst_searchclasses.cpp` finds `narwhal` through the shipped backend,
// which is where the join is exercised; and the search itself.

#include <catch2/catch_test_macros.hpp>

#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/EmbeddedFileReader.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using rawpage::pdf::cos::openWithRepair;
using rawpage::pdf::cos::XrefTable;
using rawpage::pdf::ops::EmbeddedFileReader;

namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

struct Opened {
    std::unique_ptr<std::string> bytes;
    XrefTable table;
};

Opened openBytes(std::string bytes)
{
    auto owned = std::make_unique<std::string>(std::move(bytes));
    auto opened = openWithRepair(*owned);
    REQUIRE(opened);
    return Opened{std::move(owned), std::move(opened.value().table)};
}

Opened openCorpus(std::string_view relative)
{
    return openBytes(readFile(std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative));
}

} // namespace

TEST_CASE("the embedded files of search-classes are read with their names and descriptions in tree order",
          "[embeddedfiles][A-76]")
{
    Opened file = openCorpus("ordinary/search-classes.pdf");
    const auto entries = EmbeddedFileReader::read(file.table);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == QStringLiteral("lemur.pdf"));
    CHECK(entries[0].description.isEmpty());
    CHECK(entries[1].name == QStringLiteral("ocelot.txt"));
    CHECK(entries[1].description == QStringLiteral("narwhal"));

    // THE CONTROL: a file with no /EmbeddedFiles reads empty, which is the answer for almost every document.
    Opened plain = openCorpus("ordinary/hyphenated.pdf");
    CHECK(EmbeddedFileReader::read(plain.table).empty());
}

TEST_CASE("an embedded file tree whose kids loop ends", "[embeddedfiles]")
{
    // A HOSTILE TREE: object 3 is its own kid, twice. The reader must end, not hang -- the bound on depth
    // and on nodes (the header). No xref: the repair reconstructs the table, as it does for any such file.
    const std::string pdf =
        "%PDF-1.7\n"
        "1 0 obj << /Type /Catalog /Pages 2 0 R /Names << /EmbeddedFiles 3 0 R >> >> endobj\n"
        "2 0 obj << /Type /Pages /Kids [] /Count 0 >> endobj\n"
        "3 0 obj << /Kids [3 0 R 3 0 R] /Names [(a.txt) << /Type /Filespec /F (a.txt) /Desc (loop) >>] >> endobj\n"
        "trailer << /Root 1 0 R >>\n%%EOF\n";
    Opened file = openBytes(pdf);
    const auto entries = EmbeddedFileReader::read(file.table);
    // Every visit reads the leaf's one entry, and the visits are bounded.
    REQUIRE_FALSE(entries.empty());
    CHECK(entries.size() <= 20000);
    CHECK(entries.front().description == QStringLiteral("loop"));
}

TEST_CASE("a PDF text string decodes by its byte order mark and otherwise as PDFDocEncoding", "[embeddedfiles]")
{
    // UTF-16BE behind FE FF -- what the generator writes, `pb.utf16`.
    CHECK(EmbeddedFileReader::decodeTextString(std::string("\xFE\xFF\x00\x6E\x00\xE9", 6)) == QStringLiteral("né"));
    // UTF-8 behind EF BB BF (PDF 2.0).
    CHECK(EmbeddedFileReader::decodeTextString(std::string("\xEF\xBB\xBF" "caf\xC3\xA9")) == QStringLiteral("café"));
    // PDFDocEncoding is NOT Latin-1 where the table says otherwise: 0x80 is a bullet, 0x93 the fi ligature,
    // 0x18 a breve, 0xA0 the euro sign; an undefined code is U+FFFD, never guessed.
    CHECK(EmbeddedFileReader::decodeTextString(std::string("\x80\x93\x18\xA0", 4)) == QStringLiteral("•ﬁ˘€"));
    CHECK(EmbeddedFileReader::decodeTextString(std::string("\x9F\xAD", 2)) == QStringLiteral("��"));
    // ... and IS Latin-1 elsewhere: 0xE9 is e-acute.
    CHECK(EmbeddedFileReader::decodeTextString(std::string("caf\xE9")) == QStringLiteral("café"));
}
