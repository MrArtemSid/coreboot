/* SPDX-License-Identifier: GPL-2.0-only */

Scope (\_TZ)
{
	ThermalZone (THRM)
	{
		Name (_TC1, 2)
		Name (_TC2, 3)

		/* Polling frequency: 2 seconds (in tenths of a second) */
		Name (_TZP, 20)

		/* Sampling period for passive cooling: 2 seconds */
		Name (_TSP, 20)

		/* Hardware Monitor ports on Nuvoton Super I/O (0x0a20..0x0a3f) */
		OperationRegion (NCTH, SystemIO, 0x0a25, 2)
		Field (NCTH, ByteAcc, NoLock, Preserve)
		{
			HIDX, 8, /* 0x0a25: Index Register */
			HDAT, 8  /* 0x0a26: Data Register  */
		}

		/* Convert Celsius to tenths of Kelvin */
		Method (CTOK, 1, NotSerialized)
		{
			Local0 = Arg0 * 10
			Local0 += 2732
			Return (Local0)
		}

		/* Current CPU temperature in tenths of Kelvin */
		Method (_TMP, 0, Serialized)
		{
			/* Select Bank 0 (Bank Select Register at offset 0x4e) */
			HIDX = 0x4e
			HDAT = 0x00

			/* Read CPUTIN temperature at offset 0x73 */
			HIDX = 0x73
			Local0 = HDAT

			/* Sanity check: valid CPU temperature range 15°C .. 110°C */
			If ((Local0 < 15) || (Local0 > 110)) {
				/* Try SYSTIN temperature at offset 0x27 */
				HIDX = 0x27
				Local0 = HDAT
				If ((Local0 < 15) || (Local0 > 110)) {
					Return (CTOK (45)) /* Fallback safe 45°C */
				}
			}

			Return (CTOK (Local0))
		}

		/* Critical shutdown temperature: 105°C */
		Method (_CRT, 0, NotSerialized)
		{
			Return (CTOK (105))
		}

		/* Passive cooling trip point: 85°C */
		Method (_PSV, 0, NotSerialized)
		{
			Return (CTOK (85))
		}

		/* Processor participating in passive cooling */
		Name (_PSL, Package ()
		{
			\_SB.CP00
		})
	}
}
