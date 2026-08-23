# The `checks` target -- all live enforcement gates, in one command.
#
#     cmake --build --preset x64-release --target checks
#
# THE ORDERING GUARANTEE: this target depends ONLY on the gate executables, never on the
# layer libraries. The gates link nothing but the C++ standard library (D0-1), so `checks`
# compiles two small programs and runs them. That is what lets CI run it BEFORE compiling
# anything (Tech-Stack-Plan.md 9) and have a layering violation fail in seconds rather than
# after a full Qt build. Adding a dependency on rawpage_core here would silently destroy that
# property while leaving every test passing.

include_guard(GLOBAL)

function(rawpage_add_checks_target)
    set(gate_commands)

    list(APPEND gate_commands
        COMMAND $<TARGET_FILE:layer-lint> --root "${CMAKE_SOURCE_DIR}")

    # qt-modules.txt and compiler-plugins.txt are written at configure time -- the first
    # from the real Qt link closure (D0-5), the second by TA-9. Passing them here is what
    # turns "a build that links Qt6::Charts" into a build that fails.
    set(license_args
        --manifest "${CMAKE_SOURCE_DIR}/vcpkg.json"
        --spdx-map "${CMAKE_SOURCE_DIR}/tools/license-audit/spdx-map.json"
        --emit "${CMAKE_SOURCE_DIR}/THIRD-PARTY.md")

    if(EXISTS "${CMAKE_BINARY_DIR}/qt-modules.txt")
        list(APPEND license_args --qt-modules "${CMAKE_BINARY_DIR}/qt-modules.txt")
    endif()
    if(EXISTS "${CMAKE_BINARY_DIR}/compiler-plugins.txt")
        list(APPEND license_args --compiler-plugins "${CMAKE_BINARY_DIR}/compiler-plugins.txt")
    endif()

    # An installed vcpkg tree supplies exact versions from vcpkg's own SBOM. Optional:
    # the CI gates job has no vcpkg at all and must still enforce the licence rule.
    file(GLOB installed_dirs "${CMAKE_BINARY_DIR}/vcpkg_installed/*")
    foreach(dir IN LISTS installed_dirs)
        if(IS_DIRECTORY "${dir}" AND NOT dir MATCHES "/vcpkg$")
            list(APPEND license_args --vcpkg-installed "${dir}")
            break()
        endif()
    endforeach()

    list(APPEND gate_commands COMMAND $<TARGET_FILE:license-audit> ${license_args})

    # i18n-lint (Stage 1 T6). All three inputs are passed EXPLICITLY, which is what makes an
    # absent one exit 2 rather than a skipped rule: a fixture tree legitimately has no catalogs,
    # and the real tree never does. A gate that could not run must not look like one that passed.
    list(APPEND gate_commands
        COMMAND $<TARGET_FILE:i18n-lint> --root "${CMAKE_SOURCE_DIR}"
                --rules "${CMAKE_SOURCE_DIR}/tools/i18n-lint/rules.json"
                --locales "${CMAKE_SOURCE_DIR}/i18n/locales.json"
                --actions "${CMAKE_SOURCE_DIR}/core/cmd/ActionId.def")

    add_custom_target(checks
        ${gate_commands}
        DEPENDS layer-lint license-audit i18n-lint
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Running enforcement gates (layer-lint, license-audit, i18n-lint)"
        VERBATIM)

    # mutation-lint -> Stage 3 (needs Command). THREE of the four gates exist, and `checks` runs
    # the three that do. If this target ever claims to have run four, that is the bug.
endfunction()
