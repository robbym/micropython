#ifndef MICROPY_INCLUDED_STM32_MACHINE_LISTENER_H
#define MICROPY_INCLUDED_STM32_MACHINE_LISTENER_H

#include <stdint.h>

#include "py/builtin.h"
#include "py/stream.h"

#include "uart.h"

#define OUTPUT_BUFFER_SIZE (4096)
#define INPUT_TERMINATOR_BUFFER (128)

typedef struct _listener_terminator_t
{
    size_t bytes_read;
    uint32_t timestamp;
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
    size_t last_bytes_read;

    bool line_started;
    size_t bytes_read;
    size_t bytes_written;

    uint32_t last_written;
} listener_obj_t;

#endif
