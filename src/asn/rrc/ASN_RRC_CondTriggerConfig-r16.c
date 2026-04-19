#include "ASN_RRC_CondTriggerConfig-r16.h"

#include <NativeInteger.h>
#include <NativeEnumerated.h>

/* ================================================================== */
/* Constraint functions                                                */
/* ================================================================== */

static int memb_ASN_RRC_ctc_distanceThreshFromReference_r17_constraint_1(
    const asn_TYPE_descriptor_t *td, const void *sptr,
    asn_app_constraint_failed_f *ctfailcb, void *app_key)
{
    long value;
    if (!sptr) { ASN__CTFAIL(app_key, td, sptr, "%s: value not given (%s:%d)", td->name, __FILE__, __LINE__); return -1; }
    value = *(const long *)sptr;
    if (value < 0 || value > 65525) { ASN__CTFAIL(app_key, td, sptr, "%s: constraint failed (%s:%d)", td->name, __FILE__, __LINE__); return -1; }
    return 0;
}

static int memb_ASN_RRC_ctc_latitudeSign_constraint_1(
    const asn_TYPE_descriptor_t *td, const void *sptr,
    asn_app_constraint_failed_f *ctfailcb, void *app_key)
{
    long value;
    if (!sptr) { ASN__CTFAIL(app_key, td, sptr, "%s: value not given (%s:%d)", td->name, __FILE__, __LINE__); return -1; }
    value = *(const long *)sptr;
    if (value < 0 || value > 1) { ASN__CTFAIL(app_key, td, sptr, "%s: constraint failed (%s:%d)", td->name, __FILE__, __LINE__); return -1; }
    return 0;
}

static int memb_ASN_RRC_ctc_degreesLatitude_constraint_1(
    const asn_TYPE_descriptor_t *td, const void *sptr,
    asn_app_constraint_failed_f *ctfailcb, void *app_key)
{
    long value;
    if (!sptr) { ASN__CTFAIL(app_key, td, sptr, "%s: value not given (%s:%d)", td->name, __FILE__, __LINE__); return -1; }
    value = *(const long *)sptr;
    if (value < 0 || value > 8388607) { ASN__CTFAIL(app_key, td, sptr, "%s: constraint failed (%s:%d)", td->name, __FILE__, __LINE__); return -1; }
    return 0;
}

static int memb_ASN_RRC_ctc_degreesLongitude_constraint_1(
    const asn_TYPE_descriptor_t *td, const void *sptr,
    asn_app_constraint_failed_f *ctfailcb, void *app_key)
{
    long value;
    if (!sptr) { ASN__CTFAIL(app_key, td, sptr, "%s: value not given (%s:%d)", td->name, __FILE__, __LINE__); return -1; }
    value = *(const long *)sptr;
    if (value < -8388608 || value > 8388607) { ASN__CTFAIL(app_key, td, sptr, "%s: constraint failed (%s:%d)", td->name, __FILE__, __LINE__); return -1; }
    return 0;
}

static int memb_ASN_RRC_ctc_hysteresisLocation_r17_constraint_1(
    const asn_TYPE_descriptor_t *td, const void *sptr,
    asn_app_constraint_failed_f *ctfailcb, void *app_key)
{
    long value;
    if (!sptr) { ASN__CTFAIL(app_key, td, sptr, "%s: value not given (%s:%d)", td->name, __FILE__, __LINE__); return -1; }
    value = *(const long *)sptr;
    if (value < 0 || value > 32768) { ASN__CTFAIL(app_key, td, sptr, "%s: constraint failed (%s:%d)", td->name, __FILE__, __LINE__); return -1; }
    return 0;
}

static int memb_ASN_RRC_ctc_t1_Threshold_r17_constraint_1(
    const asn_TYPE_descriptor_t *td, const void *sptr,
    asn_app_constraint_failed_f *ctfailcb, void *app_key)
{
    long value;
    if (!sptr) { ASN__CTFAIL(app_key, td, sptr, "%s: value not given (%s:%d)", td->name, __FILE__, __LINE__); return -1; }
    value = *(const long *)sptr;
    if (value < 0 || value > 549755813887L) { ASN__CTFAIL(app_key, td, sptr, "%s: constraint failed (%s:%d)", td->name, __FILE__, __LINE__); return -1; }
    return 0;
}

