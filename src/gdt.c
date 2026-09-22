#include "header/cpu/gdt.h"

struct GlobalDescriptorTable global_descriptor_table = {
    .table = {
        {
            .segment_low = 0,
            .base_low = 0,
            .base_mid = 0,
            .type_bit = 0,
            .non_system = 0,
            .dpl = 0,
            .segment_present = 0,
            .segment_limit = 0,
            .avl = 0,
            .code_segment = 0,
            .def_operation_size = 0,
            .granularity = 0,
            .base_high = 0
        },
        {
            .segment_low = 0xFFFF,
            .base_low = 0,
            .base_mid = 0,
            .type_bit = 0xA,
            .non_system = 1,
            .dpl = 0,
            .segment_present = 1,
            .segment_limit = 0xF,
            .avl = 0,
            .code_segment = 0,
            .def_operation_size = 1,
            .granularity = 1,
            .base_high = 0
        },
        {
            .segment_low = 0xFFFF,
            .base_low = 0,
            .base_mid = 0,
            .type_bit = 0x2,
            .non_system = 1,
            .dpl = 0,
            .segment_present = 1,
            .segment_limit = 0xF,
            .avl = 0,
            .code_segment = 0,
            .def_operation_size = 1,
            .granularity = 1,
            .base_high = 0
        }
    }
};

struct GDTR _gdt_gdtr = {
    .size    = sizeof(global_descriptor_table) - 1,
    .address = &global_descriptor_table
};