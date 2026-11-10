/* Adapted from anno1602-reconstruction/cmake/vita-segment-gap.ld.
 * vita-elf-create appends SCE metadata to the executable LOAD segment.
 * Keep an unmapped 64 KiB gap before writable sections. */
SECTIONS { . = . + 0x10000; }
INSERT BEFORE .init_array;