static int memb_ASN_RRC_ctc_duration_r17_constraint_1(
    const asn_TYPE_descriptor_t *td, const void *sptr,
    asn_app_constraint_failed_f *ctfailcb, void *app_key)
{
    long value;
    if (!sptr) { ASN__CTFAIL(app_key, td, sptr, "%s: value not given (%s:%d)", td->name, __FILE__, __LINE__); return -1; }
    value = *(const long *)sptr;
    if (value < 1 || value > 6000) { ASN__CTFAIL(app_key, td, sptr, "%s: constraint failed (%s:%d)", td->name, __FILE__, __LINE__); return -1; }
    return 0;
}

/* ================================================================== */
/* PER constraints                                                     */
/* ================================================================== */

static asn_per_constraints_t asn_PER_memb_ASN_RRC_ctc_distanceThreshFromReference_r17_constr CC_NOTUSED = {
    { APC_CONSTRAINED, 16, 16, 0, 65525 },
    { APC_UNCONSTRAINED, -1, -1, 0, 0 },
    0, 0
};
static asn_per_constraints_t asn_PER_memb_ASN_RRC_ctc_latitudeSign_constr CC_NOTUSED = {
    { APC_CONSTRAINED, 1, 1, 0, 1 },
    { APC_UNCONSTRAINED, -1, -1, 0, 0 },
    0, 0
};
static asn_per_constraints_t asn_PER_memb_ASN_RRC_ctc_degreesLatitude_constr CC_NOTUSED = {
    { APC_CONSTRAINED, 23, 23, 0, 8388607 },
    { APC_UNCONSTRAINED, -1, -1, 0, 0 },
    0, 0
};
static asn_per_constraints_t asn_PER_memb_ASN_RRC_ctc_degreesLongitude_constr CC_NOTUSED = {
    { APC_CONSTRAINED, 24, 24, -8388608, 8388607 },
    { APC_UNCONSTRAINED, -1, -1, 0, 0 },
    0, 0
};
static asn_per_constraints_t asn_PER_memb_ASN_RRC_ctc_hysteresisLocation_r17_constr CC_NOTUSED = {
    { APC_CONSTRAINED, 16, 16, 0, 32768 },
    { APC_UNCONSTRAINED, -1, -1, 0, 0 },
    0, 0
};
static asn_per_constraints_t asn_PER_memb_ASN_RRC_ctc_t1_Threshold_r17_constr CC_NOTUSED = {
    { APC_CONSTRAINED, 39, 39, 0, 549755813887L },
    { APC_UNCONSTRAINED, -1, -1, 0, 0 },
    0, 0
};
static asn_per_constraints_t asn_PER_memb_ASN_RRC_ctc_duration_r17_constr CC_NOTUSED = {
    { APC_CONSTRAINED, 13, 13, 1, 6000 },
    { APC_UNCONSTRAINED, -1, -1, 0, 0 },
    0, 0
};
static asn_per_constraints_t asn_PER_type_ASN_RRC_ctc_refLoc_constr CC_NOTUSED = {
    { APC_CONSTRAINED, 1, 1, 0, 1 },
    { APC_UNCONSTRAINED, -1, -1, 0, 0 },
    0, 0
};
static asn_per_constraints_t asn_PER_type_ASN_RRC_ctc_condEventId_constr CC_NOTUSED = {
    { APC_CONSTRAINED, 3, 3, 0, 4 },
    { APC_UNCONSTRAINED, -1, -1, 0, 0 },
    0, 0
};

/* ================================================================== */
/* condEventA3                                                         */
/* ================================================================== */

