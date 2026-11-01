# SPDX-License-Identifier: GPL-2.0-only

scr_dts_dir := $(top)/3rdparty/syntacore/linux/arch/riscv/boot/dts/syntacore
scr_cpu_dtsi := $(if $(CONFIG_SCR_CPU_SCR9),scr9,scr7).dtsi
scr_cluster_dtsi := vcu118_$(if $(CONFIG_SCR_CLUSTER_L3),l3,l2).dtsi

CPPFLAGS_dts += -I$(src)/mainboard/syntacore/scr_cluster/dts
CPPFLAGS_dts += -I$(scr_dts_dir) -I$(top)/3rdparty/syntacore/linux/include
CPPFLAGS_dts += -DSCR_CPU_DTSI='"$(scr_cpu_dtsi)"' -DSCR_CLUSTER_DTSI='"$(scr_cluster_dtsi)"'
CPPFLAGS_dts += -DSCR_CPU_SCR9=$(if $(CONFIG_SCR_CPU_SCR9),1,0)
CPPFLAGS_dts += -DSCR_CLUSTER_L3=$(if $(CONFIG_SCR_CLUSTER_L3),1,0)
CPPFLAGS_dts += -DSCR_CLUSTER_QEMU=$(if $(CONFIG_SCR_CLUSTER_QEMU),1,0)
CPPFLAGS_dts += -DSCR_IPI_IMSIC=$(if $(CONFIG_SCR_IPI_IMSIC),1,0)

$(obj)/preprocessed.dts: $(wildcard $(scr_dts_dir)/*.dtsi) \
	$(wildcard $(src)/mainboard/syntacore/scr_cluster/dts/*)
