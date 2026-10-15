# tests/ui -- the widget suite. Decision D1-4.
#
# WHY IT IS A SEPARATE TARGET. tests/unit links rawpage_core and rawpage_cos and nothing else, and
# rawpage_assert_headless() enforces that against the RESOLVED link closure. That assertion is the
# standing proof that core/ is testable with no display and no engine, and portable-asan verifies it
# on a machine that has neither. The first widget test is precisely the pressure that would
# otherwise "just add rawpage_ui" to that target and destroy the property while every test still
# passed. tests/platform met the same pressure from the OS side at T7 and answered it the same way.
#
# WINDOWS ONLY, and under QT_QPA_PLATFORM=offscreen. None of it can judge appearance -- that is the
# guide's manual half, and a case here claiming to have checked a LOOK is a defect in the case.
#
# DEVIATION FROM THE PLAN, RECORDED RATHER THAN SLIPPED IN: Stage1-AppShell-Plan.md 4 schedules this
# target at T15. It arrives at T9 instead, with the first widgets, on the precedent T7 set for
# tests/platform: a stage's Definition of Done is code complete AND ctest green, and four widget
# files shipping with no suite until six tasks later would make that vacuous for all six. T15 still
# owns adding it to the CI `windows` job.

find_package(Catch2 3 REQUIRED)