static asn_TYPE_member_t asn_MBR_ASN_RRC_condEventA3_2[] = {
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA3, a3_Offset),
        (ASN_TAG_CLASS_CONTEXT | (0 << 2)), +1,
        &asn_DEF_ASN_RRC_MeasTriggerQuantityOffset, 0, { 0, 0, 0 }, 0, 0, "a3-Offset" },
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA3, hysteresis),
        (ASN_TAG_CLASS_CONTEXT | (1 << 2)), -1,
        &asn_DEF_ASN_RRC_Hysteresis, 0, { 0, 0, 0 }, 0, 0, "hysteresis" },
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA3, timeToTrigger),
        (ASN_TAG_CLASS_CONTEXT | (2 << 2)), -1,
        &asn_DEF_ASN_RRC_TimeToTrigger, 0, { 0, 0, 0 }, 0, 0, "timeToTrigger" },
};
static const ber_tlv_tag_t asn_DEF_ASN_RRC_condEventA3_tags_2[] = {
    (ASN_TAG_CLASS_CONTEXT | (0 << 2)),
    (ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_condEventA3_tag2el_2[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (2 << 2)), 2, 0, 0 }
};
static asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_condEventA3_specs_2 = {
    sizeof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA3),
    offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA3, _asn_ctx),
    asn_MAP_ASN_RRC_condEventA3_tag2el_2, 3, 0, 0, 0, -1,
};
static asn_TYPE_descriptor_t asn_DEF_ASN_RRC_condEventA3_2 = {
    "condEventA3", "condEventA3", &asn_OP_SEQUENCE,
    asn_DEF_ASN_RRC_condEventA3_tags_2,
    sizeof(asn_DEF_ASN_RRC_condEventA3_tags_2) / sizeof(asn_DEF_ASN_RRC_condEventA3_tags_2[0]) - 1,
    asn_DEF_ASN_RRC_condEventA3_tags_2,
    sizeof(asn_DEF_ASN_RRC_condEventA3_tags_2) / sizeof(asn_DEF_ASN_RRC_condEventA3_tags_2[0]),
    { 0, 0, SEQUENCE_constraint },
    asn_MBR_ASN_RRC_condEventA3_2, 3, &asn_SPC_ASN_RRC_condEventA3_specs_2
};

/* ================================================================== */
/* condEventA5                                                         */
/* ================================================================== */

static asn_TYPE_member_t asn_MBR_ASN_RRC_condEventA5_3[] = {
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA5, a5_Threshold1),
        (ASN_TAG_CLASS_CONTEXT | (0 << 2)), +1,
        &asn_DEF_ASN_RRC_MeasTriggerQuantity, 0, { 0, 0, 0 }, 0, 0, "a5-Threshold1" },
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA5, a5_Threshold2),
        (ASN_TAG_CLASS_CONTEXT | (1 << 2)), +1,
        &asn_DEF_ASN_RRC_MeasTriggerQuantity, 0, { 0, 0, 0 }, 0, 0, "a5-Threshold2" },
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA5, hysteresis),
        (ASN_TAG_CLASS_CONTEXT | (2 << 2)), -1,
        &asn_DEF_ASN_RRC_Hysteresis, 0, { 0, 0, 0 }, 0, 0, "hysteresis" },
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA5, timeToTrigger),
        (ASN_TAG_CLASS_CONTEXT | (3 << 2)), -1,
        &asn_DEF_ASN_RRC_TimeToTrigger, 0, { 0, 0, 0 }, 0, 0, "timeToTrigger" },
};
static const ber_tlv_tag_t asn_DEF_ASN_RRC_condEventA5_tags_3[] = {
    (ASN_TAG_CLASS_CONTEXT | (1 << 2)),
    (ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_condEventA5_tag2el_3[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (2 << 2)), 2, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (3 << 2)), 3, 0, 0 }
};
static asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_condEventA5_specs_3 = {
    sizeof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA5),
    offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA5, _asn_ctx),
    asn_MAP_ASN_RRC_condEventA5_tag2el_3, 4, 0, 0, 0, -1,
};
static asn_TYPE_descriptor_t asn_DEF_ASN_RRC_condEventA5_3 = {
    "condEventA5", "condEventA5", &asn_OP_SEQUENCE,
    asn_DEF_ASN_RRC_condEventA5_tags_3,
    sizeof(asn_DEF_ASN_RRC_condEventA5_tags_3) / sizeof(asn_DEF_ASN_RRC_condEventA5_tags_3[0]) - 1,
    asn_DEF_ASN_RRC_condEventA5_tags_3,
    sizeof(asn_DEF_ASN_RRC_condEventA5_tags_3) / sizeof(asn_DEF_ASN_RRC_condEventA5_tags_3[0]),
    { 0, 0, SEQUENCE_constraint },
    asn_MBR_ASN_RRC_condEventA5_3, 4, &asn_SPC_ASN_RRC_condEventA5_specs_3
};

