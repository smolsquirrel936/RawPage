# core/ -- the domain layer.
#
# Depends on NOTHING but the standard library, QtCore (a deliberate, documented
# exception for QString/containers/signals), a short allowlist of portable
# pure-computation libraries, and its own two interface headers. It never includes
# ui/, pdf/, platform/win/, QtWidgets, QtGui, PDFium, or any OS header.
#
# The load-bearing consequence: this library and its full test suite must build and
# pass with NO DISPLAY AND NO PDF ENGINE LINKED. That is not an aspiration -- the
# portable-asan preset builds exactly this on Linux, so a leak in the boundary shows
# up as a build failure on a machine that has neither.

add_library(rawpage_core STATIC
    CoreModule.cpp
    Result.h
    Log.h
    Geometry.h
    Units.h

    # cmd/ -- the action layer (Stage 1 T3). ActionId.def is listed so it appears in an IDE's
    # project tree and so that editing it re-runs the build; it is #included, never compiled.
    cmd/ActionId.def
    cmd/ActionId.h
    cmd/KeyContext.h
    cmd/ActionRegistry.h
    cmd/ActionRegistry.cpp

    # T4 -- key bindings, seeded from Features.md 8
    cmd/Chord.h
    cmd/Chord.cpp
    cmd/KeyBindingDefaults.def
    cmd/KeyBindingStore.h
    cmd/KeyBindingStore.cpp

    # settings/ -- the typed settings store (Stage 1 T5). SettingsKeys.def is listed for the same
    # reason ActionId.def is: it belongs in an IDE's project tree and editing it must re-run the
    # build. It is #included, never compiled.
    settings/SettingsKeys.def
    settings/SettingsSchema.h
    settings/SettingsSchema.cpp
    settings/Settings.h
    settings/Settings.cpp
    settings/RecentFiles.h
    settings/RecentFiles.cpp
)

# The .def files are header-shaped includes, not translation units. Without this the generator
# tries to compile them and fails on the first macro invocation.
set_source_files_properties(cmd/ActionId.def cmd/KeyBindingDefaults.def settings/SettingsKeys.def
    PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(rawpage_core PUBLIC "${CMAKE_SOURCE_DIR}")

rawpage_link_qt(rawpage_core Core)
rawpage_strict(rawpage_core)          # -Werror: no warning survives in core/
