// tst_cos_filters -- guide cases A-18 and A-19. Stage 2 T7.
//
// WHAT THIS SUITE IS TRYING TO CATCH, in the order the cases appear:
//
//   1. The LOSSLESS round-trip, driven off `isLossless()` rather than off a list this file keeps
//      in its own head -- so adding a filter and forgetting to test it is a compile-visible gap
//      rather than an omission nobody notices.
//   2. The CORPUS decodes, and these are EXACT rather than structural wherever the generator's
//      bitmap can be reproduced here. `corpus/make-ordinary.py` builds its images from closed-form
//      expressions, so this file recomputes the same bytes and compares all of them. A length
//      check would pass against a decoder with the predictor wired backwards; a byte check does
//      not.
//   3. The two CCITT files, which `corpus/PROVENANCE.md` says are the SAME bitmap through two
//      completely different decoder paths -- so they must decode to identical bytes. That is the
//      strongest assertion available for a codec whose expected output nobody wants to transcribe,
//      and it needs no hand-written expectation at all.
//   4. A-19: nine filters, and `CryptFilter` deliberately absent.
//
// WHAT THIS SUITE DOES NOT COVER, named at the assertion (CLAUDE.md convention 19):
//
//   * **`/JBIG2Decode` decodes nothing** -- it is declared and refuses by name. See
//     `pdf/cos/filters/Jbig2Filter.h` and issue **I-06**: there is no permissible library
//     (jbig2dec is AGPL-3.0, which D4 forbids) and no corpus fixture. The case below asserts the
//     REFUSAL, which is a real property, and does not pretend to test a decoder.
//   * **`/CCITTFaxDecode` with `K > 0`** (mixed one- and two-dimensional Group 3) is implemented
//     and has NO corpus fixture. Asserted here only for "does not crash on nonsense"; the honest
//     fix is a `K > 0` corpus file, recorded as part of I-06.
//   * **DCT and JPX are lossy**, so guide case A-18's `encode`→`decode` identity is not a property
//     they have and is not asserted for them. `isLossless()` is where that distinction lives, and
//     the round-trip case reads it rather than skipping those two by name.

#include <catch2/catch_test_macros.hpp>

#include "pdf/cos/Parser.h"
#include "pdf/cos/filters/Ascii85Filter.h"
#include "pdf/cos/filters/AsciiHexFilter.h"
#include "pdf/cos/filters/CcittFilter.h"
#include "pdf/cos/filters/DctFilter.h"
#include "pdf/cos/filters/Filter.h"
#include "pdf/cos/filters/FlateFilter.h"
#include "pdf/cos/filters/Jbig2Filter.h"
#include "pdf/cos/filters/JpxFilter.h"
#include "pdf/cos/filters/LZWFilter.h"
#include "pdf/cos/filters/RunLengthFilter.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using rawpage::pdf::cos::Bytes;
using rawpage::pdf::cos::CosObject;
using rawpage::pdf::cos::decodeFilter;
using rawpage::pdf::cos::decodeParmsFrom;
using rawpage::pdf::cos::decodeStream;
using rawpage::pdf::cos::DecodeParms;
using rawpage::pdf::cos::encodeFilter;
using rawpage::pdf::cos::filterKindFromName;
using rawpage::pdf::cos::FilterKind;
using rawpage::pdf::cos::filterName;
using rawpage::pdf::cos::isLossless;
using rawpage::pdf::cos::Parser;
using rawpage::pdf::cos::undoPredictor;

namespace {

const FilterKind kAllFilters[] = {
    FilterKind::AsciiHex, FilterKind::Ascii85, FilterKind::LZW,  FilterKind::Flate,
    FilterKind::RunLength, FilterKind::Ccitt,  FilterKind::Dct,  FilterKind::Jpx,
    FilterKind::Jbig2,
};

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());
}

std::filesystem::path corpus(std::string_view relative)
{
    return std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative;
}

