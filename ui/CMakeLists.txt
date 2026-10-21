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
    shell/DockEdge.h
    shell/DockEdge.cpp
    shell/MainWindow.h
    shell/MainWindow.cpp
    # S6 T13 (D6-20, D6-21, D6-22): the window's comments in and out -- MainWindow members in a file of their own --
    # and one migration's run, off the UI thread over the comparison's pipeline.
    shell/MainWindowExchange.cpp
    # S6 T14 (D6-18): the window's half of a sound comment -- the recorder and the WAV asked, a sound played.
    shell/MainWindowAudio.cpp
    shell/CommentMigration.h
    shell/CommentMigration.cpp

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

    # S4 T11 -- tile, cascade, merge, the window list and the mirror between two views of one
    # document in two windows (D4-17). It adds no platform/ interface: placing our own top-level
    # windows is QWidget::setGeometry over QScreen::availableGeometry(), which Qt does everywhere,
    # and Tech-Stack-Plan.md 4.3's rule is that what Qt does cross-platform gets none. Its
    # arithmetic is pure functions of a work area and a count, so guide case A-64 judges tiling and
    # cascading over synthetic geometry with no screen at all.
    shell/WindowArranger.h
    shell/WindowArranger.cpp

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

    # S4 D4-20 -- the Loupe's sharp lens: the scheduler's third client, one region of one page at the
    # magnified scale, one request at a time with the old one cancelled. Its request is built by
    # TilePresenter::requestAt, so the cache key is shaped in one place.
    viewer/LoupeRaster.h
    viewer/LoupeRaster.cpp

    # S4 T7 -- the tool state machine and the first four of the seventeen tools
    # Architecture-FileMap.md section 3.14 schedules across Stages 4 to 10. A tool is handed a
    # ToolHost rather than a PageView, so the family names no widget: that is what keeps the
    # thirteen later files off the widget and lets a case drive a tool with no window.
    viewer/tools/Tool.h
    # Stage 6 T5 (D6-9): the fourteen annotation tools, armed; each creates from its own task (T6 ...).
    viewer/tools/AnnotationTool.h
    # Stage 6 T6 (D6-12): the tools that MAKE -- text markup and the two text edits over the selection, and
    # the sticky note -- each gesture one command, its text asked for first by the editor below.
    viewer/tools/MarkupTool.h
    viewer/tools/MarkupTool.cpp
    viewer/tools/NoteTool.h
    viewer/tools/NoteTool.cpp
    # Stage 6 T7 (D6-7, D6-8): the text box, the callout and the typewriter -- typed text, one FreeText each.
    viewer/tools/TypewriterTool.h
    viewer/tools/TypewriterTool.cpp
    # Stage 6 T8 (D6-15, D6-35): the shapes, and the pencil and the eraser.
    viewer/tools/DrawingTool.h
    viewer/tools/DrawingTool.cpp
    viewer/tools/InkTool.h
    viewer/tools/InkTool.cpp
    # Stage 6 T10 (D6-16): places the palette's stamp -- a click at its natural size, a drag fitted.
    viewer/tools/StampTool.h
    viewer/tools/StampTool.cpp
    viewer/tools/AttachFileTool.h
    viewer/tools/AttachFileTool.cpp
    # Stage 6 T12 (D6-14): distance, perimeter, area and the dimension callout, each point snapped; and calibration.
    viewer/tools/MeasureTool.h
    viewer/tools/MeasureTool.cpp
    # Stage 6 T14 (D6-25, D6-18): the redaction mark -- over text or an area -- and the two sound tools; and recording
    # and playing a sound through Qt Multimedia's Media Foundation backend, the one file that names Qt Multimedia.
    viewer/tools/RedactTool.h
    viewer/tools/RedactTool.cpp
    viewer/tools/SoundTool.h
    viewer/tools/SoundTool.cpp
    viewer/AudioRecorder.h
    viewer/AudioRecorder.cpp
    # Stage 6 T12: a document's pages' paths, asked of the text index for the measuring tools to snap to.
    viewer/SnapTargets.h
    viewer/SnapTargets.cpp
    viewer/tools/HandTool.h
    viewer/tools/HandTool.cpp
    viewer/tools/SelectTool.h
    # S5 T5 -- the Selection tool given its subject (D5-8): a drag, a multi-click and an Alt+drag into
    # the document window's SelectionOverlay, below.
    viewer/tools/SelectTool.cpp
    viewer/tools/MarqueeZoomTool.h
    viewer/tools/MarqueeZoomTool.cpp
    viewer/tools/LoupeTool.h
    viewer/tools/LoupeTool.cpp
    # S5 T10 -- the Snapshot (D5-11): the marquee's band, the loupe's request, the clipboard.
    viewer/tools/SnapshotTool.h
    viewer/tools/SnapshotTool.cpp
    # S5 T11 -- Reflow view (D5-16): the analyzer's blocks as a read-only, re-wrapped text surface.
    viewer/ReflowView.h
    viewer/ReflowView.cpp
    # S5 T13 -- the comparison (D5-18): a window of its own over TWO documents, its own two handles, two
    # PageViews scrolled by aligned page, an overlay composited at presentation, and no journal.
    viewer/ComparisonView.h
    viewer/ComparisonView.cpp
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

    # S5 T5 -- one document window's text selection (D5-8, D5-9, D5-10): the page text it asks the
    # engine thread for, the gesture, the highlight painted OVER the tiles and never into them, and the
    # copy to Qt's clipboard as plain text and HTML. The range arithmetic is core/text/TextSelection.
    viewer/SelectionOverlay.h
    viewer/SelectionOverlay.cpp

    # S6 T5 -- one document window's annotations as something a person points at (D6-9, D6-27, D6-28,
    # D6-30): the selection and its handles, the drag, the nudge, delete and the pop-ups, each gesture ONE
    # command handed to the journal; and the properties bar `Ctrl+E` opens over the page.
    viewer/AnnotationOverlay.h
    viewer/AnnotationOverlay.cpp
    viewer/PropertiesBar.h
    viewer/PropertiesBar.cpp
    # Stage 6 T8 (D6-13): what a tool draws with -- the settings family under the session's change.
    viewer/ToolDefaults.h
    viewer/ToolDefaults.cpp
    # S6 T6 -- the one-off editor a note, an insertion or a replacement is typed into before it is made.
    viewer/AnnotationTextEditor.h
    viewer/AnnotationTextEditor.cpp

    # write/ -- S5 T7. The find half of the find-and-replace bar (D5-14), at the path the FileMap gives it
    # although the rest of write/ is Stage 13's: Write mode's find is this bar with replace switched on.
    write/FindReplaceBar.h
    write/FindReplaceBar.cpp

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
    panes/CommentsPane.h
    panes/CommentsPane.cpp
    # S6 guide I-33 -- the picture at the head of each Comments pane row, drawn in code.
    panes/CommentIcon.h
    panes/CommentIcon.cpp
    panes/AttachmentsPane.h
    panes/AttachmentsPane.cpp
    panes/DestinationsPane.h
    panes/DestinationsPane.cpp
    # S5 T9 (D5-13) -- Advanced Search, a dock of its own like the log pane: core/text/FolderSearch does the
    # work off the UI thread and core/text/SearchResults owns the list's rules. Holds the stage's one planned
    # filesystem-write waiver, the CSV export.
    panes/SearchPane.h
    panes/SearchPane.cpp
    # S5 T13 -- a comparison's differences, listed and counted: the summary at Stage 5 (D5-17, Q4).
    panes/DifferencesPane.h
    panes/DifferencesPane.cpp
    # Stage 6 T10 (D6-16) -- the stamp palette, a dock like Advanced Search's: the person's stamps by category.
    panes/StampPalettePane.h
    panes/StampPalettePane.cpp
    dialogs/SettingsPage.h
    dialogs/SettingsPage.cpp
    dialogs/ShortcutsPage.h
    dialogs/ShortcutsPage.cpp
    dialogs/PreferencesDialog.h
    dialogs/PreferencesDialog.cpp
    # Stage 6 T8 (D6-13): the Commenting page's editor of each tool's defaults.
    dialogs/ToolDefaultsEditor.h
    dialogs/ToolDefaultsEditor.cpp
    # Stage 6 T10 (D6-16): a custom stamp from an image, or initials drawn or typed, for the library.
    dialogs/CustomStampDialog.h
    dialogs/CustomStampDialog.cpp
    # Stage 6 T12 (D6-14): the measurement scale from two lengths, and calibration from one measured.
    dialogs/MeasurementScaleDialog.h
    dialogs/MeasurementScaleDialog.cpp
    # S6 T13: what an import or a migration did, comment by comment. Asks nothing, constructs nothing.
    dialogs/ExchangeReportDialog.h
    dialogs/ExchangeReportDialog.cpp

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
    # S5 T13 -- the two documents and the comparison's options; refused where nobody can answer.
    dialogs/CompareDialog.h
    dialogs/CompareDialog.cpp
    dialogs/UnsavedChangesDialog.h
    dialogs/UnsavedChangesDialog.cpp

    # S4 T12 (D4-4). The one dialog in the product that collects NOTHING and constructs no command:
    # it shows what the Info dictionary, the document identity and the page tree already say, and
    # every field in it is a label rather than a disabled edit box. Editing is Stage 8's, with the
    # /ModDate policy and the XMP writer that have to stay consistent with it.
    dialogs/DocumentPropertiesDialog.h
    dialogs/DocumentPropertiesDialog.cpp

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

