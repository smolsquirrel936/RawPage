# A helper beside the fuzz targets, as tests/fuzz/seeds/ is beside tests/fuzz/: its own directory, so the
# count in the parent does not include it.
add_custom_target(fixture_seed_writer)
