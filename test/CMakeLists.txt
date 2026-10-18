if(SPH_ZSTDPP_BUILD_TESTS)
    add_executable(eval_statistics_tests eval_statistics_tests.cpp)
    target_include_directories(eval_statistics_tests PRIVATE "${PROJECT_SOURCE_DIR}/eval")
    target_compile_features(eval_statistics_tests PRIVATE cxx_std_23)
    sph_zstdpp_enable_strict_warnings(eval_statistics_tests)
    add_test(NAME eval_statistics_tests COMMAND eval_statistics_tests)

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

    add_executable(compact_sequence_tests compact_sequence_tests.cpp)
    target_link_libraries(compact_sequence_tests PRIVATE sph::zstdpp)
    target_compile_features(compact_sequence_tests PRIVATE cxx_std_23)
    sph_zstdpp_enable_strict_warnings(compact_sequence_tests)
    add_test(NAME compact_sequence_tests COMMAND compact_sequence_tests)

    add_executable(fse_transition_tests fse_transition_tests.cpp)
    target_link_libraries(fse_transition_tests PRIVATE sph::zstdpp)
    target_compile_features(fse_transition_tests PRIVATE cxx_std_23)
    sph_zstdpp_enable_strict_warnings(fse_transition_tests)
    add_test(NAME fse_transition_tests COMMAND fse_transition_tests)

    add_executable(literal_sampling_tests literal_sampling_tests.cpp)
    target_link_libraries(literal_sampling_tests PRIVATE sph::zstdpp libzstd_static)
    target_include_directories(literal_sampling_tests SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/reference_zstd/lib/common")
    target_compile_features(literal_sampling_tests PRIVATE cxx_std_23)
    sph_zstdpp_enable_strict_warnings(literal_sampling_tests)
    add_test(NAME literal_sampling_tests COMMAND literal_sampling_tests)

    add_executable(fast_literal_storage_tests fast_literal_storage_tests.cpp)
    target_link_libraries(fast_literal_storage_tests PRIVATE sph::zstdpp libzstd_static)
    target_compile_features(fast_literal_storage_tests PRIVATE cxx_std_23)
    sph_zstdpp_enable_strict_warnings(fast_literal_storage_tests)
    add_test(NAME fast_literal_storage_tests COMMAND fast_literal_storage_tests)

    add_executable(fast_match_selection_tests fast_match_selection_tests.cpp)
    target_link_libraries(fast_match_selection_tests PRIVATE sph::zstdpp)
    target_compile_features(fast_match_selection_tests PRIVATE cxx_std_23)
    sph_zstdpp_enable_strict_warnings(fast_match_selection_tests)
    add_test(NAME fast_match_selection_tests COMMAND fast_match_selection_tests)

    add_executable(fast_index_epoch_tests fast_index_epoch_tests.cpp)
    target_link_libraries(fast_index_epoch_tests PRIVATE sph::zstdpp)
    target_compile_features(fast_index_epoch_tests PRIVATE cxx_std_23)
    sph_zstdpp_enable_strict_warnings(fast_index_epoch_tests)
    add_test(NAME fast_index_epoch_tests COMMAND fast_index_epoch_tests)

    add_executable(match_index_epoch_tests match_index_epoch_tests.cpp)
    target_link_libraries(match_index_epoch_tests PRIVATE sph::zstdpp)
    target_compile_features(match_index_epoch_tests PRIVATE cxx_std_23)
    sph_zstdpp_enable_strict_warnings(match_index_epoch_tests)
    add_test(NAME match_index_epoch_tests COMMAND match_index_epoch_tests)
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
