# res/ -- data, not code (CLAUDE.md). Nothing links it; the running program loads it by path.
#
# TWO THINGS HAPPEN HERE, and the order between them is the whole point of decision D1-8:
#
#   1. tools/ribbon-validate checks res/ribbon.json against core/cmd/ActionId.def. An unknown
#      action id, a duplicate placement or an empty group FAILS THE BUILD, naming the id and the
#      line. Not a warning, and not a missing button -- the instant a typo yields a silently
#      thinner ribbon, the next person writes widget code and the declarative ribbon becomes dead
#      weight that still has to be maintained (guide case A-51).
#
#   2. Only then is the file copied beside the executable, next to i18n/. Same reasoning as the .qm
#      catalogs at T6: data Stage 15 may want to ship replaceable lives outside the binary. The
#      validation is what makes that safe -- the copied file is a proven one -- and ui/shell's
#      runtime parse is the SAME parser the validator ran, so a hand-edited copy fails with the same
#      diagnostics rather than with a quietly different ribbon.
#
# The validation is NOT a fourth gate. `checks` runs three and says three; this is a build step.

set(RAWPAGE_RES_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/res")
set(ribbon_source "${CMAKE_CURRENT_SOURCE_DIR}/ribbon.json")
set(ribbon_output "${RAWPAGE_RES_DIR}/ribbon.json")

add_custom_command(
    OUTPUT "${ribbon_output}"
    COMMAND ribbon-validate "${ribbon_source}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RAWPAGE_RES_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ribbon_source}" "${ribbon_output}"
    # ActionId.def is a real dependency, not a decorative one: adding a row there can turn a
    # previously-unknown id into a known one, and removing one does the reverse. Without this line a
    # resource that stopped being valid would keep its stale stamp and the build would stay green.
    DEPENDS "${ribbon_source}" "${CMAKE_SOURCE_DIR}/core/cmd/ActionId.def" ribbon-validate
    COMMENT "ribbon-validate res/ribbon.json"
    VERBATIM)

# ALL, for the reason rawpage_translations gives: a resource that only appears when somebody
# remembers to build a side target is a resource that is missing in CI.
add_custom_target(rawpage_resources ALL DEPENDS "${ribbon_output}")

set_property(TARGET rawpage_resources PROPERTY SOURCES "${ribbon_source}")
