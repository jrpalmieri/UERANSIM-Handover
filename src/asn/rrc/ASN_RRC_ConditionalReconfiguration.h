/*
 * Hand-crafted ASN.1 type for Conditional Handover support.
 * Models ConditionalReconfiguration from TS 38.331 Release 16/17.
 */

#ifndef	_ASN_RRC_ConditionalReconfiguration_H_
#define	_ASN_RRC_ConditionalReconfiguration_H_


#include <asn_application.h>

/* Including external dependencies */
#include <asn_SEQUENCE_OF.h>
#include <constr_SEQUENCE_OF.h>
#include <constr_SEQUENCE.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct ASN_RRC_CondReconfigToAddMod;

/* ConditionalReconfiguration */
typedef struct ASN_RRC_ConditionalReconfiguration {
	struct ASN_RRC_ConditionalReconfiguration__condReconfigToAddModList {
		A_SEQUENCE_OF(struct ASN_RRC_CondReconfigToAddMod) list;

		/* Context for parsing across buffer boundaries */
		asn_struct_ctx_t _asn_ctx;
	} *condReconfigToAddModList;	/* OPTIONAL */
	/* condReconfigToRemoveList omitted — not needed for CHO execution */

	/* Context for parsing across buffer boundaries */
	asn_struct_ctx_t _asn_ctx;
} ASN_RRC_ConditionalReconfiguration_t;

/* Implementation */
extern asn_TYPE_descriptor_t asn_DEF_ASN_RRC_ConditionalReconfiguration;
extern asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_ConditionalReconfiguration_specs_1;
extern asn_TYPE_member_t asn_MBR_ASN_RRC_ConditionalReconfiguration_1[1];

#ifdef __cplusplus
}
#endif

#endif	/* _ASN_RRC_ConditionalReconfiguration_H_ */
#include <asn_internal.h>