std::string_view view(const Bytes& bytes)
{
    return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// The one image XObject in a corpus file whose `/Subtype` is `/Image`. Every image fixture has
// exactly one, so "the first one found" is unambiguous rather than lucky.
CosObject findImage(const std::string& file, int wanted = 0)
{
    Parser parser(file);
    int seen = 0;
    for (std::size_t at = file.find(" obj"); at != std::string::npos;
         at = file.find(" obj", at + 4)) {
        std::size_t cursor = at;
        while (cursor > 0 && std::isdigit(static_cast<unsigned char>(file[cursor - 1])))
            --cursor;
        if (cursor == 0 || file[cursor - 1] != ' ')
            continue;
        --cursor;
        while (cursor > 0 && std::isdigit(static_cast<unsigned char>(file[cursor - 1])))
            --cursor;

        auto result = parser.parseIndirectObjectAt(cursor);
        if (!result)
            continue;
        const CosObject& object = result.value().object;
        if (object.isStream() && object.get("Subtype").text() == "Image") {
            if (seen == wanted)
                return object;
            ++seen;
        }
    }
    return CosObject::makeNull();
}

} // namespace

// ==========================================================================================
// 1. The lossless round-trip -- guide case A-18's first half
// ==========================================================================================

TEST_CASE("cos filters round-trip every lossless filter", "[cos][filters]")
{
    // Driven off `isLossless()` rather than off a list kept here, so a tenth filter cannot be
    // added without this case either covering it or failing to compile. The payloads are chosen to
    // exercise the shapes each filter has a branch for: runs, incompressible noise, and the
    // partial final group ASCII85 pads.
    const std::vector<std::string> payloads = {
        "",
        "A",
        "AB",
        "ABC",
        "ABCD",
        "ABCDE",
        std::string(1000, 'x'),                       // one enormous run
        std::string("\0\0\0\0\0\0\0\0", 8),           // ASCII85's `z` shorthand
        "the quick brown fox jumps over the lazy dog, twice over and then again",
    };

    // Deterministic pseudo-noise: incompressible enough to push LZW past its 9-bit boundary, which
    // is where the EarlyChange off-by-one lives.
    std::string noise;
    noise.reserve(9000);
    std::uint32_t state = 12345;
    for (int index = 0; index < 9000; ++index) {
        state = state * 1103515245u + 12345u;
        noise.push_back(static_cast<char>((state >> 16) & 0x3F));
    }

    for (const FilterKind kind : kAllFilters) {
        if (!isLossless(kind))
            continue;
        INFO("filter: " << filterName(kind));

        for (const std::string& payload : payloads) {
            INFO("payload: " << payload.size() << " bytes");
            const auto encoded = encodeFilter(kind, payload, DecodeParms{});
            REQUIRE(encoded);
            const auto decoded = decodeFilter(kind, view(encoded.value()), DecodeParms{});
            REQUIRE(decoded);
            REQUIRE(view(decoded.value()) == payload);
        }

        INFO("payload: 9000 bytes of noise");
        const auto encoded = encodeFilter(kind, noise, DecodeParms{});
        REQUIRE(encoded);
        const auto decoded = decodeFilter(kind, view(encoded.value()), DecodeParms{});
        REQUIRE(decoded);
        REQUIRE(view(decoded.value()) == noise);
    }
}

TEST_CASE("cos filters refuse to encode the four that are not reversible", "[cos][filters]")
{
    // The other half of the `isLossless()` split, and it is a real assertion rather than a
    // formality: an encoder that returned an empty buffer would let a caller believe it had
    // compressed something. DCT and JPX are lossy BY CONSTRUCTION, so A-18's round-trip is not a
    // property they have; CCITT and JBIG2 are decode-only this stage.
    for (const FilterKind kind : kAllFilters) {
        if (isLossless(kind))
            continue;
        INFO("filter: " << filterName(kind));
        const auto encoded = encodeFilter(kind, "anything", DecodeParms{});
        REQUIRE_FALSE(encoded);
        CHECK(encoded.error().code == rawpage::core::ErrorCode::Unsupported);
    }
}

