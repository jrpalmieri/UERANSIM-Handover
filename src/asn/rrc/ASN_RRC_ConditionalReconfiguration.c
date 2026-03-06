/*
 * Hand-crafted ASN.1 type for Conditional Handover support.
 * Models ConditionalReconfiguration from TS 38.331 Release 16/17.
 */

#include "ASN_RRC_ConditionalReconfiguration.h"

#include "ASN_RRC_CondReconfigToAddMod.h"

/* condReconfigToAddModList — SEQUENCE OF CondReconfigToAddMod */
static asn_per_constraints_t asn_PER_ASN_RRC_condReconfigToAddModList_constr_2 CC_NOTUSED = {
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	{ APC_CONSTRAINED,	 3,  3,  1,  8 }	/* (SIZE(1..8)) */,
	0, 0	/* No PER value map */
};
static asn_TYPE_member_t asn_MBR_ASN_RRC_condReconfigToAddModList_2[] = {
	{ ATF_POINTER, 0, 0,
		(ASN_TAG_CLASS_UNIVERSAL | (16 << 2)),
		0,
		&asn_DEF_ASN_RRC_CondReconfigToAddMod,
		0,
		{ 0, 0, 0 },
		0, 0, /* No default value */
		""
		},
};
static const ber_tlv_tag_t asn_DEF_ASN_RRC_condReconfigToAddModList_tags_2[] = {
	(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static asn_SET_OF_specifics_t asn_SPC_ASN_RRC_condReconfigToAddModList_specs_2 = {
	sizeof(struct ASN_RRC_ConditionalReconfiguration__condReconfigToAddModList),
	offsetof(struct ASN_RRC_ConditionalReconfiguration__condReconfigToAddModList, _asn_ctx),
	0,	/* XER encoding is XMLDelimitedItemList */
};
static /* Use -fall-defs-global to expose */
asn_TYPE_descriptor_t asn_DEF_ASN_RRC_condReconfigToAddModList_2 = {
	"condReconfigToAddModList",
	"condReconfigToAddModList",
	&asn_OP_SEQUENCE_OF,
	asn_DEF_ASN_RRC_condReconfigToAddModList_tags_2,
	sizeof(asn_DEF_ASN_RRC_condReconfigToAddModList_tags_2)
		/sizeof(asn_DEF_ASN_RRC_condReconfigToAddModList_tags_2[0]) - 1, /* 1 */
	asn_DEF_ASN_RRC_condReconfigToAddModList_tags_2,
	sizeof(asn_DEF_ASN_RRC_condReconfigToAddModList_tags_2)
		/sizeof(asn_DEF_ASN_RRC_condReconfigToAddModList_tags_2[0]), /* 2 */
	{ 0, &asn_PER_ASN_RRC_condReconfigToAddModList_constr_2, SEQUENCE_OF_constraint },
	asn_MBR_ASN_RRC_condReconfigToAddModList_2,
	1,	/* Single element */
	&asn_SPC_ASN_RRC_condReconfigToAddModList_specs_2	/* Additional specs */
};

asn_TYPE_member_t asn_MBR_ASN_RRC_ConditionalReconfiguration_1[] = {
	{ ATF_POINTER, 1, offsetof(struct ASN_RRC_ConditionalReconfiguration, condReconfigToAddModList),
		(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
		0,
		&asn_DEF_ASN_RRC_condReconfigToAddModList_2,
		0,
		{ 0, 0, 0 },
		0, 0, /* No default value */
		"condReconfigToAddModList"
		},
};
static const int asn_MAP_ASN_RRC_ConditionalReconfiguration_oms_1[] = { 0 };
static const ber_tlv_tag_t asn_DEF_ASN_RRC_ConditionalReconfiguration_tags_1[] = {
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_ConditionalReconfiguration_tag2el_1[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 } /* condReconfigToAddModList */
};
asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_ConditionalReconfiguration_specs_1 = {
	sizeof(struct ASN_RRC_ConditionalReconfiguration),
	offsetof(struct ASN_RRC_ConditionalReconfiguration, _asn_ctx),
	asn_MAP_ASN_RRC_ConditionalReconfiguration_tag2el_1,
	1,	/* Count of tags in the map */
	asn_MAP_ASN_RRC_ConditionalReconfiguration_oms_1,	/* Optional members */
	1, 0,	/* Root/Additions */
	-1,	/* First extension addition */
};
asn_TYPE_descriptor_t asn_DEF_ASN_RRC_ConditionalReconfiguration = {
	"ConditionalReconfiguration",
	"ConditionalReconfiguration",
	&asn_OP_SEQUENCE,
	asn_DEF_ASN_RRC_ConditionalReconfiguration_tags_1,
	sizeof(asn_DEF_ASN_RRC_ConditionalReconfiguration_tags_1)
		/sizeof(asn_DEF_ASN_RRC_ConditionalReconfiguration_tags_1[0]), /* 1 */
	asn_DEF_ASN_RRC_ConditionalReconfiguration_tags_1,
	sizeof(asn_DEF_ASN_RRC_ConditionalReconfiguration_tags_1)
		/sizeof(asn_DEF_ASN_RRC_ConditionalReconfiguration_tags_1[0]), /* 1 */
	{ 0, 0, SEQUENCE_constraint },
	asn_MBR_ASN_RRC_ConditionalReconfiguration_1,
	1,	/* Elements count */
	&asn_SPC_ASN_RRC_ConditionalReconfiguration_specs_1	/* Additional specs */
};
