/*
 * Hand-crafted ASN.1 type for Conditional Handover support.
 * Models CondReconfigToAddMod from TS 38.331 Release 16/17.
 */

#include "ASN_RRC_CondReconfigToAddMod.h"

/* ---- condReconfigId constraint: INTEGER (1..8) ---- */
static int
memb_ASN_RRC_condReconfigId_constraint_1(const asn_TYPE_descriptor_t *td, const void *sptr,
			asn_app_constraint_failed_f *ctfailcb, void *app_key) {
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
static asn_per_constraints_t asn_PER_memb_ASN_RRC_condReconfigId_constr_2 CC_NOTUSED = {
	{ APC_CONSTRAINED,	 3,  3,  1,  8 }	/* (1..8) */,
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	0, 0	/* No PER value map */
};

/* ---- condExecutionCond: SEQUENCE (SIZE(1..2)) OF MeasId ---- */
/* MeasId is INTEGER (1..64) */
static int
memb_ASN_RRC_condExecCond_member_constraint(const asn_TYPE_descriptor_t *td, const void *sptr,
			asn_app_constraint_failed_f *ctfailcb, void *app_key) {
	long value;

	if(!sptr) {
		ASN__CTFAIL(app_key, td, sptr,
			"%s: value not given (%s:%d)",
			td->name, __FILE__, __LINE__);
		return -1;
	}

	value = *(const long *)sptr;

	if(value < 1 || value > 64) {
		ASN__CTFAIL(app_key, td, sptr,
			"%s: constraint failed (%s:%d)",
			td->name, __FILE__, __LINE__);
		return -1;
	}

	return 0;
}
static asn_per_constraints_t asn_PER_memb_ASN_RRC_condExecCond_member_constr CC_NOTUSED = {
	{ APC_CONSTRAINED,	 6,  6,  1,  64 }	/* (1..64) */,
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	0, 0
};
static asn_per_constraints_t asn_PER_ASN_RRC_condExecutionCond_constr CC_NOTUSED = {
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	{ APC_CONSTRAINED,	 1,  1,  1,  2 }	/* (SIZE(1..2)) */,
	0, 0
};
static asn_TYPE_member_t asn_MBR_ASN_RRC_condExecutionCond_3[] = {
	{ ATF_POINTER, 0, 0,
		(ASN_TAG_CLASS_UNIVERSAL | (2 << 2)),
		0,
		&asn_DEF_NativeInteger,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_condExecCond_member_constr, memb_ASN_RRC_condExecCond_member_constraint },
		0, 0, /* No default value */
		""
		},
};
static const ber_tlv_tag_t asn_DEF_ASN_RRC_condExecutionCond_tags_3[] = {
	(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static asn_SET_OF_specifics_t asn_SPC_ASN_RRC_condExecutionCond_specs_3 = {
	sizeof(struct ASN_RRC_CondReconfigToAddMod__condExecutionCond),
	offsetof(struct ASN_RRC_CondReconfigToAddMod__condExecutionCond, _asn_ctx),
	0,	/* XER encoding is XMLDelimitedItemList */
};
static /* Use -fall-defs-global to expose */
asn_TYPE_descriptor_t asn_DEF_ASN_RRC_condExecutionCond_3 = {
	"condExecutionCond",
	"condExecutionCond",
	&asn_OP_SEQUENCE_OF,
	asn_DEF_ASN_RRC_condExecutionCond_tags_3,
	sizeof(asn_DEF_ASN_RRC_condExecutionCond_tags_3)
		/sizeof(asn_DEF_ASN_RRC_condExecutionCond_tags_3[0]) - 1, /* 1 */
	asn_DEF_ASN_RRC_condExecutionCond_tags_3,
	sizeof(asn_DEF_ASN_RRC_condExecutionCond_tags_3)
		/sizeof(asn_DEF_ASN_RRC_condExecutionCond_tags_3[0]), /* 2 */
	{ 0, &asn_PER_ASN_RRC_condExecutionCond_constr, SEQUENCE_OF_constraint },
	asn_MBR_ASN_RRC_condExecutionCond_3,
	1,	/* Single element */
	&asn_SPC_ASN_RRC_condExecutionCond_specs_3	/* Additional specs */
};

/* ---- condRRCReconfig: OCTET STRING (CONTAINING RRCReconfiguration) ---- */
static int
memb_ASN_RRC_condRRCReconfig_constraint_1(const asn_TYPE_descriptor_t *td, const void *sptr,
			asn_app_constraint_failed_f *ctfailcb, void *app_key) {
	const OCTET_STRING_t *st = (const OCTET_STRING_t *)sptr;

	if(!sptr) {
		ASN__CTFAIL(app_key, td, sptr,
			"%s: value not given (%s:%d)",
			td->name, __FILE__, __LINE__);
		return -1;
	}

	if(1 /* No applicable constraints whatsoever */) {
		(void)st;
	}

	return td->encoding_constraints.general_constraints(td, sptr, ctfailcb, app_key);
}
static asn_per_constraints_t asn_PER_memb_ASN_RRC_condRRCReconfig_constr CC_NOTUSED = {
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	{ APC_SEMI_CONSTRAINED,	-1, -1,  0,  0 }	/* (SIZE(0..MAX)) */,
	0, 0
};

/* ---- Main SEQUENCE ---- */
asn_TYPE_member_t asn_MBR_ASN_RRC_CondReconfigToAddMod_1[] = {
	{ ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondReconfigToAddMod, condReconfigId),
		(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
		-1,	/* IMPLICIT tag at current level */
		&asn_DEF_NativeInteger,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_condReconfigId_constr_2, memb_ASN_RRC_condReconfigId_constraint_1 },
		0, 0, /* No default value */
		"condReconfigId"
		},
	{ ATF_POINTER, 2, offsetof(struct ASN_RRC_CondReconfigToAddMod, condExecutionCond),
		(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
		0,
		&asn_DEF_ASN_RRC_condExecutionCond_3,
		0,
		{ 0, 0, 0 },
		0, 0, /* No default value */
		"condExecutionCond"
		},
	{ ATF_POINTER, 1, offsetof(struct ASN_RRC_CondReconfigToAddMod, condRRCReconfig),
		(ASN_TAG_CLASS_CONTEXT | (2 << 2)),
		-1,	/* IMPLICIT tag at current level */
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_condRRCReconfig_constr, memb_ASN_RRC_condRRCReconfig_constraint_1 },
		0, 0, /* No default value */
		"condRRCReconfig"
		},
};
static const int asn_MAP_ASN_RRC_CondReconfigToAddMod_oms_1[] = { 1, 2 };
static const ber_tlv_tag_t asn_DEF_ASN_RRC_CondReconfigToAddMod_tags_1[] = {
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_CondReconfigToAddMod_tag2el_1[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 }, /* condReconfigId */
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 }, /* condExecutionCond */
    { (ASN_TAG_CLASS_CONTEXT | (2 << 2)), 2, 0, 0 }  /* condRRCReconfig */
};
asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_CondReconfigToAddMod_specs_1 = {
	sizeof(struct ASN_RRC_CondReconfigToAddMod),
	offsetof(struct ASN_RRC_CondReconfigToAddMod, _asn_ctx),
	asn_MAP_ASN_RRC_CondReconfigToAddMod_tag2el_1,
	3,	/* Count of tags in the map */
	asn_MAP_ASN_RRC_CondReconfigToAddMod_oms_1,	/* Optional members */
	2, 0,	/* Root/Additions */
	-1,	/* First extension addition */
};
asn_TYPE_descriptor_t asn_DEF_ASN_RRC_CondReconfigToAddMod = {
	"CondReconfigToAddMod",
	"CondReconfigToAddMod",
	&asn_OP_SEQUENCE,
	asn_DEF_ASN_RRC_CondReconfigToAddMod_tags_1,
	sizeof(asn_DEF_ASN_RRC_CondReconfigToAddMod_tags_1)
		/sizeof(asn_DEF_ASN_RRC_CondReconfigToAddMod_tags_1[0]), /* 1 */
	asn_DEF_ASN_RRC_CondReconfigToAddMod_tags_1,
	sizeof(asn_DEF_ASN_RRC_CondReconfigToAddMod_tags_1)
		/sizeof(asn_DEF_ASN_RRC_CondReconfigToAddMod_tags_1[0]), /* 1 */
	{ 0, 0, SEQUENCE_constraint },
	asn_MBR_ASN_RRC_CondReconfigToAddMod_1,
	3,	/* Elements count */
	&asn_SPC_ASN_RRC_CondReconfigToAddMod_specs_1	/* Additional specs */
};
