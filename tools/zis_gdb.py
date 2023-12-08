"""
GDB plugin for ZiS C source code debugging.
"""

import gdb
from typing import Any, Optional


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


class ObjPtrPrinter:
    """Print an object pointer."""

    GC_OBJ_STAT_NAMES = [ 'NEW', 'MID', 'OLD', 'BIG' ]

    @staticmethod
    def lookup_function(v: gdb.Value) -> Optional['ObjPtrPrinter']:
        t = v.type.unqualified()
        if t.code != gdb.TYPE_CODE_PTR:
            return
        tt = t.target().unqualified()
        tts = str(tt)
        if not (tts == 'struct zis_object' or
                (tts.startswith('struct zis_') and tts.endswith('_obj'))):
            return
        return ObjPtrPrinter(v, find_current_z())

    def __init__(self, v: gdb.Value, z: gdb.Value | None = None):
        self.val = v
        self.ctx = z

    def to_string(self) -> str:
        if self.is_smallint():
            return f'smallint{{{self.smi_value()}}}'
        else:
            data = [
                ('type', self.obj_type()),
                ('gc_info', self.obj_gc_info()),
            ]
            return hex(int(self.val)) + \
                ' zis_object{ ' + ', '.join(f'{k}={v}' for k, v in data) + ' }'

    def is_smallint(self) -> bool:
        return int(self.val) & 1 != 0

    def smi_value(self) -> int:
        # See: "core/smallint.h"
        arch = gdb.selected_frame().architecture()
        width = 64 if arch.name().find('64') != -1 else 32
        raw = int(self.val)
        assert raw >= 0
        if (raw >> (width - 1)) & 1: # negative
            return -((2 ** width - (raw & (2 ** width - 1 - 1))) >> 1)
        else:
            return raw >> 1

    def obj_type(self) -> str:
        # See: "core/object.h"
        obj_meta_1 = int(self.val.dereference()['_meta']['_1'])
        type_ptr = (obj_meta_1 >> 2) << 2
        type_name = self.type_name_from_addr(type_ptr)
        return type_name or f'<{type_ptr:#x}>'

    def obj_gc_info(self) -> str:
        # See: "core/object.h"
        obj_meta = self.val.dereference()['_meta']
        obj_meta_1 = obj_meta['_1']
        obj_meta_2 = obj_meta['_2']
        gc_obj_state = ObjPtrPrinter.GC_OBJ_STAT_NAMES[obj_meta_1 & 0b11]
        gc_marked = bool(obj_meta_2 & 1)
        return f'{{state={gc_obj_state}, marked={gc_marked}}}'

    def type_name_from_addr(self, addr: int) -> Optional[str]:
        if self.ctx:
            g = self.ctx.dereference()['globals'].dereference()
            for f in g.type.fields():
                if int(g[f]) == addr:
                    name = f.name
                    if not name:
                        break
                    if name.startswith('type_'):
                        name = 'builtin/' + name[5:]
                    return name



gdb.pretty_printers.append(ObjPtrPrinter.lookup_function)

