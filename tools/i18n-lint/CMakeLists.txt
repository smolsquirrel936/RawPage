add_executable(i18n-lint
    main.cpp
    I18nLint.cpp
    I18nRules.cpp
    Catalog.cpp
    SourceScan.cpp
)

target_include_directories(i18n-lint PRIVATE "${CMAKE_SOURCE_DIR}")

# No Qt, no vcpkg, nothing but the standard library -- decision D0-1, the same as the other two
# gates. This is what lets `checks` build and run in seconds on the bare Linux `gates` runner, and
# what makes "CI runs checks BEFORE compiling" real. If a link line ever appears here, that
# guarantee is gone -- and note that this gate reads Qt Linguist .ts files WITHOUT Qt, on purpose.
rawpage_strict(i18n-lint)
