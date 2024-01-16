// Generated from "oplist.txt".

#pragma once

#define ZIS_OP_LIST_LEN  14

#define ZIS_OP_LIST_MAX_LEN  (127 + 1)

/// List of ops.
#define ZIS_OP_LIST \
    E(0x00, NOP     ) \
    E(0x01, ARG     ) \
    E(0x04, LDNIL   ) \
    E(0x05, LDBLN   ) \
    E(0x06, LDCON   ) \
    E(0x07, LDSYM   ) \
    E(0x08, MKINT   ) \
    E(0x09, MKFLT   ) \
    E(0x0a, MKTUP   ) \
    E(0x0b, MKARR   ) \
    E(0x0c, MKMAP   ) \
    E(0x10, THR     ) \
    E(0x11, RETNIL  ) \
    E(0x12, RET     ) \
// ^^^ ZIS_OP_LIST ^^^

/// List of ops (sorted by names, undefined ones included).
#define ZIS_OP_LIST_FULL \
    E(0x01, ARG     , Aw   ) \
    E(0x05, LDBLN   , ABw  ) \
    E(0x06, LDCON   , ABw  ) \
    E(0x04, LDNIL   , ABw  ) \
    E(0x07, LDSYM   , ABw  ) \
    E(0x0b, MKARR   , ABC  ) \
    E(0x09, MKFLT   , ABsCs) \
    E(0x08, MKINT   , ABsw ) \
    E(0x0c, MKMAP   , ABC  ) \
    E(0x0a, MKTUP   , ABC  ) \
    E(0x00, NOP     , Aw   ) \
    E(0x12, RET     , Aw   ) \
    E(0x11, RETNIL  , Aw   ) \
    E(0x10, THR     , Aw   ) \
    E(0x02,         , X    ) \
    E(0x03,         , X    ) \
    E(0x0d,         , X    ) \
    E(0x0e,         , X    ) \
    E(0x0f,         , X    ) \
    E(0x13,         , X    ) \
    E(0x14,         , X    ) \
    E(0x15,         , X    ) \
    E(0x16,         , X    ) \
    E(0x17,         , X    ) \
    E(0x18,         , X    ) \
    E(0x19,         , X    ) \
    E(0x1a,         , X    ) \
    E(0x1b,         , X    ) \
    E(0x1c,         , X    ) \
    E(0x1d,         , X    ) \
    E(0x1e,         , X    ) \
    E(0x1f,         , X    ) \
    E(0x20,         , X    ) \
    E(0x21,         , X    ) \
    E(0x22,         , X    ) \
    E(0x23,         , X    ) \
    E(0x24,         , X    ) \
    E(0x25,         , X    ) \
    E(0x26,         , X    ) \
    E(0x27,         , X    ) \
    E(0x28,         , X    ) \
    E(0x29,         , X    ) \
    E(0x2a,         , X    ) \
    E(0x2b,         , X    ) \
    E(0x2c,         , X    ) \
    E(0x2d,         , X    ) \
    E(0x2e,         , X    ) \
    E(0x2f,         , X    ) \
    E(0x30,         , X    ) \
    E(0x31,         , X    ) \
    E(0x32,         , X    ) \
    E(0x33,         , X    ) \
    E(0x34,         , X    ) \
    E(0x35,         , X    ) \
    E(0x36,         , X    ) \
    E(0x37,         , X    ) \
    E(0x38,         , X    ) \
    E(0x39,         , X    ) \
    E(0x3a,         , X    ) \
    E(0x3b,         , X    ) \
    E(0x3c,         , X    ) \
    E(0x3d,         , X    ) \
    E(0x3e,         , X    ) \
    E(0x3f,         , X    ) \
    E(0x40,         , X    ) \
    E(0x41,         , X    ) \
    E(0x42,         , X    ) \
    E(0x43,         , X    ) \
    E(0x44,         , X    ) \
    E(0x45,         , X    ) \
    E(0x46,         , X    ) \
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
