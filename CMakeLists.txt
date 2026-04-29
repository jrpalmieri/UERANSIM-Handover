cmake_minimum_required(VERSION 3.17)
project(ueransim)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
#set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(STATIC_BUILD "Build fully static executables for portability across Ubuntu versions" OFF)
option(BUILD_UNIT_TESTS "Build local C++ unit-test executables" ON)

include_directories(src)

include(cmake/ExternalSgp4.cmake)

#################### SUB DIRECTORIES ####################

add_subdirectory(src/ext)
add_subdirectory(src/utils)
add_subdirectory(src/asn/asn1c)
add_subdirectory(src/asn/rrc)
add_subdirectory(src/asn/ngap)
add_subdirectory(src/lib)
add_subdirectory(src/gnb)
add_subdirectory(src/ue)

#################### GNB EXECUTABLE ####################

add_executable(nr-gnb src/gnb.cpp)

target_link_libraries(nr-gnb pthread)

target_compile_options(nr-gnb PRIVATE -Wall -Wextra -pedantic)

target_link_libraries(nr-gnb asn-rrc)
target_link_libraries(nr-gnb asn-ngap)
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

    add_executable(rrc_d1_probe tools/rrc_d1_probe.cpp)
    target_compile_options(rrc_d1_probe PRIVATE -Wall -Wextra -pedantic)
    target_compile_definitions(rrc_d1_probe PRIVATE ASN_EMIT_DEBUG=1)
    target_link_libraries(rrc_d1_probe asn-rrc)
    target_link_libraries(rrc_d1_probe common-lib)
endif()
