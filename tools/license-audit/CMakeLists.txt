add_executable(license-audit
    main.cpp
    LicenseAudit.cpp
    Inventory.cpp
)

target_include_directories(license-audit PRIVATE "${CMAKE_SOURCE_DIR}")

# Standard library only -- decision D0-1. The CI `gates` job runs this on a bare Linux
# runner with no Qt and no vcpkg installed, which is what lets the licence gate fail in
# under a minute instead of behind a full toolchain provisioning step.
recto_strict(license-audit)
