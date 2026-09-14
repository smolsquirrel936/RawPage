// tst_pageopsengine -- the first-party page walk against the engine, page by page. Stage 3 T3, guide
// case A-29 (`Stage3-Mutation-Testing-Guide.md` §5.5).
//
// WHY IT EXISTS. A rotation is applied to the object `pdf/ops/PageOps` calls page n and drawn by the
// engine's own idea of page n. If the two disagree, the product saves a rotation onto a page nobody
// saw -- an S1. So over every corpus file BOTH readers open, this requires the same page count and,
// for every index, the same effective rotation and the same media box size: the properties that
// distinguish pages in this corpus, since no two readers share object numbers as an API.
//
// WHY IT IS IN tests/render. It needs PDFium, and `tests/unit` may not link it (D2-2). `tst_pageops`
// is the headless half, checked against PROVENANCE rather than against an engine.
//
// WHAT IT DOES NOT COVER: files only one reader opens (encrypted files the COS reader declines by
// design, D2-6; damaged files PDFium refuses) -- they are counted and printed, not compared -- and any
// file outside our generated corpus. Refusing to edit a document where the two disagree is
// `core/doc/Permissions` at T4 (D3-7); this case is the measurement that refusal rests on.

#include <catch2/catch_test_macros.hpp>

#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/PageOps.h"
#include "pdf/render/PdfiumDocument.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using rawpage::pdf::cos::openWithRepair;
using rawpage::pdf::ops::PageOps;
using rawpage::pdf::render::PdfiumDocument;

namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("page walk agrees with the engine on every corpus file both open", "[render][pageops]")
{
    std::size_t compared = 0;
    std::size_t pagesCompared = 0;
    std::string onlyOne;

    for (const char* directory : {"ordinary", "malformed", "signed"}) {
        for (const auto& entry :
             std::filesystem::directory_iterator(std::filesystem::path(RAWPAGE_CORPUS_DIR) / directory)) {
            if (entry.path().extension() != ".pdf")
                continue;
            const std::string name = std::string(directory) + "/" + entry.path().filename().string();
            INFO("file: " << name);

            auto engine = PdfiumDocument::open(QString::fromStdString(entry.path().string()),
                                               [](int) { return std::optional<QString>{}; });
            auto bytes = std::make_unique<std::string>(readFile(entry.path()));
            auto cos = openWithRepair(*bytes);
            auto walk = cos ? PageOps::walk(cos.value().table)
                            : rawpage::core::Result<rawpage::pdf::ops::PageWalk>(cos.error());
            if (!engine || !walk) {
                onlyOne += " " + name + (engine ? "(engine only)" : walk ? "(COS only)" : "(neither)");
                continue;
            }

            const auto& enginePages = engine.value()->pages();
            const auto& ourPages = walk.value().pages;
            INFO("engine pages " << engine.value()->pageCount() << ", walk pages " << ourPages.size());

            // THE ONE MEASURED DISAGREEMENT (Stage 3 guide issue I-07). `pages-loop.pdf` holds ONE
            // page object under a cycle; PDFium counts it twice, the walk once and notes the revisit.
            // Not copied into the walk -- page 1 and page 2 would then be one object, and rotating one
            // would rotate both -- and not skipped either: both counts are asserted EXACTLY, so this
            // case goes red the day PDFium reads the file differently. Editing it is refused at T4,
            // because the two readers disagree (D3-7).
            if (name == "malformed/pages-loop.pdf") {
                CHECK(engine.value()->pageCount() == 2);
                CHECK(ourPages.size() == 1);
                ++compared;
                continue;
            }
            REQUIRE(static_cast<std::size_t>(engine.value()->pageCount()) == ourPages.size());
            for (int index = 0; index < engine.value()->pageCount(); ++index) {
                INFO("page index " << index);
                const auto& ours = ourPages[static_cast<std::size_t>(index)];
                CHECK(static_cast<int>(enginePages.rotation(index)) == ours.effectiveRotate());
                if (ours.mediaBox) {
                    const auto& box = *ours.mediaBox;
                    const auto theirs = enginePages.pages()[static_cast<std::size_t>(index)].mediaBox();
                    CHECK(std::fabs(std::fabs(box[2] - box[0]) - theirs.size.width) < 0.01);
                    CHECK(std::fabs(std::fabs(box[3] - box[1]) - theirs.size.height) < 0.01);
                }
                ++pagesCompared;
            }
            ++compared;
        }
    }

    // Printed, so a case that compared nothing -- or quietly stopped comparing a directory -- is
    // visible in the log rather than inferred from a green tick.
    WARN("compared " << compared << " files, " << pagesCompared << " pages; opened by one reader or "
         "neither:" << onlyOne);
    CHECK(compared >= 50);
    CHECK(pagesCompared >= 1000);
}
