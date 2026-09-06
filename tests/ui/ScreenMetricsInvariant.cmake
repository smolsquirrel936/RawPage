# The physical DPI `metricsFor()` resolves must NOT move when only the OS scale factor moves.
# Guide case A-84; issue I-20.
#
# WHY THIS IS A SCRIPT AND NOT A CATCH2 CASE. `QT_SCALE_FACTOR` is read once, when the
# `QGuiApplication` is constructed, so a scale factor is a property of a PROCESS and cannot be
# varied inside one. The assertion needs two processes differing in exactly that one input, which
# is what this script arranges.
#
# WHY THE INVARIANT IS THE RIGHT ASSERTION rather than a fixed number. The offscreen platform's
# nominal DPI is Qt's business and may move with a Qt upgrade; what must never move is that *the
# dots per inch of glass* does not depend on how many device pixels Windows wants per logical
# pixel. Those are two different questions -- `core/render/DpiPolicy`'s header says so at length --
# and the defect this case exists for was exactly the two collapsed into one.
#
# THE CONTROL IS BUILT IN (convention 7): the run at 1.5 must actually REPORT a device pixel ratio
# of 1.5, or the case would pass by having failed to vary its own input -- which is the shape of
# Stage 1 T3's two probes that asserted nothing.
#
# CMake has no floating-point arithmetic, so the comparison is done on the integer part with a
# whole-percent tolerance. That is ample: the rounding this must forgive is 100 against 99.9375,
# and the defect it must catch is a factor of 1.5.

if(NOT DEFINED RAWPAGE_EXE)
    message(FATAL_ERROR "RAWPAGE_EXE was not passed to this script")
endif()

function(screen_metrics_run scale out_dpi out_dpr)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env
                "QT_QPA_PLATFORM=offscreen"
                "QT_SCALE_FACTOR=${scale}"
                "${RAWPAGE_EXE}" --print-screen-metrics
        OUTPUT_VARIABLE output
        ERROR_VARIABLE errors
        RESULT_VARIABLE code
    )
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "--print-screen-metrics exited ${code} at scale ${scale}:\n${errors}")
    endif()

    # `screen <name> dpr <r> qtPhysDpi <q> -> scale <s> physDpi <p> source <src>`
    if(NOT output MATCHES "screen[^\n]*dpr ([0-9.]+)[^\n]*physDpi ([0-9]+)")
        message(FATAL_ERROR "no screen row in the output at scale ${scale}:\n${output}")
    endif()
    set(${out_dpr} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    set(${out_dpi} "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()

screen_metrics_run(1 dpi_one dpr_one)
screen_metrics_run(1.5 dpi_scaled dpr_scaled)

# The control, and it comes first: if the second process did not actually run at a different
# device pixel ratio then the assertion below is comparing a run with itself.
if(dpr_one STREQUAL dpr_scaled)
    message(FATAL_ERROR
        "QT_SCALE_FACTOR did not change the device pixel ratio -- both runs report ${dpr_one}. "
        "This case varied nothing and proves nothing.")
endif()

if(dpi_one LESS_EQUAL 0 OR dpi_scaled LESS_EQUAL 0)
    message(FATAL_ERROR "a resolved physical DPI of ${dpi_one}/${dpi_scaled} is not a measurement")
endif()

if(dpi_scaled GREATER dpi_one)
    math(EXPR spread "(${dpi_scaled} - ${dpi_one}) * 100 / ${dpi_one}")
else()
    math(EXPR spread "(${dpi_one} - ${dpi_scaled}) * 100 / ${dpi_scaled}")
endif()

if(spread GREATER 2)
    message(FATAL_ERROR
        "the resolved physical DPI moved with the scale factor: ${dpi_one} dpi at dpr ${dpr_one}, "
        "${dpi_scaled} dpi at dpr ${dpr_scaled} -- ${spread} % apart. Actual Size is then wrong by "
        "the scale factor on every scaled monitor, which is issue I-20.")
endif()

message(STATUS
    "screen metrics invariant: ${dpi_one} dpi at dpr ${dpr_one}, ${dpi_scaled} dpi at dpr ${dpr_scaled}")
