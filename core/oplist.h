// Generated from "oplist.txt".

#pragma once

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

#define ZIS_OP_LIST_FULL \
    E(0x00, NOP     ) \
    E(0x01, ARG     ) \
    E(0x02,         ) \
    E(0x03,         ) \
    E(0x04, LDNIL   ) \
    E(0x05, LDBLN   ) \
    E(0x06, LDCON   ) \
    E(0x07, LDSYM   ) \
    E(0x08, MKINT   ) \
    E(0x09, MKFLT   ) \
    E(0x0a, MKTUP   ) \
    E(0x0b, MKARR   ) \
    E(0x0c, MKMAP   ) \
    E(0x0d,         ) \
    E(0x0e,         ) \
    E(0x0f,         ) \
    E(0x10, THR     ) \
    E(0x11, RETNIL  ) \
    E(0x12, RET     ) \
    E(0x13,         ) \
    E(0x14,         ) \
    E(0x15,         ) \
    E(0x16,         ) \
    E(0x17,         ) \
    E(0x18,         ) \
    E(0x19,         ) \
    E(0x1a,         ) \
    E(0x1b,         ) \
    E(0x1c,         ) \
    E(0x1d,         ) \
    E(0x1e,         ) \
    E(0x1f,         ) \
    E(0x20,         ) \
    E(0x21,         ) \
    E(0x22,         ) \
    E(0x23,         ) \
    E(0x24,         ) \
    E(0x25,         ) \
    E(0x26,         ) \
    E(0x27,         ) \
    E(0x28,         ) \
    E(0x29,         ) \
    E(0x2a,         ) \
    E(0x2b,         ) \
    E(0x2c,         ) \
    E(0x2d,         ) \
    E(0x2e,         ) \
    E(0x2f,         ) \
    E(0x30,         ) \
    E(0x31,         ) \
    E(0x32,         ) \
    E(0x33,         ) \
    E(0x34,         ) \
    E(0x35,         ) \
    E(0x36,         ) \
    E(0x37,         ) \
    E(0x38,         ) \
    E(0x39,         ) \
    E(0x3a,         ) \
    E(0x3b,         ) \
    E(0x3c,         ) \
    E(0x3d,         ) \
    E(0x3e,         ) \
    E(0x3f,         ) \
    E(0x40,         ) \
    E(0x41,         ) \
    E(0x42,         ) \
    E(0x43,         ) \
    E(0x44,         ) \
    E(0x45,         ) \
    E(0x46,         ) \
    E(0x47,         ) \
    E(0x48,         ) \
    E(0x49,         ) \
    E(0x4a,         ) \
    E(0x4b,         ) \
    E(0x4c,         ) \
    E(0x4d,         ) \
    E(0x4e,         ) \
    E(0x4f,         ) \
    E(0x50,         ) \
    E(0x51,         ) \
    E(0x52,         ) \
    E(0x53,         ) \
    E(0x54,         ) \
    E(0x55,         ) \
    E(0x56,         ) \
    E(0x57,         ) \
    E(0x58,         ) \
    E(0x59,         ) \
    E(0x5a,         ) \
    E(0x5b,         ) \
    E(0x5c,         ) \
    E(0x5d,         ) \
    E(0x5e,         ) \
    E(0x5f,         ) \
    E(0x60,         ) \
    E(0x61,         ) \
    E(0x62,         ) \
    E(0x63,         ) \
    E(0x64,         ) \
    E(0x65,         ) \
    E(0x66,         ) \
    E(0x67,         ) \
    E(0x68,         ) \
    E(0x69,         ) \
    E(0x6a,         ) \
    E(0x6b,         ) \
    E(0x6c,         ) \
    E(0x6d,         ) \
    E(0x6e,         ) \
    E(0x6f,         ) \
    E(0x70,         ) \
    E(0x71,         ) \
    E(0x72,         ) \
    E(0x73,         ) \
    E(0x74,         ) \
    E(0x75,         ) \
    E(0x76,         ) \
    E(0x77,         ) \
    E(0x78,         ) \
    E(0x79,         ) \
    E(0x7a,         ) \
    E(0x7b,         ) \
    E(0x7c,         ) \
    E(0x7d,         ) \
    E(0x7e,         ) \
    E(0x7f,         ) \
// ^^^ ZIS_OP_LIST_FULL ^^^

#define ZIS_OP_LIST_BY_NAME \
    E(ARG     , 0x01) \
    E(LDBLN   , 0x05) \
    E(LDCON   , 0x06) \
    E(LDNIL   , 0x04) \
    E(LDSYM   , 0x07) \
    E(MKARR   , 0x0b) \
    E(MKFLT   , 0x09) \
    E(MKINT   , 0x08) \
    E(MKMAP   , 0x0c) \
    E(MKTUP   , 0x0a) \
    E(NOP     , 0x00) \
    E(RET     , 0x12) \
    E(RETNIL  , 0x11) \
    E(THR     , 0x10) \
// ^^^ ZIS_OP_LIST_BY_NAME ^^^
