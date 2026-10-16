# CheckMultimediaDeploy.cmake -- Stage 6 guide case A-07 (D6-18, Q5): audio deploys Qt Multimedia's MEDIA FOUNDATION
# backend and NOTHING of FFmpeg.
#
#   cmake -DBIN=<build>/bin -DOBJDUMP=<objdump> -DMODULES=<build>/qt-modules.txt -DFFMPEG_PLUGIN=<qt>/plugins/multimedia/ffmpegmediaplugin.dll
#         -DMODE=check|control -DSCRATCH=<dir> -P CheckMultimediaDeploy.cmake
#
# MODE=check, over the build's own output directory:
#   * Qt6Multimedia.dll and multimedia/windowsmediaplugin.dll are beside the executable;
#   * NO ffmpegmediaplugin.dll, avcodec-*, avformat-*, avutil-*, swresample-* or swscale-* anywhere under it;
#   * rawpage.exe IMPORTS Qt6Multimedia.dll, and rawpage_tests.exe -- core/, pdf/cos/ and pdf/ops/ with no display --
#     does NOT: Qt Multimedia is ui/'s alone;
#   * qt-modules.txt, what tools/license-audit audits, names Multimedia.
# MODE=control: the deployment's two files copied to SCRATCH with the FFmpeg plugin PLANTED beside them -- the check
# must FAIL there, for that reason and no other (convention 7). A control that passed would mean the check reads nothing.
#
# A script rather than a C++ case because what it judges is the DEPLOYMENT -- files on disk and a PE import table --
# which no test binary links. It runs in the x64 presets (it needs a build), registered by tests/ui/CMakeLists.txt.

cmake_minimum_required(VERSION 3.25)

function(deployment_problems dir check_imports out)
    set(problems "")
    foreach(required IN ITEMS "Qt6Multimedia.dll" "multimedia/windowsmediaplugin.dll")
        if(NOT EXISTS "${dir}/${required}")
            list(APPEND problems "missing ${required}")
        endif()
    endforeach()
    file(GLOB_RECURSE forbidden LIST_DIRECTORIES false
         "${dir}/ffmpegmediaplugin*.dll" "${dir}/avcodec-*.dll" "${dir}/avformat-*.dll" "${dir}/avutil-*.dll"
         "${dir}/swresample-*.dll" "${dir}/swscale-*.dll")
    foreach(f IN LISTS forbidden)
        list(APPEND problems "FFmpeg deployed: ${f}")
    endforeach()
    if(check_imports)
        execute_process(COMMAND "${OBJDUMP}" -p "${dir}/rawpage.exe" OUTPUT_VARIABLE product RESULT_VARIABLE r1)
        execute_process(COMMAND "${OBJDUMP}" -p "${dir}/rawpage_tests.exe" OUTPUT_VARIABLE headless RESULT_VARIABLE r2)
        if(NOT r1 EQUAL 0 OR NOT r2 EQUAL 0)
            list(APPEND problems "objdump failed (${r1}, ${r2})")
        endif()
        if(NOT product MATCHES "DLL Name: Qt6Multimedia\\.dll")
            list(APPEND problems "rawpage.exe does not import Qt6Multimedia.dll")
        endif()
        if(headless MATCHES "Qt6Multimedia")
            list(APPEND problems "rawpage_tests.exe imports Qt Multimedia -- core/ or pdf/ links it")
        endif()
        if(product MATCHES "avcodec|avformat|ffmpeg")
            list(APPEND problems "rawpage.exe imports FFmpeg")
        endif()
        file(STRINGS "${MODULES}" modules)
        if(NOT "Multimedia" IN_LIST modules)
            list(APPEND problems "qt-modules.txt does not name Multimedia")
        endif()
    endif()
    set(${out} "${problems}" PARENT_SCOPE)
endfunction()

if(MODE STREQUAL "check")
    deployment_problems("${BIN}" TRUE problems)
    if(problems)
        string(REPLACE ";" "\n  " text "${problems}")
        message(FATAL_ERROR "A-07: the deployment is not Media Foundation alone:\n  ${text}")
    endif()
    message(STATUS "A-07: Qt6Multimedia.dll and multimedia/windowsmediaplugin.dll deployed, nothing of FFmpeg; "
                   "rawpage.exe imports Qt Multimedia and rawpage_tests.exe does not; qt-modules.txt names it")
elseif(MODE STREQUAL "control")
    file(REMOVE_RECURSE "${SCRATCH}")
    file(MAKE_DIRECTORY "${SCRATCH}/multimedia")
    file(COPY "${BIN}/Qt6Multimedia.dll" DESTINATION "${SCRATCH}")
    file(COPY "${BIN}/multimedia/windowsmediaplugin.dll" DESTINATION "${SCRATCH}/multimedia")
    deployment_problems("${SCRATCH}" FALSE clean)
    if(clean)
        message(FATAL_ERROR "A-07 control: the copied deployment should be clean before the plant, and is not: ${clean}")
    endif()
    file(COPY "${FFMPEG_PLUGIN}" DESTINATION "${SCRATCH}/multimedia")   # THE PLANT
    deployment_problems("${SCRATCH}" FALSE planted)
    file(REMOVE_RECURSE "${SCRATCH}")
    if(NOT planted MATCHES "FFmpeg deployed: .*ffmpegmediaplugin\\.dll")
        message(FATAL_ERROR "A-07 control: ffmpegmediaplugin.dll planted in the deployment and the check did not see it")
    endif()
    message(STATUS "A-07 control: the planted ffmpegmediaplugin.dll is seen -- ${planted}")
else()
    message(FATAL_ERROR "MODE must be check or control")
endif()
