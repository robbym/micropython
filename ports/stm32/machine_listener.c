/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>

#include "py/builtin.h"
#include "py/misc.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "py/stream.h"

#include "rtc.h"
#include "uart.h"
#include "strftime.h"
#include "machine_listener.h"


typedef struct _machine_listener_obj_t
{
    mp_obj_base_t base;
    listener_obj_list_t *listener_list;
} machine_listener_obj_t;

const mp_obj_type_t machine_listener_type;

STATIC size_t itoa_padded(char *out_buffer, uint32_t value, uint8_t padding, uint8_t radix)
{
    static const char *ALPHABET = "0123456789ABCDEF";

    size_t index = 0;
    while (value != 0)
    {
        out_buffer[index++] = ALPHABET[(value % radix)];
        value /= radix;
    }

    while (index < padding)
        out_buffer[index++] = 48;

    size_t length = index;
    while (index > (length / 2))
    {
        char temp = out_buffer[index - 1];
        out_buffer[index - 1] = out_buffer[length - index];
        out_buffer[length - index] = temp;
        index--;
    }

    return length;
}

STATIC void buffered_stream_flush(buffered_stream_writer_t *buffered_stream, int *errcode)
{
    if (buffered_stream->buffer_index != 0)
    {
        buffered_stream->stream->write(buffered_stream->file, buffered_stream->buffer, buffered_stream->buffer_index, errcode);
        buffered_stream->stream->ioctl(buffered_stream->file, MP_STREAM_FLUSH, 0, errcode);
        buffered_stream->buffer_index = 0;
    }
}

STATIC void buffered_stream_putchar(buffered_stream_writer_t *buffered_stream, char out_char, int *errcode)
{
    if (buffered_stream->buffer_index == OUTPUT_BUFFER_SIZE)
        buffered_stream_flush(buffered_stream, errcode);
    buffered_stream->buffer[buffered_stream->buffer_index++] = out_char;
}

STATIC void buffered_stream_write(buffered_stream_writer_t *buffered_stream, const uint8_t *buf, size_t size, int *errcode)
{
    while (size > 0)
    {
        if (buffered_stream->buffer_index == OUTPUT_BUFFER_SIZE)
            buffered_stream_flush(buffered_stream, errcode);

        size_t available = OUTPUT_BUFFER_SIZE - buffered_stream->buffer_index;
        if (size < available)
            available = size;

        memcpy(&buffered_stream->buffer[buffered_stream->buffer_index], buf, available);
        buffered_stream->buffer_index += available;
        buf += available;
        size -= available;
    }
}

STATIC void buffered_stream_escaped_write(buffered_stream_writer_t *buffered_stream, const uint8_t *buf, size_t size, int *errcode)
{
    char out_buffer[10] = "";

    for (int index = 0; index < size; index++)
    {
        char *out_buffer_ptr = out_buffer;
        char out_char = (char)buf[index];
        
        if (out_char < ' ')
        {
            switch (out_char)
            {
            case '\t':
                out_char = 't';
                goto buffered_stream_escaped;
            case '\n':
                out_char = 'n';
                goto buffered_stream_escaped;
            case '\r':
                out_char = 'r';
                goto buffered_stream_escaped;

buffered_stream_escaped:
                buffered_stream_putchar(buffered_stream, '\\', errcode);
                buffered_stream_putchar(buffered_stream, out_char, errcode);
                break;
            
            default:
                *out_buffer_ptr++ = '<';
                *out_buffer_ptr++ = '0';
                *out_buffer_ptr++ = 'x';
                out_buffer_ptr += itoa_padded(out_buffer_ptr, (uint8_t)out_char, 2, 16);
                *out_buffer_ptr++ = '>';

                buffered_stream_write(buffered_stream, (uint8_t*)out_buffer, out_buffer_ptr - out_buffer, errcode);
                break;
            }
        }
        else
        {
            buffered_stream_putchar(buffered_stream, out_char, errcode);
        }
    }
}

