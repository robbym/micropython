#include <stdint.h>

typedef struct _datetime_t
{
    uint32_t microsecond;
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t weekday;
    uint8_t month;
    uint16_t year;
} datetime_t;

size_t strftime(char* output, size_t output_size, const char* format, const datetime_t* datetime);