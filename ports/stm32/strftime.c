#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "py/runtime.h"

#include "strftime.h"
#include "rtc.h"

static const char* weekdays[] =
{
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thrusday",
    "Friday",
    "Saturday"
};

static const char* months[] =
{
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December"
};

size_t strftime(char* output, size_t output_size, const char* format, const datetime_t* datetime)
{
    const char* start = output;
    size_t offset = 0;
    
    if (output_size == 0)
        return 0;
    
    while (*format != '\0' && (output - start) < output_size)
    {
        offset = output - start;
        
        if (*format == '%')
        {
            const char* format_decimal = "%d";
            const char* format_padded_decimal_2 = "%02d";
            const char* format_padded_decimal_6 = "%06d";
            
            uint32_t formatted_decimal = 0;
            const char* formatted_decimal_format = format_decimal;
            
            switch (*(++format))
            {
                case '\0':
                    return offset;
                    
                case '%':
                    *output++ = '%';
                    break;
                    
                case 'a':
                    if ((offset + 3) < output_size)
                    {
                        for (uint8_t index = 0; index < 3; index++)
                            *output++ = weekdays[datetime->weekday][index];
                    }
                    else
                    {
                        goto end_label;
                    }
                    break;
                    
                case 'A':
                    if ((offset + strlen(weekdays[datetime->weekday])) < output_size)
                    {
                        const char* weekday = weekdays[datetime->weekday];
                        while (*weekday != '\0') *output++ = *weekday++;
                    }
                    else
                    {
                        goto end_label;
                    }
                    break;
                    
                case 'w':
                    {
                        formatted_decimal = datetime->weekday;
                        formatted_decimal_format = format_decimal;
                        goto end_switch_label;
                    }
                    break;
                
                case 'd':
                    {
                        formatted_decimal = datetime->day;
                        formatted_decimal_format = format_padded_decimal_2;
                        goto format_decimal_label;
                    }
                    break;
                    
                case 'b':
                    if ((offset + 3) < output_size)
                    {
                        for (uint8_t index = 0; index < 3; index++)
                            *output++ = months[datetime->month][index];
                    }
                    else
                    {
                        goto end_label;
                    }
                    break;
                    
                case 'B':
                    if ((offset + strlen(months[datetime->month])) < output_size)
                    {
                        const char* month = months[datetime->month];
                        while (*month != '\0') *output++ = *month++;
                    }
                    else
                    {
                        goto end_label;
                    }
                    break;
                
                case 'm':
                    {
                        formatted_decimal = datetime->month;
                        formatted_decimal_format = format_padded_decimal_2;
                        goto format_decimal_label;
                    }
                    break;
                
                case 'y':
                    {
                        formatted_decimal = datetime->year % 100;
                        formatted_decimal_format = format_padded_decimal_2;
                        goto format_decimal_label;
                    }
                    break;
                
                case 'Y':
                    {
                        formatted_decimal = datetime->year;
                        formatted_decimal_format = format_decimal;
                        goto format_decimal_label;
                    }
                    break;
                    
                case 'H':
                    {
                        formatted_decimal = datetime->hour;
                        formatted_decimal_format = format_padded_decimal_2;
                        goto format_decimal_label;
                    }
                    break;
                    
                case 'I':
                    {
                        formatted_decimal = datetime->hour % 12;
                        if (formatted_decimal == 0) formatted_decimal = 12;
                        formatted_decimal_format = format_padded_decimal_2;
                        goto format_decimal_label;
                    }
                    break;
                    
                case 'p':
                    if ((offset + 2) < output_size)
                    {
                        if (datetime->hour < 12)
                            *output++ = 'A';
                        else
                            *output++ = 'P';
                        *output++ = 'M';
                    }
                    else
                    {
                        goto end_label;
                    }
                    break;
                    
                case 'M':
                    {
                        formatted_decimal = datetime->minute;
                        formatted_decimal_format = format_padded_decimal_2;
                        goto format_decimal_label;
                    }
                    break;
                    
                case 'S':
                    {
                        formatted_decimal = datetime->second;
                        formatted_decimal_format = format_padded_decimal_2;
                        goto format_decimal_label;
                    }
                    break;
                    
                case 'f':
                    {
                        formatted_decimal = datetime->microsecond;
                        formatted_decimal_format = format_padded_decimal_6;
                        goto format_decimal_label;
                    }
                    break;
            }
            
            goto end_switch_label;
            
format_decimal_label:
            {
                int result = snprintf(output, output_size - offset, formatted_decimal_format, formatted_decimal);
                if ((offset + result) < output_size)
                    output += result;
            }
        }
        else
            *output++ = *format;
        
end_switch_label:
        format++;
    }
    
end_label:
    *output = '\0';
    return output - start;
}


datetime_t strftime_rtc_value(void)
{
    RTC_DateTypeDef date;
    RTC_TimeTypeDef time;
    HAL_RTC_GetTime(&RTCHandle, &time, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&RTCHandle, &date, RTC_FORMAT_BIN);
    
    datetime_t timestamp =
    {
        .microsecond = 0,
        .second = time.Seconds,
        .minute = time.Minutes,
        .hour = time.Hours,
        .day = date.Date,
        .weekday = date.WeekDay,
        .month = date.Month,
        .year = 2000 + date.Year,
    };

    return timestamp;
}
