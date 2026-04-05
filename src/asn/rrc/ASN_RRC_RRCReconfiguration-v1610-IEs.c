#include "ASN_RRC_RRCReconfiguration-v1610-IEs.h"

#include "ASN_RRC_ConditionalReconfiguration.h"
#include "ASN_RRC_OtherConfig.h"
#include "ASN_RRC_RRCReconfiguration-v1700-IEs.h"
#include "ASN_RRC_SetupRelease.h"
#include "ASN_RRC_SSB-MTC.h"
#include "T316-r16.h"
#include "OnDemandSIB-Request-r16.h"

#include <NULL.h>

typedef enum SetupRelease_2610P7_PR {
	SetupRelease_2610P7_PR_NOTHING,
	SetupRelease_2610P7_PR_release,
	SetupRelease_2610P7_PR_setup
} SetupRelease_2610P7_PR;

typedef enum SetupRelease_2610P6_PR {
	SetupRelease_2610P6_PR_NOTHING,
	SetupRelease_2610P6_PR_release,
	SetupRelease_2610P6_PR_setup
} SetupRelease_2610P6_PR;

typedef struct SetupRelease_2610P6 {
	SetupRelease_2610P6_PR present;
	union SetupRelease_2610P6_u {
		NULL_t release;
		OCTET_STRING_t *setup;
	} choice;
	asn_struct_ctx_t _asn_ctx;
} SetupRelease_2610P6_t;

typedef struct SetupRelease_2610P7 {
	SetupRelease_2610P7_PR present;
	union SetupRelease_2610P7_u {
		NULL_t release;
		T316_r16_t setup;
	} choice;
	asn_struct_ctx_t _asn_ctx;
} SetupRelease_2610P7_t;

typedef enum SetupRelease_2610P9_PR {
	SetupRelease_2610P9_PR_NOTHING,
	SetupRelease_2610P9_PR_release,
	SetupRelease_2610P9_PR_setup
} SetupRelease_2610P9_PR;

typedef struct SetupRelease_2610P9 {
	SetupRelease_2610P9_PR present;
	union SetupRelease_2610P9_u {
		NULL_t release;
		OnDemandSIB_Request_r16_t *setup;
	} choice;
	asn_struct_ctx_t _asn_ctx;
} SetupRelease_2610P9_t;

typedef enum SetupRelease_2610P10_PR {
	SetupRelease_2610P10_PR_NOTHING,
	SetupRelease_2610P10_PR_release,
	SetupRelease_2610P10_PR_setup
} SetupRelease_2610P10_PR;

typedef struct SetupRelease_2610P10 {
	SetupRelease_2610P10_PR present;
	union SetupRelease_2610P10_u {
		NULL_t release;
		OCTET_STRING_t *setup;
	} choice;
	asn_struct_ctx_t _asn_ctx;
} SetupRelease_2610P10_t;

typedef enum SetupRelease_2610P11_PR {
	SetupRelease_2610P11_PR_NOTHING,
	SetupRelease_2610P11_PR_release,
	SetupRelease_2610P11_PR_setup
} SetupRelease_2610P11_PR;

typedef struct SetupRelease_2610P11 {
	SetupRelease_2610P11_PR present;
	union SetupRelease_2610P11_u {
		NULL_t release;
		OCTET_STRING_t *setup;
	} choice;
	asn_struct_ctx_t _asn_ctx;
} SetupRelease_2610P11_t;

