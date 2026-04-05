#include "ASN_RRC_RRCReconfiguration-v1700-IEs.h"

static int
memb_ASN_RRC_scg_State_r17_constraint_1(const asn_TYPE_descriptor_t *td, const void *sptr,
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

static int
memb_ASN_RRC_dedicatedPagingDelivery_r17_constraint_1(const asn_TYPE_descriptor_t *td,
			const void *sptr, asn_app_constraint_failed_f *ctfailcb, void *app_key) {
	if(!sptr) {
		ASN__CTFAIL(app_key, td, sptr,
			"%s: value not given (%s:%d)",
			td->name, __FILE__, __LINE__);
		return -1;
	}

	return td->encoding_constraints.general_constraints(td, sptr, ctfailcb, app_key);
}

static asn_per_constraints_t asn_PER_memb_ASN_RRC_scg_State_r17_constr_9 CC_NOTUSED = {
	{ APC_CONSTRAINED,	 0,  0,  0,  0 },
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	0, 0
};
static asn_per_constraints_t asn_PER_memb_ASN_RRC_semiConstrainedOctets_constr CC_NOTUSED = {
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	{ APC_SEMI_CONSTRAINED,	-1, -1,  0,  0 },
	0, 0
};

asn_TYPE_member_t asn_MBR_ASN_RRC_RRCReconfiguration_v1700_IEs_1[] = {
	{ ATF_POINTER, 12, offsetof(struct ASN_RRC_RRCReconfiguration_v1700_IEs, otherConfig_v1700),
		(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
		-1,
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_semiConstrainedOctets_constr, 0 },
		0, 0,
		"otherConfig-v1700"
		},
	{ ATF_POINTER, 11, offsetof(struct ASN_RRC_RRCReconfiguration_v1700_IEs, sl_L2RelayUE_Config_r17),
		(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
		-1,
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_semiConstrainedOctets_constr, 0 },
		0, 0,
		"sl-L2RelayUE-Config-r17"
		},
	{ ATF_POINTER, 10, offsetof(struct ASN_RRC_RRCReconfiguration_v1700_IEs, sl_L2RemoteUE_Config_r17),
		(ASN_TAG_CLASS_CONTEXT | (2 << 2)),
		-1,
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_semiConstrainedOctets_constr, 0 },
		0, 0,
		"sl-L2RemoteUE-Config-r17"
		},
	{ ATF_POINTER, 9, offsetof(struct ASN_RRC_RRCReconfiguration_v1700_IEs, dedicatedPagingDelivery_r17),
		(ASN_TAG_CLASS_CONTEXT | (3 << 2)),
		-1,
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_semiConstrainedOctets_constr,
		  memb_ASN_RRC_dedicatedPagingDelivery_r17_constraint_1 },
		0, 0,
		"dedicatedPagingDelivery-r17"
		},
	{ ATF_POINTER, 8, offsetof(struct ASN_RRC_RRCReconfiguration_v1700_IEs, needForGapNCSG_ConfigNR_r17),
		(ASN_TAG_CLASS_CONTEXT | (4 << 2)),
		-1,
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_semiConstrainedOctets_constr, 0 },
		0, 0,
		"needForGapNCSG-ConfigNR-r17"
		},
	{ ATF_POINTER, 7, offsetof(struct ASN_RRC_RRCReconfiguration_v1700_IEs,
			needForGapNCSG_ConfigEUTRA_r17),
		(ASN_TAG_CLASS_CONTEXT | (5 << 2)),
		-1,
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_semiConstrainedOctets_constr, 0 },
		0, 0,
		"needForGapNCSG-ConfigEUTRA-r17"
		},
	{ ATF_POINTER, 6, offsetof(struct ASN_RRC_RRCReconfiguration_v1700_IEs, musim_GapConfig_r17),
		(ASN_TAG_CLASS_CONTEXT | (6 << 2)),
		-1,
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_semiConstrainedOctets_constr, 0 },
		0, 0,
		"musim-GapConfig-r17"
		},
	{ ATF_POINTER, 5, offsetof(struct ASN_RRC_RRCReconfiguration_v1700_IEs, ul_GapFR2_Config_r17),
		(ASN_TAG_CLASS_CONTEXT | (7 << 2)),
		-1,
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_semiConstrainedOctets_constr, 0 },
		0, 0,
		"ul-GapFR2-Config-r17"
		},
	{ ATF_POINTER, 4, offsetof(struct ASN_RRC_RRCReconfiguration_v1700_IEs, scg_State_r17),
		(ASN_TAG_CLASS_CONTEXT | (8 << 2)),
		-1,
		&asn_DEF_NativeEnumerated,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_scg_State_r17_constr_9,
		  memb_ASN_RRC_scg_State_r17_constraint_1 },
		0, 0,
		"scg-State-r17"
		},
	{ ATF_POINTER, 3, offsetof(struct ASN_RRC_RRCReconfiguration_v1700_IEs, appLayerMeasConfig_r17),
		(ASN_TAG_CLASS_CONTEXT | (9 << 2)),
		-1,
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_semiConstrainedOctets_constr, 0 },
		0, 0,
		"appLayerMeasConfig-r17"
		},
	{ ATF_POINTER, 2, offsetof(struct ASN_RRC_RRCReconfiguration_v1700_IEs,
			ue_TxTEG_RequestUL_TDOA_Config_r17),
		(ASN_TAG_CLASS_CONTEXT | (10 << 2)),
		-1,
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_semiConstrainedOctets_constr, 0 },
		0, 0,
		"ue-TxTEG-RequestUL-TDOA-Config-r17"
		},
	{ ATF_POINTER, 1, offsetof(struct ASN_RRC_RRCReconfiguration_v1700_IEs, nonCriticalExtension),
		(ASN_TAG_CLASS_CONTEXT | (11 << 2)),
		-1,
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_semiConstrainedOctets_constr, 0 },
		0, 0,
		"nonCriticalExtension"
		},
};
static const int asn_MAP_ASN_RRC_RRCReconfiguration_v1700_IEs_oms_1[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};
static const ber_tlv_tag_t asn_DEF_ASN_RRC_RRCReconfiguration_v1700_IEs_tags_1[] = {
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_RRCReconfiguration_v1700_IEs_tag2el_1[] = {
	{ (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 },
	{ (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 },
	{ (ASN_TAG_CLASS_CONTEXT | (2 << 2)), 2, 0, 0 },
	{ (ASN_TAG_CLASS_CONTEXT | (3 << 2)), 3, 0, 0 },
	{ (ASN_TAG_CLASS_CONTEXT | (4 << 2)), 4, 0, 0 },
	{ (ASN_TAG_CLASS_CONTEXT | (5 << 2)), 5, 0, 0 },
	{ (ASN_TAG_CLASS_CONTEXT | (6 << 2)), 6, 0, 0 },
	{ (ASN_TAG_CLASS_CONTEXT | (7 << 2)), 7, 0, 0 },
	{ (ASN_TAG_CLASS_CONTEXT | (8 << 2)), 8, 0, 0 },
	{ (ASN_TAG_CLASS_CONTEXT | (9 << 2)), 9, 0, 0 },
	{ (ASN_TAG_CLASS_CONTEXT | (10 << 2)), 10, 0, 0 },
	{ (ASN_TAG_CLASS_CONTEXT | (11 << 2)), 11, 0, 0 }
};
asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_RRCReconfiguration_v1700_IEs_specs_1 = {
	sizeof(struct ASN_RRC_RRCReconfiguration_v1700_IEs),
	offsetof(struct ASN_RRC_RRCReconfiguration_v1700_IEs, _asn_ctx),
	asn_MAP_ASN_RRC_RRCReconfiguration_v1700_IEs_tag2el_1,
	12,
	asn_MAP_ASN_RRC_RRCReconfiguration_v1700_IEs_oms_1,
	12, 0,
	-1,
};
asn_TYPE_descriptor_t asn_DEF_ASN_RRC_RRCReconfiguration_v1700_IEs = {
	"RRCReconfiguration-v1700-IEs",
	"RRCReconfiguration-v1700-IEs",
	&asn_OP_SEQUENCE,
	asn_DEF_ASN_RRC_RRCReconfiguration_v1700_IEs_tags_1,
	sizeof(asn_DEF_ASN_RRC_RRCReconfiguration_v1700_IEs_tags_1)
		/ sizeof(asn_DEF_ASN_RRC_RRCReconfiguration_v1700_IEs_tags_1[0]),
	asn_DEF_ASN_RRC_RRCReconfiguration_v1700_IEs_tags_1,
	sizeof(asn_DEF_ASN_RRC_RRCReconfiguration_v1700_IEs_tags_1)
		/ sizeof(asn_DEF_ASN_RRC_RRCReconfiguration_v1700_IEs_tags_1[0]),
	{ 0, 0, SEQUENCE_constraint },
	asn_MBR_ASN_RRC_RRCReconfiguration_v1700_IEs_1,
	12,
	&asn_SPC_ASN_RRC_RRCReconfiguration_v1700_IEs_specs_1
};
