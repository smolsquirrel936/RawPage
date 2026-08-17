# RectoQt.cmake -- every Qt link in the project goes through recto_link_qt().
#
# WHY A WRAPPER (decision D0-5 in Stage0-Toolchain-Plan.md):
# tools/license-audit must fail the build when something links a Qt module whose only
# open-source license is GPL-3.0 -- Qt Charts, Data Visualization, Virtual Keyboard.
# Qt Charts is INSTALLED in this machine's Qt prefix, so the trap is one
# find_package() line away rather than an install away.
#
# A hand-maintained list of "modules we use" cannot detect that, because the whole
# failure mode is somebody adding a module and not updating the list. Only the real
# link closure can. So: no target calls target_link_libraries(... Qt6::X) directly;
# they call recto_link_qt(), which records the module and then links it. Configure
# then writes qt-modules.txt for the audit to read.
#
# This lands at T2 rather than T7 on purpose -- the discipline has to exist before the
# FIRST Qt link, or the first few are already outside it.

include_guard(GLOBAL)

define_property(GLOBAL PROPERTY RECTO_QT_MODULES
    BRIEF_DOCS "Qt modules linked anywhere in this build"
    FULL_DOCS  "Accumulated by recto_link_qt(); written to qt-modules.txt for tools/license-audit.")

set_property(GLOBAL PROPERTY RECTO_QT_MODULES "")

# The nine modules of Tech-Stack-Plan.md 6.1. This list is a CONVENIENCE CHECK that
# fails early with a good message; it is NOT the enforcement point. Enforcement is
# tools/license-audit reading qt-modules.txt, because that also catches a module
# pulled in transitively or linked by a path that bypassed this function.
set(RECTO_QT_ALLOWED_MODULES
    Core Gui Widgets Network Svg PrintSupport Concurrent Multimedia LinguistTools)

function(recto_link_qt target)
    foreach(module IN LISTS ARGN)
        if(NOT module IN_LIST RECTO_QT_ALLOWED_MODULES)
            if(module STREQUAL "Charts" OR module STREQUAL "DataVisualization"
               OR module STREQUAL "VirtualKeyboard")
                message(FATAL_ERROR
                    "Target '${target}' links Qt6::${module}.\n"
                    "That module's only open-source license is GPL-3.0, which would "
                    "relicense Recto's own proprietary source. This is decision D4 in "
                    "Tech-Stack-Plan.md 2 and is release-gate tested. There is no "
                    "allowlist entry to add here -- the module cannot be used.")
            else()
                message(FATAL_ERROR
                    "Target '${target}' links Qt6::${module}, which is not among the "
                    "modules Tech-Stack-Plan.md 6.1 takes:\n"
                    "  ${RECTO_QT_ALLOWED_MODULES}\n"
                    "Adding a Qt module is a licensing decision. Record it in 6.1 first, "
                    "then extend RECTO_QT_ALLOWED_MODULES here.")
            endif()
        endif()

        set_property(GLOBAL APPEND PROPERTY RECTO_QT_MODULES ${module})

        # PUBLIC, not PRIVATE. core/'s public headers include <QString> -- Result carries a
        # QString context, Log builds QStrings, Units formats them -- so Qt's include path
        # is part of core's INTERFACE, not an implementation detail. With PRIVATE, anything
        # consuming core (the unit suite first, every other layer next) fails to find
        # QString, and the tempting repair is to re-link Qt in each consumer, which quietly
        # scatters the dependency the layer contract is trying to keep in one place.
        target_link_libraries(${target} PUBLIC Qt6::${module})
    endforeach()
endfunction()

# Call once, at the END of the top-level CMakeLists, after every target is defined --
# a global property read before that point sees a partial list, which would make the
# audit pass by accident.
function(recto_write_qt_module_list)
    get_property(modules GLOBAL PROPERTY RECTO_QT_MODULES)
    list(REMOVE_DUPLICATES modules)
    list(SORT modules)
    string(REPLACE ";" "\n" contents "${modules}")
    file(WRITE "${CMAKE_BINARY_DIR}/qt-modules.txt" "${contents}\n")
endfunction()
