/*
 * ASN.1 type for CondTriggerConfig-r16.
 * Minimal Rel-17 support carrying ntn-TriggerConfig-r17.
 */

#ifndef _ASN_RRC_CondTriggerConfig_r16_H_
#define _ASN_RRC_CondTriggerConfig_r16_H_

#include <asn_application.h>
#include <constr_SEQUENCE.h>
#include <constr_CHOICE.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct ASN_RRC_NTN_TriggerConfig_r17;

/* Dependencies */
typedef enum ASN_RRC_CondTriggerConfig_r16__triggerEvent_PR {
    ASN_RRC_CondTriggerConfig_r16__triggerEvent_PR_NOTHING,
    ASN_RRC_CondTriggerConfig_r16__triggerEvent_PR_condEventD1_r17
} ASN_RRC_CondTriggerConfig_r16__triggerEvent_PR;

typedef struct ASN_RRC_CondTriggerConfig_r16__triggerEvent__condEventD1_r17__mue_Threshold_r17 {
    long timeToExit_r17;

    /* Context for parsing across buffer boundaries */
    asn_struct_ctx_t _asn_ctx;
} ASN_RRC_CondTriggerConfig_r16__triggerEvent__condEventD1_r17__mue_Threshold_r17_t;

typedef struct ASN_RRC_CondTriggerConfig_r16__triggerEvent__condEventD1_r17 {
    ASN_RRC_CondTriggerConfig_r16__triggerEvent__condEventD1_r17__mue_Threshold_r17_t mue_Threshold_r17;
    long hysteresis_r17;
    long timeToTrigger_r17;

    /* Context for parsing across buffer boundaries */
    asn_struct_ctx_t _asn_ctx;
} ASN_RRC_CondTriggerConfig_r16__triggerEvent__condEventD1_r17_t;

typedef struct ASN_RRC_CondTriggerConfig_r16 {
    struct ASN_RRC_CondTriggerConfig_r16__triggerEvent {
        ASN_RRC_CondTriggerConfig_r16__triggerEvent_PR present;
        union ASN_RRC_CondTriggerConfig_r16__triggerEvent_u {
            ASN_RRC_CondTriggerConfig_r16__triggerEvent__condEventD1_r17_t *condEventD1_r17;
        } choice;

        /* Context for parsing across buffer boundaries */
        asn_struct_ctx_t _asn_ctx;
    } *triggerEvent; /* OPTIONAL */

    struct ASN_RRC_NTN_TriggerConfig_r17 *ntn_TriggerConfig_r17; /* OPTIONAL */

    /* Context for parsing across buffer boundaries */
    asn_struct_ctx_t _asn_ctx;
} ASN_RRC_CondTriggerConfig_r16_t;

extern asn_TYPE_descriptor_t asn_DEF_ASN_RRC_CondTriggerConfig_r16;
extern asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_CondTriggerConfig_r16_specs_1;
extern asn_TYPE_member_t asn_MBR_ASN_RRC_CondTriggerConfig_r16_1[2];

#ifdef __cplusplus
}
#endif

#endif /* _ASN_RRC_CondTriggerConfig_r16_H_ */
#include <asn_internal.h>
