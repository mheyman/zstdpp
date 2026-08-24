add_executable(unit_tests unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE sph::zstdpp libzstd_static)
target_compile_features(unit_tests PRIVATE cxx_std_23)
target_compile_definitions(
    unit_tests
    PRIVATE
        SPH_ZSTDPP_REFERENCE_TEST_DIR="${PROJECT_SOURCE_DIR}/reference_zstd/tests"
)

sph_zstdpp_enable_strict_warnings(unit_tests)

add_test(NAME unit_tests COMMAND unit_tests)
