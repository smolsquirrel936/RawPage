# THROWAWAY -- the A3 go/no-go spike. Deleted at Stage 1 T10, together with this file.
#
# NOT built for the gate configurations. tools/ is otherwise the one directory that builds on
# a bare Linux runner with no Qt at all (D0-1), and that property is what makes the CI `gates`
# job possible. This target is the single exception, so it is excluded explicitly rather than
# left to chance -- the root's add_subdirectory(tools) runs in every configuration.
if(RAWPAGE_GATES_ONLY OR RAWPAGE_PORTABLE_SUBSET)
    return()
endif()

add_executable(spike-ribbon
    main.cpp
    SpikeRibbon.h
    SpikeRibbon.cpp
)

# Q_OBJECT in SpikeRibbon.h needs moc. Enabled per target rather than globally: ui/ turns
# AUTOMOC on for the whole project at T9, and doing it here would make that change invisible.
set_target_properties(spike-ribbon PROPERTIES AUTOMOC ON)

target_include_directories(spike-ribbon PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# Through the wrapper, never target_link_libraries(... Qt6::X) directly -- D0-5. A spike that
# links Qt outside the wrapper is a hole in tools/license-audit's evidence, throwaway or not.
rawpage_link_qt(spike-ribbon Widgets Gui Core)

rawpage_common_flags(spike-ribbon)     # deliberately NOT rawpage_strict: it is throwaway