STATIC void buffered_stream_newline(buffered_stream_writer_t *buffered_stream, int *errcode)
{
    if (buffered_stream->buffer_index == OUTPUT_BUFFER_SIZE)
        buffered_stream_flush(buffered_stream, errcode);

    buffered_stream->buffer[buffered_stream->buffer_index++] = '\n';
}

#define TIMESTAMP_BUFFER_SIZE (64)
static char time_stamp_buffer[TIMESTAMP_BUFFER_SIZE];
static const char* time_format = "%Y/%m/%d\t%H:%M:%S.%f\t";
static const char* time_format_term = "%Y/%m/%d\t%H:%M:%S.%f\t*TERM*";
static const char* time_format_uart_ovfl = "%Y/%m/%d\t%H:%M:%S.%f\t*UART OVERFLOW*";
static const char* time_format_term_ovfl = "%Y/%m/%d\t%H:%M:%S.%f\t*TERM OVERFLOW*";

STATIC void process_channel(listener_obj_t *listener)
{
    pyb_uart_obj_t *uart = listener->uart;

    int err;

    const mp_stream_p_t *uart_stream = mp_get_stream(uart);

    if (listener->uartOverflowed)
    {
        listener->uartOverflowed = false;

        listener->bytes_read = 0;
        listener->bytes_written = 0;
        listener->terminators_tail = listener->terminators_head;
        uart_set_rxbuf(uart, uart->read_buf_len, uart->read_buf);

        datetime_t timestamp = strftime_rtc_value();
        size_t written = strftime(time_stamp_buffer, TIMESTAMP_BUFFER_SIZE, time_format_uart_ovfl, &timestamp);
        buffered_stream_newline(&listener->file_stream, &err);
        buffered_stream_write(&listener->file_stream, (uint8_t*)time_stamp_buffer, written, &err);
        buffered_stream_newline(&listener->file_stream, &err);

        listener->line_started = false;
    }

    if (listener->termOverflowed)
    {
        listener->termOverflowed = false;

        listener->terminators_tail = listener->terminators_head;

        datetime_t timestamp = strftime_rtc_value();
        size_t written = strftime(time_stamp_buffer, TIMESTAMP_BUFFER_SIZE, time_format_term_ovfl, &timestamp);
        buffered_stream_newline(&listener->file_stream, &err);
        buffered_stream_write(&listener->file_stream, (uint8_t*)time_stamp_buffer, written, &err);
        buffered_stream_newline(&listener->file_stream, &err);

        listener->line_started = false;
    }

    listener_terminator_t *terminator = NULL;
    size_t bytes_to_term = 0;
    if (listener->terminators_tail != listener->terminators_head)
    {
        terminator = &listener->terminators[listener->terminators_tail];
        bytes_to_term = terminator->bytes_read - listener->bytes_written;
    }

    if (uart_rx_any(uart))
    {
        mp_uint_t bytes_to_write = uart_rx_any(uart);
        if (bytes_to_write > listener->data_buffer_len)
            bytes_to_write = listener->data_buffer_len;

        uart_stream->read(uart, listener->data_buffer, bytes_to_write, &err);

        while (bytes_to_write > 0)
        {
            if (!listener->line_started)
            {
                datetime_t timestamp = strftime_rtc_value();
                listener->line_started = true;
                size_t written = strftime(time_stamp_buffer, TIMESTAMP_BUFFER_SIZE, time_format, &timestamp);
                buffered_stream_write(&listener->file_stream, (uint8_t*)time_stamp_buffer, written, &err);
            }

            if (terminator != NULL && bytes_to_term <= bytes_to_write) {
                listener->terminators_tail = (listener->terminators_tail + 1) % INPUT_TERMINATOR_BUFFER;

                listener->line_started = false;
                
                buffered_stream_escaped_write(&listener->file_stream, listener->data_buffer, bytes_to_term, &err);
                buffered_stream_newline(&listener->file_stream, &err);

                memcpy(listener->data_buffer, &listener->data_buffer[bytes_to_term], bytes_to_write - bytes_to_term);

                listener->bytes_written += bytes_to_term;
                bytes_to_write -= bytes_to_term;

                size_t written = strftime(time_stamp_buffer, TIMESTAMP_BUFFER_SIZE, time_format_term, &terminator->timestamp);
                buffered_stream_write(&listener->file_stream, (uint8_t*)time_stamp_buffer, written, &err);
                buffered_stream_newline(&listener->file_stream, &err);

                if (listener->terminators_tail != listener->terminators_head)
                {
                    terminator = &listener->terminators[listener->terminators_tail];
                    bytes_to_term = terminator->bytes_read - listener->bytes_written;
                }
                else
                {
                    terminator = NULL;
                }
            }
            else
            {
                buffered_stream_escaped_write(&listener->file_stream, listener->data_buffer, bytes_to_write, &err);
                listener->bytes_written += bytes_to_write;
                bytes_to_write = 0;
            }
        }

        listener->last_written = mp_hal_ticks_ms();
    }
    else
    {
        uint32_t time_since_write = mp_hal_ticks_ms() - listener->last_written;
        if (time_since_write > 1000)
        {
            listener->last_written = mp_hal_ticks_ms();
            buffered_stream_flush(&listener->file_stream, &err);
        }
    }
    
}

