# PdfiumAssert.cmake -- PA-1 .. PA-5, the assertions that keep the RASTERIZER pinned.
#
# WHY THIS FILE EXISTS (decision D2-3, Stage2-Rendering-Plan.md 3).
#
# A golden-image baseline is a claim about ONE rasterizer. Bump PDFium without noticing and every
# rendering regression is silently reclassified as a baseline update -- the exact laundering
# Implementation-Plan.md 2 forbids when it says baselines are regenerated "only through
# tools/golden-update with a stated reason". That mechanism has no teeth unless something notices the
# engine moved. This is TA-1's argument applied to the second pinned dependency: Stage 0 already
# decided that a version this project's correctness rests on is ASSERTED rather than assumed.
#
# It is deliberately shaped like ToolchainAssert.cmake, down to the diagnostic: found, expected, and
# the fix -- because the person reading it is by definition someone whose third_party/pdfium/ differs
# from the pin, most likely because they ran a newer fetch script or unpacked an archive by hand.
#
# WHERE THE PIN LIVES, AND WHY IT IS NOT IN third_party/. `third_party/pdfium/` is FETCHED and
# .gitignore'd -- only its README is committed -- so a pin stored there would be a pin the repository
# does not contain, and a mismatched fetch would agree with itself. The expected values are here, in
# a committed file; the found values are read out of the fetched tree. That asymmetry IS the check.
#
# WHAT IT DOES NOT DO: it does not verify the archive's SHA-256. scripts/fetch-pdfium.ps1 does that
# at download time, and re-hashing a 12 MB DLL on every configure would buy little -- the failure
# this file is built for is a DIFFERENT VERSION correctly fetched, not a corrupted one.

include_guard(GLOBAL)

set(RAWPAGE_PDFIUM_EXPECTED_VERSION "153.0.8009.0" CACHE STRING
    "The pinned PDFium version (D2-3). Changing this is a baseline-review decision, not a build tweak.")

function(_rawpage_pdfium_error label found expected)
    # ARGN joined rather than a named `fix` parameter, for the reason ToolchainAssert's own helper
    # records as issue I-09: CMake does not concatenate adjacent quoted literals the way C does, so a
    # named parameter keeps the first fragment and drops the actionable rest into ARGN.
    set(_parts ${ARGN})
    list(JOIN _parts "" fix)

    message(FATAL_ERROR
        "\n"
        "PDFium pin violated: ${label}\n"
        "  found:    ${found}\n"
        "  expected: ${expected}\n"
        "\n"
        "${fix}\n"
        "\n"
        "The rasterizer is pinned because tests/golden/ compares rendered pixels against committed "
        "baselines, and those baselines are a claim about ONE engine build. An unpinned upgrade does "
        "not fail here -- it fails as a golden suite that goes red everywhere at once and gets "
        "\"fixed\" by regenerating the baselines, which is how a real rendering regression ships "
        "(D2-3, Stage2-Rendering-Plan.md 3).\n")
endfunction()

