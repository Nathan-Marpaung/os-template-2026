#include "header/cpu/gdt.h"

/**
 * global_descriptor_table, predefined GDT.
 * Initial SegmentDescriptor already set properly according to Intel Manual & OSDev.
 * Table entry : [{Null Descriptor}, {Kernel Code}, {Kernel Data (variable, etc)}, ...].
 */
struct GlobalDescriptorTable global_descriptor_table = {
    .table = {
        {
            // Null Descriptor, first entry of GDT is required to be all zero
            0
        },
        {
            // Kernel Code Segment, byte pattern: FF FF 00 00 00 9A CF 00
            .segment_low  = 0xFFFF, // Limit bit 0-15, with granularity spans 4 GiB
            .base_low     = 0,
            .base_mid     = 0,
            .type_bit     = 0xA,    // 0b1010: code, non-conforming, readable, not accessed
            .non_system   = 1,      // S bit, code or data segment
            .privilege    = 0,      // DPL 0, kernel ring
            .valid_bit    = 1,      // P bit, segment is present
            .segment_high = 0xF,    // Limit bit 16-19
            .available    = 0,      // AVL
            .long_mode    = 0,      // L bit, this is not a 64-bit segment
            .opr_32_bit   = 1,      // D/B bit, 32-bit operand size
            .granularity  = 1,      // G bit, limit is scaled by 4 KiB
            .base_high    = 0,
        },
        {
            // Kernel Data Segment, byte pattern: FF FF 00 00 00 92 CF 00
            .segment_low  = 0xFFFF,
            .base_low     = 0,
            .base_mid     = 0,
            .type_bit     = 0x2,    // 0b0010: data, expand-up, writable, not accessed
            .non_system   = 1,
            .privilege    = 0,
            .valid_bit    = 1,
            .segment_high = 0xF,
            .available    = 0,
            .long_mode    = 0,
            .opr_32_bit   = 1,
            .granularity  = 1,
            .base_high    = 0,
        }
    }
};

/**
 * _gdt_gdtr, predefined system GDTR.
 * GDT pointed by this variable is already set to point global_descriptor_table above.
 * From: https://wiki.osdev.org/Global_Descriptor_Table, GDTR.size is GDT size minus 1.
 */
struct GDTR _gdt_gdtr = {
    .size    = sizeof(global_descriptor_table) - 1,
    .address = &global_descriptor_table,
};
