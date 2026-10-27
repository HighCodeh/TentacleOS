#!/usr/bin/env python3
# Wrap a compiled app.elf into a signed .hb bundle (HighBoy App).
# Layout (little-endian): 40-byte header, the raw ELF, then a 64-byte Ed25519
# signature over (header || elf).
#   0  magic "HBAP"     4
#   4  format_ver u16   (=2, signed)
#   6  flags u16
#   8  abi_major u16
#  10  abi_minor u16
#  12  caps u32          (tos_cap_t bitmask the app requests)
#  16  name[16]          null-padded
#  32  elf_len u32
#  36  reserved u32
#  40  elf bytes...
#  40+elf_len  signature[64]
import struct, argparse
import nacl.signing

ap = argparse.ArgumentParser()
ap.add_argument("elf")
ap.add_argument("out")
ap.add_argument("--sign", required=True, help="Ed25519 private-seed file")
ap.add_argument("--name", default="app")
ap.add_argument("--caps", default="0")
ap.add_argument("--abi-major", type=int, default=1)
ap.add_argument("--abi-minor", type=int, default=1)
a = ap.parse_args()

elf = open(a.elf, "rb").read()
name = a.name.encode()[:15].ljust(16, b"\0")
hdr = b"HBAP" + struct.pack("<HHHHI", 2, 0, a.abi_major, a.abi_minor, int(a.caps, 0)) \
      + name + struct.pack("<II", len(elf), 0)
assert len(hdr) == 40, len(hdr)

body = hdr + elf
sk = nacl.signing.SigningKey(open(a.sign, "rb").read())
sig = sk.sign(body).signature  # 64-byte detached signature
assert len(sig) == 64
open(a.out, "wb").write(body + sig)
print(f"wrote {a.out}: 40 + elf {len(elf)} + sig 64 = {len(body)+64} bytes, "
      f"caps={int(a.caps,0):#x}, signed")
