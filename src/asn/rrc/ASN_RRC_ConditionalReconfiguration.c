/*
 * Hand-crafted ASN.1 type for Conditional Handover support.
 * Models ConditionalReconfiguration from TS 38.331 Release 16/17.
 */

#include "ASN_RRC_ConditionalReconfiguration.h"

#include "ASN_RRC_CondReconfigToAddMod.h"
#include <NativeInteger.h>

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

/* condReconfigToRemoveList — SEQUENCE OF CondReconfigId (INTEGER 1..8) */
static int
memb_ASN_RRC_condReconfigToRemoveList_member_constraint_1(const asn_TYPE_descriptor_t *td,
		const void *sptr, asn_app_constraint_failed_f *ctfailcb, void *app_key) {
	long value;

	if(!sptr) {
		ASN__CTFAIL(app_key, td, sptr,
			"%s: value not given (%s:%d)",
			td->name, __FILE__, __LINE__);
		return -1;
	}

	value = *(const long *)sptr;

	if(value < 1 || value > 8) {
		ASN__CTFAIL(app_key, td, sptr,
			"%s: constraint failed (%s:%d)",
			td->name, __FILE__, __LINE__);
		return -1;
	}

	return 0;
}

static asn_per_constraints_t asn_PER_memb_ASN_RRC_condReconfigToRemoveList_member_constr_1 CC_NOTUSED = {
	{ APC_CONSTRAINED,	 3,  3,  1,  8 } /* (1..8) */,
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	0, 0
};

static asn_per_constraints_t asn_PER_ASN_RRC_condReconfigToRemoveList_constr_3 CC_NOTUSED = {
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	{ APC_CONSTRAINED,	 3,  3,  1,  8 } /* (SIZE(1..8)) */,
	0, 0
};

static asn_TYPE_member_t asn_MBR_ASN_RRC_condReconfigToRemoveList_3[] = {
	{ ATF_POINTER, 0, 0,
		(ASN_TAG_CLASS_UNIVERSAL | (2 << 2)),
		0,
		&asn_DEF_NativeInteger,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_condReconfigToRemoveList_member_constr_1,
		  memb_ASN_RRC_condReconfigToRemoveList_member_constraint_1 },
		0, 0, /* No default value */
		""
		},
};

static const ber_tlv_tag_t asn_DEF_ASN_RRC_condReconfigToRemoveList_tags_3[] = {
	(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};

static asn_SET_OF_specifics_t asn_SPC_ASN_RRC_condReconfigToRemoveList_specs_3 = {
	sizeof(struct ASN_RRC_ConditionalReconfiguration__condReconfigToRemoveList),
	offsetof(struct ASN_RRC_ConditionalReconfiguration__condReconfigToRemoveList, _asn_ctx),
	0, /* XER encoding is XMLDelimitedItemList */
};

static asn_TYPE_descriptor_t asn_DEF_ASN_RRC_condReconfigToRemoveList_3 = {
	"condReconfigToRemoveList",
	"condReconfigToRemoveList",
	&asn_OP_SEQUENCE_OF,
	asn_DEF_ASN_RRC_condReconfigToRemoveList_tags_3,
	sizeof(asn_DEF_ASN_RRC_condReconfigToRemoveList_tags_3)
		/sizeof(asn_DEF_ASN_RRC_condReconfigToRemoveList_tags_3[0]) - 1, /* 1 */
	asn_DEF_ASN_RRC_condReconfigToRemoveList_tags_3,
	sizeof(asn_DEF_ASN_RRC_condReconfigToRemoveList_tags_3)
		/sizeof(asn_DEF_ASN_RRC_condReconfigToRemoveList_tags_3[0]), /* 2 */
	{ 0, &asn_PER_ASN_RRC_condReconfigToRemoveList_constr_3, SEQUENCE_OF_constraint },
	asn_MBR_ASN_RRC_condReconfigToRemoveList_3,
	1, /* Single element */
	&asn_SPC_ASN_RRC_condReconfigToRemoveList_specs_3 /* Additional specs */
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
	{ ATF_POINTER, 1, offsetof(struct ASN_RRC_ConditionalReconfiguration, condReconfigToRemoveList),
		(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
		0,
		&asn_DEF_ASN_RRC_condReconfigToRemoveList_3,
		0,
		{ 0, 0, 0 },
		0, 0, /* No default value */
		"condReconfigToRemoveList"
		},
};
static const int asn_MAP_ASN_RRC_ConditionalReconfiguration_oms_1[] = { 0, 1 };
static const ber_tlv_tag_t asn_DEF_ASN_RRC_ConditionalReconfiguration_tags_1[] = {
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_ConditionalReconfiguration_tag2el_1[] = {
	{ (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 }, /* condReconfigToAddModList */
	{ (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 }  /* condReconfigToRemoveList */
};
asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_ConditionalReconfiguration_specs_1 = {
	sizeof(struct ASN_RRC_ConditionalReconfiguration),
	offsetof(struct ASN_RRC_ConditionalReconfiguration, _asn_ctx),
	asn_MAP_ASN_RRC_ConditionalReconfiguration_tag2el_1,
	2,	/* Count of tags in the map */
	asn_MAP_ASN_RRC_ConditionalReconfiguration_oms_1,	/* Optional members */
	2, 0,	/* Root/Additions */
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
	2,	/* Elements count */
	&asn_SPC_ASN_RRC_ConditionalReconfiguration_specs_1	/* Additional specs */
};