/* ================================================================== */
/* condEventA4_r17                                                     */
/* ================================================================== */

static asn_TYPE_member_t asn_MBR_ASN_RRC_condEventA4_r17_4[] = {
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA4_r17, a4_Threshold_r17),
        (ASN_TAG_CLASS_CONTEXT | (0 << 2)), +1,
        &asn_DEF_ASN_RRC_MeasTriggerQuantity, 0, { 0, 0, 0 }, 0, 0, "a4-Threshold-r17" },
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA4_r17, hysteresis_r17),
        (ASN_TAG_CLASS_CONTEXT | (1 << 2)), -1,
        &asn_DEF_ASN_RRC_Hysteresis, 0, { 0, 0, 0 }, 0, 0, "hysteresis-r17" },
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA4_r17, timeToTrigger_r17),
        (ASN_TAG_CLASS_CONTEXT | (2 << 2)), -1,
        &asn_DEF_ASN_RRC_TimeToTrigger, 0, { 0, 0, 0 }, 0, 0, "timeToTrigger-r17" },
};
static const ber_tlv_tag_t asn_DEF_ASN_RRC_condEventA4_r17_tags_4[] = {
    (ASN_TAG_CLASS_CONTEXT | (2 << 2)),
    (ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_condEventA4_r17_tag2el_4[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (2 << 2)), 2, 0, 0 }
};
static asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_condEventA4_r17_specs_4 = {
    sizeof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA4_r17),
    offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventA4_r17, _asn_ctx),
    asn_MAP_ASN_RRC_condEventA4_r17_tag2el_4, 3, 0, 0, 0, -1,
};
static asn_TYPE_descriptor_t asn_DEF_ASN_RRC_condEventA4_r17_4 = {
    "condEventA4-r17", "condEventA4-r17", &asn_OP_SEQUENCE,
    asn_DEF_ASN_RRC_condEventA4_r17_tags_4,
    sizeof(asn_DEF_ASN_RRC_condEventA4_r17_tags_4) / sizeof(asn_DEF_ASN_RRC_condEventA4_r17_tags_4[0]) - 1,
    asn_DEF_ASN_RRC_condEventA4_r17_tags_4,
    sizeof(asn_DEF_ASN_RRC_condEventA4_r17_tags_4) / sizeof(asn_DEF_ASN_RRC_condEventA4_r17_tags_4[0]),
    { 0, 0, SEQUENCE_constraint },
    asn_MBR_ASN_RRC_condEventA4_r17_4, 3, &asn_SPC_ASN_RRC_condEventA4_r17_specs_4
};

/* ================================================================== */
/* fixedReferenceLocation-r17 (inside condEventD1-r17)                */
/* ================================================================== */

