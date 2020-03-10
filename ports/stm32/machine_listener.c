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

STATIC int buffered_stream_write(buffered_stream_writer_t *buffered_stream, const uint8_t *buf, size_t size, int *errcode)
{
    while (size > 0)
    {
        size_t bytes_to_copy = OUTPUT_BUFFER_SIZE - buffered_stream->buffer_index;
        if (size < bytes_to_copy)
            bytes_to_copy = size;

        memcpy(&buffered_stream->buffer[buffered_stream->buffer_index], buf, bytes_to_copy);

        buffered_stream->buffer_index += bytes_to_copy;
        buf += bytes_to_copy;
        size -= bytes_to_copy;

        if (buffered_stream->buffer_index == OUTPUT_BUFFER_SIZE)
        {
            buffered_stream->stream->write(buffered_stream->file, buffered_stream->buffer, OUTPUT_BUFFER_SIZE, errcode);
            buffered_stream->stream->ioctl(buffered_stream->file, MP_STREAM_FLUSH, 0, errcode);
            buffered_stream->buffer_index = 0;
        }
    }

    return 0;
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

static char time_stamp_buffer[64];
static const char* time_format = "%Y/%m/%d, %H:%M:%S.%f, ";
static const char* time_format_term = "%Y/%m/%d, %H:%M:%S.%f, *TERMINATOR*\n";

STATIC void process_channel(listener_obj_t *listener)
{
    pyb_uart_obj_t *uart = listener->uart;

    int err;

    const mp_stream_p_t *uart_stream = mp_get_stream(uart);

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
                size_t written = strftime(time_stamp_buffer, 64, time_format, &timestamp);
                buffered_stream_write(&listener->file_stream, (uint8_t*)time_stamp_buffer, written, &err);
            }

            if (terminator != NULL && bytes_to_term <= bytes_to_write) {
                listener->terminators_tail = (listener->terminators_tail + 1) % INPUT_TERMINATOR_BUFFER;

                listener->line_started = false;
                
                buffered_stream_write(&listener->file_stream, listener->data_buffer, bytes_to_term, &err);

                memcpy(listener->data_buffer, &listener->data_buffer[bytes_to_term], bytes_to_write - bytes_to_term);

                listener->bytes_written += bytes_to_term;
                bytes_to_write -= bytes_to_term;

                size_t written = strftime(time_stamp_buffer, 64, time_format_term, &terminator->timestamp);
                buffered_stream_write(&listener->file_stream, (uint8_t*)time_stamp_buffer, written, &err);

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
                buffered_stream_write(&listener->file_stream, listener->data_buffer, bytes_to_write, &err);
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
