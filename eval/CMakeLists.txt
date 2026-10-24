set(SPH_ZSTDPP_EVAL_LEVELS "1;3;5;7;8;9;15" CACHE STRING
    "Positive compression levels to build and benchmark (1 through 22)")
if("${SPH_ZSTDPP_EVAL_LEVELS}" STREQUAL "")
    message(FATAL_ERROR "SPH_ZSTDPP_EVAL_LEVELS must not be empty")
endif()
foreach(level IN LISTS SPH_ZSTDPP_EVAL_LEVELS)
    if(NOT level MATCHES "^[1-9][0-9]*$" OR level GREATER 22)
        message(FATAL_ERROR "Invalid evaluation level '${level}': expected 1 through 22")
    endif()
endforeach()
list(REMOVE_DUPLICATES SPH_ZSTDPP_EVAL_LEVELS)
list(JOIN SPH_ZSTDPP_EVAL_LEVELS "," SPH_ZSTDPP_EVAL_LEVELS_CPP)
set(SPH_ZSTDPP_EVAL_CORPUS_SIZE 1048576 CACHE STRING
    "Input size in bytes for the templated evaluation workers")
if(NOT SPH_ZSTDPP_EVAL_CORPUS_SIZE MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "SPH_ZSTDPP_EVAL_CORPUS_SIZE must be a positive integer")
endif()
set(SPH_ZSTDPP_EVAL_RUNTIME_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

function(sph_zstdpp_add_eval_worker target source)
    add_executable(${target} "${source}")
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${SPH_ZSTDPP_EVAL_RUNTIME_DIRECTORY}"
    )
    sph_zstdpp_enable_strict_warnings(${target})
    # The evaluation compares optimized implementations.  Header-only template
    # workers benefit from MSVC's most aggressive inlining mode; apply it to every
    # worker so the reference and candidate remain on the same compiler settings.
    target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/Ob3>)
    if(WIN32)
        target_link_libraries(${target} PRIVATE Psapi)
    endif()
endfunction()

set(eval_workers)
foreach(level IN LISTS SPH_ZSTDPP_EVAL_LEVELS)
    set(sph_compress_target "sph_compress_l${level}")
    sph_zstdpp_add_eval_worker(${sph_compress_target} sph_compress_worker.cpp)
    target_link_libraries(${sph_compress_target} PRIVATE sph::zstdpp)
    target_compile_definitions(${sph_compress_target} PRIVATE
        SPH_EVAL_LEVEL=${level}
        SPH_EVAL_CORPUS_SIZE=${SPH_ZSTDPP_EVAL_CORPUS_SIZE}
    )
    if(SPH_ZSTDPP_TRACE_BT AND (level EQUAL 15 OR level EQUAL 16))
        target_compile_definitions(${sph_compress_target} PRIVATE SPH_ZSTDPP_TRACE_BT=1)
        target_compile_definitions(libzstd_static PRIVATE SPH_ZSTDPP_TRACE_BT=1)
    endif()
    if(SPH_ZSTDPP_TRACE_OPTIMAL AND level GREATER_EQUAL 16)
        target_compile_definitions(${sph_compress_target} PRIVATE SPH_ZSTDPP_TRACE_OPTIMAL=1)
        target_compile_definitions(libzstd_static PRIVATE SPH_ZSTDPP_TRACE_OPTIMAL=1)
    endif()
    if(SPH_ZSTDPP_TRACE_PHASES AND level EQUAL 15)
        target_compile_definitions(${sph_compress_target} PRIVATE SPH_ZSTDPP_TRACE_PHASES=1)
    endif()

    set(reference_compress_target "reference_compress_l${level}")
    sph_zstdpp_add_eval_worker(${reference_compress_target} reference_compress_worker.cpp)
    target_link_libraries(${reference_compress_target} PRIVATE libzstd_static)
    target_compile_definitions(${reference_compress_target} PRIVATE SPH_EVAL_LEVEL=${level})
    if(SPH_ZSTDPP_TRACE_BT AND (level EQUAL 15 OR level EQUAL 16))
        target_compile_definitions(${reference_compress_target} PRIVATE SPH_ZSTDPP_TRACE_BT=1)
    endif()
    if(SPH_ZSTDPP_TRACE_OPTIMAL AND level GREATER_EQUAL 16)
        target_compile_definitions(${reference_compress_target} PRIVATE SPH_ZSTDPP_TRACE_OPTIMAL=1)
    endif()

    set(sph_decompress_target "sph_decompress_l${level}")
    sph_zstdpp_add_eval_worker(${sph_decompress_target} sph_decompress_worker.cpp)
    target_link_libraries(${sph_decompress_target} PRIVATE sph::zstdpp)

    set(reference_decompress_target "reference_decompress_l${level}")
    sph_zstdpp_add_eval_worker(${reference_decompress_target} reference_decompress_worker.cpp)
    target_link_libraries(${reference_decompress_target} PRIVATE libzstd_static)

    list(APPEND eval_workers
        ${sph_compress_target}
        ${reference_compress_target}
        ${sph_decompress_target}
        ${reference_decompress_target}
    )
endforeach()

add_executable(zstdpp_eval eval_controller.cpp)
target_compile_features(zstdpp_eval PRIVATE cxx_std_23)
target_include_directories(zstdpp_eval PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(zstdpp_eval PRIVATE
    SPH_EVAL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    SPH_EVAL_BINARY_DIR="${CMAKE_BINARY_DIR}"
    SPH_EVAL_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    SPH_EVAL_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    SPH_EVAL_CORPUS_SIZE=${SPH_ZSTDPP_EVAL_CORPUS_SIZE}
    SPH_EVAL_LEVELS=${SPH_ZSTDPP_EVAL_LEVELS_CPP}
)
set_target_properties(zstdpp_eval PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${SPH_ZSTDPP_EVAL_RUNTIME_DIRECTORY}"
)
sph_zstdpp_enable_strict_warnings(zstdpp_eval)
add_dependencies(zstdpp_eval ${eval_workers})

add_executable(zstdpp_pair pair_controller.cpp)
target_compile_features(zstdpp_pair PRIVATE cxx_std_23)
target_include_directories(zstdpp_pair PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(zstdpp_pair PRIVATE
    SPH_EVAL_BINARY_DIR="${CMAKE_BINARY_DIR}"
    SPH_EVAL_CORPUS_SIZE=${SPH_ZSTDPP_EVAL_CORPUS_SIZE}
)
set_target_properties(zstdpp_pair PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${SPH_ZSTDPP_EVAL_RUNTIME_DIRECTORY}"
)
sph_zstdpp_enable_strict_warnings(zstdpp_pair)
add_dependencies(zstdpp_pair ${eval_workers})
if(BUILD_TESTING AND CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    list(GET SPH_ZSTDPP_EVAL_LEVELS 0 SPH_ZSTDPP_EVAL_SMOKE_LEVEL)
    add_test(NAME paired_benchmark_smoke
        COMMAND $<TARGET_FILE:zstdpp_pair> --level ${SPH_ZSTDPP_EVAL_SMOKE_LEVEL}
            --iterations 1 --trials 2)
    set_tests_properties(paired_benchmark_smoke PROPERTIES LABELS eval)
endif()

add_custom_target(run_eval
    COMMAND zstdpp_eval
    DEPENDS zstdpp_eval
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Benchmarking sph-zstd++ against reference zstd and updating README.md"
)
