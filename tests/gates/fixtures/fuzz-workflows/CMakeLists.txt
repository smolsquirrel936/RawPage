# Fixture for gates.fuzz_workflows.* (tests/gates/CheckFuzzWorkflows.cmake): two targets, read as text, never configured.
function(rawpage_add_fuzz_target name source)
endfunction()
rawpage_add_fuzz_target(fuzz_alpha fuzz_alpha.cpp)
rawpage_add_fuzz_target(fuzz_beta fuzz_beta.cpp rawpage_ops)
# rawpage_add_fuzz_target(fuzz_gamma fuzz_gamma.cpp) -- a comment, not a target
