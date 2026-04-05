/*
 * Rel-16 structure referenced by RRCReconfiguration-v1610-IEs.
 */

#include "OnDemandSIB-Request-r16.h"

/*
 * This type is implemented using NativeEnumerated,
 * so here we adjust the DEF accordingly.
 */
static asn_per_constraints_t asn_PER_type_onDemandSIB_RequestProhibitTimer_r16_constr_2 CC_NOTUSED = {
	{ APC_CONSTRAINED, 3, 3, 0, 7 },
	{ APC_UNCONSTRAINED, -1, -1, 0, 0 },
	0, 0
};
static const asn_INTEGER_enum_map_t asn_MAP_onDemandSIB_RequestProhibitTimer_r16_value2enum_2[] = {
	{ 0, 2, "s0" },
	{ 1, 6, "s0dot5" },
	{ 2, 2, "s1" },
	{ 3, 2, "s2" },
	{ 4, 2, "s5" },
	{ 5, 3, "s10" },
	{ 6, 3, "s20" },
	{ 7, 3, "s30" }
};
static const unsigned int asn_MAP_onDemandSIB_RequestProhibitTimer_r16_enum2value_2[] = {
	0, 1, 2, 5, 3, 6, 7, 4
};
static const asn_INTEGER_specifics_t asn_SPC_onDemandSIB_RequestProhibitTimer_r16_specs_2 = {
	asn_MAP_onDemandSIB_RequestProhibitTimer_r16_value2enum_2,
	asn_MAP_onDemandSIB_RequestProhibitTimer_r16_enum2value_2,
	8,
	0,
	1,
	0,
	0
};
static const ber_tlv_tag_t asn_DEF_onDemandSIB_RequestProhibitTimer_r16_tags_2[] = {
	(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
	(ASN_TAG_CLASS_UNIVERSAL | (10 << 2))
};
static asn_TYPE_descriptor_t asn_DEF_onDemandSIB_RequestProhibitTimer_r16_2 = {
	"onDemandSIB-RequestProhibitTimer-r16",
	"onDemandSIB-RequestProhibitTimer-r16",
	&asn_OP_NativeEnumerated,
	asn_DEF_onDemandSIB_RequestProhibitTimer_r16_tags_2,
	sizeof(asn_DEF_onDemandSIB_RequestProhibitTimer_r16_tags_2)
		/ sizeof(asn_DEF_onDemandSIB_RequestProhibitTimer_r16_tags_2[0]) - 1,
	asn_DEF_onDemandSIB_RequestProhibitTimer_r16_tags_2,
	sizeof(asn_DEF_onDemandSIB_RequestProhibitTimer_r16_tags_2)
		/ sizeof(asn_DEF_onDemandSIB_RequestProhibitTimer_r16_tags_2[0]),
	{ 0, &asn_PER_type_onDemandSIB_RequestProhibitTimer_r16_constr_2, NativeEnumerated_constraint },
	0, 0,
	&asn_SPC_onDemandSIB_RequestProhibitTimer_r16_specs_2
};

asn_TYPE_member_t asn_MBR_OnDemandSIB_Request_r16_1[] = {
	{ ATF_NOFLAGS, 0, offsetof(struct OnDemandSIB_Request_r16, onDemandSIB_RequestProhibitTimer_r16),
		(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
		-1,
		&asn_DEF_onDemandSIB_RequestProhibitTimer_r16_2,
		0,
		{ 0, 0, 0 },
		0, 0,
		"onDemandSIB-RequestProhibitTimer-r16" },
};
static const ber_tlv_tag_t asn_DEF_OnDemandSIB_Request_r16_tags_1[] = {
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_OnDemandSIB_Request_r16_tag2el_1[] = {
	{ (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 }
};
asn_SEQUENCE_specifics_t asn_SPC_OnDemandSIB_Request_r16_specs_1 = {
	sizeof(struct OnDemandSIB_Request_r16),
	offsetof(struct OnDemandSIB_Request_r16, _asn_ctx),
	asn_MAP_OnDemandSIB_Request_r16_tag2el_1,
	1,
	0, 0, 0,
	-1
};
asn_TYPE_descriptor_t asn_DEF_OnDemandSIB_Request_r16 = {
	"OnDemandSIB-Request-r16",
	"OnDemandSIB-Request-r16",
	&asn_OP_SEQUENCE,
	asn_DEF_OnDemandSIB_Request_r16_tags_1,
	sizeof(asn_DEF_OnDemandSIB_Request_r16_tags_1) / sizeof(asn_DEF_OnDemandSIB_Request_r16_tags_1[0]),
	asn_DEF_OnDemandSIB_Request_r16_tags_1,
	sizeof(asn_DEF_OnDemandSIB_Request_r16_tags_1) / sizeof(asn_DEF_OnDemandSIB_Request_r16_tags_1[0]),
	{ 0, 0, SEQUENCE_constraint },
	asn_MBR_OnDemandSIB_Request_r16_1,
	1,
	&asn_SPC_OnDemandSIB_Request_r16_specs_1
};

