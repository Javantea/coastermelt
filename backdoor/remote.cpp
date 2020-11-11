/*
 * Backdoor Remote Control for Python
 *
 * Copyright (c) 2014 Micah Elizabeth Scott
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <Python.h>
#include <algorithm>
#include "mt1939_scsi.h"
#include "tinyscsi.h"
#include "hexdump.h"


typedef struct {
    PyObject_HEAD
    TinySCSI *scsi;
} Device;


static PyObject* device_open(Device *self)
{
    if (!self->scsi) {
        TinySCSI *scsi = new TinySCSI();
        bool ok;
    
        Py_BEGIN_ALLOW_THREADS;
        ok = MT1939::open(*scsi);
        Py_END_ALLOW_THREADS;

        if (ok) {
            self->scsi = scsi;
        } else {
            delete scsi;
            PyErr_SetString(PyExc_IOError, "Failed to open SCSI device");
            return 0;
        }
    }
    Py_RETURN_NONE;
}


static PyObject* device_close(Device *self)
{
    if (self->scsi) {
        delete self->scsi;
        self->scsi = 0;
    }
    Py_RETURN_NONE;
}


static PyObject* device_reset(Device *self)
{
    PyObject *open_result = device_open(self);
    if (!open_result) {
        return 0;
    }
    Py_DECREF(open_result);

    bool ok;    
    Py_BEGIN_ALLOW_THREADS;
    ok = MT1939::reset(*self->scsi);
    Py_END_ALLOW_THREADS;

    if (!ok) {
        PyErr_SetString(PyExc_IOError, "Failed to reset / reopen SCSI device");
        Py_XDECREF(device_close(self));
        return 0;
    }

    Py_RETURN_NONE;
}


static int device_init(Device *self, PyObject *args, PyObject *kw)
{
    if (!PyArg_ParseTuple(args, "")) {
        return -1;
    }

    PyObject *open_result = device_open(self);
    if (!open_result) {
        return -1;
    }
    Py_DECREF(open_result);

    return 0;
}


static void device_dealloc(Device *self)
{
    Py_DECREF(device_close(self));
    PyObject_Del((PyObject *)self);
}


static PyObject* device_get_signature(Device *self)
{
    if (!self->scsi) {
        PyErr_SetString(PyExc_IOError, "Device closed");
        return 0;
    }

    MT1939::BackdoorSignature bdsig;
    bool ok;

    Py_BEGIN_ALLOW_THREADS;
    ok = MT1939::backdoorSignature(*self->scsi, &bdsig);
    Py_END_ALLOW_THREADS;

    if (!ok) {
        PyErr_SetString(PyExc_IOError, "Failed to read backdoor signature. Is the patched firmware installed?");
        return 0;
    }

    return PyBytes_FromStringAndSize((const char *) &bdsig.bytes[0], sizeof bdsig.bytes);
}


static PyObject* device_scsi_out(Device *self, PyObject *args)
{
    Py_buffer cdb, data;

    if (!self->scsi) {
        PyErr_SetString(PyExc_IOError, "Device closed");
        return 0;
    }

    if (!PyArg_ParseTuple(args, "s*s*", &cdb, &data)) {
        return 0;
    }

    bool ok;

    Py_BEGIN_ALLOW_THREADS
    ok = self->scsi->out((uint8_t*) cdb.buf, (unsigned)cdb.len, (uint8_t*) data.buf, (unsigned)data.len);
    Py_END_ALLOW_THREADS

    PyBuffer_Release(&cdb);

    if (!ok) {
        PyErr_SetString(PyExc_IOError, "SCSI command failed");
        fprintf(stderr, "[SCSI] Failed OUT command, %d bytes\n", (int)data.len);
        hexdump((uint8_t*)cdb.buf, (unsigned)cdb.len);
        PyBuffer_Release(&data);    
        return 0;
    }

    PyBuffer_Release(&data);    
    Py_RETURN_NONE;
}


static PyObject* device_scsi_in(Device *self, PyObject *args)
{
    unsigned size = 0;
    Py_buffer cdb;

    if (!self->scsi) {
        PyErr_SetString(PyExc_IOError, "Device closed");
        return 0;
    }

    if (!PyArg_ParseTuple(args, "s*|I", &cdb, &size)) {
        return 0;
    }

    bool ok;
    uint8_t *buffer = new uint8_t[size];

    Py_BEGIN_ALLOW_THREADS
    ok = self->scsi->in((uint8_t*) cdb.buf, (unsigned)cdb.len, buffer, size);
    Py_END_ALLOW_THREADS

    PyBuffer_Release(&cdb);

    if (!ok) {
        PyErr_SetString(PyExc_IOError, "SCSI command failed");
        fprintf(stderr, "[SCSI] Failed IN command, %d bytes\n", size);
        hexdump((uint8_t*)cdb.buf, (unsigned)cdb.len);
        delete[] buffer;
        return 0;
    }

    PyObject *result = PyBytes_FromStringAndSize((const char *) buffer, size);
    delete[] buffer;
    return result;
}


static PyObject* device_peek(Device *self, PyObject *args)
{
    unsigned address;

    if (!PyArg_ParseTuple(args, "I", &address)) {
        return 0;
    }

    if (!self->scsi) {
        PyErr_SetString(PyExc_IOError, "Device closed");
        return 0;
    }

    bool ok;
    uint32_t cdb[3] = { 0x6b6565ac, address, 0 };
    uint32_t result[2];

    Py_BEGIN_ALLOW_THREADS
    ok = self->scsi->in((uint8_t*) cdb, sizeof cdb, (uint8_t*)result, sizeof result);
    Py_END_ALLOW_THREADS

    if (!ok) {
        PyErr_SetString(PyExc_IOError, "Backdoor command failed");
        return 0;
    }
    
    if (result[0] != address) {
        PyErr_SetString(PyExc_IOError, "Backdoor command gave incorrect result, patch may be malfunctioning!");
        return 0;
    }

    return PyLong_FromUnsignedLong(result[1]);
}


static PyObject* device_poke(Device *self, PyObject *args)
{
    unsigned address, data;

    if (!PyArg_ParseTuple(args, "II", &address, &data)) {
        return 0;
    }

    if (!self->scsi) {
        PyErr_SetString(PyExc_IOError, "Device closed");
        return 0;
    }

    bool ok;
    uint32_t cdb[3] = { 0x656b6fac, address, data };
    uint32_t result[2];

    Py_BEGIN_ALLOW_THREADS
    ok = self->scsi->in((uint8_t*) cdb, sizeof cdb, (uint8_t*)result, sizeof result);
    Py_END_ALLOW_THREADS

    if (!ok) {
        PyErr_SetString(PyExc_IOError, "Backdoor command failed");
        return 0;
    }
    
    if (result[0] != address || result[1] != data) {
        PyErr_SetString(PyExc_IOError, "Backdoor command gave incorrect result, patch may be malfunctioning!");
        return 0;
    }

    Py_RETURN_NONE;
}

static PyObject* device_fill(Device *self, PyObject *args)
{
    unsigned address, word, wordcount;

    if (!PyArg_ParseTuple(args, "III", &address, &word, &wordcount)) {
        return 0;
    }

    if (!self->scsi) {
        PyErr_SetString(PyExc_IOError, "Device closed");
        return 0;
    }

    unsigned pat8 = word & 0xff000000;
    bool ok;

    Py_BEGIN_ALLOW_THREADS
    
    if (word == (pat8 | (pat8 >> 8) | (pat8 >> 16) | (pat8 >> 24))) {
        // This is a commpn repeating byte pattern that we have a special command for

        uint32_t cdb[3] = { 0x3e77ac | (word & 0xff000000), address, wordcount };
        uint32_t result[2] = { 0 };
        ok = self->scsi->in((uint8_t*) cdb, sizeof cdb, (uint8_t*)result, sizeof result);
        ok = ok && result[0] == address;

    } else {
        // Some other strange fill. Do it with a lot of pokes.

        while (wordcount) {
            uint32_t cdb[3] = { 0x656b6fac, address, word };
            uint32_t result[2];
            ok = self->scsi->in((uint8_t*) cdb, sizeof cdb, (uint8_t*)result, sizeof result);
            if (!ok) {
                break;
            }
            address += 4;
            wordcount--;
        }
    }

    Py_END_ALLOW_THREADS

    if (!ok) {
        PyErr_SetString(PyExc_IOError, "Backdoor command failed");
        return 0;
    }
    
    Py_RETURN_NONE;
}

static PyObject* device_peek_byte(Device *self, PyObject *args)
{
    unsigned address;

    if (!PyArg_ParseTuple(args, "I", &address)) {
        return 0;
    }

    if (!self->scsi) {
        PyErr_SetString(PyExc_IOError, "Device closed");
        return 0;
    }

    bool ok;
    uint32_t cdb[3] = { 0x426565ac, address, 0 };
    uint32_t result[2];

    Py_BEGIN_ALLOW_THREADS
    ok = self->scsi->in((uint8_t*) cdb, sizeof cdb, (uint8_t*)result, sizeof result); 
    Py_END_ALLOW_THREADS

    if (!ok) {
        PyErr_SetString(PyExc_IOError, "Backdoor command failed");
        return 0;
    }
    
    if (result[0] != address) {
        PyErr_SetString(PyExc_IOError, "Backdoor command gave incorrect result, patch may be malfunctioning!");
        return 0;
    }

    return PyLong_FromUnsignedLong(result[1]);
}


static PyObject* device_poke_byte(Device *self, PyObject *args)
{
    unsigned address, data;

    if (!PyArg_ParseTuple(args, "II", &address, &data)) {
        return 0;
    }

    if (data > 0xFF) {
        PyErr_SetString(PyExc_ValueError, "Byte value out of range");
        return 0;
    }

    if (!self->scsi) {
        PyErr_SetString(PyExc_IOError, "Device closed");
        return 0;
    }

    bool ok;
    uint32_t cdb[3] = { 0x426b6fac, address, data };
    uint32_t result[2];

    Py_BEGIN_ALLOW_THREADS
    ok = self->scsi->in((uint8_t*) cdb, sizeof cdb, (uint8_t*)result, sizeof result);
    Py_END_ALLOW_THREADS

    if (!ok) {
        PyErr_SetString(PyExc_IOError, "Backdoor command failed");
        return 0;
    }
    
    if (result[0] != address || result[1] != data) {
        PyErr_SetString(PyExc_IOError, "Backdoor command gave incorrect result, patch may be malfunctioning!");
        return 0;
    }

    Py_RETURN_NONE;
}


static PyObject* device_read_block(Device *self, PyObject *args)
{
    unsigned address, wordcount;

    if (!PyArg_ParseTuple(args, "II", &address, &wordcount)) {
        return 0;
    }

    if (!self->scsi) {
        PyErr_SetString(PyExc_IOError, "Device closed");
        return 0;
    }

    // We use the backdoor's small PIO block read. It can usually handle
    // up to 0x1c words, but when there's a disc spinning some mode seems
    // to change that resets this to 4 words. Blah.
    const unsigned max_words = 4;

    wordcount = std::min<unsigned>(wordcount, max_words);
    uint32_t result[max_words];
    uint32_t cdb[3] = { 0x636f6cac, address, wordcount };
    bool ok;

    Py_BEGIN_ALLOW_THREADS
    ok = self->scsi->in((uint8_t*) cdb, sizeof cdb, (uint8_t*)result, 4 * wordcount);
    Py_END_ALLOW_THREADS

    if (!ok) {
        PyErr_SetString(PyExc_IOError, "Backdoor command failed");
        return 0;
    }
    
    return PyBytes_FromStringAndSize((const char *) result, 4 * wordcount);
}


static PyObject* device_blx(Device *self, PyObject *args)
{
    unsigned address, arg0 = 0;

    if (!PyArg_ParseTuple(args, "I|I", &address, &arg0)) {
        return 0;
    }

    if (!self->scsi) {
        PyErr_SetString(PyExc_IOError, "Device closed");
        return 0;
    }

    bool ok;
    uint32_t cdb[3] = { 0x584c42ac, address, arg0 };
    uint32_t result[2];

    Py_BEGIN_ALLOW_THREADS
    ok = self->scsi->in((uint8_t*) cdb, sizeof cdb, (uint8_t*)result, sizeof result);
    Py_END_ALLOW_THREADS

    if (!ok) {
        PyErr_SetString(PyExc_IOError, "Backdoor command failed");
        return 0;
    }
    
    return Py_BuildValue("II", result[0], result[1]);
}


static PyMethodDef device_methods[] =
{
    { "close", (PyCFunction) device_close, METH_NOARGS,
      "close() -> None\n"
      "Disconnect from the device, giving it back to the OS.\n"
      "You can get it back again by calling open().\n"
    },
    { "open", (PyCFunction) device_open, METH_NOARGS,
      "open() -> None\n"
      "Open the device. Normally the constructor does this, but you\n"
      "can also explicitly open the device if it's been closed.\n"
      "Has no effect if the device is already open.\n"
    },
    { "reset", (PyCFunction) device_reset, METH_NOARGS,
      "reset() -> None\n"
      "Send the strongest reboot we know how to send, and try to reopen the device.\n"
      "This uses the USB stack to send a hardware reset and re-enumeration request.\n"
      "The operating system will get a chance to talk to the device again, so if there's\n"
      "a CD loaded you may have to eject before we can get it back.\n"
    },
    { "get_signature", (PyCFunction) device_get_signature, METH_NOARGS,
      "get_signature() -> string\n"
      "Return a 12-byte signature identifying the firmware patch.\n"
    },
    { "scsi_out", (PyCFunction) device_scsi_out, METH_VARARGS,
      "scsi_out(cdb, data) -> None\n"
      "Send a low-level SCSI packet with outgoing data.\n"
    },
    { "scsi_in", (PyCFunction) device_scsi_in, METH_VARARGS,
      "scsi_in(cdb, size) -> data\n"
      "Send a low-level SCSI packet that expects incoming data.\n"
    },
    { "peek", (PyCFunction) device_peek, METH_VARARGS,
      "peek(address) -> word\n"
    },
    { "poke", (PyCFunction) device_poke, METH_VARARGS,
      "poke(address, word) -> None\n"
    },
    { "fill", (PyCFunction) device_fill, METH_VARARGS,
      "fill(address, word, wordcount) -> None\n"
    },
    { "peek_byte", (PyCFunction) device_peek_byte, METH_VARARGS,
      "peek_byte(address) -> byte\n"
    },
    { "poke_byte", (PyCFunction) device_poke_byte, METH_VARARGS,
      "poke_byte(address, word) -> None\n"
    },
    { "read_block", (PyCFunction) device_read_block, METH_VARARGS,
      "read_block(address, wordcount) -> string\n"
      "Seems to work with up to 0x1c words of data.\n"
    },    
    { "blx", (PyCFunction) device_blx, METH_VARARGS,
      "blx(address, [r0]) -> (r0, r1)\n"
      "Invoke a function with one argument word and two return words."
    },
    {0}
};

static PyTypeObject device_type =
{
    .ob_base = { PyObject_HEAD_INIT(&PyType_Type) },
    .tp_name = "remote.Device",
    .tp_basicsize = sizeof(Device),
    .tp_dealloc = (destructor) device_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_methods = device_methods,
    .tp_init = (initproc) device_init,
    .tp_alloc = PyType_GenericAlloc,
    .tp_new = PyType_GenericNew,
};

static PyModuleDef module_def =
{
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "remote",
};

PyMODINIT_FUNC PyInit_remote(void)
{
    PyObject *m = PyModule_Create(&module_def);

    Py_INCREF(&device_type);
    PyModule_AddObject(m, "Device", (PyObject *) &device_type);

    return m;
}

