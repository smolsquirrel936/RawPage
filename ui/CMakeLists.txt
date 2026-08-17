# ui/ -- presentation. Qt Widgets only.
#
# Two rules the linker and tools/layer-lint enforce between them:
#   1. ui/ may never include pdf/ or platform/win/. Note below that it links neither.
#   2. ui/ may never mutate a Document. Its entire mutation vocabulary is: construct a
#      Command, hand it to CommandJournal. A dialog containing document logic is a
#      review rejection, not a style preference.
#
# ui/ carries a warning grace list where core/ and pdf/ carry none -- see
# RECTO_UI_WARNING_GRACE in cmake/RectoWarnings.cmake. It may only shrink.

add_library(recto_ui STATIC
    UiModule.cpp
)

target_include_directories(recto_ui PUBLIC "${CMAKE_SOURCE_DIR}")

# recto_core only. NOT recto_pdf, NOT recto_platform -- if this line ever grows one of
# those, the boundary is gone regardless of what any lint says.
target_link_libraries(recto_ui PUBLIC recto_core)

recto_link_qt(recto_ui Widgets Gui Svg)
recto_ui_grace(recto_ui)
