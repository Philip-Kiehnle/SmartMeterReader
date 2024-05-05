#ifndef SML_PARSER_H_
#define SML_PARSER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SML_PARSE_FAIL 0
#define SML_PARSE_OKAY 1

typedef struct {
	int32_t power;
	uint32_t e_import_100mWh;
	uint32_t e_export_100mWh;
} meterdata_t;


// this fast parser lib support two models from ISKRA at the moment
typedef enum
{
	ISKRA_MT175 = 0,  // no CRC check at the moment
	ISKRA_MT631 = 1
} meter_model_t;

int parse_sml(const char* data, int size, const meter_model_t meter_model, meterdata_t* meterdata);


#ifdef __cplusplus
}
#endif

#endif /* SML_PARSER_H_ */
