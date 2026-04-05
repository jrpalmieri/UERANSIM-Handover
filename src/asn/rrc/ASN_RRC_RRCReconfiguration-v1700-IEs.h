/*
 * ASN.1 type for RRCReconfiguration-v1700-IEs.
 *
 * This keeps the Rel-17 extension chain concrete and decode-safe while several
 * deep Rel-17 payload types are still represented as opaque payloads.
 */

#ifndef _ASN_RRC_RRCReconfiguration_v1700_IEs_H_
#define _ASN_RRC_RRCReconfiguration_v1700_IEs_H_

#include <asn_application.h>

/* Including external dependencies */
#include <NativeEnumerated.h>
#include <OCTET_STRING.h>
#include <constr_SEQUENCE.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dependencies */
typedef enum ASN_RRC_RRCReconfiguration_v1700_IEs__scg_State_r17 {
	ASN_RRC_RRCReconfiguration_v1700_IEs__scg_State_r17_deactivated = 0
} e_ASN_RRC_RRCReconfiguration_v1700_IEs__scg_State_r17;

/* ASN_RRC_RRCReconfiguration-v1700-IEs */
typedef struct ASN_RRC_RRCReconfiguration_v1700_IEs {
	OCTET_STRING_t *otherConfig_v1700; /* OPTIONAL */
	OCTET_STRING_t *sl_L2RelayUE_Config_r17; /* OPTIONAL */
	OCTET_STRING_t *sl_L2RemoteUE_Config_r17; /* OPTIONAL */
	OCTET_STRING_t *dedicatedPagingDelivery_r17; /* OPTIONAL */
	OCTET_STRING_t *needForGapNCSG_ConfigNR_r17; /* OPTIONAL */
	OCTET_STRING_t *needForGapNCSG_ConfigEUTRA_r17; /* OPTIONAL */
	OCTET_STRING_t *musim_GapConfig_r17; /* OPTIONAL */
	OCTET_STRING_t *ul_GapFR2_Config_r17; /* OPTIONAL */
	long *scg_State_r17; /* OPTIONAL, ENUMERATED { deactivated(0) } */
	OCTET_STRING_t *appLayerMeasConfig_r17; /* OPTIONAL */
	OCTET_STRING_t *ue_TxTEG_RequestUL_TDOA_Config_r17; /* OPTIONAL */
	OCTET_STRING_t *nonCriticalExtension; /* OPTIONAL */

	/* Context for parsing across buffer boundaries */
	asn_struct_ctx_t _asn_ctx;
} ASN_RRC_RRCReconfiguration_v1700_IEs_t;

/* Implementation */
extern asn_TYPE_descriptor_t asn_DEF_ASN_RRC_RRCReconfiguration_v1700_IEs;
extern asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_RRCReconfiguration_v1700_IEs_specs_1;
extern asn_TYPE_member_t asn_MBR_ASN_RRC_RRCReconfiguration_v1700_IEs_1[12];

#ifdef __cplusplus
}
#endif

#endif /* _ASN_RRC_RRCReconfiguration_v1700_IEs_H_ */
