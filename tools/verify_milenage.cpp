#include <cstdio>
#include <cstring>
#include <crypt-ext/milenage.hpp>

static void hex_to_bytes(const char *hex, uint8_t *out, int len) {
    for (int i = 0; i < len; i++)
        sscanf(hex + 2*i, "%2hhx", &out[i]);
}

static void print_hex(const char *label, const uint8_t *data, int len) {
    printf("%s = ", label);
    for (int i = 0; i < len; i++) printf("%02X", data[i]);
    printf("\n");
}

int main() {
    uint8_t k[16], op[16], opc[16], rand_val[16], sqn[6], amf[2];
    uint8_t mac_a[8], mac_s[8], res[8], ck[16], ik[16], ak[6], akstar[6];

    hex_to_bytes("465B5CE8B199B49FAA5F0A2EE238A6BC", k, 16);
    hex_to_bytes("E8ED289DEBA952E4283B54E88E6183CA", op, 16);
    hex_to_bytes("23553CBE9637A89D218AE64DAE47BF35", rand_val, 16);
    memset(sqn, 0, 6);
    amf[0] = 0x80; amf[1] = 0x00;

    // Compute OPc
    milenage_opc_gen(opc, k, op);
    print_hex("OPc", opc, 16);

    // f1 → MAC-A
    milenage_f1(opc, k, rand_val, sqn, amf, mac_a, mac_s);
    print_hex("MAC-A", mac_a, 8);
    print_hex("MAC-S", mac_s, 8);

    // f2345
    milenage_f2345(opc, k, rand_val, res, ck, ik, ak, akstar);
    print_hex("RES  ", res, 8);
    print_hex("CK   ", ck, 16);
    print_hex("IK   ", ik, 16);
    print_hex("AK   ", ak, 6);

    // AUTN
    uint8_t autn[16];
    for (int i = 0; i < 6; i++) autn[i] = sqn[i] ^ ak[i];
    autn[6] = amf[0];
    autn[7] = amf[1];
    memcpy(autn + 8, mac_a, 8);
    print_hex("AUTN ", autn, 16);

    return 0;
}
