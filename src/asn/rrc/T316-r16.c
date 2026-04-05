/*
 * Rel-16 timer type used by RRCReconfiguration-v1610-IEs SetupRelease wrappers.
 */

#include "T316-r16.h"

asn_per_constraints_t asn_PER_type_T316_r16_constr_1 CC_NOTUSED = {
	{ APC_CONSTRAINED, 4, 4, 0, 9 },
	{ APC_UNCONSTRAINED, -1, -1, 0, 0 },
	0, 0
};
static const asn_INTEGER_enum_map_t asn_MAP_T316_r16_value2enum_1[] = {
	{ 0, 4, "ms50" },
	{ 1, 5, "ms100" },
	{ 2, 5, "ms200" },
	{ 3, 5, "ms300" },
	{ 4, 5, "ms400" },
	{ 5, 5, "ms500" },
	{ 6, 5, "ms600" },
	{ 7, 6, "ms1000" },
	{ 8, 6, "ms1500" },
	{ 9, 6, "ms2000" }
};
static const unsigned int asn_MAP_T316_r16_enum2value_1[] = {
	1, 7, 8, 2, 9, 3, 4, 5, 6, 0
};
const asn_INTEGER_specifics_t asn_SPC_T316_r16_specs_1 = {
	asn_MAP_T316_r16_value2enum_1,
	asn_MAP_T316_r16_enum2value_1,
	10,
	0,
	1,
	0,
	0
};
static const ber_tlv_tag_t asn_DEF_T316_r16_tags_1[] = {
	(ASN_TAG_CLASS_UNIVERSAL | (10 << 2))
};
asn_TYPE_descriptor_t asn_DEF_T316_r16 = {
	"T316-r16",
	"T316-r16",
	&asn_OP_NativeEnumerated,
	asn_DEF_T316_r16_tags_1,
	sizeof(asn_DEF_T316_r16_tags_1) / sizeof(asn_DEF_T316_r16_tags_1[0]),
	asn_DEF_T316_r16_tags_1,
	sizeof(asn_DEF_T316_r16_tags_1) / sizeof(asn_DEF_T316_r16_tags_1[0]),
	{ 0, &asn_PER_type_T316_r16_constr_1, NativeEnumerated_constraint },
	0, 0,
	&asn_SPC_T316_r16_specs_1
};


