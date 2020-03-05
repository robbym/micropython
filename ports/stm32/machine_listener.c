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

#include "uart.h"


#define OUTPUT_BUFFER_SIZE (2048)

typedef struct _listener_obj_t
{
    pyb_uart_obj_t *uart;
    mp_obj_t file;
    uint8_t *buffer;
    size_t out_idx;
} listener_obj_t;

typedef struct _machine_listener_obj_t
{
    mp_obj_base_t base;
    listener_obj_t *listener_list;
    size_t listener_list_length;
} machine_listener_obj_t;

const mp_obj_type_t machine_listener_type;

STATIC void process_channel(listener_obj_t *listener)
{
    pyb_uart_obj_t *uart = listener->uart;
    mp_obj_t file = listener->file;
    uint8_t *buffer = listener->buffer;
    size_t buffer_size = OUTPUT_BUFFER_SIZE;
    size_t* out_idx = &listener->out_idx;

    int err;
    bool receivedBytes = false;

    const mp_stream_p_t *uart_stream = mp_get_stream(uart);
    const mp_stream_p_t *file_stream = mp_get_stream(file);

    if (uart_rx_any(uart))
    {
        receivedBytes = true;

        mp_uint_t remaining = buffer_size - *out_idx;

        mp_uint_t length = uart_rx_any(uart);
        if (length > remaining)
            length = remaining;

        uart_stream->read(uart, &buffer[*out_idx], length, &err);
        *out_idx += length;

        if (*out_idx == buffer_size)
        {
            file_stream->write(file, buffer, *out_idx, &err);
            file_stream->ioctl(file, MP_STREAM_FLUSH, 0, &err);
            *out_idx = 0;
        }
    }

    if (!receivedBytes && *out_idx > 0)
    {
        file_stream->write(file, buffer, *out_idx, &err);
        file_stream->ioctl(file, MP_STREAM_FLUSH, 0, &err);
        *out_idx = 0;
    }
}

STATIC void machine_listener_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mp_printf(print, "Listener()");
}

STATIC mp_obj_t machine_listener_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    machine_listener_obj_t *self = m_new_obj(machine_listener_obj_t);
    self->base.type = &machine_listener_type;
    self->listener_list = NULL;
    self->listener_list_length = 0;
    return MP_OBJ_FROM_PTR(self);
}

STATIC mp_obj_t machine_listener_add(mp_obj_t self_in, mp_obj_t uart_in, mp_obj_t file_in) {
    machine_listener_obj_t *self = MP_OBJ_TO_PTR(self_in);
    pyb_uart_obj_t *uart = MP_OBJ_TO_PTR(uart_in);

    self->listener_list = m_renew(listener_obj_t, self->listener_list, self->listener_list_length, self->listener_list_length + 1);
    self->listener_list[self->listener_list_length] = (listener_obj_t)
    {
        .uart = uart,
        .file = file_in,
        .buffer = m_new0(uint8_t, OUTPUT_BUFFER_SIZE),
        .out_idx = 0,
    };
    self->listener_list_length++;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(machine_listener_add_obj, machine_listener_add);

STATIC mp_obj_t machine_listener_listen(mp_obj_t self_in) {
    machine_listener_obj_t *self = MP_OBJ_TO_PTR(self_in);

    for (;;)
    {
        for (size_t index = 0; index < self->listener_list_length; index++)
            process_channel(&self->listener_list[index]);
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
