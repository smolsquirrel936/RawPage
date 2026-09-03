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
    # CoreModule.cpp -- Stage 0's placeholder translation unit -- was DELETED at Stage 1 T15, as
    # its own comment instructed and as Stage1-AppShell-Plan.md 10 had been carrying since T7. It
    # existed so this archive was non-empty and the link order was exercised from the first commit;
    # core/ has had real translation units since T3 and nothing has called its anchor since T1
    # rewrote main.cpp. A placeholder that outlives its purpose becomes a mystery.
    Result.h
    Log.h
    Geometry.h
    Units.h

    # The ENGINE boundary (Stage 2 T1, decision D2-7). The second of the two interface headers this
    # architecture rests on -- header-only, like platform/platform.h, and constrained by its own
    # tools/layer-lint entry because core/, pdf/, ui/ and app/ all include it.
    backend.h

    # The in-memory tail of the log, so that ui/panes/LogPane has something to show (Stage 1 T14).
    # It is here rather than in ui/ because core/Log.h has ONE sink and setLogSink replaces it: a
    # pane that chained the installed sink would be dropped the next time app/CompositionRoot
    # re-applies the logging settings, which is the very thing T14's Preferences page does.
    LogBuffer.h
    LogBuffer.cpp

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

    # doc/ -- the document VOCABULARY, and the first thing in core/doc/ (Stage 2 T8).
    #
    # Revision and Scope are BROUGHT FORWARD FROM STAGE 3 by decision D2-5, because Stage 2 already
    # names them: `RenderCache::invalidate(Scope, Revision)` is a Stage 2 signature and D2-12 makes
    # the Revision inside a RenderRequest mandatory rather than defaulted. Building them at Stage 3
    # would force Stage 2 to invent a parallel type and then delete it.
    #
    # RepairReport is natively Stage 2's: `pdf/cos/XrefRepair` populates it and Stage 3 reads it to
    # decide whether a document's first save is forced to Save-As. Revision.h has no .cpp on
    # purpose -- it is constexpr and header-only, exactly as Architecture-FileMap.md 3.2 lists it.
    #
    # CLAUDE.md's Stage 2 note says a FIFTH type appearing in core/doc/ during this stage is a
    # finding. These are three of the four D2-5 names; Page and PageTree are T9's.
    doc/Revision.h
    doc/Scope.h
    doc/Scope.cpp
    doc/RepairReport.h
    doc/RepairReport.cpp
)

# The .def files are header-shaped includes, not translation units. Without this the generator
# tries to compile them and fails on the first macro invocation.
set_source_files_properties(cmd/ActionId.def cmd/KeyBindingDefaults.def settings/SettingsKeys.def
    PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(rawpage_core PUBLIC "${CMAKE_SOURCE_DIR}")

rawpage_link_qt(rawpage_core Core)
rawpage_strict(rawpage_core)          # -Werror: no warning survives in core/
