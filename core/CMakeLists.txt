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

    # The snap resolver -- S4 T8, D4-6. Page space in, page space out, with a scale so the ONE
    # threshold can be stated in device pixels; no widget and no painter, which is what lets
    # tests/unit/tst_snap.cpp exercise the whole precedence with no display. ui/viewer/GridGuideLayer
    # holds the guides and the drag and calls this for the sums.
    Snap.h
    Snap.cpp

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

    # Stage 3 T5 -- mutation through commands (D3-10, D3-12). Commands.def is the command TABLE:
    # CommandRegistry is generated from it and tools/mutation-lint (T6) reads it as text.
    cmd/Command.h
    cmd/Commands.def
    cmd/CommandRegistry.h
    cmd/CommandRegistry.cpp
    cmd/CommandJournal.h
    cmd/CommandJournal.cpp
    cmd/commands/PageCommands.h
    cmd/commands/PageCommands.cpp

    # Stage 3 T9 (D3-11): what the journal tells a durable recorder, and the recorder -- the recovery
    # directory, one flushed line per operation, discovery and replay by undo step.
    cmd/OperationRecorder.h
    cmd/RecoveryJournal.h
    cmd/RecoveryJournal.cpp

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

    # S4 T3 -- session state: the open-document set, per-arrangement window geometry, and guides.
    # In core/settings/ because it WRITES a file, which is that directory's one added privilege.
    settings/SessionState.h
    settings/SessionState.cpp

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

    # T9 completes D2-5's four: Page and a READ-ONLY PageTree. Stage 3 T4 adds PageTree's ROTATION;
    # insert/move/remove/duplicate are Stage 7's, with the commands that call them (D3-8).
    doc/Page.h
    doc/Page.cpp
    doc/PageTree.h
    doc/PageTree.cpp

    # Stage 3 T4 -- the rest of D3-8's list, and no more: a type with no Stage 3 caller is tested only
    # against its own idea of what a caller wants. Document is the model CommandJournal (T5) will be
    # the only holder of a mutable reference to; DocumentId is header-only; Permissions is the
    # PRE-FLIGHT half of a Stage 10 file (D3-7).
    doc/Document.h
    doc/Document.cpp
    doc/DocumentId.h
    doc/Permissions.h
    doc/Permissions.cpp

    # Stage 3 T8 (D3-4, D3-6, D3-16): the order of a save, and every decision in it. It writes no byte:
    # the backend writes and re-reads, platform::IDurableFiles flushes and replaces.
    doc/SaveCoordinator.h
    doc/SaveCoordinator.cpp

    # S4 T4 (D4-3, D4-4) -- the READ-ONLY halves of what a document says about itself. Hydrated
    # through the PDFium facade at open; the editing halves are Stage 7's and Stage 8's, and they
    # arrive with a first-party writer rather than with a second reader.
    doc/Outline.h
    doc/Outline.cpp
    doc/Destinations.h
    doc/Destinations.cpp
    doc/Metadata.h
    doc/Metadata.cpp

    # S4 T10 (D4-12) -- the document's own presentation instructions. The model is here like the
    # three above; what is NOT like them is where it is hydrated from, because PDFium has no
    # /Trans call at all and pdf/ops/TransitionReader reads it first-party. Transition.h is the
    # enum, beside core/Units.h, because fullScreen/transition is a settings key and
    # SettingsKeys.def may only name core/ types.
    Transition.h
    doc/PageTransitions.h
    doc/PageTransitions.cpp

    # render/ -- the rendering POLICY layer. RenderRequest was T10's by the plan and was built at T9
    # because IPageRasterizer::raster() takes one and T9's exit criterion is a rasterized page;
    # core/backend.h has carried a forward declaration of it since T1.
    #
    # NO RASTERIZATION HAPPENS HERE (Architecture-FileMap.md 3.4's own heading). These files decide
    # what to rasterize, at what resolution, where to put the result and what to throw away; the
    # pixels are pdf/render/'s. That is what keeps the whole policy layer testable with no display
    # and no engine, which is the property rawpage_assert_headless() exists to hold.
    render/RenderRequest.h

    # T10 -- the arithmetic. All three are pure and all three are tested headless.
    render/DpiPolicy.h
    render/DpiPolicy.cpp
    render/TileGrid.h
    render/TileGrid.cpp
    render/PagePresentation.h
    render/PagePresentation.cpp

    # S4 T12 -- WHICH RECTANGLE A FIT MEASURES (D4-14), and the zoom that satisfies it. It is here
    # rather than in ui/viewer/PageLayoutEngine, where it lived, because the observable I-28 names
    # is a number read off a real file and only rawpage_render_tests can open one -- and that target
    # links no widgets. The file's own header argues it.
    render/FitPolicy.h
    render/FitPolicy.cpp

    # T11 -- the cache and the scheduler. RenderCache is pure and single-threaded by construction
    # (only the pumping thread touches it); RenderScheduler owns the worker threads, and its header
    # is where issue I-05 is closed -- ONE render thread with a queue, chosen on T3's measurement.
    #
    # DiskTileStore is a file the plan's 4 manifest does not list, and it is here because D2-14
    # requires the disk tier to exist behind the same interface as the memory tier. It is the ONLY
    # file in core/render/ that writes to disk and it carries two layer-lint waivers for it.
    render/RenderCache.h
    render/RenderCache.cpp
    render/DiskTileStore.h
    render/DiskTileStore.cpp
    render/RenderScheduler.h
    render/RenderScheduler.cpp

    # T16: WHICH face stands in for a font a document names and does not embed. It is in core/ and
    # not in platform/win/ because platform/ makes no policy decisions and this is nothing but
    # policy -- an ordered list of families, the same judgement on every operating system. The
    # consequence is that guide case A-66's claim ("our policy, not the engine's") is asserted by
    # tests/unit with no display, no Windows and no PDFium linked.
    render/FontSubstitution.h
    render/FontSubstitution.cpp

    # text/ -- Stage 5. PageTextModel is T2's (D5-2): characters with geometry, filled by
    # pdf/render/PdfiumTextExtractor through core/backend.h's ITextExtractor, and nothing in it names
    # an engine -- which is what lets the headless suite and fuzz_text_pipeline (D5-21) build it.
    text/PageTextModel.h
    text/PageTextModel.cpp
    # T4 (D5-5, D5-6): one normalizer for find, search and compare, and the analyzer every reading
    # order comes from -- geometry only, measured against corpus/labels/ (D5-7).
    text/TextNormalizer.h
    text/TextNormalizer.cpp
    text/LayoutAnalyzer.h
    text/LayoutAnalyzer.cpp
    # T5 (D5-8, D5-9): a selection as a range over the reading order, or a column; its hit-testing and
    # its copy as plain text, HTML and a table. Pure -- the clipboard is ui/viewer/SelectionOverlay's.
    text/TextSelection.h
    text/TextSelection.cpp
)

# The .def files are header-shaped includes, not translation units. Without this the generator
# tries to compile them and fails on the first macro invocation.
set_source_files_properties(cmd/ActionId.def cmd/KeyBindingDefaults.def cmd/Commands.def settings/SettingsKeys.def
    PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(rawpage_core PUBLIC "${CMAKE_SOURCE_DIR}")

rawpage_link_qt(rawpage_core Core)
rawpage_strict(rawpage_core)          # -Werror: no warning survives in core/
