# ui/ -- presentation. Qt Widgets only.
#
# Two rules the linker and tools/layer-lint enforce between them:
#   1. ui/ may never include pdf/ or platform/win/. Note below that it links neither.
#   2. ui/ may never mutate a Document. Its entire mutation vocabulary is: construct a
#      Command, hand it to CommandJournal. A dialog containing document logic is a
#      review rejection, not a style preference.
#
# ui/ carries a warning grace list where core/ and pdf/ carry none -- see
# RAWPAGE_UI_WARNING_GRACE in cmake/RawPageWarnings.cmake. It may only shrink.

# UiModule.cpp -- the Stage 0 placeholder TU -- was DELETED at T9, which is what its own header
# comment instructed: "replaced by real content at Stage 1. Delete this file then." The layer now
# has real translation units, so the archive is no longer empty without it, and nothing had called
# its anchor since T1 rewrote main.cpp.
add_library(rawpage_ui STATIC
    # shell/ -- the application frame (T9) and the ribbon (T10).
    shell/ShellKeys.def
    shell/Catalog.h
    shell/Catalog.cpp
    shell/ThemeManager.h
    shell/ThemeManager.cpp
    shell/StatusBar.h
    shell/StatusBar.cpp
    shell/SidebarDock.h
    shell/SidebarDock.cpp
    shell/MainWindow.h
    shell/MainWindow.cpp

    # The ribbon -- T10. RibbonModel is the declarative layout's parser and is deliberately
    # WIDGET-FREE: tools/ribbon-validate compiles that one file in so the build-time check and the
    # runtime build share a parser rather than agreeing by hand. See its header.
    shell/RibbonModel.h
    shell/RibbonModel.cpp
    shell/RawPageAction.h
    shell/ActionToolButton.h
    shell/ActionFactory.h
    shell/ActionFactory.cpp
    shell/Ribbon.h
    shell/Ribbon.cpp
    shell/RibbonTabBuilder.h
    shell/RibbonTabBuilder.cpp

    # The Quick Access Toolbar, the customization it serializes, and the tool finder -- T11.
    # UiCustomization is widget-free on purpose: it is the shape of a file that leaves this machine
    # (Features.md 1.1's export/import), and a class that needs a window to describe a file is a
    # class the Stage 14 batch runner cannot use.
    shell/UiCustomization.h
    shell/UiCustomization.cpp
    shell/QuickAccessToolbar.h
    shell/QuickAccessToolbar.cpp
    shell/ToolFinder.h
    shell/ToolFinder.cpp

    # Document tabs and the stub they carry -- T12, decision D1-2. DocumentWindow holds a path and
    # nothing else: no Document, no CommandJournal, no page state. Guide case A-57 is the guard
    # against that stub calcifying before Stage 2 fills it in.
    shell/DocumentWindow.h
    shell/DocumentWindow.cpp
    shell/DocumentTabBar.h
    shell/DocumentTabBar.cpp

    # The document-condition banner -- S2 T14. It says something true about the FILE that the file
    # cannot say: repaired, protected, signed, has a form. A persistent bar rather than a status
    # message because a repaired document stays repaired, and Stage 3 forces its first save to
    # Save-As on exactly that statement -- a banner the user missed is a banner that let them
    # overwrite an original.
    shell/MessageBar.h
    shell/MessageBar.cpp

    # The keyboard -- T13. ShortcutBinder is the ONLY path from a key press to a command, and the
    # only place a QKeySequence is turned into a core/cmd/Chord: core/ may not name a QtGui type,
    # which is why the binding model is first-party and the conversion happens at this boundary.
    shell/ShortcutBinder.h
    shell/ShortcutBinder.cpp

    # panes/ and dialogs/ -- T14. The first two files in each directory, and the first widgets in
    # this layer that are not the shell frame itself.
    #
    # SettingsPage is the load-bearing one: it #includes core/settings/SettingsKeys.def and builds
    # a row per declared key from the same table the schema is generated from, so adding a
    # preference in a later stage is a row in that .def and no widget code at all. See its header.
    # viewer/ -- the page surface. S2 T13, and the thing Stage 2 is named for.
    #
    # The split is by QUESTION rather than by size. ViewportController is arithmetic over four
    # numbers and names no widget, so every zoom-about-the-cursor and scroll-clamp case runs with
    # no window at all. PageLayoutEngine resolves the three inputs DpiPolicy needs -- and it is
    # where platform::IDisplayTopology (T12) finally has a consumer. TilePresenter is the ONLY
    # place in the product where a core::RasterImage becomes a QImage. PageView is the widget, and
    # it is the first thing in the product to declare a KeyContext (D2-8).
    viewer/ViewportController.h
    viewer/ViewportController.cpp
    viewer/PageLayoutEngine.h
    viewer/PageLayoutEngine.cpp
    viewer/TilePresenter.h
    viewer/TilePresenter.cpp

    # S4 T5. The engine boundary's Qt side, extracted from TilePresenter when the thumbnails pane
    # became its second consumer: a QImage over a RasterImage's bytes, HELD TOGETHER with them.
    # Both of its mistakes -- ignoring bytesPerLine, and letting the buffer die under a borrowing
    # QImage -- are silent, which is why there is one of it rather than two.
    viewer/HeldRaster.h
    viewer/HeldRaster.cpp

    # S4 T7 -- the tool state machine and the first four of the seventeen tools
    # Architecture-FileMap.md section 3.14 schedules across Stages 4 to 10. A tool is handed a
    # ToolHost rather than a PageView, so the family names no widget: that is what keeps the
    # thirteen later files off the widget and lets a case drive a tool with no window.
    viewer/tools/Tool.h
    viewer/tools/HandTool.h
    viewer/tools/HandTool.cpp
    viewer/tools/SelectTool.h
    viewer/tools/MarqueeZoomTool.h
    viewer/tools/MarqueeZoomTool.cpp
    viewer/tools/LoupeTool.h
    viewer/tools/LoupeTool.cpp
    viewer/ToolController.h
    viewer/ToolController.cpp

    # S4 T8 -- the rulers, the grid, the guides and the snap they feed (D4-6, D4-11). The
    # ARITHMETIC is core/Snap.h, not here: Stage4-ViewingSurface-Plan.md puts tst_snap in
    # rawpage_tests, which links no display, so the precedence and the device-pixel threshold have
    # to be provable with no widget. What is here is what a widget must hold -- the guides, the
    # appearance, the drag, and a painter that runs OVER the tiles and never into them.
    viewer/GridGuideLayer.h
    viewer/GridGuideLayer.cpp
    viewer/RulerWidget.h
    viewer/RulerWidget.cpp

    viewer/PageView.h
    viewer/PageView.cpp

    # S4 T9 -- the split (D4-13). AFTER PageView because it constructs them: N page surfaces over
    # ONE DocumentHandle, ONE Document, ONE CommandJournal and ONE revision. A split that opened a
    # second handle would need a second copy of DocumentWindow's three-step close protocol and
    # would double the document's memory to show the same pages; two views of one document in two
    # WINDOWS is a different thing and is T11's.
    viewer/SplitViewContainer.h
    viewer/SplitViewContainer.cpp

    # S4 T10 -- Full Screen and presentation (D4-12). ONE controller with TWO levels, because
    # there is one screen saver and two controllers would mean two answers to whether it may
    # start. The controller is a state machine and PresentationSurface is the widget it draws
    # into, split so a case can drive every level, exit path and power-state pairing with no
    # window ever shown -- which is what guide case A-58 needs.
    viewer/PresentationSurface.h
    viewer/PresentationSurface.cpp
    viewer/FullScreenController.h
    viewer/FullScreenController.cpp

    panes/LogPane.h
    panes/LogPane.cpp

    # S4 T5 -- the first pane that is a view of a DOCUMENT, and the first product caller of
    # RenderPriority::Speculative. It is a VIEWPORT over the page set rather than a view of it
    # (D4-10): a 900-page document costs the visible slots plus one screen, never 900 requests.
    panes/ThumbnailsPane.h
    panes/ThumbnailsPane.cpp

    # S4 T6 -- the two navigation panes over the read-only models T4 hydrated (D4-3). Both are
    # QTreeWidgets rather than hand-drawn surfaces: a bookmark tree and a destination table are
    # what an item view is for, and neither is a viewport over a render stream the way the
    # thumbnails pane is. Navigation only; every editing row in Features.md section 8's Bookmarks
    # table stays unregistered and leaves its key alone, which is T6's control.
    panes/BookmarksPane.h
    panes/BookmarksPane.cpp
    panes/DestinationsPane.h
    panes/DestinationsPane.cpp
    dialogs/SettingsPage.h
    dialogs/SettingsPage.cpp
    dialogs/ShortcutsPage.h
    dialogs/ShortcutsPage.cpp
    dialogs/PreferencesDialog.h
    dialogs/PreferencesDialog.cpp

    # S2 T14. The password prompt is a DIALOG and the protected banner is not: the prompt is a
    # question the engine asks mid-open and cannot continue without, the banner is a statement about
    # a document that is already open. It refuses to raise itself where there is nobody to ask
    # (Stage 1's I-23), which is what keeps a headless run from hanging on a modal.
    dialogs/PasswordDialog.h
    dialogs/PasswordDialog.cpp

    # Stage 3 T10. Two dialogs that collect a parameter and construct nothing else: which pages to
    # rotate (D3-13), and what to do with a dirty document being closed (D3-14). Each has a
    # parameterized twin a headless case answers through, and neither is raised where nobody can answer.
    dialogs/RotatePagesDialog.h
    dialogs/RotatePagesDialog.cpp
    dialogs/UnsavedChangesDialog.h
    dialogs/UnsavedChangesDialog.cpp

    # Stage 3 T11. The recovery prompt's question -- recover, discard or decide later, per document -- and
    # nothing else: app/RecoveryPrompt discovers, and MainWindow::recoverDocument replays.
    dialogs/RecoveryDialog.h
    dialogs/RecoveryDialog.cpp
)

# ui/ reads res/ribbon.json at startup, and res/'s own target is what proves that file sound before
# it is copied beside the executable. Depending on it here means an invalid resource fails the build
# of the layer that consumes it, rather than at whatever later point somebody happens to run.
add_dependencies(rawpage_ui rawpage_resources)

# A header-shaped include, not a translation unit -- same treatment as core/'s three .def tables.
set_source_files_properties(shell/ShellKeys.def PROPERTIES HEADER_FILE_ONLY ON)

# Every widget here is a QObject. See app/CMakeLists.txt for why this is per target.
set_target_properties(rawpage_ui PROPERTIES AUTOMOC ON)

target_include_directories(rawpage_ui PUBLIC "${CMAKE_SOURCE_DIR}")

# rawpage_core only. NOT rawpage_cos, NOT rawpage_pdfium, NOT rawpage_platform -- if this line ever grows one of
# those, the boundary is gone regardless of what any lint says.
target_link_libraries(rawpage_ui PUBLIC rawpage_core)

rawpage_link_qt(rawpage_ui Widgets Gui Svg)
rawpage_ui_grace(rawpage_ui)