static asn_TYPE_member_t asn_MBR_ASN_RRC_ctc_fixedReferenceLocation_r17_5[] = {
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17__fixedReferenceLocation_r17, latitudeSign),
        (ASN_TAG_CLASS_CONTEXT | (0 << 2)), -1,
        &asn_DEF_NativeEnumerated, 0,
        { 0, &asn_PER_memb_ASN_RRC_ctc_latitudeSign_constr, memb_ASN_RRC_ctc_latitudeSign_constraint_1 },
        0, 0, "latitudeSign" },
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17__fixedReferenceLocation_r17, degreesLatitude),
        (ASN_TAG_CLASS_CONTEXT | (1 << 2)), -1,
        &asn_DEF_NativeInteger, 0,
        { 0, &asn_PER_memb_ASN_RRC_ctc_degreesLatitude_constr, memb_ASN_RRC_ctc_degreesLatitude_constraint_1 },
        0, 0, "degreesLatitude" },
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17__fixedReferenceLocation_r17, degreesLongitude),
        (ASN_TAG_CLASS_CONTEXT | (2 << 2)), -1,
        &asn_DEF_NativeInteger, 0,
        { 0, &asn_PER_memb_ASN_RRC_ctc_degreesLongitude_constr, memb_ASN_RRC_ctc_degreesLongitude_constraint_1 },
        0, 0, "degreesLongitude" },
};
static const ber_tlv_tag_t asn_DEF_ASN_RRC_ctc_fixedReferenceLocation_r17_tags_5[] = {
    (ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_ctc_fixedReferenceLocation_r17_tag2el_5[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (2 << 2)), 2, 0, 0 }
};
static asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_ctc_fixedReferenceLocation_r17_specs_5 = {
    sizeof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17__fixedReferenceLocation_r17),
    offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17__fixedReferenceLocation_r17, _asn_ctx),
    asn_MAP_ASN_RRC_ctc_fixedReferenceLocation_r17_tag2el_5, 3, 0, 0, 0, -1,
};
static asn_TYPE_descriptor_t asn_DEF_ASN_RRC_ctc_fixedReferenceLocation_r17_5 = {
    "fixedReferenceLocation-r17", "fixedReferenceLocation-r17", &asn_OP_SEQUENCE,
    asn_DEF_ASN_RRC_ctc_fixedReferenceLocation_r17_tags_5,
    sizeof(asn_DEF_ASN_RRC_ctc_fixedReferenceLocation_r17_tags_5) / sizeof(asn_DEF_ASN_RRC_ctc_fixedReferenceLocation_r17_tags_5[0]),
    asn_DEF_ASN_RRC_ctc_fixedReferenceLocation_r17_tags_5,
    sizeof(asn_DEF_ASN_RRC_ctc_fixedReferenceLocation_r17_tags_5) / sizeof(asn_DEF_ASN_RRC_ctc_fixedReferenceLocation_r17_tags_5[0]),
    { 0, 0, SEQUENCE_constraint },
    asn_MBR_ASN_RRC_ctc_fixedReferenceLocation_r17_5, 3,
    &asn_SPC_ASN_RRC_ctc_fixedReferenceLocation_r17_specs_5
};

/* ================================================================== */
/* referenceLocation-r17 CHOICE (inside condEventD1-r17)              */
/* ================================================================== */

static asn_TYPE_member_t asn_MBR_ASN_RRC_ctc_referenceLocation_r17_6[] = {
    { ATF_POINTER, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17, choice.fixedReferenceLocation_r17),
        (ASN_TAG_CLASS_CONTEXT | (0 << 2)), +1,
        &asn_DEF_ASN_RRC_ctc_fixedReferenceLocation_r17_5, 0,
        { 0, 0, 0 }, 0, 0, "fixedReferenceLocation-r17" },
    { ATF_POINTER, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17, choice.nadirReferenceLocation_r17),
        (ASN_TAG_CLASS_CONTEXT | (1 << 2)), -1,
        &asn_DEF_NULL, 0,
        { 0, 0, 0 }, 0, 0, "nadirReferenceLocation-r17" },
};
static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_ctc_referenceLocation_r17_tag2el_6[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 }
};
static asn_CHOICE_specifics_t asn_SPC_ASN_RRC_ctc_referenceLocation_r17_specs_6 = {
    sizeof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17),
    offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17, _asn_ctx),
    offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17, present),
    sizeof(((struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17 *)0)->present),
    asn_MAP_ASN_RRC_ctc_referenceLocation_r17_tag2el_6, 2,
    0, 0, 2
};
static asn_TYPE_descriptor_t asn_DEF_ASN_RRC_ctc_referenceLocation_r17_6 = {
    "referenceLocation-r17", "referenceLocation-r17", &asn_OP_CHOICE,
    0, 0, 0, 0,
    { 0, &asn_PER_type_ASN_RRC_ctc_refLoc_constr, CHOICE_constraint },
    asn_MBR_ASN_RRC_ctc_referenceLocation_r17_6, 2,
    &asn_SPC_ASN_RRC_ctc_referenceLocation_r17_specs_6
};