static int
memb_ASN_RRC_daps_SourceRelease_r16_constraint_1(const asn_TYPE_descriptor_t *td, const void *sptr,
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
memb_ASN_RRC_iab_IP_AddressConfigurationList_r16_constraint_1(const asn_TYPE_descriptor_t *td,
			const void *sptr, asn_app_constraint_failed_f *ctfailcb, void *app_key) {
	if(!sptr) {
		ASN__CTFAIL(app_key, td, sptr,
			"%s: value not given (%s:%d)",
			td->name, __FILE__, __LINE__);
		return -1;
	}

	return td->encoding_constraints.general_constraints(td, sptr, ctfailcb, app_key);
}

static int
memb_ASN_RRC_dedicatedPosSysInfoDelivery_r16_constraint_1(const asn_TYPE_descriptor_t *td,
			const void *sptr, asn_app_constraint_failed_f *ctfailcb, void *app_key) {
	if(!sptr) {
		ASN__CTFAIL(app_key, td, sptr,
			"%s: value not given (%s:%d)",
			td->name, __FILE__, __LINE__);
		return -1;
	}

	return td->encoding_constraints.general_constraints(td, sptr, ctfailcb, app_key);
}

static asn_per_constraints_t asn_PER_memb_ASN_RRC_daps_SourceRelease_r16_constr_5 CC_NOTUSED = {
	{ APC_CONSTRAINED,	 0,  0,  0,  0 },
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	0, 0
};
static asn_per_constraints_t asn_PER_memb_ASN_RRC_iab_IP_AddressConfigurationList_r16_constr_3 CC_NOTUSED = {
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	{ APC_SEMI_CONSTRAINED,	-1, -1,  0,  0 },
	0, 0
};
static asn_per_constraints_t asn_PER_memb_ASN_RRC_dedicatedPosSysInfoDelivery_r16_constr_8 CC_NOTUSED = {
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	{ APC_SEMI_CONSTRAINED,	-1, -1,  0,  0 },
	0, 0
};
static asn_per_constraints_t asn_PER_memb_ASN_RRC_opaqueSetupPayload_constr CC_NOTUSED = {
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	{ APC_SEMI_CONSTRAINED,	-1, -1,  0,  0 },
	0, 0
};

static asn_per_constraints_t asn_PER_type_SetupRelease_2610P7_constr CC_NOTUSED = {
	{ APC_CONSTRAINED,	 1,  1,  0,  1 },
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	0, 0
};
static asn_per_constraints_t asn_PER_type_SetupRelease_2610P6_constr CC_NOTUSED = {
	{ APC_CONSTRAINED,	 1,  1,  0,  1 },
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	0, 0
};
static asn_per_constraints_t asn_PER_type_SetupRelease_2610P9_constr CC_NOTUSED = {
	{ APC_CONSTRAINED,	 1,  1,  0,  1 },
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	0, 0
};
static asn_per_constraints_t asn_PER_type_SetupRelease_2610P10_constr CC_NOTUSED = {
	{ APC_CONSTRAINED,	 1,  1,  0,  1 },
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	0, 0
};
static asn_per_constraints_t asn_PER_type_SetupRelease_2610P11_constr CC_NOTUSED = {
	{ APC_CONSTRAINED,	 1,  1,  0,  1 },
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	0, 0
};

static asn_TYPE_member_t asn_MBR_SetupRelease_2610P6[] = {
	{ ATF_NOFLAGS, 0, offsetof(SetupRelease_2610P6_t, choice.release),
		(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
		-1,
		&asn_DEF_NULL,
		0,
		{ 0, 0, 0 },
		0, 0,
		"release"
		},
	{ ATF_POINTER, 0, offsetof(SetupRelease_2610P6_t, choice.setup),
		(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
		-1,
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_opaqueSetupPayload_constr, 0 },
		0, 0,
		"setup"
		},
};
static const asn_TYPE_tag2member_t asn_MAP_SetupRelease_2610P6_tag2el[] = {
	{ (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 },
	{ (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 }
};
static asn_CHOICE_specifics_t asn_SPC_SetupRelease_2610P6_specs = {
	sizeof(SetupRelease_2610P6_t),
	offsetof(SetupRelease_2610P6_t, _asn_ctx),
	offsetof(SetupRelease_2610P6_t, present),
	sizeof(((SetupRelease_2610P6_t *)0)->present),
	asn_MAP_SetupRelease_2610P6_tag2el,
	2,
	0, 0,
	-1
};
static asn_TYPE_descriptor_t asn_DEF_SetupRelease_2610P6 = {
	"SetupRelease_2610P6",
	"SetupRelease_2610P6",
	&asn_OP_CHOICE,
	0, 0,
	0, 0,
	{ 0, &asn_PER_type_SetupRelease_2610P6_constr, CHOICE_constraint },
	asn_MBR_SetupRelease_2610P6,
	2,
	&asn_SPC_SetupRelease_2610P6_specs
};

static asn_TYPE_member_t asn_MBR_SetupRelease_2610P7[] = {
	{ ATF_NOFLAGS, 0, offsetof(SetupRelease_2610P7_t, choice.release),
		(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
		-1,
		&asn_DEF_NULL,
		0,
		{ 0, 0, 0 },
		0, 0,
		"release"
		},
	{ ATF_NOFLAGS, 0, offsetof(SetupRelease_2610P7_t, choice.setup),
		(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
		-1,
		&asn_DEF_T316_r16,
		0,
		{ 0, 0, 0 },
		0, 0,
		"setup"
		},
};
static const asn_TYPE_tag2member_t asn_MAP_SetupRelease_2610P7_tag2el[] = {
	{ (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 },
	{ (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 }
};
static asn_CHOICE_specifics_t asn_SPC_SetupRelease_2610P7_specs = {
	sizeof(SetupRelease_2610P7_t),
	offsetof(SetupRelease_2610P7_t, _asn_ctx),
	offsetof(SetupRelease_2610P7_t, present),
	sizeof(((SetupRelease_2610P7_t *)0)->present),
	asn_MAP_SetupRelease_2610P7_tag2el,
	2,
	0, 0,
	-1
};
static asn_TYPE_descriptor_t asn_DEF_SetupRelease_2610P7 = {
	"SetupRelease_2610P7",
	"SetupRelease_2610P7",
	&asn_OP_CHOICE,
	0, 0,
	0, 0,
	{ 0, &asn_PER_type_SetupRelease_2610P7_constr, CHOICE_constraint },
	asn_MBR_SetupRelease_2610P7,
	2,
	&asn_SPC_SetupRelease_2610P7_specs
};

static asn_TYPE_member_t asn_MBR_SetupRelease_2610P9[] = {
	{ ATF_NOFLAGS, 0, offsetof(SetupRelease_2610P9_t, choice.release),
		(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
		-1,
		&asn_DEF_NULL,
		0,
		{ 0, 0, 0 },
		0, 0,
		"release"
		},
	{ ATF_POINTER, 0, offsetof(SetupRelease_2610P9_t, choice.setup),
		(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
		-1,
		&asn_DEF_OnDemandSIB_Request_r16,
		0,
		{ 0, 0, 0 },
		0, 0,
		"setup"
		},
};
static const asn_TYPE_tag2member_t asn_MAP_SetupRelease_2610P9_tag2el[] = {
	{ (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 },
	{ (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 }
};
static asn_CHOICE_specifics_t asn_SPC_SetupRelease_2610P9_specs = {
	sizeof(SetupRelease_2610P9_t),
	offsetof(SetupRelease_2610P9_t, _asn_ctx),
	offsetof(SetupRelease_2610P9_t, present),
	sizeof(((SetupRelease_2610P9_t *)0)->present),
	asn_MAP_SetupRelease_2610P9_tag2el,
	2,
	0, 0,
	-1
};
static asn_TYPE_descriptor_t asn_DEF_SetupRelease_2610P9 = {
	"SetupRelease_2610P9",
	"SetupRelease_2610P9",
	&asn_OP_CHOICE,
	0, 0,
	0, 0,
	{ 0, &asn_PER_type_SetupRelease_2610P9_constr, CHOICE_constraint },
	asn_MBR_SetupRelease_2610P9,
	2,
	&asn_SPC_SetupRelease_2610P9_specs
};

static asn_TYPE_member_t asn_MBR_SetupRelease_2610P10[] = {
	{ ATF_NOFLAGS, 0, offsetof(SetupRelease_2610P10_t, choice.release),
		(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
		-1,
		&asn_DEF_NULL,
		0,
		{ 0, 0, 0 },
		0, 0,
		"release"
		},
	{ ATF_POINTER, 0, offsetof(SetupRelease_2610P10_t, choice.setup),
		(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
		-1,
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_opaqueSetupPayload_constr, 0 },
		0, 0,
		"setup"
		},
};
static const asn_TYPE_tag2member_t asn_MAP_SetupRelease_2610P10_tag2el[] = {
	{ (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 },
	{ (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 }
};
static asn_CHOICE_specifics_t asn_SPC_SetupRelease_2610P10_specs = {
	sizeof(SetupRelease_2610P10_t),
	offsetof(SetupRelease_2610P10_t, _asn_ctx),
	offsetof(SetupRelease_2610P10_t, present),
	sizeof(((SetupRelease_2610P10_t *)0)->present),
	asn_MAP_SetupRelease_2610P10_tag2el,
	2,
	0, 0,
	-1
};
static asn_TYPE_descriptor_t asn_DEF_SetupRelease_2610P10 = {
	"SetupRelease_2610P10",
	"SetupRelease_2610P10",
	&asn_OP_CHOICE,
	0, 0,
	0, 0,
	{ 0, &asn_PER_type_SetupRelease_2610P10_constr, CHOICE_constraint },
	asn_MBR_SetupRelease_2610P10,
	2,
	&asn_SPC_SetupRelease_2610P10_specs
};

static asn_TYPE_member_t asn_MBR_SetupRelease_2610P11[] = {
	{ ATF_NOFLAGS, 0, offsetof(SetupRelease_2610P11_t, choice.release),
		(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
		-1,
		&asn_DEF_NULL,
		0,
		{ 0, 0, 0 },
		0, 0,
		"release"
		},
	{ ATF_POINTER, 0, offsetof(SetupRelease_2610P11_t, choice.setup),
		(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
		-1,
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_opaqueSetupPayload_constr, 0 },
		0, 0,
		"setup"
		},
};
static const asn_TYPE_tag2member_t asn_MAP_SetupRelease_2610P11_tag2el[] = {
	{ (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 },
	{ (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 }
};
static asn_CHOICE_specifics_t asn_SPC_SetupRelease_2610P11_specs = {
	sizeof(SetupRelease_2610P11_t),
	offsetof(SetupRelease_2610P11_t, _asn_ctx),
	offsetof(SetupRelease_2610P11_t, present),
	sizeof(((SetupRelease_2610P11_t *)0)->present),
	asn_MAP_SetupRelease_2610P11_tag2el,
	2,
	0, 0,
	-1
};
static asn_TYPE_descriptor_t asn_DEF_SetupRelease_2610P11 = {
	"SetupRelease_2610P11",
	"SetupRelease_2610P11",
	&asn_OP_CHOICE,
	0, 0,
	0, 0,
	{ 0, &asn_PER_type_SetupRelease_2610P11_constr, CHOICE_constraint },
	asn_MBR_SetupRelease_2610P11,
	2,
	&asn_SPC_SetupRelease_2610P11_specs
};

asn_TYPE_member_t asn_MBR_ASN_RRC_RRCReconfiguration_v1610_IEs_1[] = {
	{ ATF_POINTER, 13, offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs, otherConfig_v1610),
		(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
		-1,
		&asn_DEF_ASN_RRC_OtherConfig,
		0,
		{ 0, 0, 0 },
		0, 0,
		"otherConfig-v1610"
		},
	{ ATF_POINTER, 12, offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs, bap_Config_r16),
		(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
		+1,
		&asn_DEF_SetupRelease_2610P6,
		0,
		{ 0, 0, 0 },
		0, 0,
		"bap-Config-r16"
		},
	{ ATF_POINTER, 11, offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs,
			iab_IP_AddressConfigurationList_r16),
		(ASN_TAG_CLASS_CONTEXT | (2 << 2)),
		-1,
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_iab_IP_AddressConfigurationList_r16_constr_3,
		  memb_ASN_RRC_iab_IP_AddressConfigurationList_r16_constraint_1 },
		0, 0,
		"iab-IP-AddressConfigurationList-r16"
		},
	{ ATF_POINTER, 10, offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs, conditionalReconfiguration),
		(ASN_TAG_CLASS_CONTEXT | (3 << 2)),
		-1,
		&asn_DEF_ASN_RRC_ConditionalReconfiguration,
		0,
		{ 0, 0, 0 },
		0, 0,
		"conditionalReconfiguration-r16"
		},
	{ ATF_POINTER, 9, offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs, daps_SourceRelease_r16),
		(ASN_TAG_CLASS_CONTEXT | (4 << 2)),
		-1,
		&asn_DEF_NativeEnumerated,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_daps_SourceRelease_r16_constr_5,
		  memb_ASN_RRC_daps_SourceRelease_r16_constraint_1 },
		0, 0,
		"daps-SourceRelease-r16"
		},
	{ ATF_POINTER, 8, offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs, t316_r16),
		(ASN_TAG_CLASS_CONTEXT | (5 << 2)),
		+1,
		&asn_DEF_SetupRelease_2610P7,
		0,
		{ 0, 0, 0 },
		0, 0,
		"t316-r16"
		},
	{ ATF_POINTER, 7, offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs, needForGapsConfigNR_r16),
		(ASN_TAG_CLASS_CONTEXT | (6 << 2)),
		+1,
		&asn_DEF_ASN_RRC_SetupRelease_GapConfig,
		0,
		{ 0, 0, 0 },
		0, 0,
		"needForGapsConfigNR-r16"
		},
	{ ATF_POINTER, 6, offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs, onDemandSIB_Request_r16),
		(ASN_TAG_CLASS_CONTEXT | (7 << 2)),
		+1,
		&asn_DEF_SetupRelease_2610P9,
		0,
		{ 0, 0, 0 },
		0, 0,
		"onDemandSIB-Request-r16"
		},
	{ ATF_POINTER, 5, offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs, dedicatedPosSysInfoDelivery_r16),
		(ASN_TAG_CLASS_CONTEXT | (8 << 2)),
		-1,
		&asn_DEF_OCTET_STRING,
		0,
		{ 0, &asn_PER_memb_ASN_RRC_dedicatedPosSysInfoDelivery_r16_constr_8,
		  memb_ASN_RRC_dedicatedPosSysInfoDelivery_r16_constraint_1 },
		0, 0,
		"dedicatedPosSysInfoDelivery-r16"
		},
	{ ATF_POINTER, 4, offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs, sl_ConfigDedicatedNR_r16),
		(ASN_TAG_CLASS_CONTEXT | (9 << 2)),
		+1,
		&asn_DEF_SetupRelease_2610P10,
		0,
		{ 0, 0, 0 },
		0, 0,
		"sl-ConfigDedicatedNR-r16"
		},
	{ ATF_POINTER, 3, offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs,
			sl_ConfigDedicatedEUTRA_Info_r16),
		(ASN_TAG_CLASS_CONTEXT | (10 << 2)),
		+1,
		&asn_DEF_SetupRelease_2610P11,
		0,
		{ 0, 0, 0 },
		0, 0,
		"sl-ConfigDedicatedEUTRA-Info-r16"
		},
	{ ATF_POINTER, 2, offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs, targetCellSMTC_SCG_r16),
		(ASN_TAG_CLASS_CONTEXT | (11 << 2)),
		-1,
		&asn_DEF_ASN_RRC_SSB_MTC,
		0,
		{ 0, 0, 0 },
		0, 0,
		"targetCellSMTC-SCG-r16"
		},
	{ ATF_POINTER, 1, offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs, nonCriticalExtension),
		(ASN_TAG_CLASS_CONTEXT | (12 << 2)),
		-1,
		&asn_DEF_ASN_RRC_RRCReconfiguration_v1700_IEs,
		0,
		{ 0, 0, 0 },
		0, 0,
		"nonCriticalExtension"
		},
};
static const int asn_MAP_ASN_RRC_RRCReconfiguration_v1610_IEs_oms_1[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
};
static const ber_tlv_tag_t asn_DEF_ASN_RRC_RRCReconfiguration_v1610_IEs_tags_1[] = {
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_RRCReconfiguration_v1610_IEs_tag2el_1[] = {
	{ (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 }, /* otherConfig-v1610 */
	{ (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 }, /* bap-Config-r16 */
	{ (ASN_TAG_CLASS_CONTEXT | (2 << 2)), 2, 0, 0 }, /* iab-IP-AddressConfigurationList-r16 */
	{ (ASN_TAG_CLASS_CONTEXT | (3 << 2)), 3, 0, 0 }, /* conditionalReconfiguration-r16 */
	{ (ASN_TAG_CLASS_CONTEXT | (4 << 2)), 4, 0, 0 }, /* daps-SourceRelease-r16 */
	{ (ASN_TAG_CLASS_CONTEXT | (5 << 2)), 5, 0, 0 }, /* t316-r16 */
	{ (ASN_TAG_CLASS_CONTEXT | (6 << 2)), 6, 0, 0 }, /* needForGapsConfigNR-r16 */
	{ (ASN_TAG_CLASS_CONTEXT | (7 << 2)), 7, 0, 0 }, /* onDemandSIB-Request-r16 */
	{ (ASN_TAG_CLASS_CONTEXT | (8 << 2)), 8, 0, 0 }, /* dedicatedPosSysInfoDelivery-r16 */
	{ (ASN_TAG_CLASS_CONTEXT | (9 << 2)), 9, 0, 0 }, /* sl-ConfigDedicatedNR-r16 */
	{ (ASN_TAG_CLASS_CONTEXT | (10 << 2)), 10, 0, 0 }, /* sl-ConfigDedicatedEUTRA-Info-r16 */
	{ (ASN_TAG_CLASS_CONTEXT | (11 << 2)), 11, 0, 0 }, /* targetCellSMTC-SCG-r16 */
	{ (ASN_TAG_CLASS_CONTEXT | (12 << 2)), 12, 0, 0 } /* nonCriticalExtension */
};
asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_RRCReconfiguration_v1610_IEs_specs_1 = {
	sizeof(struct ASN_RRC_RRCReconfiguration_v1610_IEs),
	offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs, _asn_ctx),
	asn_MAP_ASN_RRC_RRCReconfiguration_v1610_IEs_tag2el_1,
	13,	/* Count of tags in the map */
	asn_MAP_ASN_RRC_RRCReconfiguration_v1610_IEs_oms_1,	/* Optional members */
	13, 0,	/* Root/Additions */
	-1,	/* First extension addition */
};
asn_TYPE_descriptor_t asn_DEF_ASN_RRC_RRCReconfiguration_v1610_IEs = {
	"RRCReconfiguration-v1610-IEs",
	"RRCReconfiguration-v1610-IEs",
	&asn_OP_SEQUENCE,
	asn_DEF_ASN_RRC_RRCReconfiguration_v1610_IEs_tags_1,
	sizeof(asn_DEF_ASN_RRC_RRCReconfiguration_v1610_IEs_tags_1)
		/sizeof(asn_DEF_ASN_RRC_RRCReconfiguration_v1610_IEs_tags_1[0]), /* 1 */
	asn_DEF_ASN_RRC_RRCReconfiguration_v1610_IEs_tags_1,	/* Same as above */
	sizeof(asn_DEF_ASN_RRC_RRCReconfiguration_v1610_IEs_tags_1)
		/sizeof(asn_DEF_ASN_RRC_RRCReconfiguration_v1610_IEs_tags_1[0]), /* 1 */
	{ 0, 0, SEQUENCE_constraint },
	asn_MBR_ASN_RRC_RRCReconfiguration_v1610_IEs_1,
	13,	/* Elements count */
	&asn_SPC_ASN_RRC_RRCReconfiguration_v1610_IEs_specs_1	/* Additional specs */
};
