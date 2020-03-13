#ifndef MICROPY_INCLUDED_STM32_MACHINE_LISTENER_H
#define MICROPY_INCLUDED_STM32_MACHINE_LISTENER_H

#include <stdint.h>

#include "py/builtin.h"
#include "py/stream.h"

#include "uart.h"
#include "strftime.h"

#define OUTPUT_BUFFER_SIZE (4096)
#define INPUT_TERMINATOR_BUFFER (1024)

typedef struct _listener_terminator_t
{
    size_t bytes_read;
    datetime_t timestamp;
} listener_terminator_t;

typedef struct _buffered_stream_writer_t
{
    const mp_stream_p_t *stream;
    mp_obj_t file;
    uint8_t *buffer;
    size_t buffer_index;
} buffered_stream_writer_t;

struct _pyb_uart_obj_t;

typedef struct _listener_obj_t
{
    struct _pyb_uart_obj_t *uart;
    buffered_stream_writer_t file_stream;

    listener_terminator_t terminators[INPUT_TERMINATOR_BUFFER];
    size_t terminators_head;
    size_t terminators_tail;

    bool line_started;
    size_t bytes_read;
    size_t bytes_written;

    uint32_t last_written;

    uint8_t *data_buffer;
    size_t data_buffer_len;

    bool uartOverflowed;
    bool termOverflowed;
} listener_obj_t;

typedef struct _listener_obj_list_t
{
    listener_obj_t listener;
    struct _listener_obj_list_t *next;
} listener_obj_list_t;

bool machine_listener_is_terminator(listener_obj_t *listener, char term);

#endif