/* ================================================================== */
/* condEventD1-r17                                                     */
/* ================================================================== */

static asn_TYPE_member_t asn_MBR_ASN_RRC_condEventD1_r17_7[] = {
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17, distanceThreshFromReference1_r17),
        (ASN_TAG_CLASS_CONTEXT | (0 << 2)), -1,
        &asn_DEF_NativeInteger, 0,
        { 0, &asn_PER_memb_ASN_RRC_ctc_distanceThreshFromReference_r17_constr, memb_ASN_RRC_ctc_distanceThreshFromReference_r17_constraint_1 },
        0, 0, "distanceThreshFromReference1-r17" },
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17, distanceThreshFromReference2_r17),
        (ASN_TAG_CLASS_CONTEXT | (1 << 2)), -1,
        &asn_DEF_NativeInteger, 0,
        { 0, &asn_PER_memb_ASN_RRC_ctc_distanceThreshFromReference_r17_constr, memb_ASN_RRC_ctc_distanceThreshFromReference_r17_constraint_1 },
        0, 0, "distanceThreshFromReference2-r17" },
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17, referenceLocation1_r17),
        (ASN_TAG_CLASS_CONTEXT | (2 << 2)), +1,
        &asn_DEF_ASN_RRC_ctc_referenceLocation_r17_6, 0,
        { 0, 0, 0 }, 0, 0, "referenceLocation1-r17" },
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17, referenceLocation2_r17),
        (ASN_TAG_CLASS_CONTEXT | (3 << 2)), +1,
        &asn_DEF_ASN_RRC_ctc_referenceLocation_r17_6, 0,
        { 0, 0, 0 }, 0, 0, "referenceLocation2-r17" },
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17, hysteresisLocation_r17),
        (ASN_TAG_CLASS_CONTEXT | (4 << 2)), -1,
        &asn_DEF_NativeInteger, 0,
        { 0, &asn_PER_memb_ASN_RRC_ctc_hysteresisLocation_r17_constr, memb_ASN_RRC_ctc_hysteresisLocation_r17_constraint_1 },
        0, 0, "hysteresisLocation-r17" },
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17, timeToTrigger_r17),
        (ASN_TAG_CLASS_CONTEXT | (5 << 2)), -1,
        &asn_DEF_ASN_RRC_TimeToTrigger, 0,
        { 0, 0, 0 }, 0, 0, "timeToTrigger-r17" },
};
static const ber_tlv_tag_t asn_DEF_ASN_RRC_condEventD1_r17_tags_7[] = {
    (ASN_TAG_CLASS_CONTEXT | (3 << 2)),
    (ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_condEventD1_r17_tag2el_7[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (2 << 2)), 2, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (3 << 2)), 3, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (4 << 2)), 4, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (5 << 2)), 5, 0, 0 }
};
static asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_condEventD1_r17_specs_7 = {
    sizeof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17),
    offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17, _asn_ctx),
    asn_MAP_ASN_RRC_condEventD1_r17_tag2el_7, 6, 0, 0, 0, -1,
};
static asn_TYPE_descriptor_t asn_DEF_ASN_RRC_condEventD1_r17_7 = {
    "condEventD1-r17", "condEventD1-r17", &asn_OP_SEQUENCE,
    asn_DEF_ASN_RRC_condEventD1_r17_tags_7,
    sizeof(asn_DEF_ASN_RRC_condEventD1_r17_tags_7) / sizeof(asn_DEF_ASN_RRC_condEventD1_r17_tags_7[0]) - 1,
    asn_DEF_ASN_RRC_condEventD1_r17_tags_7,
    sizeof(asn_DEF_ASN_RRC_condEventD1_r17_tags_7) / sizeof(asn_DEF_ASN_RRC_condEventD1_r17_tags_7[0]),
    { 0, 0, SEQUENCE_constraint },
    asn_MBR_ASN_RRC_condEventD1_r17_7, 6, &asn_SPC_ASN_RRC_condEventD1_r17_specs_7
};

