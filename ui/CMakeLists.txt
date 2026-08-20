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

add_library(rawpage_ui STATIC
    UiModule.cpp
)

target_include_directories(rawpage_ui PUBLIC "${CMAKE_SOURCE_DIR}")

# rawpage_core only. NOT rawpage_pdf, NOT rawpage_platform -- if this line ever grows one of
# those, the boundary is gone regardless of what any lint says.
target_link_libraries(rawpage_ui PUBLIC rawpage_core)

rawpage_link_qt(rawpage_ui Widgets Gui Svg)
rawpage_ui_grace(rawpage_ui)
