// Generated from "oplist.txt".

#pragma once

#define ZIS_OP_LIST_LEN  67

#define ZIS_OP_LIST_MAX_LEN  (127 + 1)

/// List of ops.
#define ZIS_OP_LIST \
    E(0x00, NOP     ) \
    E(0x01, ARG     ) \
    E(0x03, BRK     ) \
    E(0x04, LDNIL   ) \
    E(0x05, LDBLN   ) \
    E(0x06, LDCON   ) \
    E(0x07, LDSYM   ) \
    E(0x08, MKINT   ) \
    E(0x09, MKFLT   ) \
    E(0x0a, MKTUP   ) \
    E(0x0b, MKARR   ) \
    E(0x0c, MKMAP   ) \
    E(0x0d, MKRNG   ) \
    E(0x0e, MKRNGX  ) \
    E(0x10, THR     ) \
    E(0x11, RETNIL  ) \
    E(0x12, RET     ) \
    E(0x13, CALL    ) \
    E(0x15, CALLV   ) \
    E(0x16, CALLP   ) \
    E(0x17, LDMTH   ) \
    E(0x18, IMP     ) \
    E(0x19, IMPSUB  ) \
    E(0x1a, LDLOC   ) \
    E(0x1b, STLOC   ) \
    E(0x1c, LDGLB   ) \
    E(0x1d, STGLB   ) \
    E(0x1e, LDGLBX  ) \
    E(0x1f, STGLBX  ) \
    E(0x20, LDFLDY  ) \
    E(0x21, STFLDY  ) \
    E(0x22, LDFLDX  ) \
    E(0x23, STFLDX  ) \
    E(0x24, LDELM   ) \
    E(0x25, STELM   ) \
    E(0x26, LDELMI  ) \
    E(0x27, STELMI  ) \
    E(0x28, JMP     ) \
    E(0x29, JMPT    ) \
    E(0x2a, JMPF    ) \
    E(0x2b, JMPLE   ) \
    E(0x2c, JMPLT   ) \
    E(0x2d, JMPEQ   ) \
    E(0x2e, JMPGT   ) \
    E(0x2f, JMPGE   ) \
    E(0x30, JMPNE   ) \
    E(0x31, CMP     ) \
    E(0x32, CMPLE   ) \
    E(0x33, CMPLT   ) \
    E(0x34, CMPEQ   ) \
    E(0x35, CMPGT   ) \
    E(0x36, CMPGE   ) \
    E(0x37, CMPNE   ) \
    E(0x38, ADD     ) \
    E(0x39, SUB     ) \
    E(0x3a, MUL     ) \
    E(0x3b, DIV     ) \
    E(0x3c, REM     ) \
    E(0x3d, POW     ) \
    E(0x3e, SHL     ) \
    E(0x3f, SHR     ) \
    E(0x40, BITAND  ) \
    E(0x41, BITOR   ) \
    E(0x42, BITXOR  ) \
    E(0x44, NOT     ) \
    E(0x45, NEG     ) \
    E(0x46, BITNOT  ) \
// ^^^ ZIS_OP_LIST ^^^

