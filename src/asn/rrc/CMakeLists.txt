file(GLOB HDR_FILES CONFIGURE_DEPENDS *.h)
file(GLOB SRC_FILES CONFIGURE_DEPENDS *.c)

add_library(asn-rrc ${HDR_FILES} ${SRC_FILES})
target_link_libraries(asn-rrc asn-asn1c)
