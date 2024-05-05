
#include <sml/sml_parser.h>
#include <sml/sml_crc16.h>
#include <stdio.h>
#include <stdbool.h>

int debug_sml = 0; /* 0=off, 1=on basic */

//#include <byteswap.h>
/* Swap bytes in 16 bit value.  */
#ifndef __bswap_constant_16
#define __bswap_constant_16(x)					\
  ((__uint16_t) ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8)))
#endif

// /* Swap bytes in 32 bit value.  */
#ifndef __bswap_constant_32
#define __bswap_constant_32(x) \
     ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) |                      \
      (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))
#endif

// /* Swap bytes in 64 bit value.  */
#ifndef __bswap_constant_64
#define __bswap_constant_64(x)			\
  ((((x) & 0xff00000000000000ull) >> 56)	\
   | (((x) & 0x00ff000000000000ull) >> 40)	\
   | (((x) & 0x0000ff0000000000ull) >> 24)	\
   | (((x) & 0x000000ff00000000ull) >> 8)	\
   | (((x) & 0x00000000ff000000ull) << 8)	\
   | (((x) & 0x0000000000ff0000ull) << 24)	\
   | (((x) & 0x000000000000ff00ull) << 40)	\
   | (((x) & 0x00000000000000ffull) << 56))
#endif


bool extract_value(const char* buf, uint32_t* target)
{
    if ( *buf == 0x59) {  // datatype is int64
        int64_t int64;
        memcpy(&int64, buf+1, 8);  // direct access causes hard fault error in STM32
		*target =  (uint32_t) __bswap_constant_64( int64 );  // todo overflow after ~429MWh/(10MWh/year)=42.9 years
    } else if ( *buf == 0x65) {  // datatype is uint32
		*target =  (uint32_t) __bswap_constant_32( *(uint32_t*) (buf+1) );
    } else {
        return false;
    }
    return true;
}


int parse_sml(const char* data, int size, const meter_model_t meter_model, meterdata_t* meterdata)
{
    int status = SML_PARSE_FAIL;
    uint16_t sml_start_idx = 0;
    uint16_t sml_stop_idx = 0;

//    for (int j = 0; j<size; j++) {
//        printf("0x%.2X, ", (uint8_t)data[j]);
//    }

    for (int i = 0; i < size-8; i++) {
        if ( (i > 53) && *((uint16_t*)(data+i)) == 0x0576 ) { // second SML-Message
            sml_start_idx = i;
        }

        uint64_t magic_number;
        memcpy(&magic_number, data+i, 8);  // direct access causes hard fault error in STM32

        if ( magic_number == 0x65FF000801000107) {  // 1.8.0 Active Energy Import; Wirkarbeit Bezug (in 0.1 Wh) //0x07010001 0800FF65 -> bigend
            if (debug_sml) printf("found 1.8.0 Active Energy Import\n");
            if (!extract_value((data+i+17), &meterdata->e_import_100mWh)) {
                return SML_PARSE_FAIL;
            }

        } else if ( magic_number == 0x01FF000802000107) {  // 2.8.0 Active Energy Export; Wirkarbeit Lieferung (in 0.1 Wh) //0x07010002 0800FF01 -> bigend
            if (debug_sml) printf("found 2.8.0 Active Energy Export\n");
            if (!extract_value((data+i+13), &meterdata->e_export_100mWh)) {
                return SML_PARSE_FAIL;
            }

        } else if ( magic_number == 0x01FF000710000107) {  // 16.7.0 Active Power; Wirkleistung (Momentanwert in kW) //0x07010010 0700FF01 -> bigend
            if (debug_sml) printf("found 16.7.0 Active Power\n");
            sml_stop_idx = i+16;
            if ( *(uint8_t*) (data+i+13) == 0x52) {  // datatype is int8
                meterdata->power = *(int8_t*) (data+i+14);
            } else if ( *(uint8_t*) (data+i+13) == 0x53) {  // datatype is int16
                meterdata->power = (int16_t) __bswap_constant_16( *(uint16_t*) (data+i+14) );
            } else if ( *(uint8_t*) (data+i+13) == 0x55) {  // datatype is int32
                meterdata->power = (int32_t) __bswap_constant_32( *(uint32_t*) (data+i+14) );
            } else if ( *(uint8_t*) (data+i+13) == 0x59) {  // datatype is int64
                uint64_t val64;
                memcpy(&val64, data+i+14, 8);  // direct access causes hard fault error in STM32
                meterdata->power = (int32_t) __bswap_constant_64( val64 );
            } else {
                return SML_PARSE_FAIL;
            }

            if (meter_model == ISKRA_MT631) {
                break;
            } else if (meter_model == ISKRA_MT175) {
                return SML_PARSE_OKAY;  // todo: parse rest of message and checksum
            }
        }
    }

    uint16_t crc16_rx;

    for (int n_fill = 0; n_fill < 3; n_fill++) {
        if (*(uint8_t*)&data[sml_stop_idx] == 0x01) {
            sml_stop_idx++;
        } else {
            break;
        }
    }

    if ( *(uint8_t*)&data[sml_stop_idx] == 0x62) {  // datatype is uint8
        crc16_rx = *(uint8_t*)&data[sml_stop_idx+1];
    } else if ( *(uint8_t*)&data[sml_stop_idx] == 0x63) {  // datatype is uint16
        crc16_rx = __bswap_constant_16( *(uint16_t*)&data[sml_stop_idx+1] );
    } else {
        if (debug_sml) printf("Could not find CRC datatype, datatype field is 0x%0x\n", data[sml_stop_idx]);
        return SML_PARSE_FAIL;
    }

    if (sml_stop_idx < sml_start_idx) {  // prevents memory fault in case of comm errors
        return SML_PARSE_FAIL;
    }

    uint16_t crc16_calc = sml_crc16_calculate((unsigned char *) &data[sml_start_idx], sml_stop_idx-sml_start_idx);

    if (debug_sml) {
        printf("\nCRC16_rx: %.2X\n", crc16_rx);
        printf("CRC16_sml: %.2X\n", crc16_calc);

//    int k = 0;
//    for (int j = sml_start_idx; j<sml_stop_idx+3; j++) {
//        printf("0x%.2X, ", (uint8_t)data[j]);
//    }
    }

    if (crc16_rx == crc16_calc)
        status=SML_PARSE_OKAY;

    return status;
}
