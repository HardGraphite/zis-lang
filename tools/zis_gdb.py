"""
GDB plugin for ZiS C source code debugging.
"""

import gdb
import json # for quoted strings
from typing import Any, Iterator, Optional


def find_current_z() -> Optional[gdb.Value]:
    """Find current runtime context variable."""

    frame = gdb.selected_frame()
    while frame is not None:
        for sym in frame.block():
            var = frame.read_var(sym)
            vt = var.type.strip_typedefs().unqualified()
            if vt.code == gdb.TYPE_CODE_PTR and str(vt.target()) == 'struct zis_context':
                return var
        frame = frame.older()


def guess_type_name(type_addr: int, z: gdb.Value | None = None) -> Optional[str]:
    if z is None:
        z = find_current_z()
        if z is None:
            return
    g = z.dereference()['globals'].dereference()
    for f in g.type.fields():
        if int(g[f]) == type_addr:
            name = f.name
            if not name:
                break
            if name.startswith('type_'):
                name = name[5:]
            return name


class ObjMetaPrinter:
    GC_OBJ_STAT_NAMES = [ 'NEW', 'MID', 'OLD', 'BIG' ]

    @staticmethod
    def lookup_function(v: gdb.Value) -> Optional['ObjMetaPrinter']:
        t = v.type.unqualified()
        if t.code != gdb.TYPE_CODE_STRUCT:
            return
        if str(t) != 'struct zis_object_meta':
            return
        return ObjMetaPrinter(v)

    @staticmethod
    def get_obj_type_ptr(obj_meta: gdb.Value) -> int:
        obj_meta_1 = int(obj_meta['_1'])
        return (obj_meta_1 >> 2) << 2

    def __init__(self, v: gdb.Value, z: gdb.Value | None = None):
        self.val = v
        self.ctx = z if z is not None else find_current_z()

    def repr_obj_type(self) -> str:
        # See: "core/object.h"
        type_ptr = ObjMetaPrinter.get_obj_type_ptr(self.val)
        type_name = guess_type_name(type_ptr, self.ctx)
        return type_name or f'<{type_ptr:#x}>'

    def repr_obj_gc_info(self) -> str:
        # See: "core/object.h"
        obj_meta = self.val
        obj_meta_1 = int(obj_meta['_1'])
        obj_meta_2 = int(obj_meta['_2'])
        gc_obj_state = ObjMetaPrinter.GC_OBJ_STAT_NAMES[obj_meta_1 & 0b11]
        gc_marked = bool(obj_meta_2 & 1)
        return f'{{state={gc_obj_state}, marked={gc_marked}}}'

    def to_string(self) -> str:
        return f'{{type={self.repr_obj_type()}, gc_info={self.repr_obj_gc_info()}}}'

gdb.pretty_printers.append(ObjMetaPrinter.lookup_function)


class ObjPtrPrinter:
    OBJ_PTR_PRINTERS = { } # { name : class, ... }

    OBJ_TYPE_NAME = None

    def __init_subclass__(cls):
        assert isinstance(cls.OBJ_TYPE_NAME, str)
        ObjPtrPrinter.OBJ_PTR_PRINTERS[cls.OBJ_TYPE_NAME] = cls

    @staticmethod
    def lookup_function(v: gdb.Value) -> Optional['ObjPtrPrinter']:
        """Look up for an object pointer printer."""
        t = v.type.unqualified()
        if t.code != gdb.TYPE_CODE_PTR or t.target().code != gdb.TYPE_CODE_STRUCT:
            return
        if (ts := t.target().unqualified().name) == 'zis_object':
            obj_type_name = ''
        elif ts.startswith('zis_') and ts.endswith('_obj'):
            obj_type_name = ts[4:-4]
        else:
            return
        return ObjPtrPrinter.OBJ_PTR_PRINTERS.get(obj_type_name, ObjPtrPrinter)(v)

    @staticmethod
    def is_smallint(ptr) -> bool:
        return int(ptr) & 1 != 0

    @staticmethod
    def smi_value(ptr) -> int:
        # See: "core/smallint.h"
        arch = gdb.selected_frame().architecture()
        width = 64 if arch.name().find('64') != -1 else 32
        raw = int(ptr)
        assert raw >= 0
        if (raw >> (width - 1)) & 1: # negative
            return -((2 ** width - (raw & (2 ** width - 1 - 1))) >> 1)
        else:
            return raw >> 1

    @staticmethod
    def empty_iter() -> Iterator[tuple]:
        return iter(())

    def __init__(self, v: gdb.Value, z: gdb.Value | None = None):
        self.val = v
        self.ctx = z if z is not None else find_current_z()

    def get_obj_meta(self) -> gdb.Value:
        return self.val['_meta']

    def repr_obj_value(self) -> Optional[str]:
        pass

    def iter_obj_fields(self) -> Optional[Iterator[tuple[str, Any]]]:
        pass

    def children(self) -> Iterator[tuple[str, Any]]:
        ptr = int(self.val)
        if ptr == 0:
            return ObjPtrPrinter.empty_iter()
        if ObjPtrPrinter.is_smallint(ptr):
            return iter((('smallint_value', ObjPtrPrinter.smi_value(ptr)),))
        if (it := self.iter_obj_fields()) is not None:
            return it
        return map(lambda field_name: (field_name, self.val[field_name]), self.val.type.target())

    def to_string(self) -> str:
        ptr = int(self.val)
        ptr_str = hex(ptr)
        if ptr == 0:
            return ptr_str
        if ObjPtrPrinter.is_smallint(ptr):
            return f'{ptr_str} zis_smallint ({ObjPtrPrinter.smi_value(ptr)})'
        type_name = guess_type_name(ObjMetaPrinter.get_obj_type_ptr(self.get_obj_meta())) or 'object'
        val_repr = self.repr_obj_value() or '{...}'
        return f'{ptr_str} zis.{type_name} {val_repr}'

