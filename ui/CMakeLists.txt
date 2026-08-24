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
    panes/LogPane.h
    panes/LogPane.cpp
    dialogs/SettingsPage.h
    dialogs/SettingsPage.cpp
    dialogs/ShortcutsPage.h
    dialogs/ShortcutsPage.cpp
    dialogs/PreferencesDialog.h
    dialogs/PreferencesDialog.cpp
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

# rawpage_core only. NOT rawpage_pdf, NOT rawpage_platform -- if this line ever grows one of
# those, the boundary is gone regardless of what any lint says.
target_link_libraries(rawpage_ui PUBLIC rawpage_core)

rawpage_link_qt(rawpage_ui Widgets Gui Svg)
rawpage_ui_grace(rawpage_ui)
