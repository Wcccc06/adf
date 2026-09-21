/* adf KPM 链接脚本（改自 KernelPatch KPM 模板） */
SECTIONS
{
    . = ALIGN(8);
    .kpm.info : { KEEP(*(.kpm.info)) }
    .kpm.init : { KEEP(*(.kpm.init)) }
    .kpm.ctl0 : { KEEP(*(.kpm.ctl0)) }
    .kpm.ctl1 : { KEEP(*(.kpm.ctl1)) }
    .kpm.exit : { KEEP(*(.kpm.exit)) }
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.*) }
    /DISCARD/ : { *(.comment) *(.note.*) }
}
INSERT AFTER .text;