// ==========================================================================================
// 2. The individual tolerances
// ==========================================================================================

TEST_CASE("cos filters pad an odd hex digit count rather than failing", "[cos][filters]")
{
    // ISO 32000-1 7.4.2, and the same rule `pdf/cos/Lexer` applies to `<41A>` one layer down.
    const auto decoded = rawpage::pdf::cos::filters::asciiHexDecode("41A>");
    REQUIRE(decoded);
    REQUIRE(decoded.value().size() == 2);
    CHECK(decoded.value()[0] == 0x41);
    CHECK(decoded.value()[1] == 0xA0);

    // Whitespace anywhere, and a stray byte skipped the way the lexer skips one.
    const auto spaced = rawpage::pdf::cos::filters::asciiHexDecode("4 1\n4 2\t43>");
    REQUIRE(spaced);
    CHECK(view(spaced.value()) == "ABC");
}

TEST_CASE("cos filters handle the ascii85 partial group and its z shorthand", "[cos][filters]")
{
    // The partial final group is padded with 'u' and yields n-1 bytes. Getting this wrong corrupts
    // the tail of every stream whose length is not a multiple of four, which is most of them.
    const auto one = rawpage::pdf::cos::filters::ascii85Decode("/c~>");
    REQUIRE(one);
    CHECK(one.value().size() == 1);

    const auto zeros = rawpage::pdf::cos::filters::ascii85Decode("z~>");
    REQUIRE(zeros);
    CHECK(zeros.value().size() == 4);
    CHECK(zeros.value()[0] == 0);

    // `z` inside a partial group is malformed: accepting it would silently shift everything after.
    CHECK_FALSE(rawpage::pdf::cos::filters::ascii85Decode("!z~>"));

    // The leading `<~` is Adobe's PostScript convention rather than ISO 32000-1's, and producers
    // emit it. Treating it as data would corrupt the first four bytes.
    const auto adobe = rawpage::pdf::cos::filters::ascii85Decode("<~87cURD]~>");
    REQUIRE(adobe);
    CHECK(view(adobe.value()) == "Hello");
}

TEST_CASE("cos filters undo the PNG and TIFF predictors", "[cos][filters]")
{
    // The PNG predictor prefixes every row with a filter TYPE byte, so the encoded row is one byte
    // longer than the decoded one. Getting that wrong shifts every row after the first -- which
    // produces a recognisable image that is subtly wrong, not a failure.
    DecodeParms parms;
    parms.predictor = 12;          // PNG Up
    parms.colors = 1;
    parms.bitsPerComponent = 8;
    parms.columns = 4;

    // Two rows: `2` is the Up filter, so row two's bytes are deltas against row one.
    const Bytes encoded = {2, 10, 20, 30, 40,
                           2, 1,  1,  1,  1};
    const auto decoded = undoPredictor(encoded, parms);
    REQUIRE(decoded);
    REQUIRE(decoded.value().size() == 8);
    CHECK(decoded.value()[4] == 11);
    CHECK(decoded.value()[7] == 41);

    // TIFF horizontal differencing, which is a different rule entirely: no per-row type byte, and
    // the delta is against the pixel to the LEFT.
    DecodeParms tiff;
    tiff.predictor = 2;
    tiff.colors = 1;
    tiff.bitsPerComponent = 8;
    tiff.columns = 4;
    const Bytes tiffEncoded = {10, 1, 1, 1};
    const auto tiffDecoded = undoPredictor(tiffEncoded, tiff);
    REQUIRE(tiffDecoded);
    REQUIRE(tiffDecoded.value().size() == 4);
    CHECK(tiffDecoded.value()[3] == 13);

    // The control: predictor 1 is the identity, so a case that passed for every predictor value
    // would be proving nothing about the two above.
    DecodeParms none;
    const auto untouched = undoPredictor(encoded, none);
    REQUIRE(untouched);
    CHECK(untouched.value() == encoded);
}

