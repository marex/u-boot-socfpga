#!/bin/sh
set -ve

BOARD_DEFCONFIG=socfpga_arria10_defconfig
ORIG_DTB=dts/dt.dtb
SPL_DTB=keys/dt.dtb
SPL_ROOT_KEYS=keys/root
SPL_UBOOT_ITS=board/altera/arria10-socdk/fit_spl_fpga.its

# Generate the keys
rm -rf keys
mkdir -p keys/root
cd keys/root
openssl genrsa -F4 -out dev.key 4096
openssl req -batch -new -x509 -key dev.key -out dev.crt
cd ../..

# Compile U-Boot DTBs, the SPL DTB must be patched with the ROOT key
# and the entire U-Boot and SPL must be recompiled with this patched
# DTB, since the DTB contains the public key in /signature node.
make clean
make ${BOARD_DEFCONFIG}
make -j13 dtbs

# Patch DTB with the public ROOT key
cp ${ORIG_DTB} ${SPL_DTB}
touch u-boot-nodtb.bin u-boot.dtb
rm -f u-boot-signed.itb
./tools/mkimage -f ${SPL_UBOOT_ITS} -K ${SPL_DTB} -k ${SPL_ROOT_KEYS} -r u-boot-signed.itb

# Build U-Boot with the patched DTB
make clean
make ${BOARD_DEFCONFIG}
make -j13 EXT_DTB=`pwd`/${SPL_DTB}

#####################################################################

# Generate U-Boot fitImage signed with the SIGNING key
./tools/mkimage -f ${SPL_UBOOT_ITS} -k ${SPL_ROOT_KEYS} -r u-boot-signed.itb

# Generate u-boot.ubi and u-boot-spl.sfp
cat spl/u-boot-spl.sfp spl/u-boot-spl.sfp spl/u-boot-spl.sfp spl/u-boot-spl.sfp > u-boot-spl.sfp
/usr/sbin/ubinize -vv -o u-boot.ubi -m 2048 -p 128KiB board/altera/arria10-socdk/ubinize.cfg
