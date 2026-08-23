# tools/ribbon-validate -- decision D1-8's build-time validation of res/ribbon.json.
#
# NOT a gate, and not built for the gate configurations. The three gates link nothing but the
# standard library so that CI's `gates` job runs on a bare Linux runner with no Qt (D0-1); this
# needs QtCore, because it shares the product's own JSON parser rather than reimplementing one.
# Excluded explicitly rather than left to chance, the way tools/spike-ribbon was, because the
# root's add_subdirectory(tools) runs in every configuration.
if(RAWPAGE_GATES_ONLY OR RAWPAGE_PORTABLE_SUBSET)
    return()
endif()

add_executable(ribbon-validate
    main.cpp
    # COMPILED IN, not linked. rawpage_ui links Qt Widgets and this tool must not, and the
    # alternative -- a second parser here -- is the failure mode the header warns about: a
    # validator that certifies a file the runtime then reads differently. Same technique
    # tests/unit uses for app/CommandLine.cpp, and for the same reason.
    "${CMAKE_SOURCE_DIR}/ui/shell/RibbonModel.cpp"
)

target_include_directories(ribbon-validate PRIVATE "${CMAKE_SOURCE_DIR}")

# Through the wrapper, never target_link_libraries(... Qt6::X) directly -- D0-5. QtCore only:
# RibbonModel names no widget, which is what makes it shareable with a tool at all.
rawpage_link_qt(ribbon-validate Core)

# Strict, unlike the throwaway spike: this runs on every build of ui/ from here to Stage 16.
rawpage_strict(ribbon-validate)