TEST_CASE("cos filters keep what a truncated flate stream produced", "[cos][filters]")
{
    // `corpus/malformed/truncated-mid-stream.pdf` is an interrupted download, which is the most
    // common damaged file a reader is handed. The page that decodes from the first 90 % of a
    // content stream is worth more than nothing.
    const std::string payload(4000, 'q');
    const auto encoded = rawpage::pdf::cos::filters::flateEncode(payload);
    REQUIRE(encoded);
    REQUIRE(encoded.value().size() > 8);

    const std::string_view whole = view(encoded.value());
    const auto partial =
        rawpage::pdf::cos::filters::flateDecode(whole.substr(0, whole.size() - 4));
    REQUIRE(partial);
    CHECK(partial.value().size() > 0);
    CHECK(partial.value().size() <= payload.size());

    // The control: the complete stream decodes to all of it, so the assertion above is about
    // truncation rather than about a decoder that always stops early.
    const auto complete = rawpage::pdf::cos::filters::flateDecode(whole);
    REQUIRE(complete);
    CHECK(view(complete.value()) == payload);
}

// ==========================================================================================
// 3. The corpus, decoded EXACTLY
// ==========================================================================================

TEST_CASE("cos filters decode the corpus filter chain to the exact source bitmap",
          "[cos][filters][corpus]")
{
    // `corpus/ordinary/image-lzw-ascii85.pdf` carries `/Filter [/ASCII85Decode /LZWDecode]`, which
    // is the case a reader handling one filter per stream gets wrong while passing every
    // single-filter fixture in the corpus.
    //
    // The expected bytes are RECOMPUTED here from `corpus/make-ordinary.py`'s closed-form
    // expression rather than checked for length. A length check passes against an LZW decoder
    // whose EarlyChange boundary is off by one; a byte check does not, and that boundary is the
    // only hard part of LZW.
    const std::string file = readFile(corpus("ordinary/image-lzw-ascii85.pdf"));
    REQUIRE_FALSE(file.empty());

    const CosObject chained = findImage(file, 0);
    REQUIRE(chained.isStream());
    REQUIRE(chained.get("Width").integerValue() == 80);
    REQUIRE(chained.get("Height").integerValue() == 60);

    Bytes expected;
    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 80; ++x) {
            expected.push_back(static_cast<std::uint8_t>(std::min(255, x * 3)));
            expected.push_back(static_cast<std::uint8_t>(std::min(255, y * 4)));
            expected.push_back(static_cast<std::uint8_t>(128));
        }
    }

    const auto decoded = decodeStream(chained, file);
    REQUIRE(decoded);
    CHECK(decoded.value().size() == expected.size());
    CHECK(decoded.value() == expected);

    // The second image in the same file, through `/RunLengthDecode`.
    const CosObject rle = findImage(file, 1);
    REQUIRE(rle.isStream());
    Bytes expectedRle;
    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 80; ++x) {
            const bool band = ((y / 10) % 2) != 0;
            expectedRle.push_back(static_cast<std::uint8_t>(band ? 230 : 30));
            expectedRle.push_back(static_cast<std::uint8_t>(band ? 80 : 60));
            expectedRle.push_back(static_cast<std::uint8_t>(band ? 40 : 160));
        }
    }
    const auto decodedRle = decodeStream(rle, file);
    REQUIRE(decodedRle);
    CHECK(decodedRle.value() == expectedRle);
}

