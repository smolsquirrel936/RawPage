// rawpage_fuzz_seeds -- write the writer's own outputs, as seeds for `fuzz_cos_roundtrip`. Stage 3 T13,
// decision D3-17 (`Stage3-Mutation-Plan.md` §3): "Seeds gain the writer's own outputs."
//
//     rawpage_fuzz_seeds <output-dir> <input file or directory>...
//
// Every `.pdf` under the inputs goes through `tests/fuzz/RoundtripFuzzBody.h` -- the SAME function the
// fuzz target calls -- and every one it composes a revision for is written to `<output-dir>` as
// `<dir>_<name>` with the parent directory folded into the name, so `ordinary/x.pdf` and
// `malformed/x.pdf` cannot collide.
//
// WHY SEEDS OF THIS KIND. Nothing in `corpus/` was written by RawPage: every file there came from a
// Python generator. A file that already carries a `/Prev` update or a complete section written by
// `pdf/cos/IncrementalWriter` is the shape the NEXT save meets, and the fuzzer mutating one reaches an
// update section on the first input instead of having to invent one.
//
// A HELPER, NOT A FUZZ TARGET: it links no libFuzzer and builds on every preset but `gates`, which is
// what lets its CTest case run on Windows and under `portable-asan`. It lives in a subdirectory of
// `tests/fuzz/` because `cmake/RawPageFuzzTargets.cmake` counts that directory's own targets only.
//
// EXIT CODE: 0 when at least one seed was written and no input broke an invariant; 1 when an input did
// (the same finding the fuzz target would abort on, named on stderr) or nothing was written -- zero
// seeds is a failure for the reason zero fuzz targets is; 2 on a usage or I/O error.

#include "tests/fuzz/RoundtripFuzzBody.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<fs::path> pdfsUnder(const fs::path& input)
{
    std::vector<fs::path> files;
    if (fs::is_regular_file(input)) {
        files.push_back(input);
    } else if (fs::is_directory(input)) {
        for (const auto& entry : fs::recursive_directory_iterator(input)) {
            if (entry.is_regular_file() && entry.path().extension() == ".pdf")
                files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: rawpage_fuzz_seeds <output-dir> <input file or directory>...\n");
        return 2;
    }
    const fs::path output(argv[1]);
    std::error_code error;
    fs::create_directories(output, error);
    if (error) {
        std::fprintf(stderr, "cannot create %s: %s\n", output.string().c_str(), error.message().c_str());
        return 2;
    }

    std::size_t examined = 0;
    std::size_t editable = 0;
    std::size_t written = 0;
    std::size_t complete = 0;
    std::size_t findings = 0;

    for (int index = 2; index < argc; ++index) {
        const fs::path input(argv[index]);
        if (!fs::exists(input)) {
            std::fprintf(stderr, "no such input: %s\n", input.string().c_str());
            return 2;
        }
        for (const fs::path& file : pdfsUnder(input)) {
            std::ifstream stream(file, std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            ++examined;

            const auto verdict = rawpage::fuzz::roundtripFuzzOnce(
                reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), true);
            if (!verdict.ok) {
                std::fprintf(stderr, "FINDING in %s: %s\n", file.string().c_str(), verdict.failure.c_str());
                ++findings;
                continue;
            }
            if (verdict.editable)
                ++editable;
            if (!verdict.written)
                continue;

            const fs::path seed = output / (file.parent_path().filename().string() + "_" + file.filename().string());
            std::ofstream out(seed, std::ios::binary | std::ios::trunc);
            out.write(verdict.output.constData(), static_cast<std::streamsize>(verdict.output.size()));
            out.close();
            if (!out) {
                std::fprintf(stderr, "cannot write %s\n", seed.string().c_str());
                return 2;
            }
            ++written;
            if (verdict.complete)
                ++complete;
        }
    }

    std::printf("roundtrip seeds: examined %zu, editable %zu, wrote %zu (%zu with a complete section), "
                "findings %zu -> %s\n",
                examined, editable, written, complete, findings, output.string().c_str());
    if (findings > 0)
        return 1;
    if (written == 0) {
        std::fprintf(stderr, "no seed was written -- the inputs reached no editable page tree\n");
        return 1;
    }
    return 0;
}
