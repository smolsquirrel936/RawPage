# app/ -- the composition root.
#
# The ONLY place that names a concrete backend or a concrete platform implementation.
# It creates every concrete, injects interfaces downward, and holds no domain logic.
# That is why it is the one target linking all four layers: nobody else may.

add_executable(rawpage
    main.cpp
)

target_link_libraries(rawpage PRIVATE
    rawpage_core
    rawpage_pdf
    rawpage_platform
    rawpage_ui
)

rawpage_app_flags(rawpage)            # -mwindows; -municode arrives with Stage 1's entry point

set_target_properties(rawpage PROPERTIES OUTPUT_NAME "rawpage")