/* ================================================================== */
/* condEventT1-r17                                                     */
/* ================================================================== */

static asn_TYPE_member_t asn_MBR_ASN_RRC_condEventT1_r17_8[] = {
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventT1_r17, t1_Threshold_r17),
        (ASN_TAG_CLASS_CONTEXT | (0 << 2)), -1,
        &asn_DEF_NativeInteger, 0,
        { 0, &asn_PER_memb_ASN_RRC_ctc_t1_Threshold_r17_constr, memb_ASN_RRC_ctc_t1_Threshold_r17_constraint_1 },
        0, 0, "t1-Threshold-r17" },
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventT1_r17, duration_r17),
        (ASN_TAG_CLASS_CONTEXT | (1 << 2)), -1,
        &asn_DEF_NativeInteger, 0,
        { 0, &asn_PER_memb_ASN_RRC_ctc_duration_r17_constr, memb_ASN_RRC_ctc_duration_r17_constraint_1 },
        0, 0, "duration-r17" },
};
static const ber_tlv_tag_t asn_DEF_ASN_RRC_condEventT1_r17_tags_8[] = {
    (ASN_TAG_CLASS_CONTEXT | (4 << 2)),
    (ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_condEventT1_r17_tag2el_8[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 }
};
static asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_condEventT1_r17_specs_8 = {
    sizeof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventT1_r17),
    offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId__condEventT1_r17, _asn_ctx),
    asn_MAP_ASN_RRC_condEventT1_r17_tag2el_8, 2, 0, 0, 0, -1,
};
static asn_TYPE_descriptor_t asn_DEF_ASN_RRC_condEventT1_r17_8 = {
    "condEventT1-r17", "condEventT1-r17", &asn_OP_SEQUENCE,
    asn_DEF_ASN_RRC_condEventT1_r17_tags_8,
    sizeof(asn_DEF_ASN_RRC_condEventT1_r17_tags_8) / sizeof(asn_DEF_ASN_RRC_condEventT1_r17_tags_8[0]) - 1,
    asn_DEF_ASN_RRC_condEventT1_r17_tags_8,
    sizeof(asn_DEF_ASN_RRC_condEventT1_r17_tags_8) / sizeof(asn_DEF_ASN_RRC_condEventT1_r17_tags_8[0]),
    { 0, 0, SEQUENCE_constraint },
    asn_MBR_ASN_RRC_condEventT1_r17_8, 2, &asn_SPC_ASN_RRC_condEventT1_r17_specs_8
};

/* ================================================================== */
/* condEventId CHOICE                                                  */
/* ================================================================== */

