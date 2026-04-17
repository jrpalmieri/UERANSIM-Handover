/*
 * ASN.1 type for Conditional Handover support.
 * Tracks Rel-17 ordering for CondReconfigToAddMod-r16 while preserving ASN_RRC naming.
 */

#ifndef	_ASN_RRC_CondReconfigToAddMod_H_
#define	_ASN_RRC_CondReconfigToAddMod_H_


#include <asn_application.h>

/* Including external dependencies */
#include <NativeInteger.h>
#include <OCTET_STRING.h>
#include "ASN_RRC_MeasId.h"
#include <asn_SEQUENCE_OF.h>
#include <constr_SEQUENCE_OF.h>
#include <constr_SEQUENCE.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct ASN_RRC_CondTriggerConfig_r16;

/* CondReconfigToAddMod */
typedef struct ASN_RRC_CondReconfigToAddMod {
	long	 condReconfigId;	/* INTEGER (1..8) */
	struct ASN_RRC_CondReconfigToAddMod__condExecutionCond {
		A_SEQUENCE_OF(ASN_RRC_MeasId_t) list;	/* SEQUENCE (SIZE(1..2)) OF MeasId */

		/* Context for parsing across buffer boundaries */
		asn_struct_ctx_t _asn_ctx;
	} *condExecutionCond;		/* OPTIONAL */
	OCTET_STRING_t	*condRRCReconfig;	/* OPTIONAL */
	/* Rel-17 extension addition. */
	OCTET_STRING_t	*condExecutionCondSCG;	/* OPTIONAL */
	/* Rel-16/17 extension addition for NTN timing trigger. */
	struct ASN_RRC_CondTriggerConfig_r16 *condTriggerConfig_r16;	/* OPTIONAL */

	/* Context for parsing across buffer boundaries */
	asn_struct_ctx_t _asn_ctx;
} ASN_RRC_CondReconfigToAddMod_t;

/* Implementation */
extern asn_TYPE_descriptor_t asn_DEF_ASN_RRC_CondReconfigToAddMod;
extern asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_CondReconfigToAddMod_specs_1;
extern asn_TYPE_member_t asn_MBR_ASN_RRC_CondReconfigToAddMod_1[5];

#ifdef __cplusplus
}
#endif

#endif	/* _ASN_RRC_CondReconfigToAddMod_H_ */
#include <asn_internal.h>
