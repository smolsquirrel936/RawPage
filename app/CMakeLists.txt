# app/ -- the composition root.
#
# The ONLY place that names a concrete backend or a concrete platform implementation.
# It creates every concrete, injects interfaces downward, and holds no domain logic.
# That is why it is the one target linking all four layers: nobody else may.

add_executable(recto
    main.cpp
)

target_link_libraries(recto PRIVATE
    recto_core
    recto_pdf
    recto_platform
    recto_ui
)

recto_app_flags(recto)            # -mwindows; -municode arrives with Stage 1's entry point

set_target_properties(recto PROPERTIES OUTPUT_NAME "recto")