static asn_TYPE_member_t asn_MBR_ASN_RRC_condEventId_9[] = {
    { ATF_POINTER, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId, choice.condEventA3),
        (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0,
        &asn_DEF_ASN_RRC_condEventA3_2, 0, { 0, 0, 0 }, 0, 0, "condEventA3" },
    { ATF_POINTER, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId, choice.condEventA5),
        (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 0,
        &asn_DEF_ASN_RRC_condEventA5_3, 0, { 0, 0, 0 }, 0, 0, "condEventA5" },
    { ATF_POINTER, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId, choice.condEventA4_r17),
        (ASN_TAG_CLASS_CONTEXT | (2 << 2)), 0,
        &asn_DEF_ASN_RRC_condEventA4_r17_4, 0, { 0, 0, 0 }, 0, 0, "condEventA4-r17" },
    { ATF_POINTER, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId, choice.condEventD1_r17),
        (ASN_TAG_CLASS_CONTEXT | (3 << 2)), 0,
        &asn_DEF_ASN_RRC_condEventD1_r17_7, 0, { 0, 0, 0 }, 0, 0, "condEventD1-r17" },
    { ATF_POINTER, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId, choice.condEventT1_r17),
        (ASN_TAG_CLASS_CONTEXT | (4 << 2)), 0,
        &asn_DEF_ASN_RRC_condEventT1_r17_8, 0, { 0, 0, 0 }, 0, 0, "condEventT1-r17" },
};
static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_condEventId_tag2el_9[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 }, /* condEventA3 */
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 }, /* condEventA5 */
    { (ASN_TAG_CLASS_CONTEXT | (2 << 2)), 2, 0, 0 }, /* condEventA4-r17 */
    { (ASN_TAG_CLASS_CONTEXT | (3 << 2)), 3, 0, 0 }, /* condEventD1-r17 */
    { (ASN_TAG_CLASS_CONTEXT | (4 << 2)), 4, 0, 0 }  /* condEventT1-r17 */
};
static asn_CHOICE_specifics_t asn_SPC_ASN_RRC_condEventId_specs_9 = {
    sizeof(struct ASN_RRC_CondTriggerConfig_r16__condEventId),
    offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId, _asn_ctx),
    offsetof(struct ASN_RRC_CondTriggerConfig_r16__condEventId, present),
    sizeof(((struct ASN_RRC_CondTriggerConfig_r16__condEventId *)0)->present),
    asn_MAP_ASN_RRC_condEventId_tag2el_9, 5,
    0, 0, -1
};
static asn_TYPE_descriptor_t asn_DEF_ASN_RRC_condEventId_9 = {
    "condEventId", "condEventId", &asn_OP_CHOICE,
    0, 0, 0, 0,
    { 0, &asn_PER_type_ASN_RRC_ctc_condEventId_constr, CHOICE_constraint },
    asn_MBR_ASN_RRC_condEventId_9, 5,
    &asn_SPC_ASN_RRC_condEventId_specs_9
};

/* ================================================================== */
/* CondTriggerConfig-r16 (top level)                                   */
/* ================================================================== */

asn_TYPE_member_t asn_MBR_ASN_RRC_CondTriggerConfig_r16_1[] = {
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16, condEventId),
        (ASN_TAG_CLASS_CONTEXT | (0 << 2)), +1,
        &asn_DEF_ASN_RRC_condEventId_9, 0,
        { 0, 0, 0 }, 0, 0, "condEventId" },
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_CondTriggerConfig_r16, rsType_r16),
        (ASN_TAG_CLASS_CONTEXT | (1 << 2)), -1,
        &asn_DEF_ASN_RRC_NR_RS_Type, 0,
        { 0, 0, 0 }, 0, 0, "rsType-r16" },
};

static const ber_tlv_tag_t asn_DEF_ASN_RRC_CondTriggerConfig_r16_tags_1[] = {
    (ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_CondTriggerConfig_r16_tag2el_1[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 }
};

asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_CondTriggerConfig_r16_specs_1 = {
    sizeof(struct ASN_RRC_CondTriggerConfig_r16),
    offsetof(struct ASN_RRC_CondTriggerConfig_r16, _asn_ctx),
    asn_MAP_ASN_RRC_CondTriggerConfig_r16_tag2el_1, 2,
    0, 0, 0, -1,
};

asn_TYPE_descriptor_t asn_DEF_ASN_RRC_CondTriggerConfig_r16 = {
    "CondTriggerConfig-r16", "CondTriggerConfig-r16", &asn_OP_SEQUENCE,
    asn_DEF_ASN_RRC_CondTriggerConfig_r16_tags_1,
    sizeof(asn_DEF_ASN_RRC_CondTriggerConfig_r16_tags_1) / sizeof(asn_DEF_ASN_RRC_CondTriggerConfig_r16_tags_1[0]),
    asn_DEF_ASN_RRC_CondTriggerConfig_r16_tags_1,
    sizeof(asn_DEF_ASN_RRC_CondTriggerConfig_r16_tags_1) / sizeof(asn_DEF_ASN_RRC_CondTriggerConfig_r16_tags_1[0]),
    { 0, 0, SEQUENCE_constraint },
    asn_MBR_ASN_RRC_CondTriggerConfig_r16_1, 2,
    &asn_SPC_ASN_RRC_CondTriggerConfig_r16_specs_1,
};
