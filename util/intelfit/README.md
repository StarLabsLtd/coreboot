/* SPDX-License-Identifier: GPL-2.0-only */

Once coreboot rom has been built, from inside this directory, run `meson build`
with the options you want. For example:

  `meson build -D processor=apollolake -D stitch_dir=/home/sean/Documents/APL`

BootGuard support can be enabled with:
  `-D bootguard=true`

Requires the following files to exist in the stitch directory:

* fit				(Intel Flash Image Tool)
* vsccommn.bin
* meu				(Intel Manifest Extension Utility)

* private.pem			(Private key for signing)

* dsp_fw.bin
* iUnit.bin
* ISH.bin
* INTC_pdt_APL_NS_BOM3_SENSORS	(ApolloLake only)
* pdr.bin
* pmcp.bin
* smip_iafw.bin

