set(SPH_ZSTDPP_EVAL_LEVELS 1 3 5 9 15)
set(SPH_ZSTDPP_EVAL_CORPUS_SIZE 1048576)
set(SPH_ZSTDPP_EVAL_RUNTIME_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

function(sph_zstdpp_add_eval_worker target source)
    add_executable(${target} "${source}")
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${SPH_ZSTDPP_EVAL_RUNTIME_DIRECTORY}"
    )
    sph_zstdpp_enable_strict_warnings(${target})
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

    set(reference_compress_target "reference_compress_l${level}")
    sph_zstdpp_add_eval_worker(${reference_compress_target} reference_compress_worker.cpp)
    target_link_libraries(${reference_compress_target} PRIVATE libzstd_static)
    target_compile_definitions(${reference_compress_target} PRIVATE SPH_EVAL_LEVEL=${level})

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
)
set_target_properties(zstdpp_eval PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${SPH_ZSTDPP_EVAL_RUNTIME_DIRECTORY}"
)
sph_zstdpp_enable_strict_warnings(zstdpp_eval)
add_dependencies(zstdpp_eval ${eval_workers})

add_custom_target(run_eval
    COMMAND zstdpp_eval
    DEPENDS zstdpp_eval
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Benchmarking sph-zstd++ against reference zstd and updating README.md"
)