TEST_CASE("cos filters decode the two CCITT files to the same bitmap", "[cos][filters][corpus]")
{
    // `corpus/PROVENANCE.md`: the G3 and G4 files are the SAME bitmap through two completely
    // different decoder paths -- one-dimensional Modified Huffman against two-dimensional coding
    // relative to the row above. So they must decode to identical bytes, which is the strongest
    // assertion available for a codec whose expected output nobody wants to transcribe, and it
    // needs no hand-written expectation at all.
    const std::string g4File = readFile(corpus("ordinary/image-ccitt-g4.pdf"));
    const std::string g3File = readFile(corpus("ordinary/image-ccitt-g3.pdf"));
    REQUIRE_FALSE(g4File.empty());
    REQUIRE_FALSE(g3File.empty());

    const CosObject g4 = findImage(g4File);
    const CosObject g3 = findImage(g3File);
    REQUIRE(g4.isStream());
    REQUIRE(g3.isStream());
    REQUIRE(g4.get("Width").integerValue() == 128);
    REQUIRE(g4.get("Height").integerValue() == 120);

    // The two coding modes really are different -- if the fixtures had the same `/K` this case
    // would be comparing a decoder with itself.
    const DecodeParms g4Parms = decodeParmsFrom(g4.get("DecodeParms"));
    const DecodeParms g3Parms = decodeParmsFrom(g3.get("DecodeParms"));
    REQUIRE(g4Parms.k < 0);
    REQUIRE(g3Parms.k == 0);

    const auto g4Bits = decodeStream(g4, g4File);
    const auto g3Bits = decodeStream(g3, g3File);
    REQUIRE(g4Bits);
    REQUIRE(g3Bits);

    // 128 pixels at one bit each is 16 bytes per row, padded to a byte boundary; 120 rows.
    CHECK(g4Bits.value().size() == 16u * 120u);
    CHECK(g3Bits.value().size() == 16u * 120u);
    CHECK(g4Bits.value() == g3Bits.value());

    // AND THE EXACT BITMAP, recomputed from `corpus/make-ordinary.py`'s `_fax_bitmap`, which is a
    // closed-form expression. The equality above is a strong cross-check and it has one blind
    // spot: `/BlackIs1` is a POLARITY, and inverting it inverts BOTH files identically -- so two
    // decoders that agreed with each other would still both be producing a photographic negative,
    // which reads as a colour-space bug and is not one. Only an absolute comparison catches that.
    //
    // Both fixtures set `/BlackIs1 true`, so an ink pixel is a 1 bit, MSB first.
    REQUIRE(g4Parms.blackIs1);
    REQUIRE(g3Parms.blackIs1);

    Bytes expected(16u * 120u, 0);
    for (int y = 0; y < 120; ++y) {
        for (int x = 0; x < 128; ++x) {
            const bool rule = (y == 6 || y == 7 || y == 120 - 8 || y == 120 - 7);
            const bool block = (x >= 18 && x < 90 && y >= 20 && y < 34);
            const bool speckle = (y >= 40 && y < 96 && ((x / 3 + y * 2) % 11) < 2);
            const bool longRun = (y >= 100 && y < 108);
            if (!(rule || block || speckle || longRun))
                continue;
            const std::size_t index = static_cast<std::size_t>(y) * 16u
                                      + static_cast<std::size_t>(x) / 8u;
            expected[index] = static_cast<std::uint8_t>(expected[index]
                                                        | (0x80u >> (x % 8)));
        }
    }
    CHECK(g4Bits.value() == expected);

    // And it is not all one colour, which is what a decoder that gave up on row one would produce
    // and which an equality against a mistakenly all-zero expectation would happily accept.
    const std::set<std::uint8_t> distinct(g4Bits.value().begin(), g4Bits.value().end());
    CHECK(distinct.size() > 1);
}

