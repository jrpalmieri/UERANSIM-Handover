/*
 * Hand-crafted extension for Conditional Handover (CHO) support.
 * Models RRCReconfiguration-v1610-IEs from TS 38.331 Release 16/17.
 *
 * Only the fields relevant to CHO are included; others are omitted.
 */

#ifndef	_ASN_RRC_RRCReconfiguration_v1610_IEs_H_
#define	_ASN_RRC_RRCReconfiguration_v1610_IEs_H_


#include <asn_application.h>

/* Including external dependencies */
#include <constr_SEQUENCE.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct ASN_RRC_ConditionalReconfiguration;

/* ASN_RRC_RRCReconfiguration-v1610-IEs */
typedef struct ASN_RRC_RRCReconfiguration_v1610_IEs {
	struct ASN_RRC_ConditionalReconfiguration	*conditionalReconfiguration;	/* OPTIONAL */
	struct ASN_RRC_RRCReconfiguration_v1610_IEs__nonCriticalExtension {

		/* Context for parsing across buffer boundaries */
		asn_struct_ctx_t _asn_ctx;
	} *nonCriticalExtension;	/* OPTIONAL */

	/* Context for parsing across buffer boundaries */
	asn_struct_ctx_t _asn_ctx;
} ASN_RRC_RRCReconfiguration_v1610_IEs_t;

/* Implementation */
extern asn_TYPE_descriptor_t asn_DEF_ASN_RRC_RRCReconfiguration_v1610_IEs;
extern asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_RRCReconfiguration_v1610_IEs_specs_1;
extern asn_TYPE_member_t asn_MBR_ASN_RRC_RRCReconfiguration_v1610_IEs_1[2];

#ifdef __cplusplus
}
#endif

#endif	/* _ASN_RRC_RRCReconfiguration_v1610_IEs_H_ */
#include <asn_internal.h>
