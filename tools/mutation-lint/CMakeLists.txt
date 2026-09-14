add_executable(mutation-lint
    main.cpp
    MutationLint.cpp
)

target_include_directories(mutation-lint PRIVATE "${CMAKE_SOURCE_DIR}")

# No Qt, no vcpkg, nothing but the standard library -- decision D0-1, the same as the other three
# gates. It reads core/cmd/Commands.def and Catch2 tags AS TEXT (D3-12) precisely so that it does not
# need to compile or link the code it checks, and `checks` can run it before anything is built.
rawpage_strict(mutation-lint)
