/*
 * Hand-crafted extension for Conditional Handover (CHO) support.
 * Models RRCReconfiguration-v1610-IEs from TS 38.331 Release 16/17.
 */

#include "ASN_RRC_RRCReconfiguration-v1610-IEs.h"

#include "ASN_RRC_ConditionalReconfiguration.h"

/* nonCriticalExtension — empty stub for future extensions */
static const ber_tlv_tag_t asn_DEF_ASN_RRC_v1610_nonCriticalExtension_tags_4[] = {
	(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_v1610_nonCriticalExtension_specs_4 = {
	sizeof(struct ASN_RRC_RRCReconfiguration_v1610_IEs__nonCriticalExtension),
	offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs__nonCriticalExtension, _asn_ctx),
	0,	/* No top level tags */
	0,	/* No tags in the map */
	0, 0, 0,	/* Optional elements (not needed) */
	-1,	/* First extension addition */
};
static /* Use -fall-defs-global to expose */
asn_TYPE_descriptor_t asn_DEF_ASN_RRC_v1610_nonCriticalExtension_4 = {
	"nonCriticalExtension",
	"nonCriticalExtension",
	&asn_OP_SEQUENCE,
	asn_DEF_ASN_RRC_v1610_nonCriticalExtension_tags_4,
	sizeof(asn_DEF_ASN_RRC_v1610_nonCriticalExtension_tags_4)
		/sizeof(asn_DEF_ASN_RRC_v1610_nonCriticalExtension_tags_4[0]) - 1, /* 1 */
	asn_DEF_ASN_RRC_v1610_nonCriticalExtension_tags_4,
	sizeof(asn_DEF_ASN_RRC_v1610_nonCriticalExtension_tags_4)
		/sizeof(asn_DEF_ASN_RRC_v1610_nonCriticalExtension_tags_4[0]), /* 2 */
	{ 0, 0, SEQUENCE_constraint },
	0, 0,	/* No members */
	&asn_SPC_ASN_RRC_v1610_nonCriticalExtension_specs_4	/* Additional specs */
};

asn_TYPE_member_t asn_MBR_ASN_RRC_RRCReconfiguration_v1610_IEs_1[] = {
	{ ATF_POINTER, 2, offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs, conditionalReconfiguration),
		(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
		-1,	/* IMPLICIT tag at current level */
		&asn_DEF_ASN_RRC_ConditionalReconfiguration,
		0,
		{ 0, 0, 0 },
		0, 0, /* No default value */
		"conditionalReconfiguration"
		},
	{ ATF_POINTER, 1, offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs, nonCriticalExtension),
		(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
		0,
		&asn_DEF_ASN_RRC_v1610_nonCriticalExtension_4,
		0,
		{ 0, 0, 0 },
		0, 0, /* No default value */
		"nonCriticalExtension"
		},
};
static const int asn_MAP_ASN_RRC_RRCReconfiguration_v1610_IEs_oms_1[] = { 0, 1 };
static const ber_tlv_tag_t asn_DEF_ASN_RRC_RRCReconfiguration_v1610_IEs_tags_1[] = {
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_RRCReconfiguration_v1610_IEs_tag2el_1[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 }, /* conditionalReconfiguration */
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 } /* nonCriticalExtension */
};
asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_RRCReconfiguration_v1610_IEs_specs_1 = {
	sizeof(struct ASN_RRC_RRCReconfiguration_v1610_IEs),
	offsetof(struct ASN_RRC_RRCReconfiguration_v1610_IEs, _asn_ctx),
	asn_MAP_ASN_RRC_RRCReconfiguration_v1610_IEs_tag2el_1,
	2,	/* Count of tags in the map */
	asn_MAP_ASN_RRC_RRCReconfiguration_v1610_IEs_oms_1,	/* Optional members */
	2, 0,	/* Root/Additions */
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
	2,	/* Elements count */
	&asn_SPC_ASN_RRC_RRCReconfiguration_v1610_IEs_specs_1	/* Additional specs */
};
