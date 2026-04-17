#include "ASN_RRC_CondTriggerConfig-r16.h"

#include "ASN_RRC_NTN-TriggerConfig-r17.h"

#include <NativeInteger.h>

static int memb_ASN_RRC_timeToExit_r17_constraint_1(
    const asn_TYPE_descriptor_t *td,
    const void *sptr,
    asn_app_constraint_failed_f *ctfailcb,
    void *app_key)
{
    long value;

    if (!sptr)
    {
        ASN__CTFAIL(app_key, td, sptr, "%s: value not given (%s:%d)", td->name, __FILE__, __LINE__);
        return -1;
    }

    value = *(const long *)sptr;
    if (value < 0 || value > 65535)
    {
        ASN__CTFAIL(app_key, td, sptr, "%s: constraint failed (%s:%d)", td->name, __FILE__, __LINE__);
        return -1;
    }

    return 0;
}

static asn_per_constraints_t asn_PER_memb_ASN_RRC_timeToExit_r17_constr_2 CC_NOTUSED = {
    { APC_CONSTRAINED, 16, 16, 0, 65535 },
    { APC_UNCONSTRAINED, -1, -1, 0, 0 },
    0, 0
};

static asn_TYPE_member_t asn_MBR_ASN_RRC_mue_Threshold_r17_2[] = {
    { ATF_NOFLAGS, 0,
            offsetof(ASN_RRC_CondTriggerConfig_r16__triggerEvent__condEventD1_r17__mue_Threshold_r17_t, timeToExit_r17),
      (ASN_TAG_CLASS_CONTEXT | (0 << 2)),
      -1,
      &asn_DEF_NativeInteger,
      0,
      { 0, &asn_PER_memb_ASN_RRC_timeToExit_r17_constr_2, memb_ASN_RRC_timeToExit_r17_constraint_1 },
      0, 0,
      "timeToExit-r17" },
};

static const ber_tlv_tag_t asn_DEF_ASN_RRC_mue_Threshold_r17_tags_2[] = {
    (ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};

static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_mue_Threshold_r17_tag2el_2[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 }
};

static asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_mue_Threshold_r17_specs_2 = {
    sizeof(ASN_RRC_CondTriggerConfig_r16__triggerEvent__condEventD1_r17__mue_Threshold_r17_t),
    offsetof(ASN_RRC_CondTriggerConfig_r16__triggerEvent__condEventD1_r17__mue_Threshold_r17_t, _asn_ctx),
    asn_MAP_ASN_RRC_mue_Threshold_r17_tag2el_2,
    1,
    0,
    0, 0,
    -1,
};

static asn_TYPE_descriptor_t asn_DEF_ASN_RRC_mue_Threshold_r17_2 = {
    "mue-Threshold-r17",
    "mue-Threshold-r17",
    &asn_OP_SEQUENCE,
    asn_DEF_ASN_RRC_mue_Threshold_r17_tags_2,
    sizeof(asn_DEF_ASN_RRC_mue_Threshold_r17_tags_2) / sizeof(asn_DEF_ASN_RRC_mue_Threshold_r17_tags_2[0]),
    asn_DEF_ASN_RRC_mue_Threshold_r17_tags_2,
    sizeof(asn_DEF_ASN_RRC_mue_Threshold_r17_tags_2) / sizeof(asn_DEF_ASN_RRC_mue_Threshold_r17_tags_2[0]),
    { 0, 0, SEQUENCE_constraint },
    asn_MBR_ASN_RRC_mue_Threshold_r17_2,
    1,
    &asn_SPC_ASN_RRC_mue_Threshold_r17_specs_2,
};

static asn_TYPE_member_t asn_MBR_ASN_RRC_condEventD1_r17_3[] = {
    { ATF_NOFLAGS, 0,
    offsetof(ASN_RRC_CondTriggerConfig_r16__triggerEvent__condEventD1_r17_t, mue_Threshold_r17),
      (ASN_TAG_CLASS_CONTEXT | (0 << 2)),
      +1,
      &asn_DEF_ASN_RRC_mue_Threshold_r17_2,
      0,
      { 0, 0, 0 },
      0, 0,
      "mue-Threshold-r17" },
    { ATF_NOFLAGS, 0,
            offsetof(ASN_RRC_CondTriggerConfig_r16__triggerEvent__condEventD1_r17_t, hysteresis_r17),
      (ASN_TAG_CLASS_CONTEXT | (1 << 2)),
      -1,
      &asn_DEF_NativeInteger,
      0,
      { 0, 0, 0 },
      0, 0,
      "hysteresis-r17" },
    { ATF_NOFLAGS, 0,
            offsetof(ASN_RRC_CondTriggerConfig_r16__triggerEvent__condEventD1_r17_t, timeToTrigger_r17),
      (ASN_TAG_CLASS_CONTEXT | (2 << 2)),
      -1,
      &asn_DEF_NativeInteger,
      0,
      { 0, 0, 0 },
      0, 0,
      "timeToTrigger-r17" },
};

static const ber_tlv_tag_t asn_DEF_ASN_RRC_condEventD1_r17_tags_3[] = {
    (ASN_TAG_CLASS_CONTEXT | (0 << 2)),
    (ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};

static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_condEventD1_r17_tag2el_3[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (2 << 2)), 2, 0, 0 },
};

static asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_condEventD1_r17_specs_3 = {
    sizeof(ASN_RRC_CondTriggerConfig_r16__triggerEvent__condEventD1_r17_t),
    offsetof(ASN_RRC_CondTriggerConfig_r16__triggerEvent__condEventD1_r17_t, _asn_ctx),
    asn_MAP_ASN_RRC_condEventD1_r17_tag2el_3,
    3,
    0,
    0, 0,
    -1,
};

