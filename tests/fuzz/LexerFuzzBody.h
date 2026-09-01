// The body of `fuzz_cos_lexer`, in a header so that TWO callers share exactly one copy of it.
//
// WHY THIS IS NOT SIMPLY INSIDE fuzz_cos_lexer.cpp. libFuzzer needs clang and a Linux runner
// (`portable-fuzz`); this is a MinGW GCC project on Windows, so the fuzz target is built by a
// preset that does not run here and does not run in the Windows CI job either. A fuzz entry point
// that only ever compiles somewhere else is one that rots between the day it is written and the day
// anybody reads its output -- and Stage 1's I-30 is the precedent for what that costs: a CI job
// that had never run, in a stage everybody believed was covered.
//
// So the invariants live here, and:
//
//   * `tests/fuzz/fuzz_cos_lexer.cpp` wraps them in `LLVMFuzzerTestOneInput` for `portable-fuzz`;
//   * `tests/unit/tst_cos_lexer.cpp` replays every file in `corpus/malformed/` and a table of
//     adversarial byte strings through the SAME function, on every Windows `ctest` run.
//
// The second is not a substitute for the first -- replaying 24 known files is not fuzzing, and it
// finds nothing new by construction. It is what keeps the entry point compiling, keeps the
// invariants honest, and makes a `portable-fuzz` finding reproducible in `x64-debug`, which
// CLAUDE.md's convention 11 corollary requires before it counts as fixed.
//
// WHY IT RETURNS A VERDICT RATHER THAN CALLING abort(). An abort is right for libFuzzer and wrong
// inside a Catch2 run, where it takes the whole suite down with no indication of which of 24 files
// did it. The caller decides: the fuzz target aborts, the test reports.

#ifndef RAWPAGE_TESTS_FUZZ_LEXERFUZZBODY_H
#define RAWPAGE_TESTS_FUZZ_LEXERFUZZBODY_H

#include "pdf/cos/Lexer.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace rawpage::fuzz {

struct LexerVerdict {
    bool ok = true;
    std::string failure;      // empty when ok; names the invariant and the position
    std::size_t tokens = 0;
    std::size_t notes = 0;
};

// Lex `size` bytes and check every invariant `pdf/cos/Lexer.h` promises. Never throws, never reads
// out of bounds, and always terminates -- the last of those is what invariant 1 below enforces
// rather than assumes, because a lexer that stops advancing turns a fuzz run into a hang, and a
// hang is the one failure a fuzzer reports badly.
inline LexerVerdict lexerFuzzOnce(const std::uint8_t* data, std::size_t size)
{
    using rawpage::pdf::cos::Lexer;
    using rawpage::pdf::cos::Token;
    using rawpage::pdf::cos::TokenType;

    LexerVerdict verdict;
    auto fail = [&verdict](std::string reason) {
        verdict.ok = false;
        verdict.failure = std::move(reason);
        return verdict;
    };

    Lexer lexer(data, size);
    std::size_t previous = lexer.position();
    std::size_t previousNotes = 0;

    for (;;) {
        // Invariant 4: peek() must agree with next() and must not move anything. Checked BEFORE
        // next() so a disagreement is attributed to the token it belongs to.
        const Token looked = lexer.peek();
        if (lexer.position() != previous)
            return fail("peek() moved the position at " + std::to_string(previous));
        if (lexer.noteCount() != previousNotes)
            return fail("peek() changed noteCount at " + std::to_string(previous));

        const Token token = lexer.next();
        ++verdict.tokens;

        if (token.type != looked.type || token.offset != looked.offset
            || token.length != looked.length)
            return fail("peek() disagreed with next() at " + std::to_string(previous));

        // Invariant 3: the span stays inside the buffer. A token whose offset+length runs past the
        // end is how a byte-copying rewriter reads someone else's memory in Stage 3.
        if (token.offset > size || token.length > size || token.offset + token.length > size)
            return fail("token span leaves the buffer at " + std::to_string(token.offset));

        // The decoded form is never longer than the bytes it came from. True of every construct:
        // `(a)` decodes to one byte from three, `<41>` to one from four, `/A#42` to two from five.
        // A decoded string longer than its source means an escape ran away.
        if (token.text.size() > token.length)
            return fail("decoded text longer than its source span at "
                        + std::to_string(token.offset));

        if (lexer.noteCount() < previousNotes)
            return fail("noteCount went backwards at " + std::to_string(token.offset));
        previousNotes = lexer.noteCount();

        if (token.type == TokenType::End) {
            // Invariant 2: End is only produced at the end, and it is stable there.
            if (token.offset != size || token.length != 0)
                return fail("End token is not at the end of the buffer");
            if (lexer.position() != size)
                return fail("position is not at the end after End");
            const Token again = lexer.next();
            if (again.type != TokenType::End)
                return fail("End was not stable on the next call");
            break;
        }

        // Invariant 1, and the load-bearing one: PROGRESS. Everything else here is a memory-safety
        // check that ASan would also catch; this is the one that only an explicit assertion finds,
        // because its failure mode is an infinite loop rather than a fault.
        if (lexer.position() <= previous)
            return fail("no progress at " + std::to_string(previous) + " (token "
                        + rawpage::pdf::cos::tokenTypeName(token.type) + ")");
        previous = lexer.position();
    }

    verdict.notes = lexer.noteCount();
    return verdict;
}

} // namespace rawpage::fuzz

#endif // RAWPAGE_TESTS_FUZZ_LEXERFUZZBODY_H
