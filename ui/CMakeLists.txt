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
    # shell/ -- the application frame (Stage 1 T9). Ribbon, QAT, tabs and the shortcut binder are
    # T10 through T13 and are not here yet.
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
)

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
