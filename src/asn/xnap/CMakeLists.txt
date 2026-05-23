file(GLOB HDR_FILES *.h)
file(GLOB SRC_FILES ASN_XNAP_*.c)

add_library(asn-xnap ${HDR_FILES} ${SRC_FILES})
target_include_directories(asn-xnap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(asn-xnap PUBLIC -DASN_DISABLE_OER_SUPPORT)
target_link_libraries(asn-xnap asn-asn1c)
