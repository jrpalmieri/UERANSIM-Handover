#include "ASN_RRC_NTN-TriggerConfig-r17.h"

#include <NativeInteger.h>

static int memb_ASN_RRC_t_Service_r17_constraint_1(
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
    if (value < 0 || value > 5400)
    {
        ASN__CTFAIL(app_key, td, sptr, "%s: constraint failed (%s:%d)", td->name, __FILE__, __LINE__);
        return -1;
    }

    return 0;
}

static asn_per_constraints_t asn_PER_memb_ASN_RRC_t_Service_r17_constr_2 CC_NOTUSED = {
    { APC_CONSTRAINED, 13, 13, 0, 5400 },
    { APC_UNCONSTRAINED, -1, -1, 0, 0 },
    0, 0
};

asn_TYPE_member_t asn_MBR_ASN_RRC_NTN_TriggerConfig_r17_1[] = {
    { ATF_NOFLAGS, 0, offsetof(struct ASN_RRC_NTN_TriggerConfig_r17, t_Service_r17),
      (ASN_TAG_CLASS_CONTEXT | (0 << 2)),
      -1,
      &asn_DEF_NativeInteger,
      0,
      { 0, &asn_PER_memb_ASN_RRC_t_Service_r17_constr_2, memb_ASN_RRC_t_Service_r17_constraint_1 },
      0, 0,
      "t-Service-r17" },
};

static const ber_tlv_tag_t asn_DEF_ASN_RRC_NTN_TriggerConfig_r17_tags_1[] = {
    (ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};

static const asn_TYPE_tag2member_t asn_MAP_ASN_RRC_NTN_TriggerConfig_r17_tag2el_1[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 }
};

asn_SEQUENCE_specifics_t asn_SPC_ASN_RRC_NTN_TriggerConfig_r17_specs_1 = {
    sizeof(struct ASN_RRC_NTN_TriggerConfig_r17),
    offsetof(struct ASN_RRC_NTN_TriggerConfig_r17, _asn_ctx),
    asn_MAP_ASN_RRC_NTN_TriggerConfig_r17_tag2el_1,
    1,
    0,
    0, 0,
    -1,
};

asn_TYPE_descriptor_t asn_DEF_ASN_RRC_NTN_TriggerConfig_r17 = {
    "NTN-TriggerConfig-r17",
    "NTN-TriggerConfig-r17",
    &asn_OP_SEQUENCE,
    asn_DEF_ASN_RRC_NTN_TriggerConfig_r17_tags_1,
    sizeof(asn_DEF_ASN_RRC_NTN_TriggerConfig_r17_tags_1)
        / sizeof(asn_DEF_ASN_RRC_NTN_TriggerConfig_r17_tags_1[0]),
    asn_DEF_ASN_RRC_NTN_TriggerConfig_r17_tags_1,
    sizeof(asn_DEF_ASN_RRC_NTN_TriggerConfig_r17_tags_1)
        / sizeof(asn_DEF_ASN_RRC_NTN_TriggerConfig_r17_tags_1[0]),
    { 0, 0, SEQUENCE_constraint },
    asn_MBR_ASN_RRC_NTN_TriggerConfig_r17_1,
    1,
    &asn_SPC_ASN_RRC_NTN_TriggerConfig_r17_specs_1,
};