add_executable(rawpage_ui_tests
    main.cpp
    tst_theme.cpp
    tst_shell.cpp
    tst_ribbonbuild.cpp
    tst_actionwiring.cpp
    tst_qat.cpp
    tst_toolfinder.cpp
    tst_documenttabs.cpp
    tst_context.cpp
    tst_preferences.cpp   # T14
    tst_logpane.cpp       # T14

    # S2 T13 -- the viewer. tst_viewport is PURE and needs no window: that is the whole reason
    # ViewportController and PageLayoutEngine are separate objects from PageView. tst_pageview is
    # the widget half, and every case in it asserts the geometry the layout GAVE the widget before
    # it asserts anything that depends on it (I-36).
    tst_viewport.cpp
    tst_pageview.cpp

    # S2 T14 -- the §8 Viewer commands, and the chrome that reports on the document. Every case in
    # tst_viewercommands drives the CHORD through ShortcutBinder rather than dispatching an id,
    # because "the command is implemented" and "the key reaches it" are different claims and I-42
    # is what happens when only the first is checked.
    tst_viewercommands.cpp
    tst_documentchrome.cpp

    # S2 T15 -- the two generated Preferences pages, and the sixteen keys reaching the viewer.
    # A-65 is deliberately split: the three flags that change PIXELS are measured in tests/render
    # over the real engine, and what is asserted here is that the preference reaches the request.
    tst_viewsettings.cpp

    # S2 I-39 -- guide case A-73's second clause: a saved shortcut and a saved toolbar survive an
    # action rename, driven through the real Shortcuts page and the real toolbar restore.
    tst_aliassurvival.cpp

    # Stage 3 T10 -- undo, redo, rotate, save and close: the mutation UI (guide cases A-71 … A-81).
    tst_mutationcommands.cpp

    # Stage 3 T11 -- the recovery prompt at launch (guide cases A-67's untitled half, A-82's prompt half).
    # app/RecoveryPrompt.cpp is COMPILED IN rather than linked, as tests/unit compiles three app/ units:
    # app/ is an executable, and this file names core/ and ui/ only, so the target's closure is unchanged.
    tst_recoverydialog.cpp
    ${CMAKE_SOURCE_DIR}/app/RecoveryPrompt.cpp

    # Stage 4 T5 -- the sidebar's pane registry and the Thumbnails pane (guide cases A-31 … A-34,
    # A-37 … A-39). Every case in it drives layout and REQUIREs a non-zero visible slot count
    # before it looks at what was enqueued: a pane that was never laid out asks for nothing, and
    # every "only the visible slots" assertion would pass vacuously (convention 12, guide rule 4).
    tst_panes.cpp

    # Stage 4 T7 -- the tool state machine and the four tools (guide cases A-40 … A-44). Every
    # gesture is sent as a real mouse event to the viewport so it travels PageView -> ToolController
    # -> tool, rather than calling the tool directly: "the chord arms it" and "the drag reaches it"
    # are different claims and only the second is about the wiring.
    tst_tools.cpp

    # Stage 4 T8 -- the rulers, the guides on the surface, the grid's painting and the inversion
    # (guide cases A-47, A-49 ... A-52). The ARITHMETIC is tests/unit/tst_snap.cpp's, headless; what
    # is here is what only a widget can answer, and two of its cases assert a NEGATIVE on the
    # scheduler's own counter -- a grid and an inversion that reached the rasterizer would be D4-11
    # broken, and nothing about the picture would look wrong.
    tst_rulers.cpp

    # Stage 4 T9 -- the split (guide cases A-53 ... A-56). The claim the task rests on is a
    # NEGATIVE one, that a split does not open a second document, so its first case asserts an
    # IDENTITY between two views rather than anything either one draws: a split that quietly opened
    # a second DocumentHandle would look right in every screenshot and be wrong in the two places
    # nothing on screen reports -- the close protocol, which exists once, and the memory.
    tst_split.cpp

    # Stage 4 T10 -- Full Screen and presentation (guide cases A-57 ... A-63). The case this file
    # exists for is A-58, and it is an S1: a presentation that exits without pairing its
    # setPresentationModeActive(true) leaves a machine that will not lock or sleep, silently, until
    # the battery is flat. It is asserted as a CALL SEQUENCE rather than a final state, because a
    # controller that asked twice and released once ends inactive and is still leaking a request.
    tst_presentation.cpp

    # Stage 4 T11 -- the window arranger (guide cases A-64 ... A-66). Its ARITHMETIC is judged over
    # synthetic work areas rather than over the offscreen platform's one screen: "the tiles fill the
    # work area and do not overlap" is a claim about numbers, and a case written against whatever
    # screen the machine reports would have to be loosened until it asserted nothing. The rest is
    # what only a window can answer -- a merge that keeps five tabs in order, a second view of one
    # document holding its own handle and no journal of its own, and A2's named work, which is
    # asserted on the scheduler's lane counters because nothing on screen can tell the two apart.
    tst_arranger.cpp

    # S4 T12 -- guide cases A-29, A-68's widget half, A-69 and A-70, and the closure of I-05. The
    # ENGINE half of the content box is tests/render/tst_contentbox.cpp, over a real file; what is
    # here is a viewer asking for one, fitting to it and SCROLLING to it, the page box reading a
    # logical label, the read-only properties dialog, and a reading mode that restores the window it
    # hid rather than a default one.
    tst_properties.cpp

    # S5 T5 -- the Selection tool and SelectionOverlay as a WIDGET (guide A-24's selection half, A-43's
    # gesture, A-46, A-51 ... A-55): Ctrl+C and Ctrl+A from key events, the Background lane of a copy,
    # Esc, the panes of a split, the tabs, and every text widget keeping its own Ctrl+C and Ctrl+A. The
    # fixture's text is fake (ShellFixture.h); geometry against INK is tests/composition's.
    tst_selecttool.cpp

    # S5 T5 -- guide A-50: the copy's HTML read back through Win32 as CF_HTML. SKIPS under ctest
    # (offscreen has no system clipboard) and is run by hand on the windows platform, "[D5-10]".
    tst_clipboard.cpp

    # S5 T6 -- the text index as the product's windows use it (guide A-62, A-64 and A-66's widget halves):
    # window.new's companion on its leader's entry, no text asked for by opening and scrolling, a rotation
    # through the journal reaching the index.
    tst_documentindex.cpp

    # S5 T7 -- the find bar as a widget driven by key events (guide A-69 ... A-75, A-57's bar half):
    # every Find / Search chord asserted on its effect, the debounce, wrap, Esc, the toggles as the
    # settings, scope, panes and tabs. Ink is tests/composition's.
    tst_findbar.cpp

    # S5 T9 -- Advanced Search as a widget driven by key events over a scratch folder (guide A-89, A-87, and
    # the pane's halves of A-80, A-84, A-85, A-86, A-88). The engine-backed folder search is tests/render's.
    tst_searchpane.cpp

    # S5 T10 -- the Snapshot tool (D5-11; guide A-91, A-114 and A-90's widget half): `S` as a key event,
    # Esc and a click, every size at every pageDisplay/snapshotDpi, the 50-megapixel bound, the overlays
    # and the inversion absent, the tile cache bypassed. Ink is tests/composition/tst_snapshotink.cpp.
    tst_snapshot.cpp

    # S5 T11 -- Reflow view (D5-16; guide A-92 ... A-94 as a widget): Ctrl+4 both ways as key events, the
    # place both ways, the zoom chords as the text size, nothing rasterized while it shows, the reading keys
    # its own, a find hit leaving it, the window growing. A real page's order is tests/composition's.
    tst_reflow.cpp

    # S6 T5 -- the annotation overlay and the context chain from mouse and key events (guide A-40, A-42 ...
    # A-47, A-39's status bar): one gesture one command, the locks, the nudge burst, the properties bar, Esc
    # kept by every widget that owns it, pop-ups as view state. Over the fake backend, which draws nothing:
    # the same gestures against the INK are tests/composition/tst_annotink.cpp's (A-41).
    tst_annotoverlay.cpp
    tst_annottools.cpp
    tst_drawtools.cpp
    tst_commentspane.cpp       # Stage 6 T9 -- the Comments pane: its table from key events (A-70), the list as
                               #   the query (A-67), the identity's checkmark (A-69), deletes (A-71), numbers (A-72)
    tst_stamps.cpp             # Stage 6 T10 -- Ctrl+Alt+S, the palette over a SCRATCH library, the custom-stamp
                               #   dialog (A-78), the bar's turn (A-80), an image pasted and Ctrl+V kept (A-81)
    tst_attachments.cpp        # Stage 6 T11 -- the Attachments pane: Ctrl+Shift+A, both kinds listed, a pane that
                               #   never widens its window, convention 16's disabling, a pushpin deleted with its pop-up
    tst_measuretool.cpp        # Stage 6 T12 -- the measuring tools from mouse and key events: four kinds, a snap three
                               #   device pixels off landing exactly at two zooms (A-89), calibration (A-90), the dense
                               #   page said once (A-91), the comment and the history (A-92), the scale dialog
    tst_exchange.cpp           # Stage 6 T13 -- annotations cut, copied and pasted from key events, across tabs and as
                               #   bytes alone; text fields keeping their own Ctrl+X (A-81); export what the pane
                               #   lists (A-96); an import as one command and its report; the file questions disabled
                               #   where nobody can be asked (convention 16)

    # S5 T13 -- the comparison as a window driven by key events over two fake documents (guide A-103, and
    # the widget halves of A-106 and A-107): Ctrl+Alt+C through the binder with an answer installed, the
    # dialog's widgets, the steps disabled and then stepping, the claimed keys, the bar as the settings, the
    # handles closed. Real pages are tests/composition/tst_comparisonink.cpp's.
    tst_comparison.cpp
)

