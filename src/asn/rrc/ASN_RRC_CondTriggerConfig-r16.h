/*
 * ASN.1 type for CondTriggerConfig-r16 per 3GPP TS 38.331 Rel-17.
 */

#ifndef _ASN_RRC_CondTriggerConfig_r16_H_
#define _ASN_RRC_CondTriggerConfig_r16_H_

#include <asn_application.h>
#include <constr_SEQUENCE.h>
#include <constr_CHOICE.h>
#include <NativeInteger.h>
#include <NativeEnumerated.h>
#include <NULL.h>
#include "ASN_RRC_MeasTriggerQuantityOffset.h"
#include "ASN_RRC_MeasTriggerQuantity.h"
#include "ASN_RRC_Hysteresis.h"
#include "ASN_RRC_TimeToTrigger.h"
#include "ASN_RRC_NR-RS-Type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Dependencies */
typedef enum ASN_RRC_CondTriggerConfig_r16__condEventId_PR {
    ASN_RRC_CondTriggerConfig_r16__condEventId_PR_NOTHING,
    ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventA3,
    ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventA5,
    /* Extensions may appear below */
    ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventA4_r17,
    ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventD1_r17,
    ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventT1_r17
} ASN_RRC_CondTriggerConfig_r16__condEventId_PR;

typedef enum ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17_PR {
    ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17_PR_NOTHING,
    ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17_PR_fixedReferenceLocation_r17,
    ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17_PR_nadirReferenceLocation_r17
    /* Extensions may appear below */
} ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17_PR;

typedef enum ASN_RRC_CondTriggerConfig_r16__latitudeSign {
    ASN_RRC_CondTriggerConfig_r16__latitudeSign_north = 0,
    ASN_RRC_CondTriggerConfig_r16__latitudeSign_south = 1
} e_ASN_RRC_CondTriggerConfig_r16__latitudeSign;

/* ASN_RRC_CondTriggerConfig_r16 */
typedef struct ASN_RRC_CondTriggerConfig_r16 {
    struct ASN_RRC_CondTriggerConfig_r16__condEventId {
        ASN_RRC_CondTriggerConfig_r16__condEventId_PR present;
        union ASN_RRC_CondTriggerConfig_r16__ASN_RRC_condEventId_u {
            struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA3 {
                ASN_RRC_MeasTriggerQuantityOffset_t  a3_Offset;
                ASN_RRC_Hysteresis_t                 hysteresis;
                ASN_RRC_TimeToTrigger_t              timeToTrigger;

                /* Context for parsing across buffer boundaries */
                asn_struct_ctx_t _asn_ctx;
            } *condEventA3;
            struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA5 {
                ASN_RRC_MeasTriggerQuantity_t  a5_Threshold1;
                ASN_RRC_MeasTriggerQuantity_t  a5_Threshold2;
                ASN_RRC_Hysteresis_t           hysteresis;
                ASN_RRC_TimeToTrigger_t        timeToTrigger;

                /* Context for parsing across buffer boundaries */
                asn_struct_ctx_t _asn_ctx;
            } *condEventA5;
            struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA4_r17 {
                ASN_RRC_MeasTriggerQuantity_t  a4_Threshold_r17;
                ASN_RRC_Hysteresis_t           hysteresis_r17;
                ASN_RRC_TimeToTrigger_t        timeToTrigger_r17;

                /* Context for parsing across buffer boundaries */
                asn_struct_ctx_t _asn_ctx;
            } *condEventA4_r17;
            struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17 {
                long  distanceThreshFromReference1_r17;  /* INTEGER(0..65525) */
                long  distanceThreshFromReference2_r17;  /* INTEGER(0..65525) */
                struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17 {
                    ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17_PR present;
                    union ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17_u {
                        struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17__fixedReferenceLocation_r17 {
                            long  latitudeSign;     /* e_ASN_RRC_CondTriggerConfig_r16__latitudeSign */
                            long  degreesLatitude;  /* INTEGER(0..8388607) */
                            long  degreesLongitude; /* INTEGER(-8388608..8388607) */

                            /* Context for parsing across buffer boundaries */
                            asn_struct_ctx_t _asn_ctx;
                        } *fixedReferenceLocation_r17;
                        NULL_t *nadirReferenceLocation_r17;
                        /*
                         * This type is extensible,
                         * possible extensions are below.
                         */
                    } choice;

                    /* Context for parsing across buffer boundaries */
                    asn_struct_ctx_t _asn_ctx;
                } referenceLocation1_r17;
                struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17
                    referenceLocation2_r17;
                long  hysteresisLocation_r17;  /* INTEGER(0..32768) */
                ASN_RRC_TimeToTrigger_t  timeToTrigger_r17;

                /* Context for parsing across buffer boundaries */
                asn_struct_ctx_t _asn_ctx;
            } *condEventD1_r17;
            struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventT1_r17 {
                long  t1_Threshold_r17;  /* INTEGER(0..549755813887) */
                long  duration_r17;      /* INTEGER(1..6000) */

                /* Context for parsing across buffer boundaries */
                asn_struct_ctx_t _asn_ctx;
            } *condEventT1_r17;
            /*
             * This type is extensible,
             * possible extensions are below.
             */
        } choice;

        /* Context for parsing across buffer boundaries */
        asn_struct_ctx_t _asn_ctx;
    } condEventId;
    ASN_RRC_NR_RS_Type_t  rsType_r16;

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
