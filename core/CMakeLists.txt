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

add_library(recto_core STATIC
    CoreModule.cpp
)

target_include_directories(recto_core PUBLIC "${CMAKE_SOURCE_DIR}")

recto_link_qt(recto_core Core)
recto_strict(recto_core)          # -Werror: no warning survives in core/
