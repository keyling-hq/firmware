#!/usr/bin/env bash
# Signs a `release` build and packages it.
#
#   tools/sign_release.sh <build dir> <private key> <public key> <out dir> <name>
#
# Writes <name>.bin (signed app, for updates over Bluetooth) and <name>-full.bin (bootloader,
# partition table, OTA data and signed app, for USB at 0x0). Fails unless the signature verifies
# with <public key>. Needs esptool 5 for $PYTHON (default: python).
set -euo pipefail

build=$1 key=$2 pub=$3 out=$4 name=$5
py=${PYTHON:-python}

mkdir -p "$out"
"$py" -m espsecure sign-data --version 2 --keyfile "$key" --output "$out/$name.bin" "$build/firmware.bin"
"$py" -m espsecure verify-signature --version 2 --keyfile "$pub" "$out/$name.bin"
"$py" -m esptool --chip esp32s3 merge-bin -o "$out/$name-full.bin" \
    0x0 "$build/bootloader.bin" 0x8000 "$build/partitions.bin" \
    0xf000 "$build/ota_data_initial.bin" 0x20000 "$out/$name.bin"