TEST_CASE("cos filters decode the flate predictor image to the exact source bitmap",
          "[cos][filters][corpus]")
{
    // `/FlateDecode` with a PNG Up predictor. A decoder that ignores `/DecodeParms` produces an
    // image that is wrong in a specific way -- each row offset from the one above -- rather than
    // failing, which is exactly why the predictor needs its own fixture and an exact comparison.
    const std::string file = readFile(corpus("ordinary/image-flate-predictor.pdf"));
    REQUIRE_FALSE(file.empty());

    const CosObject image = findImage(file);
    REQUIRE(image.isStream());
    const long long width = image.get("Width").integerValue();
    const long long height = image.get("Height").integerValue();
    REQUIRE(width == 120);
    REQUIRE(height == 90);

    const DecodeParms parms = decodeParmsFrom(image.get("DecodeParms"));
    REQUIRE(parms.predictor == 12);
    REQUIRE(parms.colors == 3);
    REQUIRE(parms.columns == 120);

    // The expected bytes are RECOMPUTED from `corpus/make-ordinary.py`'s `_photo_samples`, which
    // is a closed-form expression, so this is an EXACT comparison rather than a statistical one.
    // The first attempt here measured smoothness along a row instead, and its control was
    // backwards: a PNG **Up** predictor leaves VERTICAL residuals, so the un-predicted data is
    // smooth along a row and the properly decoded photograph is not. Measuring the wrong axis
    // would have passed against a decoder that applied the predictor in the wrong direction.
    Bytes expected;
    expected.reserve(static_cast<std::size_t>(width * height * 3));
    for (long long y = 0; y < height; ++y) {
        for (long long x = 0; x < width; ++x) {
            int red = static_cast<int>(std::min<long long>(255, x * 255 / std::max<long long>(width - 1, 1)));
            int green = static_cast<int>(std::min<long long>(255, y * 255 / std::max<long long>(height - 1, 1)));
            int blue = (((x / 8) + (y / 8)) % 2) != 0 ? 220 : 60;
            if (x > width * 2 / 3) {
                red = 250;
                green = 250;
                blue = 250;
            }
            expected.push_back(static_cast<std::uint8_t>(red));
            expected.push_back(static_cast<std::uint8_t>(green));
            expected.push_back(static_cast<std::uint8_t>(blue));
        }
    }

    const auto decoded = decodeStream(image, file);
    REQUIRE(decoded);
    REQUIRE(decoded.value().size() == expected.size());
    CHECK(decoded.value() == expected);

    // THE CONTROL. The same stream decoded WITHOUT undoing the predictor must NOT match -- without
    // it, the comparison above would pass just as well against a file that never had a predictor.
    auto rawFlate = rawpage::pdf::cos::filters::flateDecode(image.rawStreamData(file));
    REQUIRE(rawFlate);
    CHECK(rawFlate.value() != expected);
    // And it is longer by exactly one filter-type byte per row, which is the detail that shifts
    // every row after the first when a reader forgets it.
    CHECK(rawFlate.value().size() == expected.size() + static_cast<std::size_t>(height));
}

TEST_CASE("cos filters decode the DCT images at their declared size", "[cos][filters][corpus]")
{
    // JPEG is lossy, so there is no exact expectation to compare against -- which is precisely why
    // A-18's round-trip is not asserted for it. What IS checkable: the header agrees with the
    // image dictionary, and the decoded buffer is exactly width × height × components.
    for (const char* name : {"ordinary/image-dct-rgb.pdf", "ordinary/image-dct-gray.pdf"}) {
        INFO("file: " << name);
        const std::string file = readFile(corpus(name));
        REQUIRE_FALSE(file.empty());

        const CosObject image = findImage(file);
        REQUIRE(image.isStream());
        const long long width = image.get("Width").integerValue();
        const long long height = image.get("Height").integerValue();
        REQUIRE(width > 0);
        REQUIRE(height > 0);

        const auto info = rawpage::pdf::cos::filters::dctInspect(image.rawStreamData(file));
        REQUIRE(info);
        CHECK(info.value().width == static_cast<int>(width));
        CHECK(info.value().height == static_cast<int>(height));

        const auto decoded = decodeStream(image, file);
        REQUIRE(decoded);
        CHECK(decoded.value().size()
              == static_cast<std::size_t>(width * height * info.value().components));
    }
}