target_include_directories(rawpage_ui_tests PRIVATE "${CMAKE_SOURCE_DIR}")
# Stage 6 T10: tst_stamps reads the corpus's stamp images and a PDF the custom-stamp dialog must refuse -- READ only.
target_compile_definitions(rawpage_ui_tests PRIVATE RAWPAGE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus")

# Catch2::Catch2 without WithMain -- main.cpp supplies one, because these tests need a QApplication
# on the offscreen platform before any of them runs. See the comment there.
target_link_libraries(rawpage_ui_tests PRIVATE
    rawpage_core
    rawpage_ui
    Catch2::Catch2
)

# dwmapi for guide case A-94 (issue I-23): the frame Windows DRAWS -- DWM's extended frame bounds --
# is the one witness to a title bar off the screen, because Qt's own numbers stayed self-consistent
# while it was. The test binary's dependency only; nothing under ui/ names DWM.
if(WIN32)
    target_link_libraries(rawpage_ui_tests PRIVATE dwmapi)
endif()

set_target_properties(rawpage_ui_tests PROPERTIES AUTOMOC ON)

# The suite installs rawpage_en.qm so the widgets are exercised against real text rather than
# against their own catalog keys. See the comment in main.cpp. It also needs res/ribbon.json beside
# the binary, for the same reason and with a second one on top: the ribbon cases assert that the
# SHIPPED resource is the one that loads, so depending on the validated copy is what makes them
# about the product rather than about a fixture.
add_dependencies(rawpage_ui_tests rawpage_translations rawpage_resources)

rawpage_common_flags(rawpage_ui_tests)

include(Catch)
catch_discover_tests(rawpage_ui_tests
    TEST_PREFIX "ui."
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    DL_PATHS "${RAWPAGE_MINGW_ROOT}/bin"
)

# A-84 -- and it drives `rawpage.exe` rather than linking anything, because the input it varies
# (`QT_SCALE_FACTOR`) is read when the `QGuiApplication` is built and is therefore a property of a
# process. It keeps the `ui.` prefix on purpose: a sixth CTest prefix would have to be remembered
# separately by CI's non-empty check, which is the same reasoning that put `render.golden.` inside
# `render.` at T17.
add_test(NAME ui.screen_metrics_invariant
    COMMAND ${CMAKE_COMMAND}
            -DRAWPAGE_EXE=$<TARGET_FILE:rawpage>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/ScreenMetricsInvariant.cmake")
set_tests_properties(ui.screen_metrics_invariant PROPERTIES
    # ENVIRONMENT_MODIFICATION, NEVER `ENVIRONMENT "PATH=<dir>;$ENV{PATH}"`. `ENVIRONMENT` is a CMake
    # LIST, so the semicolons of a Windows PATH split that one value into entries (113 on the CI runner) and the child
    # ran with `PATH=<mingw>/bin` and nothing else. It passed wherever the Qt DLLs had been copied
    # beside rawpage.exe and exited 0xC0000135 in every clean build -- CI runs 34724531551 and
    # 34772420519. It also froze the CONFIGURING shell's PATH into the test. The bench targets use
    # this form already (tests/bench/CMakeLists.txt).
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin")

# A-87 -- issue I-27, and like A-84 it drives `rawpage.exe` instead of linking anything, for the
# same class of reason: the condition it asserts is a property of a PROCESS. A diagnostic that
# composes and exits must print its answer while another process holds the single-instance bus, and
# a bus claim needs a claimant, so the case needs two processes and cannot be a Catch2 case.
#
# POWERSHELL RATHER THAN `cmake -P`, because CMake's `execute_process` has no way to start a process
# and keep it -- and the obvious workaround, `taskkill /IM rawpage.exe`, would kill the window of
# whoever is running the suite. `Start-Process -PassThru` gives a pid, and the script stops only
# what it started and adopts anything already running.
#
# It touches no settings file, which is what lets it be a CTest case where A-61's two-process half
# (`scripts/check-single-instance.ps1`) cannot be: that one has to edit settings.ini.
#
# 77 is SKIP: the case measures nothing on a machine whose `general/singleInstance` is false, and
# the script says so rather than passing. The skip is reached only through the CONTROL failing --
# a second GUI process that did not forward -- so a skip always means the bus was not claimed.
add_test(NAME ui.diagnostics_ignore_the_bus
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/DiagnosticsIgnoreTheBus.ps1"
            -Exe $<TARGET_FILE:rawpage>)
set_tests_properties(ui.diagnostics_ignore_the_bus PROPERTIES
    # The same trap as A-84's above, and here it did not even fail: a rawpage.exe that could not
    # load Qt never exited, the control read that as "the bus is not claimed", and the case SKIPPED.
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin"
    SKIP_RETURN_CODE 77
    RUN_SERIAL TRUE)
