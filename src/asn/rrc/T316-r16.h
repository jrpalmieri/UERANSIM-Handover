/*
 * Rel-16 timer type used by RRCReconfiguration-v1610-IEs SetupRelease wrappers.
 */

#ifndef _T316_r16_H_
#define _T316_r16_H_

#include <asn_application.h>
#include <NativeEnumerated.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum T316_r16 {
	T316_r16_ms50 = 0,
	T316_r16_ms100 = 1,
	T316_r16_ms200 = 2,
	T316_r16_ms300 = 3,
	T316_r16_ms400 = 4,
	T316_r16_ms500 = 5,
	T316_r16_ms600 = 6,
	T316_r16_ms1000 = 7,
	T316_r16_ms1500 = 8,
	T316_r16_ms2000 = 9
} e_T316_r16;

typedef long T316_r16_t;

extern asn_TYPE_descriptor_t asn_DEF_T316_r16;
extern asn_per_constraints_t asn_PER_type_T316_r16_constr_1;
extern const asn_INTEGER_specifics_t asn_SPC_T316_r16_specs_1;

#ifdef __cplusplus
}
#endif

#endif /* _T316_r16_H_ */