gdb.pretty_printers.append(ObjPtrPrinter.lookup_function)


class BoolObjPtrPrinter(ObjPtrPrinter):
    OBJ_TYPE_NAME = 'bool'

    def repr_obj_value(self) -> str:
        return 'true' if bool(self.val['_value']) else 'false'

class ArraySlotsObjPtrPrinter(ObjPtrPrinter):
    OBJ_TYPE_NAME = 'array_slots'

    @staticmethod
    def array_slots_len(val) -> int:
        v_slots_num = val['_slots_num']
        assert ObjPtrPrinter.is_smallint(v_slots_num)
        slots_num = ObjPtrPrinter.smi_value(v_slots_num)
        assert slots_num >= 1
        return slots_num - 1 # (-1): the member `_slots_num` it self.

    def repr_obj_value(self) -> str:
        return f'[{ArraySlotsObjPtrPrinter.array_slots_len(self.val)}]'

    def iter_obj_fields(self) -> Iterator[tuple[str, gdb.Value]]:
        slots_num = ArraySlotsObjPtrPrinter.array_slots_len(self.val)
        data = self.val['_data']
        return map(lambda i: (str(i), data[i]), range(slots_num))


class ArrayObjPtrPrinter(ObjPtrPrinter):
    OBJ_TYPE_NAME = 'array'

    def repr_obj_value(self) -> str:
        return f'[{ArraySlotsObjPtrPrinter.array_slots_len(self.val["_data"])}]'

    def iter_obj_fields(self) -> Iterator[tuple[str, Any]]:
        val = self.val
        v_data = val['_data']
        return iter((
            ('_meta', val['_meta']),
            ('(capacity)', ArraySlotsObjPtrPrinter.array_slots_len(v_data)),
            ('length', val['length']),
            ('_data', v_data),
        ))


class TupleObjPtrPrinter(ArraySlotsObjPtrPrinter):
    OBJ_TYPE_NAME = 'tuple'

    @staticmethod
    def tuple_len(val) -> int:
        return ArraySlotsObjPtrPrinter.array_slots_len(val)


class SymbolObjPtrPrinter(ObjPtrPrinter):
    OBJ_TYPE_NAME = 'symbol'

    def calc_data_size(self) -> int:
        v_bytes_size = self.val['_bytes_size']
        v_data = self.val['data']
        return int(v_bytes_size) - (int(v_data.address) - int(v_bytes_size.address))

    def repr_obj_value(self) -> str:
        v_data = self.val['data']
        data_size = self.calc_data_size()
        data_buf = bytearray(data_size)
        for i in range(data_size):
            c = int(v_data[i])
            if c == 0:
                del data_buf[i:]
                break
            data_buf[i] = c
        data = data_buf.decode('UTF-8')
        return json.dumps(data)

    def iter_obj_fields(self) -> Iterator[tuple[str, Any]]:
        val = self.val
        return iter((
            ('_meta', val['_meta']),
            ('hash', val['hash']),
            ('(size)', self.calc_data_size()),
            ('_data', val['data']),
        ))


class StringObjPtrPrinter(ObjPtrPrinter):
    OBJ_TYPE_NAME = 'string'

    def calc_size_info(self) -> tuple[int, int, int]: # (char_size, char_count, data_size)
        v_bytes_size = self.val['_bytes_size']
        v_data = self.val['_data']
        data_size = int(v_bytes_size) - (int(v_data.address) - int(v_bytes_size.address))
        type_and_length = int(self.val['_type_and_length'])
        char_size = (type_and_length & 0b11) + 1
        str_length = type_and_length >> 2
        return char_size, str_length, data_size

    def repr_obj_value(self) -> str:
        v_data = self.val['_data']
        char_size, str_length, data_size = self.calc_size_info()
        data_size = char_size * str_length
        assert data_size <= data_size
        data_buf = bytearray(data_size)
        for i in range(data_size):
            c = int(v_data[i])
            data_buf[i] = c
        data = data_buf.decode('UTF-8')
        return json.dumps(data)

    def iter_obj_fields(self) -> Iterator[tuple[str, Any]]:
        val = self.val
        char_size, str_length, data_size = self.calc_size_info()
        return iter((
            ('_meta', val['_meta']),
            ('_bytes_size', val['_bytes_size']),
            ('(length)', str_length),
            ('(char_size)', char_size),
            ('(data_size)', data_size),
            ('_data', val['_data']),
        ))