# Multimedia since Stage 6 T14 (D6-18, Tech-Stack-Plan.md 6.1): ui/ ONLY -- core/ never links it, which the A-07
# deployment case checks against the unit binary's imports.
rawpage_link_qt(rawpage_ui Widgets Gui Svg Multimedia)

# --- Qt Multimedia beside the executable: Media Foundation, and NOTHING of FFmpeg (D6-18, guide A-07) -------------
# The Qt prefix carries two Windows backends, and Qt 6 prefers FFmpeg wherever its plugin can be found. So the build
# deploys Qt6Multimedia.dll and the WINDOWS backend's plugin -- in windeployqt's layout, multimedia/ beside the
# executable, a directory Qt searches as an application library path -- and never ffmpegmediaplugin.dll or the five
# DLLs it brings (avcodec, avformat, avutil, swresample, swscale). ui/viewer/AudioRecorder sets
# QT_MEDIA_BACKEND=windows besides, for a process whose PATH reaches the Qt prefix's own plugins. A custom target,
# as pdfium.dll's is (pdf/CMakeLists.txt), so every executable that links rawpage_ui finds both from a fresh tree.
set(RAWPAGE_MEDIA_PLUGIN "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/multimedia/windowsmediaplugin.dll")
add_custom_command(
    OUTPUT "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Qt6Multimedia.dll"
           "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/multimedia/windowsmediaplugin.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Multimedia>"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Qt6Multimedia.dll"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/multimedia"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RAWPAGE_MEDIA_PLUGIN}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/multimedia/windowsmediaplugin.dll"
    DEPENDS "${RAWPAGE_MEDIA_PLUGIN}"
    COMMENT "Deploying Qt Multimedia (the Media Foundation backend only) beside the executable"
    VERBATIM)
add_custom_target(rawpage_multimedia_runtime DEPENDS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Qt6Multimedia.dll"
                                                     "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/multimedia/windowsmediaplugin.dll")
add_dependencies(rawpage_ui rawpage_multimedia_runtime)
rawpage_ui_grace(rawpage_ui)