static asn_TYPE_descriptor_t asn_DEF_ASN_RRC_condEventD1_r17_3 = {
    "condEventD1-r17",
    "condEventD1-r17",
    &asn_OP_SEQUENCE,
    asn_DEF_ASN_RRC_condEventD1_r17_tags_3,
    sizeof(asn_DEF_ASN_RRC_condEventD1_r17_tags_3)
        / sizeof(asn_DEF_ASN_RRC_condEventD1_r17_tags_3[0]) - 1,
    asn_DEF_ASN_RRC_condEventD1_r17_tags_3,
    sizeof(asn_DEF_ASN_RRC_condEventD1_r17_tags_3) / sizeof(asn_DEF_ASN_RRC_condEventD1_r17_tags_3[0]),
    { 0, 0, SEQUENCE_constraint },
    asn_MBR_ASN_RRC_condEventD1_r17_3,
    3,
    &asn_SPC_ASN_RRC_condEventD1_r17_specs_3,
};

static asn_TYPE_member_t asn_MBR_ASN_RRC_triggerEvent_4[] = {
    { ATF_POINTER, 0,
      offsetof(struct ASN_RRC_CondTriggerConfig_r16__triggerEvent, choice.condEventD1_r17),
      (ASN_TAG_CLASS_CONTEXT | (0 << 2)),
      +1,
      &asn_DEF_ASN_RRC_condEventD1_r17_3,
      0,
      { 0, 0, 0 },
      0, 0,
      "condEventD1-r17" },
};

static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_triggerEvent_tag2el_4[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 }
};

static asn_CHOICE_specifics_t asn_SPC_ASN_RRC_triggerEvent_specs_4 = {
    sizeof(struct ASN_RRC_CondTriggerConfig_r16__triggerEvent),
    offsetof(struct ASN_RRC_CondTriggerConfig_r16__triggerEvent, _asn_ctx),
    offsetof(struct ASN_RRC_CondTriggerConfig_r16__triggerEvent, present),
    sizeof(((struct ASN_RRC_CondTriggerConfig_r16__triggerEvent *)0)->present),
    asn_MAP_ASN_RRC_triggerEvent_tag2el_4,
    1,
    0, 0,
    -1,
};

static asn_TYPE_descriptor_t asn_DEF_ASN_RRC_triggerEvent_4 = {
    "triggerEvent",
    "triggerEvent",
    &asn_OP_CHOICE,
    0,
    0,
    0,
    0,
    { 0, 0, CHOICE_constraint },
    asn_MBR_ASN_RRC_triggerEvent_4,
    1,
    &asn_SPC_ASN_RRC_triggerEvent_specs_4,
};

asn_TYPE_member_t asn_MBR_ASN_RRC_CondTriggerConfig_r16_1[] = {
    { ATF_POINTER, 2, offsetof(struct ASN_RRC_CondTriggerConfig_r16, triggerEvent),
      (ASN_TAG_CLASS_CONTEXT | (1 << 2)),
      +1,
      &asn_DEF_ASN_RRC_triggerEvent_4,
      0,
      { 0, 0, 0 },
      0, 0,
      "triggerEvent" },
    { ATF_POINTER, 1, offsetof(struct ASN_RRC_CondTriggerConfig_r16, ntn_TriggerConfig_r17),
      (ASN_TAG_CLASS_CONTEXT | (0 << 2)),
      -1,
      &asn_DEF_ASN_RRC_NTN_TriggerConfig_r17,
      0,
      { 0, 0, 0 },
      0, 0,
      "ntn-TriggerConfig-r17" },
};

static const int asn_MAP_ASN_RRC_CondTriggerConfig_r16_oms_1[] = { 0, 1 };

static const ber_tlv_tag_t asn_DEF_ASN_RRC_CondTriggerConfig_r16_tags_1[] = {
    (ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};

static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_CondTriggerConfig_r16_tag2el_1[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 1, 0, 0 },
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 0, 0, 0 }
};

asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_CondTriggerConfig_r16_specs_1 = {
    sizeof(struct ASN_RRC_CondTriggerConfig_r16),
    offsetof(struct ASN_RRC_CondTriggerConfig_r16, _asn_ctx),
    asn_MAP_ASN_RRC_CondTriggerConfig_r16_tag2el_1,
    2,
    asn_MAP_ASN_RRC_CondTriggerConfig_r16_oms_1,
    1, 1,
    -1,
};

asn_TYPE_descriptor_t asn_DEF_ASN_RRC_CondTriggerConfig_r16 = {
    "CondTriggerConfig-r16",
    "CondTriggerConfig-r16",
    &asn_OP_SEQUENCE,
    asn_DEF_ASN_RRC_CondTriggerConfig_r16_tags_1,
    sizeof(asn_DEF_ASN_RRC_CondTriggerConfig_r16_tags_1)
        / sizeof(asn_DEF_ASN_RRC_CondTriggerConfig_r16_tags_1[0]),
    asn_DEF_ASN_RRC_CondTriggerConfig_r16_tags_1,
    sizeof(asn_DEF_ASN_RRC_CondTriggerConfig_r16_tags_1)
        / sizeof(asn_DEF_ASN_RRC_CondTriggerConfig_r16_tags_1[0]),
    { 0, 0, SEQUENCE_constraint },
    asn_MBR_ASN_RRC_CondTriggerConfig_r16_1,
    2,
    &asn_SPC_ASN_RRC_CondTriggerConfig_r16_specs_1,
};
