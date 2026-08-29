if(SPH_ZSTDPP_BUILD_TESTS)
    add_executable(unit_tests unit_tests.cpp)
    target_link_libraries(unit_tests PRIVATE sph::zstdpp libzstd_static)
    target_include_directories(unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/eval")
    target_compile_features(unit_tests PRIVATE cxx_std_23)
    target_compile_definitions(
        unit_tests
        PRIVATE
            SPH_ZSTDPP_REFERENCE_TEST_DIR="${PROJECT_SOURCE_DIR}/reference_zstd/tests"
    )

    sph_zstdpp_enable_strict_warnings(unit_tests)
    add_test(NAME unit_tests COMMAND unit_tests)
endif()

if(SPH_ZSTDPP_BUILD_ACCEPTANCE_TESTS)
    add_executable(compression_acceptance compression_acceptance.cpp)
    target_link_libraries(compression_acceptance PRIVATE sph::zstdpp libzstd_static)
    target_include_directories(compression_acceptance PRIVATE "${PROJECT_SOURCE_DIR}/eval")
    target_compile_features(compression_acceptance PRIVATE cxx_std_23)
    sph_zstdpp_enable_strict_warnings(compression_acceptance)

    add_test(NAME compression_parity COMMAND compression_acceptance)
    set_tests_properties(compression_parity PROPERTIES LABELS acceptance)
endif()
