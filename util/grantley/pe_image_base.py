#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import struct
import sys


def get_image_base(filepath):
	with open(filepath, "rb") as f:
		hdr = f.read(0x400)
		if len(hdr) < 0x80 or hdr[:2] != b"MZ":
			return None
		e_lfanew = struct.unpack_from("<I", hdr, 0x3c)[0]
		if len(hdr) < e_lfanew + 0x38:
			return None
		base = struct.unpack_from("<I", hdr, e_lfanew + 0x34)[0]
		return hex(base)


if __name__ == "__main__":
	if len(sys.argv) > 1:
		res = get_image_base(sys.argv[1])
		if res:
			print(res)