TEST_CASE("cos filters reach OpenJPEG for the JPX image", "[cos][filters][corpus]")
{
    // `corpus/PROVENANCE.md` and `corpus/jpxenc.py`: the codestream carries the EBCOT **cleanup
    // pass only**, so it is TRUNCATED rather than malformed and a 0/255 source decodes to roughly
    // 0/128. This case therefore asserts the shape and the tone COUNT -- never the tone values,
    // because asserting those would be asserting the corpus encoder's truncation as if it were
    // JPEG 2000 fidelity. It proves this filter reaches OpenJPEG with the right container and gets
    // samples of the right size back, and it makes no claim beyond that.
    const std::string file = readFile(corpus("ordinary/image-jpx.pdf"));
    REQUIRE_FALSE(file.empty());

    const CosObject image = findImage(file);
    REQUIRE(image.isStream());
    const long long width = image.get("Width").integerValue();
    const long long height = image.get("Height").integerValue();

    rawpage::pdf::cos::filters::JpxInfo info;
    const auto decoded = rawpage::pdf::cos::filters::jpxDecode(image.rawStreamData(file), info);
    REQUIRE(decoded);
    CHECK(info.width == static_cast<int>(width));
    CHECK(info.height == static_cast<int>(height));
    CHECK(decoded.value().size()
          == static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
                 * static_cast<std::size_t>(info.components));

    const std::set<std::uint8_t> tones(decoded.value().begin(), decoded.value().end());
    CHECK(tones.size() >= 2);
}

// ==========================================================================================
// 4. What is deliberately absent -- guide case A-19, and I-06
// ==========================================================================================

TEST_CASE("cos filters ship nine kinds and CryptFilter is not one of them", "[cos][filters]")
{
    // GUIDE CASE A-19, asserted over the FILTER LIST rather than by counting files in the
    // directory: `Filter.{h,cpp}` is the registry and the shared predictor, not a tenth filter, so
    // a file count would have to be told to ignore it and would then ignore a real tenth filter too.
    CHECK(std::size(kAllFilters) == 9);

    for (const FilterKind kind : kAllFilters) {
        INFO("filter: " << filterName(kind));
        CHECK(std::string(filterName(kind)) != "?");
        CHECK(filterKindFromName(filterName(kind)) == kind);
    }

    // `/Crypt` is a REAL filter that this stage does not have (D2-4): it needs the
    // `StandardSecurityHandler` whose key it decrypts with, which is Stage 10's.
    CHECK_FALSE(filterKindFromName("Crypt"));
    CHECK_FALSE(std::filesystem::exists(
        std::filesystem::path(RAWPAGE_FILTERS_DIR) / "CryptFilter.cpp"));
    CHECK_FALSE(std::filesystem::exists(
        std::filesystem::path(RAWPAGE_FILTERS_DIR) / "CryptFilter.h"));

    // Each of the nine has its own translation unit, which is what the FileMap row promises.
    for (const char* base : {"AsciiHexFilter", "Ascii85Filter", "LZWFilter", "FlateFilter",
                             "RunLengthFilter", "CcittFilter", "DctFilter", "JpxFilter",
                             "Jbig2Filter"}) {
        INFO("file: " << base);
        CHECK(std::filesystem::exists(std::filesystem::path(RAWPAGE_FILTERS_DIR)
                                      / (std::string(base) + ".cpp")));
    }

    // The abbreviations from ISO 32000-1 table 6 are legal in any stream, not only inline images.
    CHECK(filterKindFromName("Fl") == FilterKind::Flate);
    CHECK(filterKindFromName("AHx") == FilterKind::AsciiHex);
    CHECK(filterKindFromName("CCF") == FilterKind::Ccitt);
    CHECK_FALSE(filterKindFromName("NotAFilter"));
}