STATIC void machine_listener_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mp_printf(print, "Listener()");
}

STATIC mp_obj_t machine_listener_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    machine_listener_obj_t *self = m_new_obj(machine_listener_obj_t);
    self->base.type = &machine_listener_type;
    self->listener_list = NULL;
    return MP_OBJ_FROM_PTR(self);
}

STATIC mp_obj_t machine_listener_add(mp_obj_t self_in, mp_obj_t uart_in, mp_obj_t file_in) {
    machine_listener_obj_t *self = MP_OBJ_TO_PTR(self_in);
    pyb_uart_obj_t *uart = MP_OBJ_TO_PTR(uart_in);

    listener_obj_list_t *new_item = m_new(listener_obj_list_t, 1);
    *new_item = (listener_obj_list_t)
    {
        .listener =
        {
            .uart = uart,
            .file_stream = {
                .stream = mp_get_stream(file_in),
                .file = file_in,
                .buffer = m_new0(uint8_t, OUTPUT_BUFFER_SIZE),
                .buffer_index = 0,
            },
            
            .terminators_head = 0,
            .terminators_head = 0,

            .line_started = false,
            .bytes_read = 0,
            .bytes_written = 0,

            .last_written = 0,

            .data_buffer = m_new0(uint8_t, OUTPUT_BUFFER_SIZE),
            .data_buffer_len = OUTPUT_BUFFER_SIZE,

            .uartOverflowed = false,
            .termOverflowed = false,
        },

        .next = self->listener_list
    };
    uart->listener = &new_item->listener;

    self->listener_list = new_item;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(machine_listener_add_obj, machine_listener_add);

STATIC mp_obj_t machine_listener_listen(mp_obj_t self_in) {
    machine_listener_obj_t *self = MP_OBJ_TO_PTR(self_in);

    const mp_obj_t args[] =
    {
        mp_const_none,
        mp_obj_new_int(1000)
    };
    pyb_rtc_wakeup(2, args);

    for (;;)
    {
        for (listener_obj_list_t *item = self->listener_list; item != NULL; item = item->next)
            process_channel(&item->listener);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_listener_listen_obj, machine_listener_listen);

STATIC const mp_rom_map_elem_t machine_listener_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_add), MP_ROM_PTR(&machine_listener_add_obj) },
    { MP_ROM_QSTR(MP_QSTR_listen), MP_ROM_PTR(&machine_listener_listen_obj) },
};
STATIC MP_DEFINE_CONST_DICT(machine_listener_locals_dict, machine_listener_locals_dict_table);

const mp_obj_type_t machine_listener_type = {
    { &mp_type_type },
    .name = MP_QSTR_Listener,
    .print = machine_listener_print,
    .make_new = machine_listener_make_new,
    .locals_dict = (mp_obj_dict_t *)&machine_listener_locals_dict,
};

bool machine_listener_is_terminator(listener_obj_t *listener, char term)
{
    switch (term)
    {
        case '\r':
        case '\n':
        case '>':
            return true;

        default:
            return false;
    }
}
