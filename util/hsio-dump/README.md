# hsio-dump

Extract PCH HSIO / mPHY IOBP tables from a firmware image.

Firmware images and coreboot describe these tables with the same
three-word entry:

	struct hsio_table_row {
		uint32_t addr;		/* IOBP address, 0xea00LLRR */
		uint32_t and_mask;
		uint32_t or_mask;
	};

The target value of an entry is `(current & and_mask) | or_mask`. Vendor
images keep whole tables as flat arrays of these entries, so they can be
found without any symbol information: the address always matches
`0xea00LLRR`, where `LL` selects the lane and `RR` the register inside it.

## Usage

	util/hsio-dump/hsio-dump.py IMAGE
	util/hsio-dump/hsio-dump.py IMAGE --compare src/southbridge/intel/lynxpoint/hsio/wbg_b0.c
	util/hsio-dump/hsio-dump.py IMAGE --emit 2 > table.c

`--compare` names every run it finds, by direct match, by the concatenation
of two reference tables (vendor images pack the dedicated and shared tables
back to back) or by the best partial match. Comparison is order insensitive:
vendor builds sort the rows differently from the coreboot tables while the
values stay the same.

`--emit N` prints run N as a C array in coreboot style, with the mask written
in its complemented form (`~0x00039ef7`), ready to paste into a `hsio/*.c`
file. `--emit all` skips runs shorter than eight rows.

## Notes

- A run is reported when at least three consecutive entries are valid; use
  `--min` to change that.
- Two images from the same chipset family normally contain byte identical
  tables at different offsets; compare the reported `sha` to tell a real
  variant from a relocation.
- The tool reads the image only, it never touches hardware.
