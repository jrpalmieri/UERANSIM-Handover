/*
 * ASN.1 type for NTN-TriggerConfig-r17.
 * Minimal Rel-17 support carrying t-Service-r17.
 */

#ifndef _ASN_RRC_NTN_TriggerConfig_r17_H_
#define _ASN_RRC_NTN_TriggerConfig_r17_H_

#include <asn_application.h>
#include <constr_SEQUENCE.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ASN_RRC_NTN_TriggerConfig_r17 {
    long t_Service_r17;

    /* Context for parsing across buffer boundaries */
    asn_struct_ctx_t _asn_ctx;
} ASN_RRC_NTN_TriggerConfig_r17_t;

extern asn_TYPE_descriptor_t asn_DEF_ASN_RRC_NTN_TriggerConfig_r17;
extern asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_NTN_TriggerConfig_r17_specs_1;
extern asn_TYPE_member_t asn_MBR_ASN_RRC_NTN_TriggerConfig_r17_1[1];

#ifdef __cplusplus
}
#endif

#endif /* _ASN_RRC_NTN_TriggerConfig_r17_H_ */
#include <asn_internal.h>
