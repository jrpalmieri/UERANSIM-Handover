include(ExternalProject)

option(ENABLE_EXTERNAL_SGP4 "Build and link SGP4 from an external repository" ON)

set(SGP4_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../sgp4" CACHE PATH
    "Path to the SGP4 source repository")
set(SGP4_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/sgp4-build" CACHE PATH
    "Build directory used for SGP4")
set(SGP4_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/sgp4-install" CACHE PATH
    "Install prefix used for SGP4")
set(SGP4_LIBRARY_NAME "sgp4" CACHE STRING
    "Static SGP4 library basename, e.g. sgp4 for libsgp4.a")

if(NOT ENABLE_EXTERNAL_SGP4)
    return()
endif()

if(NOT EXISTS "${SGP4_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
            "SGP4 source not found at ${SGP4_SOURCE_DIR}. Set -DSGP4_SOURCE_DIR=/path/to/sgp4 or disable "
            "with -DENABLE_EXTERNAL_SGP4=OFF")
endif()

ExternalProject_Add(sgp4_external
    SOURCE_DIR "${SGP4_SOURCE_DIR}"
    BINARY_DIR "${SGP4_BINARY_DIR}"
    INSTALL_DIR "${SGP4_INSTALL_DIR}"
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${SGP4_INSTALL_DIR}
    INSTALL_COMMAND
        ${CMAKE_COMMAND} --install <BINARY_DIR>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        <BINARY_DIR>/libsgp4/lib${SGP4_LIBRARY_NAME}.a
        ${SGP4_INSTALL_DIR}/lib/lib${SGP4_LIBRARY_NAME}.a
    BUILD_BYPRODUCTS "${SGP4_INSTALL_DIR}/lib/lib${SGP4_LIBRARY_NAME}.a"
)

# Imported target include/library paths must exist during configure.
file(MAKE_DIRECTORY "${SGP4_INSTALL_DIR}/include")
file(MAKE_DIRECTORY "${SGP4_INSTALL_DIR}/lib")

add_library(sgp4 STATIC IMPORTED GLOBAL)
add_dependencies(sgp4 sgp4_external)

set_target_properties(sgp4 PROPERTIES
    IMPORTED_LOCATION "${SGP4_INSTALL_DIR}/lib/lib${SGP4_LIBRARY_NAME}.a"
    INTERFACE_INCLUDE_DIRECTORIES "${SGP4_INSTALL_DIR}/include"
)