/// List of ops (sorted by names, undefined ones included).
#define ZIS_OP_LIST_FULL \
    E(0x38, ADD     , ABC  ) \
    E(0x01, ARG     , Aw   ) \
    E(0x40, BITAND  , ABC  ) \
    E(0x46, BITNOT  , ABw  ) \
    E(0x41, BITOR   , ABC  ) \
    E(0x42, BITXOR  , ABC  ) \
    E(0x03, BRK     , Aw   ) \
    E(0x13, CALL    , Aw   ) \
    E(0x16, CALLP   , ABw  ) \
    E(0x15, CALLV   , ABC  ) \
    E(0x31, CMP     , ABC  ) \
    E(0x34, CMPEQ   , ABC  ) \
    E(0x36, CMPGE   , ABC  ) \
    E(0x35, CMPGT   , ABC  ) \
    E(0x32, CMPLE   , ABC  ) \
    E(0x33, CMPLT   , ABC  ) \
    E(0x37, CMPNE   , ABC  ) \
    E(0x3b, DIV     , ABC  ) \
    E(0x18, IMP     , ABw  ) \
    E(0x19, IMPSUB  , ABw  ) \
    E(0x28, JMP     , Asw  ) \
    E(0x2d, JMPEQ   , AsBC ) \
    E(0x2a, JMPF    , AsBw ) \
    E(0x2f, JMPGE   , AsBC ) \
    E(0x2e, JMPGT   , AsBC ) \
    E(0x2b, JMPLE   , AsBC ) \
    E(0x2c, JMPLT   , AsBC ) \
    E(0x30, JMPNE   , AsBC ) \
    E(0x29, JMPT    , AsBw ) \
    E(0x05, LDBLN   , ABw  ) \
    E(0x06, LDCON   , ABw  ) \
    E(0x24, LDELM   , ABC  ) \
    E(0x26, LDELMI  , AsBC ) \
    E(0x22, LDFLDX  , ABC  ) \
    E(0x20, LDFLDY  , ABC  ) \
    E(0x1c, LDGLB   , ABw  ) \
    E(0x1e, LDGLBX  , ABw  ) \
    E(0x1a, LDLOC   , ABw  ) \
    E(0x17, LDMTH   , ABw  ) \
    E(0x04, LDNIL   , ABw  ) \
    E(0x07, LDSYM   , ABw  ) \
    E(0x0b, MKARR   , ABC  ) \
    E(0x09, MKFLT   , ABsCs) \
    E(0x08, MKINT   , ABsw ) \
    E(0x0c, MKMAP   , ABC  ) \
    E(0x0d, MKRNG   , ABC  ) \
    E(0x0e, MKRNGX  , ABC  ) \
    E(0x0a, MKTUP   , ABC  ) \
    E(0x3a, MUL     , ABC  ) \
    E(0x45, NEG     , ABw  ) \
    E(0x00, NOP     , Aw   ) \
    E(0x44, NOT     , ABw  ) \
    E(0x3d, POW     , ABC  ) \
    E(0x3c, REM     , ABC  ) \
    E(0x12, RET     , Aw   ) \
    E(0x11, RETNIL  , Aw   ) \
    E(0x3e, SHL     , ABC  ) \
    E(0x3f, SHR     , ABC  ) \
    E(0x25, STELM   , ABC  ) \
    E(0x27, STELMI  , AsBC ) \
    E(0x23, STFLDX  , ABC  ) \
    E(0x21, STFLDY  , ABC  ) \
    E(0x1d, STGLB   , ABw  ) \
    E(0x1f, STGLBX  , ABw  ) \
    E(0x1b, STLOC   , ABw  ) \
    E(0x39, SUB     , ABC  ) \
    E(0x10, THR     , Aw   ) \
    E(0x02,         , X    ) \
    E(0x0f,         , X    ) \
    E(0x14,         , X    ) \
    E(0x43,         , X    ) \
    E(0x47,         , X    ) \
    E(0x48,         , X    ) \
    E(0x49,         , X    ) \
    E(0x4a,         , X    ) \
    E(0x4b,         , X    ) \
    E(0x4c,         , X    ) \
    E(0x4d,         , X    ) \
    E(0x4e,         , X    ) \
    E(0x4f,         , X    ) \
    E(0x50,         , X    ) \
    E(0x51,         , X    ) \
    E(0x52,         , X    ) \
    E(0x53,         , X    ) \
    E(0x54,         , X    ) \
    E(0x55,         , X    ) \
    E(0x56,         , X    ) \
    E(0x57,         , X    ) \
    E(0x58,         , X    ) \
    E(0x59,         , X    ) \
    E(0x5a,         , X    ) \
    E(0x5b,         , X    ) \
    E(0x5c,         , X    ) \
    E(0x5d,         , X    ) \
    E(0x5e,         , X    ) \
    E(0x5f,         , X    ) \
    E(0x60,         , X    ) \
    E(0x61,         , X    ) \
    E(0x62,         , X    ) \
    E(0x63,         , X    ) \
    E(0x64,         , X    ) \
    E(0x65,         , X    ) \
    E(0x66,         , X    ) \
    E(0x67,         , X    ) \
    E(0x68,         , X    ) \
    E(0x69,         , X    ) \
    E(0x6a,         , X    ) \
    E(0x6b,         , X    ) \
    E(0x6c,         , X    ) \
    E(0x6d,         , X    ) \
    E(0x6e,         , X    ) \
    E(0x6f,         , X    ) \
    E(0x70,         , X    ) \
    E(0x71,         , X    ) \
    E(0x72,         , X    ) \
    E(0x73,         , X    ) \
    E(0x74,         , X    ) \
    E(0x75,         , X    ) \
    E(0x76,         , X    ) \
    E(0x77,         , X    ) \
    E(0x78,         , X    ) \
    E(0x79,         , X    ) \
    E(0x7a,         , X    ) \
    E(0x7b,         , X    ) \
    E(0x7c,         , X    ) \
    E(0x7d,         , X    ) \
    E(0x7e,         , X    ) \
    E(0x7f,         , X    ) \
// ^^^ ZIS_OP_LIST_FULL ^^^