TEST_CASE("cos filters refuse JBIG2 by name rather than returning a blank image",
          "[cos][filters]")
{
    // ISSUE I-06, asserted rather than described. `/JBIG2Decode` is DECLARED and decodes nothing:
    // there is no permissible library (jbig2dec is AGPL-3.0, which `Tech-Stack-Plan.md` D4 forbids
    // outright) and no corpus fixture to test a first-party decoder against. What this case
    // protects is the REFUSAL -- an empty buffer would reach the image pipeline as a blank page,
    // indistinguishable from a page that is genuinely blank, which is convention 6's argument
    // about silent stubs one layer down from `platform/`.
    const auto refused = rawpage::pdf::cos::filters::jbig2Decode("anything", {});
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == rawpage::core::ErrorCode::Unsupported);

    // A key of its OWN, not the generic unsupported-filter one: a reader seeing this in a log
    // should learn "this build cannot read JBIG2", not "this file names a filter nobody knows".
    CHECK(refused.error().messageKey == QLatin1String("error.pdf.filterJbig2"));

    // And it is reached through the ordinary chain, not only by calling the function directly --
    // otherwise a registry that never dispatched to it would pass this case.
    CosObject stream = CosObject::makeStream(
        std::vector<CosObject::Entry>{{"Filter", CosObject::makeName("JBIG2Decode")}},
        rawpage::pdf::cos::CosStreamData{});
    const auto viaChain = decodeStream(stream, "");
    REQUIRE_FALSE(viaChain);
    CHECK(viaChain.error().messageKey == QLatin1String("error.pdf.filterJbig2"));
}

TEST_CASE("cos filters name an unknown filter and Crypt differently", "[cos][filters]")
{
    // Two different repairs, so two different keys. "Unknown filter" sends a reader looking for a
    // typo; `/Crypt` sends them to Stage 10, which is where the security handler lands.
    CosObject crypt = CosObject::makeStream(
        std::vector<CosObject::Entry>{{"Filter", CosObject::makeName("Crypt")}},
        rawpage::pdf::cos::CosStreamData{});
    const auto cryptResult = decodeStream(crypt, "");
    REQUIRE_FALSE(cryptResult);
    CHECK(cryptResult.error().messageKey == QLatin1String("error.pdf.filterCrypt"));

    CosObject unknown = CosObject::makeStream(
        std::vector<CosObject::Entry>{{"Filter", CosObject::makeName("MadeUpDecode")}},
        rawpage::pdf::cos::CosStreamData{});
    const auto unknownResult = decodeStream(unknown, "");
    REQUIRE_FALSE(unknownResult);
    CHECK(unknownResult.error().messageKey == QLatin1String("error.pdf.filterUnsupported"));
}

TEST_CASE("cos filters survive nonsense without crashing", "[cos][filters]")
{
    // Not fuzzing -- that is `fuzz_cos_parser`'s neighbour work and these filters have no target of
    // their own yet. This is the bounded version: every filter, over inputs designed to reach its
    // guards, asserting only that it returns rather than what it returns.
    const std::vector<std::string> inputs = {
        "", " ", "~>", ">", "z", "\x80", "\xff\xff\xff\xff",
        std::string(64, '\0'), std::string(64, '\xff'),
        "\x78\x9c", "\x78\x9c\xff\xff\xff",
    };
    for (const FilterKind kind : kAllFilters) {
        for (const std::string& input : inputs) {
            INFO("filter: " << filterName(kind) << " input: " << input.size() << " bytes");
            DecodeParms parms;
            parms.columnsCcitt = 64;
            parms.rows = 4;
            const auto result = decodeFilter(kind, input, parms);
            // Either outcome is fine; NOT crashing and NOT hanging is the assertion, and a
            // `CHECK` that reads the result is what keeps the call from being optimized away.
            CHECK((result.hasValue() || !result.hasValue()));
        }
    }

    // `/K > 0` -- mixed one- and two-dimensional Group 3 -- is implemented and has NO corpus
    // fixture (I-06). It is exercised here only for "does not crash on nonsense", and this comment
    // is the convention-19 note that says the coverage stops there.
    DecodeParms mixed;
    mixed.k = 4;
    mixed.columnsCcitt = 64;
    mixed.rows = 4;
    const auto result = rawpage::pdf::cos::filters::ccittDecode(std::string(32, '\x55'), mixed);
    CHECK((result.hasValue() || !result.hasValue()));
}
