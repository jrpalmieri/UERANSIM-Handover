cmake_minimum_required(VERSION 3.17)
project(ueransim)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
#set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(STATIC_BUILD "Build fully static executables for portability across Ubuntu versions" OFF)
option(BUILD_UNIT_TESTS "Build local C++ unit-test executables" ON)
option(USE_CCACHE "Route compilation through ccache when it is installed" ON)

#################### COMPILER CACHE ####################

# This tree compiles about 5,500 translation units, roughly 5,200 of them the
# generated ASN.1 sources under src/asn. Those sources change only when the
# definitions in asn1-definitions are regenerated, so on a normal rebuild almost
# every one of them is recompiled to a result that is byte-for-byte identical to
# the previous one. ccache turns each of those recompilations into a cache
# lookup. It is used only when installed, and the build behaves the same without
# it, so this stays enabled by default rather than being a developer opt-in.
#
# The launcher variables are read when a target is created, so this must come
# before the add_subdirectory calls below.
if(USE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        message(STATUS "ccache enabled: ${CCACHE_PROGRAM}")
    else()
        message(STATUS "ccache not found; compiling without a compiler cache")
    endif()
endif()

include_directories(src)

include(cmake/ExternalSgp4.cmake)

#################### SUB DIRECTORIES ####################

add_subdirectory(src/ext)
add_subdirectory(src/utils)
add_subdirectory(src/asn/asn1c)
add_subdirectory(src/asn/rrc)
add_subdirectory(src/asn/ngap)
add_subdirectory(src/asn/xnap)
add_subdirectory(src/lib)
add_subdirectory(src/gnb)
add_subdirectory(src/ue)

#################### GNB EXECUTABLE ####################

add_executable(nr-gnb src/gnb.cpp)

target_link_libraries(nr-gnb pthread)

target_compile_options(nr-gnb PRIVATE -Wall -Wextra -pedantic)

target_link_libraries(nr-gnb asn-rrc)
target_link_libraries(nr-gnb asn-ngap)
target_link_libraries(nr-gnb asn-xnap)
target_link_libraries(nr-gnb gnb)
target_link_libraries(nr-gnb common-lib)

if(STATIC_BUILD)
    target_link_options(nr-gnb PRIVATE -static)
endif()

#################### UE EXECUTABLE ####################

add_executable(nr-ue src/ue.cpp)
target_link_libraries(nr-ue pthread)
target_compile_options(nr-ue PRIVATE -Wall -Wextra -pedantic)

target_link_libraries(nr-ue ue)
target_link_libraries(nr-ue common-lib)

if(STATIC_BUILD)
    target_link_options(nr-ue PRIVATE -static)
endif()

###################### IF BINDER ######################
add_library(devbnd SHARED src/binder.cpp)
target_compile_options(devbnd PRIVATE -D_GNU_SOURCE -Wall -Wextra)
target_link_options(devbnd PRIVATE -nostartfiles)
target_link_libraries(devbnd dl)
if(STATIC_BUILD)
    target_link_options(devbnd PRIVATE -static-libgcc -static-libstdc++)
endif()

#################### CLI EXECUTABLE ####################
add_executable(nr-cli src/cli.cpp)
target_link_libraries(nr-cli pthread)
target_compile_options(nr-cli PRIVATE -Wall -Wextra -pedantic)

target_link_libraries(nr-cli common-lib)

if(STATIC_BUILD)
    target_link_options(nr-cli PRIVATE -static)
endif()

#################### UNIT TEST EXECUTABLES ####################

if(BUILD_UNIT_TESTS)
    add_executable(sat_time_tests tests/sat_time_tests.cpp)
    target_compile_options(sat_time_tests PRIVATE -Wall -Wextra -pedantic)
    target_link_libraries(sat_time_tests common-lib)

    add_executable(sat_lib_tests tests/sat_lib_tests.cpp)
    target_compile_options(sat_lib_tests PRIVATE -Wall -Wextra -pedantic)
    target_link_libraries(sat_lib_tests common-lib)
    target_compile_definitions(sat_lib_tests PRIVATE
        TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data"
    )

    add_executable(rrc_reference_location_tests tests/rrc_reference_location_tests.cpp)
    target_compile_options(rrc_reference_location_tests PRIVATE -Wall -Wextra -pedantic)
    target_link_libraries(rrc_reference_location_tests asn-rrc)
    target_link_libraries(rrc_reference_location_tests common-lib)

    # Round trip of the custom source-to-target handover container, which carries the
    # UE's RRC context between the two gNBs. See src/gnb/rrc/handover_container.hpp.
    add_executable(handover_container_tests tests/handover_container_tests.cpp)
    target_compile_options(handover_container_tests PRIVATE -Wall -Wextra -pedantic)
    target_link_libraries(handover_container_tests gnb)

    # Golden-vector harness: detects any change in ASN.1 codec behaviour when
    # the skeleton runtime is replaced. See docs/ASN1_R18_Migration_Plan.md.
    add_executable(asn1_golden tests/asn1_golden.cpp)
    target_compile_options(asn1_golden PRIVATE -Wall -Wextra -pedantic)
    target_link_libraries(asn1_golden asn-ngap)

    add_executable(rrc_d1_probe tools/rrc_d1_probe.cpp)
    target_compile_options(rrc_d1_probe PRIVATE -Wall -Wextra -pedantic)
    target_compile_definitions(rrc_d1_probe PRIVATE ASN_EMIT_DEBUG=1)
    target_link_libraries(rrc_d1_probe asn-rrc)
    target_link_libraries(rrc_d1_probe common-lib)
endif()
