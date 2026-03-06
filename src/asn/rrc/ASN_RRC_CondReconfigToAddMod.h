/*
 * Hand-crafted ASN.1 type for Conditional Handover support.
 * Models CondReconfigToAddMod from TS 38.331 Release 16/17.
 *
 * CondReconfigToAddMod ::= SEQUENCE {
 *     condReconfigId          CondReconfigId,             -- INTEGER (1..8)
 *     condExecutionCond       CondReconfigExecCond,       -- SEQUENCE (SIZE(1..2)) OF MeasId  (OPTIONAL)
 *     condRRCReconfig         OCTET STRING (CONTAINING RRCReconfiguration) (OPTIONAL)
 * }
 */

#ifndef	_ASN_RRC_CondReconfigToAddMod_H_
#define	_ASN_RRC_CondReconfigToAddMod_H_


#include <asn_application.h>

/* Including external dependencies */
#include <NativeInteger.h>
#include <OCTET_STRING.h>
#include <asn_SEQUENCE_OF.h>
#include <constr_SEQUENCE_OF.h>
#include <constr_SEQUENCE.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CondReconfigToAddMod */
typedef struct ASN_RRC_CondReconfigToAddMod {
	long	 condReconfigId;	/* INTEGER (1..8) */
	struct ASN_RRC_CondReconfigToAddMod__condExecutionCond {
		A_SEQUENCE_OF(long) list;   /* SEQUENCE (SIZE(1..2)) OF MeasId (INTEGER 1..64) */

		/* Context for parsing across buffer boundaries */
		asn_struct_ctx_t _asn_ctx;
	} *condExecutionCond;		/* OPTIONAL */
	OCTET_STRING_t	*condRRCReconfig;	/* OPTIONAL — contains UPER-encoded RRCReconfiguration */

	/* Context for parsing across buffer boundaries */
	asn_struct_ctx_t _asn_ctx;
} ASN_RRC_CondReconfigToAddMod_t;

/* Implementation */
extern asn_TYPE_descriptor_t asn_DEF_ASN_RRC_CondReconfigToAddMod;
extern asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_CondReconfigToAddMod_specs_1;
extern asn_TYPE_member_t asn_MBR_ASN_RRC_CondReconfigToAddMod_1[3];

#ifdef __cplusplus
}
#endif

#endif	/* _ASN_RRC_CondReconfigToAddMod_H_ */
#include <asn_internal.h>
