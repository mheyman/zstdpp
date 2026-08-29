cmake_minimum_required(VERSION 3.28)

project(sph-zstdpp VERSION 0.1.0 LANGUAGES CXX)

option(SPH_ZSTDPP_BUILD_TESTS "Build sph-zstd++ tests" ${PROJECT_IS_TOP_LEVEL})
option(SPH_ZSTDPP_BUILD_ACCEPTANCE_TESTS "Build reference-parity acceptance tests" OFF)
option(SPH_ZSTDPP_BUILD_REFERENCE "Make the downloaded reference zstd target available" OFF)
option(SPH_ZSTDPP_BUILD_EVAL "Build implementation-comparison executables" OFF)

include(GNUInstallDirs)

add_library(sph-zstdpp INTERFACE)
add_library(sph::zstdpp ALIAS sph-zstdpp)
target_compile_features(sph-zstdpp INTERFACE cxx_std_23)
target_include_directories(
    sph-zstdpp
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
set_target_properties(sph-zstdpp PROPERTIES EXPORT_NAME zstdpp)

function(sph_zstdpp_enable_strict_warnings target)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE
            /W4 /WX /permissive- /utf-8 /EHsc
            /w14242 /w14254 /w14263 /w14265 /w14287 /w14296
            /w14311 /w14545 /w14546 /w14547 /w14549 /w14555
            /w14619 /w14640 /w14826 /w14905 /w14906 /w14928
        )
    else()
        target_compile_options(${target} PRIVATE
            -Werror -Wall -Wextra -Wpedantic -Wshadow
            -Wconversion -Wsign-conversion -Wnull-dereference
            -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough
        )
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target} PRIVATE
                -Wduplicated-cond -Wduplicated-branches -Wlogical-op
                -Wuseless-cast -Wmisleading-indentation
            )
        endif()
    endif()
endfunction()

if(SPH_ZSTDPP_BUILD_REFERENCE OR SPH_ZSTDPP_BUILD_TESTS OR
    SPH_ZSTDPP_BUILD_ACCEPTANCE_TESTS OR SPH_ZSTDPP_BUILD_EVAL)
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_DICTBUILDER OFF CACHE BOOL "" FORCE)
    set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
    add_subdirectory(reference_zstd/build/cmake reference-zstd EXCLUDE_FROM_ALL)
endif()

if(SPH_ZSTDPP_BUILD_TESTS OR SPH_ZSTDPP_BUILD_ACCEPTANCE_TESTS)
    include(CTest)
    add_subdirectory(test)
endif()

if(SPH_ZSTDPP_BUILD_EVAL)
    add_subdirectory(eval)
endif()

install(TARGETS sph-zstdpp EXPORT sph-zstdpp-targets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(
    EXPORT sph-zstdpp-targets
    FILE sph-zstdpp-config.cmake
    NAMESPACE sph::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/sph-zstdpp"
)
