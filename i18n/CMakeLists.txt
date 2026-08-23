# i18n/ -- the message catalogs, compiled.
#
# WHY THE .qm FILES SIT BESIDE THE EXECUTABLE RATHER THAN INSIDE IT.
#
# A Qt resource would be one line shorter and would make a customer-supplied catalog a rebuild.
# Enterprise deployments genuinely ask to correct a term in their own copy, and Stage 15 ships
# these as replaceable files for the same reason it ships the Qt DLLs replaceable -- so the layout
# is settled here, at the first release that has any strings at all, rather than discovered during
# packaging.
#
# lrelease is a BUILD TOOL, not a linked dependency: it is run, never linked, so it does not appear
# in qt-modules.txt and tools/license-audit does not see it. That is correct -- the audit's subject
# is the link closure of what we ship. (`LinguistTools` is nevertheless already in both
# RAWPAGE_QT_ALLOWED_MODULES and license-audit's own list, from Stage 0.)
#
# NOTE ON `unfinished` ENTRIES, measured at T9 rather than assumed: lrelease KEEPS a message marked
# type="unfinished" as long as it has text, so the 28 provisional zh-TW terms (decision D1-14,
# reviewed at manual case M-06) do reach the running product and are what a reviewer sees. Only an
# EMPTY translation is dropped. app/Application installs rawpage_en.qm underneath every other
# locale so that a dropped or missing entry renders as English rather than as a raw catalog key --
# which is a smaller set than expected, and still the right arrangement for the day a key is added
# to `en` and not yet to zh-TW.

set(RAWPAGE_QM_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/i18n")

set(RAWPAGE_CATALOGS rawpage_en rawpage_zh_TW)

set(qm_files "")
foreach(catalog IN LISTS RAWPAGE_CATALOGS)
    set(ts "${CMAKE_CURRENT_SOURCE_DIR}/${catalog}.ts")
    set(qm "${RAWPAGE_QM_DIR}/${catalog}.qm")
    add_custom_command(
        OUTPUT "${qm}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RAWPAGE_QM_DIR}"
        COMMAND Qt6::lrelease "${ts}" -qm "${qm}"
        DEPENDS "${ts}"
        COMMENT "lrelease ${catalog}.ts"
        VERBATIM)
    list(APPEND qm_files "${qm}")
endforeach()

# ALL, so that an ordinary `cmake --build` produces them. A translation that only appears when
# somebody remembers to build a side target is a translation that is missing in CI.
add_custom_target(rawpage_translations ALL DEPENDS ${qm_files})

# The .ts files themselves are listed as sources of the custom target's own dummy so they appear in
# an IDE project tree; they are data, not code (CLAUDE.md), and nothing compiles them.
set_property(TARGET rawpage_translations PROPERTY SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/rawpage_en.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/rawpage_zh_TW.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/locales.json)
