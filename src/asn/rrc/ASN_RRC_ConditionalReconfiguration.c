#include "ASN_RRC_ConditionalReconfiguration.h"

#include "ASN_RRC_CondReconfigToAddMod.h"
#include <NativeInteger.h>
#include <NativeEnumerated.h>

static int
memb_ASN_RRC_attemptCondReconfig_constraint_1(const asn_TYPE_descriptor_t *td, const void *sptr,
			asn_app_constraint_failed_f *ctfailcb, void *app_key) {
	long value;

	if(!sptr) {
		ASN__CTFAIL(app_key, td, sptr,
			"%s: value not given (%s:%d)",
			td->name, __FILE__, __LINE__);
		return -1;
	}

	value = *(const long *)sptr;
	if(value != 0) {
		ASN__CTFAIL(app_key, td, sptr,
			"%s: constraint failed (%s:%d)",
			td->name, __FILE__, __LINE__);
		return -1;
	}

	return 0;
}

static asn_per_constraints_t asn_PER_memb_ASN_RRC_attemptCondReconfig_constr_1 CC_NOTUSED = {
	{ APC_CONSTRAINED,	 0,  0,  0,  0 },
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	0, 0
};

/* condReconfigToAddModList — SEQUENCE OF CondReconfigToAddMod */
static asn_per_constraints_t asn_PER_ASN_RRC_condReconfigToAddModList_constr_3 CC_NOTUSED = {
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	/* Decode-tolerant list size: runtime parser enforces practical bounds. */
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	0, 0	/* No PER value map */
};
static asn_TYPE_member_t asn_MBR_ASN_RRC_condReconfigToAddModList_3[] = {
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
static const ber_tlv_tag_t asn_DEF_ASN_RRC_condReconfigToAddModList_tags_3[] = {
	(ASN_TAG_CLASS_CONTEXT | (2 << 2)),
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static asn_SET_OF_specifics_t asn_SPC_ASN_RRC_condReconfigToAddModList_specs_3 = {
	sizeof(struct ASN_RRC_ConditionalReconfiguration__condReconfigToAddModList),
	offsetof(struct ASN_RRC_ConditionalReconfiguration__condReconfigToAddModList, _asn_ctx),
	0,	/* XER encoding is XMLDelimitedItemList */
};
static /* Use -fall-defs-global to expose */
asn_TYPE_descriptor_t asn_DEF_ASN_RRC_condReconfigToAddModList_3 = {
	"condReconfigToAddModList",
	"condReconfigToAddModList",
	&asn_OP_SEQUENCE_OF,
	asn_DEF_ASN_RRC_condReconfigToAddModList_tags_3,
	sizeof(asn_DEF_ASN_RRC_condReconfigToAddModList_tags_3)
		/sizeof(asn_DEF_ASN_RRC_condReconfigToAddModList_tags_3[0]) - 1, /* 1 */
	asn_DEF_ASN_RRC_condReconfigToAddModList_tags_3,
	sizeof(asn_DEF_ASN_RRC_condReconfigToAddModList_tags_3)
		/sizeof(asn_DEF_ASN_RRC_condReconfigToAddModList_tags_3[0]), /* 2 */
	{ 0, &asn_PER_ASN_RRC_condReconfigToAddModList_constr_3, SEQUENCE_OF_constraint },
	asn_MBR_ASN_RRC_condReconfigToAddModList_3,
	1,	/* Single element */
	&asn_SPC_ASN_RRC_condReconfigToAddModList_specs_3	/* Additional specs */
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

static asn_per_constraints_t asn_PER_ASN_RRC_condReconfigToRemoveList_constr_2 CC_NOTUSED = {
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	/* Decode-tolerant list size: member constraint still enforces CondReconfigId range. */
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	0, 0
};

static asn_TYPE_member_t asn_MBR_ASN_RRC_condReconfigToRemoveList_2[] = {
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

static const ber_tlv_tag_t asn_DEF_ASN_RRC_condReconfigToRemoveList_tags_2[] = {
	(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};

static asn_SET_OF_specifics_t asn_SPC_ASN_RRC_condReconfigToRemoveList_specs_2 = {
	sizeof(struct ASN_RRC_ConditionalReconfiguration__condReconfigToRemoveList),
	offsetof(struct ASN_RRC_ConditionalReconfiguration__condReconfigToRemoveList, _asn_ctx),
	0, /* XER encoding is XMLDelimitedItemList */
};

static asn_TYPE_descriptor_t asn_DEF_ASN_RRC_condReconfigToRemoveList_2 = {
	"condReconfigToRemoveList",
	"condReconfigToRemoveList",
	&asn_OP_SEQUENCE_OF,
	asn_DEF_ASN_RRC_condReconfigToRemoveList_tags_2,
	sizeof(asn_DEF_ASN_RRC_condReconfigToRemoveList_tags_2)
		/sizeof(asn_DEF_ASN_RRC_condReconfigToRemoveList_tags_2[0]) - 1, /* 1 */
	asn_DEF_ASN_RRC_condReconfigToRemoveList_tags_2,
	sizeof(asn_DEF_ASN_RRC_condReconfigToRemoveList_tags_2)
		/sizeof(asn_DEF_ASN_RRC_condReconfigToRemoveList_tags_2[0]), /* 2 */
	{ 0, &asn_PER_ASN_RRC_condReconfigToRemoveList_constr_2, SEQUENCE_OF_constraint },
	asn_MBR_ASN_RRC_condReconfigToRemoveList_2,
	1, /* Single element */
	&asn_SPC_ASN_RRC_condReconfigToRemoveList_specs_2 /* Additional specs */
};

asn_TYPE_member_t asn_MBR_ASN_RRC_ConditionalReconfiguration_1[] = {
	{ ATF_POINTER, 3, offsetof(struct ASN_RRC_ConditionalReconfiguration, attemptCondReconfig),
		(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
		-1,
		&asn_DEF_NativeEnumerated,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_attemptCondReconfig_constr_1,
		  memb_ASN_RRC_attemptCondReconfig_constraint_1 },
		0, 0,
		"attemptCondReconfig"
		},
	{ ATF_POINTER, 2, offsetof(struct ASN_RRC_ConditionalReconfiguration, condReconfigToRemoveList),
		(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
		-1,
		&asn_DEF_ASN_RRC_condReconfigToRemoveList_2,
		0,
		{ 0, 0, 0 },
		0, 0,
		"condReconfigToRemoveList"
		},
	{ ATF_POINTER, 1, offsetof(struct ASN_RRC_ConditionalReconfiguration, condReconfigToAddModList),
		(ASN_TAG_CLASS_CONTEXT | (2 << 2)),
		-1,
		&asn_DEF_ASN_RRC_condReconfigToAddModList_3,
		0,
		{ 0, 0, 0 },
		0, 0, /* No default value */
		"condReconfigToAddModList"
		},
};
static const int asn_MAP_ASN_RRC_ConditionalReconfiguration_oms_1[] = { 0, 1, 2 };
static const ber_tlv_tag_t asn_DEF_ASN_RRC_ConditionalReconfiguration_tags_1[] = {
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_ConditionalReconfiguration_tag2el_1[] = {
	{ (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 }, /* attemptCondReconfig */
	{ (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 }, /* condReconfigToRemoveList */
	{ (ASN_TAG_CLASS_CONTEXT | (2 << 2)), 2, 0, 0 }  /* condReconfigToAddModList */
};
asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_ConditionalReconfiguration_specs_1 = {
	sizeof(struct ASN_RRC_ConditionalReconfiguration),
	offsetof(struct ASN_RRC_ConditionalReconfiguration, _asn_ctx),
	asn_MAP_ASN_RRC_ConditionalReconfiguration_tag2el_1,
	3,	/* Count of tags in the map */
	asn_MAP_ASN_RRC_ConditionalReconfiguration_oms_1,	/* Optional members */
	3, 0,	/* Root/Additions */
	3,	/* First extension addition */
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
	3,	/* Elements count */
	&asn_SPC_ASN_RRC_ConditionalReconfiguration_specs_1	/* Additional specs */
};
