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
#include "py/mphal.h"
#include "py/runtime.h"
#include "py/stream.h"

#include "uart.h"


typedef struct _machine_listener_obj_t
{
    mp_obj_base_t base;
    pyb_uart_obj_t *uart1;
    pyb_uart_obj_t *uart2;
    mp_obj_t file1;
    mp_obj_t file2;
} machine_listener_obj_t;

const mp_obj_type_t machine_listener_type;

STATIC void process_channel(pyb_uart_obj_t *uart, mp_obj_t file, uint8_t *buffer, size_t buffer_size, uint32_t* outIdx)
{
    int err;
    bool receivedBytes = false;

    const mp_stream_p_t *uart_stream = mp_get_stream(uart);
    const mp_stream_p_t *file_stream = mp_get_stream(file);

    if (uart_rx_any(uart))
    {
        receivedBytes = true;

        mp_uint_t remaining = buffer_size - *outIdx;

        mp_uint_t length = uart_rx_any(uart);
        if (length > remaining)
            length = remaining;

        uart_stream->read(uart, &buffer[*outIdx], length, &err);
        *outIdx += length;

        if (*outIdx == buffer_size)
        {
            file_stream->write(file, buffer, *outIdx, &err);
            file_stream->ioctl(file, MP_STREAM_FLUSH, 0, &err);
            *outIdx = 0;
        }
    }

    if (!receivedBytes && *outIdx > 0)
    {
        file_stream->write(file, buffer, *outIdx, &err);
        file_stream->ioctl(file, MP_STREAM_FLUSH, 0, &err);
        *outIdx = 0;
    }
}

STATIC void machine_listener_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mp_printf(print, "Listener()");
}

STATIC mp_obj_t machine_listener_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    machine_listener_obj_t *self = m_new_obj(machine_listener_obj_t);
    self->base.type = &machine_listener_type;
    self->uart1 = NULL;
    self->uart2 = NULL;

    const char *file1_name = "log1.txt";
    const char *file2_name = "log2.txt";
    const char *file_args = "a+";

    mp_obj_t open_args1[2] = {
        mp_obj_new_str(file1_name, strlen(file1_name)),
        mp_obj_new_str(file_args, strlen(file_args)),
    };

    mp_obj_t open_args2[2] = {
        mp_obj_new_str(file2_name, strlen(file2_name)),
        mp_obj_new_str(file_args, strlen(file_args)),
    };

    self->file1 = mp_builtin_open(2, open_args1, (mp_map_t *)&mp_const_empty_map);
    self->file2 = mp_builtin_open(2, open_args2, (mp_map_t *)&mp_const_empty_map);

    return MP_OBJ_FROM_PTR(self);
}

STATIC mp_obj_t machine_listener_add_periph(mp_obj_t self_in, mp_obj_t uart1_in, mp_obj_t uart2_in) {
    machine_listener_obj_t *self = MP_OBJ_TO_PTR(self_in);
    pyb_uart_obj_t *uart1 = MP_OBJ_TO_PTR(uart1_in);
    pyb_uart_obj_t *uart2 = MP_OBJ_TO_PTR(uart2_in);
    self->uart1 = uart1;
    self->uart2 = uart2;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(machine_listener_add_periph_obj, machine_listener_add_periph);

STATIC uint8_t listener_buffer1[2048];
STATIC uint8_t listener_buffer2[2048];
STATIC mp_obj_t machine_listener_process(mp_obj_t self_in) {
    machine_listener_obj_t *self = MP_OBJ_TO_PTR(self_in);

    uint32_t outIdx1 = 0;
    uint32_t outIdx2 = 0;

    for (;;)
    {
        process_channel(self->uart1, self->file1, listener_buffer1, sizeof(listener_buffer1), &outIdx1);
        process_channel(self->uart2, self->file2, listener_buffer2, sizeof(listener_buffer2), &outIdx2);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_listener_process_obj, machine_listener_process);


STATIC const mp_rom_map_elem_t machine_listener_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_add_periph), MP_ROM_PTR(&machine_listener_add_periph_obj) },
    { MP_ROM_QSTR(MP_QSTR_process), MP_ROM_PTR(&machine_listener_process_obj) },
};
STATIC MP_DEFINE_CONST_DICT(machine_listener_locals_dict, machine_listener_locals_dict_table);

const mp_obj_type_t machine_listener_type = {
    { &mp_type_type },
    .name = MP_QSTR_Listener,
    .print = machine_listener_print,
    .make_new = machine_listener_make_new,
    .locals_dict = (mp_obj_dict_t *)&machine_listener_locals_dict,
};
