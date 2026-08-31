# app/ -- the composition root.
#
# The ONLY place that names a concrete backend or a concrete platform implementation.
# It creates every concrete, injects interfaces downward, and holds no domain logic.
# That is why it is the one target linking all four layers: nobody else may.

add_executable(rawpage
    main.cpp
    CrashHandler.h
    CrashHandler.cpp

    # T8 -- the composition root proper. Services.h is an INTERFACE header: it names only core/
    # types and platform::Platform, and tools/layer-lint declares it as its own layer entry (the
    # same treatment platform/platform.h gets, for the same reason) so that ui/ may include it
    # without ui/ acquiring app/.
    Services.h
    CommandLine.h
    CommandLine.cpp
    CompositionRoot.h
    CompositionRoot.cpp
    Application.h
    Application.cpp
    SingleInstance.h
    SingleInstance.cpp

    # T14 -- the rolling log file. It lives here for the reason core/Log.h states in its own
    # header: core/ may not write files, so the sink is app/'s. It carries two of the seven live
    # layer-lint filesystem-write waivers, both named and both visible in --explain.
    LogFile.h
    LogFile.cpp
)

# Application is a QObject (QApplication subclass), so this target needs moc. Set per target
# rather than globally: core/ deliberately contains no QObject, and a project-wide AUTOMOC would
# run a scan over it that must always find nothing -- a check that cannot fail.
set_target_properties(rawpage PROPERTIES AUTOMOC ON)

# The catalogs are loaded from <application dir>/i18n at startup, so a build that produced the
# executable and not the catalogs is a build whose UI renders catalog keys. Make that impossible
# rather than documented.
add_dependencies(rawpage rawpage_translations)

target_link_libraries(rawpage PRIVATE
    rawpage_core
    rawpage_cos
    rawpage_platform
    rawpage_ui
)

# Named explicitly rather than relying on the MinGW default spec, because the default is a
# property of the driver rather than of this project: shell32 for SHGetFolderPathW (the crash
# directory, resolved before Qt exists) and user32 for the per-monitor-v2 DPI calls.
# dbghelp is deliberately ABSENT -- CrashHandler loads it lazily, so a process that never
# crashes never maps a megabyte of it.
target_link_libraries(rawpage PRIVATE shell32 user32)

# The version reaches --version through the compiler rather than through a generated header:
# one define beats a configure_file whose only content is one string.
target_compile_definitions(rawpage PRIVATE RAWPAGE_VERSION="${PROJECT_VERSION}")

rawpage_app_flags(rawpage)            # -mwindows; -municode stays off -- D1-3, measured

set_target_properties(rawpage PROPERTIES OUTPUT_NAME "rawpage")
