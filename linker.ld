OUTPUT_FORMAT(elf64-x86-64)
ENTRY(multiboot_entry)

PHDRS
{
  text PT_LOAD FLAGS(0x5);
  rodata PT_LOAD FLAGS(0x4);
  data PT_LOAD FLAGS(0x6);
}

SECTIONS
{
  . = 1M;

  _kernel_start = .;

  .multiboot ALIGN(8) : {
    KEEP(*(.multiboot))
  } :text

  .text ALIGN(4K) : {
    *(.text*)
  } :text

  .rodata ALIGN(4K) : {
    *(.rodata*)
  } :rodata

  .data ALIGN(4K) : {
    *(.data*)
    *(.got*)
  } :data

  .bss ALIGN(4K) : {
    *(COMMON)
    *(.bss*)
  } :data

  _kernel_end = .;

  /DISCARD/ : {
    *(.comment)
    *(.eh_frame*)
    *(.note*)
    *(.debug*)
  }
}
