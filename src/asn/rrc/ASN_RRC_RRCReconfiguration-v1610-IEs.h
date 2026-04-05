/*
 * ASN.1 type for RRCReconfiguration-v1610-IEs with Rel-17 root field ordering.
 * Preserves ASN_RRC naming while keeping CHO-bearing field positions standards-aligned.
 */

#ifndef	_ASN_RRC_RRCReconfiguration_v1610_IEs_H_
#define	_ASN_RRC_RRCReconfiguration_v1610_IEs_H_


#include <asn_application.h>

/* Including external dependencies */
#include <NativeEnumerated.h>
#include <OCTET_STRING.h>
#include <constr_SEQUENCE.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct ASN_RRC_ConditionalReconfiguration;
struct ASN_RRC_SetupRelease;
struct ASN_RRC_SSB_MTC;
struct ASN_RRC_OtherConfig;
struct ASN_RRC_RRCReconfiguration_v1700_IEs;

/* ASN_RRC_RRCReconfiguration-v1610-IEs */
typedef struct ASN_RRC_RRCReconfiguration_v1610_IEs {
	struct ASN_RRC_OtherConfig	*otherConfig_v1610;	/* OPTIONAL */
	struct ASN_RRC_SetupRelease	*bap_Config_r16;	/* OPTIONAL */
	OCTET_STRING_t	*iab_IP_AddressConfigurationList_r16;	/* OPTIONAL */
	struct ASN_RRC_ConditionalReconfiguration	*conditionalReconfiguration;	/* OPTIONAL */
	long	*daps_SourceRelease_r16;	/* OPTIONAL, ENUMERATED { true(0) } */
	struct ASN_RRC_SetupRelease	*t316_r16;	/* OPTIONAL */
	struct ASN_RRC_SetupRelease	*needForGapsConfigNR_r16;	/* OPTIONAL */
	struct ASN_RRC_SetupRelease	*onDemandSIB_Request_r16;	/* OPTIONAL */
	OCTET_STRING_t	*dedicatedPosSysInfoDelivery_r16;	/* OPTIONAL */
	struct ASN_RRC_SetupRelease	*sl_ConfigDedicatedNR_r16;	/* OPTIONAL */
	struct ASN_RRC_SetupRelease	*sl_ConfigDedicatedEUTRA_Info_r16;	/* OPTIONAL */
	struct ASN_RRC_SSB_MTC	*targetCellSMTC_SCG_r16;	/* OPTIONAL */
	struct ASN_RRC_RRCReconfiguration_v1700_IEs *nonCriticalExtension; /* OPTIONAL */

	/* Context for parsing across buffer boundaries */
	asn_struct_ctx_t _asn_ctx;
} ASN_RRC_RRCReconfiguration_v1610_IEs_t;

/* Implementation */
extern asn_TYPE_descriptor_t asn_DEF_ASN_RRC_RRCReconfiguration_v1610_IEs;
extern asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_RRCReconfiguration_v1610_IEs_specs_1;
extern asn_TYPE_member_t asn_MBR_ASN_RRC_RRCReconfiguration_v1610_IEs_1[13];

#ifdef __cplusplus
}
#endif

#endif	/* _ASN_RRC_RRCReconfiguration_v1610_IEs_H_ */
#include <asn_internal.h>