# `rawpage_assert_pdfium(<dir>)` -- reads the fetched tree at <dir> and sets, in the caller's scope:
#
#   RAWPAGE_PDFIUM_VERSION   the verified version string, for the --version line and the log
#
# Every failure below is FATAL. There is deliberately no "warn and continue": a build that has
# already decided to link the engine cannot meaningfully proceed against an unknown one.
function(rawpage_assert_pdfium pdfium_dir)
    set(expected "${RAWPAGE_PDFIUM_EXPECTED_VERSION}")

    # --- PA-1 - the fetched tree exists ------------------------------------------------
    # Named separately from the version check because the fix is completely different, and because
    # this is the failure a fresh clone hits: third_party/pdfium/ holds nothing but a README until
    # somebody runs the fetch script.
    if(NOT EXISTS "${pdfium_dir}/VERSION")
        _rawpage_pdfium_error("PA-1 PDFium is not fetched"
            "no ${pdfium_dir}/VERSION" "a populated third_party/pdfium/"
            "Run  .\\scripts\\fetch-pdfium.ps1  -- it downloads the pinned archive, verifies its "
            "SHA-256, and generates the GNU import library that upstream does not ship. Nothing "
            "under third_party/pdfium/ is committed except its README (see .gitignore), so a fresh "
            "clone always needs this step once.")
    endif()

    # --- PA-2 - the version file parses ------------------------------------------------
    # The upstream file is four KEY=VALUE lines (MAJOR/MINOR/BUILD/PATCH), not a version string, so
    # this reassembles it rather than reading one. A parse that quietly produced ".." and compared it
    # against the pin would fail PA-3 with a diagnostic naming a nonsense "found" value, which is the
    # kind of message that sends its reader after the wrong problem.
    file(READ "${pdfium_dir}/VERSION" _version_text)
    set(_parts "")
    foreach(key MAJOR MINOR BUILD PATCH)
        if(NOT _version_text MATCHES "${key}=([0-9]+)")
            _rawpage_pdfium_error("PA-2 PDFium VERSION is not parseable"
                "no ${key}= line in ${pdfium_dir}/VERSION" "MAJOR=, MINOR=, BUILD= and PATCH= lines"
                "This file comes from the upstream archive verbatim. If it has been edited by hand, "
                "re-run  .\\scripts\\fetch-pdfium.ps1  to restore the fetched tree.")
        endif()
        list(APPEND _parts "${CMAKE_MATCH_1}")
    endforeach()
    list(JOIN _parts "." found)

    # --- PA-3 - the version IS the pin -------------------------------------------------
    # The diagnostic names BOTH versions on purpose (guide case A-05's control): "PDFium is the wrong
    # version" is not something a reader can act on, and the two numbers are the whole of what they
    # need to decide whether to re-fetch or to change the pin deliberately.
    if(NOT found STREQUAL expected)
        _rawpage_pdfium_error("PA-3 PDFium version" "${found}" "${expected}"
            "Either re-fetch the pinned build with  .\\scripts\\fetch-pdfium.ps1  , or -- if the "
            "upgrade is deliberate -- change RAWPAGE_PDFIUM_EXPECTED_VERSION in "
            "cmake/PdfiumAssert.cmake, the SHA-256 and version in scripts/fetch-pdfium.ps1 and "
            "third_party/pdfium/README.md, and the version in tools/license-audit/spdx-map.json, "
            "IN THE SAME CHANGE -- and then regenerate every golden baseline through "
            "tools/golden-update with a stated reason, reviewing the diffs. That review is the "
            "point of the pin.")
    endif()

    # --- PA-4 - V8 and XFA are off -----------------------------------------------------
    # Read out of the archive's own args.gn rather than trusted to the filename. The -v8- variant
    # adds ~9 MB and a JavaScript engine we never call, and XFA adds a second forms implementation
    # with its own parser -- both are attack surface for entry points this product does not use, and
    # both change rendering behaviour, which makes them a baseline question as well as a size one.
    if(NOT EXISTS "${pdfium_dir}/args.gn")
        _rawpage_pdfium_error("PA-4 PDFium build arguments are missing"
            "no ${pdfium_dir}/args.gn" "the args.gn shipped in the pinned archive"
            "Re-fetch with  .\\scripts\\fetch-pdfium.ps1  . Without this file the build "
            "configuration of the engine is unknown, and \"probably the non-V8 one\" is not "
            "something a licence audit or a golden baseline can rest on.")
    endif()
    file(READ "${pdfium_dir}/args.gn" _args_text)
    foreach(flag pdf_enable_v8 pdf_enable_xfa)
        if(NOT _args_text MATCHES "${flag}[ \t]*=[ \t]*(true|false)")
            _rawpage_pdfium_error("PA-4 ${flag} is not stated"
                "no ${flag} line in args.gn" "${flag} = false"
                "The pinned archive states both flags explicitly. An absent flag means a different "
                "build than the one this project measured.")
        endif()
        if(NOT CMAKE_MATCH_1 STREQUAL "false")
            _rawpage_pdfium_error("PA-4 ${flag}" "true" "false"
                "This is the wrong archive: fetch the plain pdfium-win-x64.tgz, not the -v8- "
                "variant. RawPage uses PDFium for read and raster only, so a JavaScript engine or a "
                "second forms implementation is pure attack surface behind an entry point we never "
                "call.")
        endif()
    endforeach()

    # --- PA-5 - the DLL and the GNU import library are both present ---------------------
    # Two files, two failure modes. Upstream ships an MSVC-format pdfium.dll.lib that GNU ld cannot
    # read; scripts/fetch-pdfium.ps1 generates libpdfium.dll.a with gendef + dlltool. A tree fetched
    # by unpacking the archive by hand has the DLL and not the import library, and the resulting
    # error is a wall of undefined FPDF_ references rather than a sentence about what is missing.
    foreach(pair "bin/pdfium.dll@the engine itself" "lib/libpdfium.dll.a@the GNU import library")
        string(REPLACE "@" ";" _pair "${pair}")
        list(GET _pair 0 _rel)
        list(GET _pair 1 _what)
        if(NOT EXISTS "${pdfium_dir}/${_rel}")
            _rawpage_pdfium_error("PA-5 ${_what} is missing" "no ${pdfium_dir}/${_rel}" "${_rel}"
                "Run  .\\scripts\\fetch-pdfium.ps1  . It fetches the DLL and then runs gendef and "
                "dlltool from the pinned binutils to produce libpdfium.dll.a, because upstream ships "
                "only an MSVC-format import library that GNU ld cannot read.")
        endif()
    endforeach()

    set(RAWPAGE_PDFIUM_VERSION "${found}" PARENT_SCOPE)
    message(STATUS "PDFium pin verified: ${found}, V8 off, XFA off")
endfunction()
