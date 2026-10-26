# Santoni static diagnostic payload

This is an entry-only smoke test, **not an OS-ready payload**. It writes
one generation beacon, re-arms the previously tested KPSS watchdog for 3000 ms,
and waits for that bite. It does not draw: santoni's inherited framebuffer is
720x1280 RGB888, stride 2160, not XRGB8888. It never configures GPIO, clocks,
pinmux or UART. Production payloads must own
their watchdog policy; do not copy this diagnostic reset loop into an OS.

Build with the supported coreboot crossgcc (`make crossgcc-aarch64`):

```
python3 util/msm89x7-bootimg/build_payload.py \
  --output /absolute/private/path/payload_test.elf \
  --generation 0xYOUR_FRESH_UINT64
python3 -m unittest discover -s util/msm89x7-bootimg -p 'test_*.py' -v
```

Use a fresh, nonzero/non-ff 64-bit generation (replace the illustrative token).
Set `CONFIG_PAYLOAD_FILE` to that **exact absolute output path** in a private
config and use an external `obj=`. No generated ELF is tracked; the old root
`payload_test.elf` is ignored and is not rebuilt implicitly. Save image, ROM,
config, payload and stage ELFs, source diff and hashes before any RAM boot.

The linker emits a single explicit PT_LOAD, no ELF headers in that segment,
no interpreter/dynamic loader, and asserts the entire image fits
`[0x84400000,0x84500000)`. Static linking and stripped temporary object names
make byte-for-byte output reproducible for the same source/toolchain/generation.
The validator checks every load's physical bounds, identity mapping and the
executable entry, not only the entry address.

## Placement evidence and limits

The actual santoni runtime map in hardware run
`early-nc-20261026_114057-7611cd/console.txt` reports RAM
`[0x82408000,0x84a00000)` and firmware RESERVED
`[0x84a00000,0x8be00000)`. The old ELF LOAD
`[0x84ff0000,0x850001a8)` was rejected by selfboot. Never remove that reservation.
The chosen 1-MiB window is also outside the **full declared** ramstage region
`[0x822f4000,0x832f4000)` and postram CBFS cache
`[0x83380000,0x84380000)`, not just the small currently used ramstage range.
ROM `[0x80080000,0x81080000)`, appended-DTB guarded region through `0x81100000`,
stack `[0x82280000,0x82290000)`, TTB `[0x82294000,0x822b4000)`, romstage,
preram console, CBMEM/config tables, framebuffer, SMEM and all runtime reserved
ranges are disjoint. This is a board/config-specific audit, **not a portable
allocation API**. Re-audit when changing memlayout, RAM map or config. Check the
CBFS payload's actual segments after packaging too.

The only beacon store is the u64 at `0xbfffffa8`, inside the existing explicit
reserved diagnostic tail `[0xbfffff80,0xc0000000)`, already mapped NC. It does not
modify DBGC header/start/size, console bytes actually written, fault words
`[0xbfffff80,0xbfffffa8)`, printk `[0xbfffffb0,0xbfffffc0)`, or MMU/progress words
`[0xbfffffc0,0xc0000000)`. The ramoops writer now excludes the final 256 bytes from its ring capacity,
so wrap cannot overwrite the diagnostic tail. The whole LK raw allocation
`[0xbff80000,0xc0000000)` is reserved, including lower dump records. This is
not a generic pstore ABI: LK's allocation remains 256 KiB for the console,
whereas coreboot's usable ring is 256 bytes shorter; use the bounded raw dump
and coreboot's reduced capacity when decoding wrapped logs. Compare a fresh
generation exactly; old bytes are not proof of entry. Read raw 512 KiB first
after warm-reset return, then LK log.

Hardware testing requires explicit authorization, serial/product checks,
PMIC warm-reset readback, and one-shot RAM boot with <=15-second reconnect.
No return means stop and inspect the panel, not force-reset or retry.
