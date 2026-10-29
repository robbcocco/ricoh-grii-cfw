#!/usr/bin/env python3
"""GR II firmware (.frm) container tool — offline, stdlib only.

Subcommands
  info   FRM                 container tree + update-header fields of every image
  extract FRM OUTDIR         decrypt images, split the A: drive into OUTDIR/afs/
  ls     FRM                 list the A: drive (name, size, offset in b01firm6)
  shell  FRM                 list the debug-shell commands (base table + A:/command.txt)
  verify FRM                 unpack -> repack -> compare (byte-exact round trip)
  repack FRM IMAGE_DIR OUT   re-encrypt same-length images from IMAGE_DIR into FRM

Format facts (all verified against rg2_v003.frm):
  * Container "UNITY FILE V1.10 / RICOH COMPANY": 0x800-byte header, directory of
    64-byte entries at 0x40 (u32be sector, u32be length, name[11], ...), payloads
    at sector*512.  A payload that does not start with the magic is encrypted.
  * Encryption: every 512-byte block is XORed with a 32-bit big-endian key,
    key(0) = 0xF8E69612, key(n+1) = key(n) + 0xBE39B193 (mod 2^32).  Involution.
  * Entry b01firm6 = flash image holding the A: drive; entry rg2_v001up is a
    nested container holding b01firm8 (main RTOS image, Thumb, big-endian,
    loaded at 0xA0020200 + file offset).
  * Every decrypted image starts with a 0x200-byte update header.  A: drive file
    offsets in the directory are relative to the end of that header.
  * The update header carries nUpdateCsum (0x16C): b01firm6 0xA3A80234,
    b01firm8 0x826A5E33.  Its algorithm is NOT known.  Ruled out (plaintext and
    ciphertext): 8/16/32-bit additive sums (also negated/complemented) and 32-bit
    XOR over every contiguous window starting in the first 0x800 bytes; reflected
    CRC32 variants, one's-complement sums and constant-offset sums over the
    common windows.  This tool therefore reports the field but cannot recompute
    it: repacked output is only valid for byte-identical payloads and must not
    be flashed after edits.
"""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

MAGIC = b"UNITY FILE V1.10 / RICOH COMPANY"
CONTAINER_HDR = 0x800
ENTRY_SIZE = 0x40
SECTOR = 512
KEY0 = 0xF8E69612
KEY_STEP = 0xBE39B193

IMAGE_HDR = 0x200          # update header at the start of every decrypted image
AFS_IMAGE = "b01firm6"     # image that carries the A: drive
AFS_DIR_PTR = 0x200        # u32be, payload-relative offset of the A: directory
AFS_ENTRY = 64
RTOS_IMAGE = "b01firm8"
RTOS_BASE = 0xA0020200     # RAM address of file offset 0 (verified via job table)


# ── block XOR ──────────────────────────────────────────────────────────────
def xor_blocks(data):
    """Encrypt/decrypt (same operation). Partial tail bytes use the key prefix."""
    out = bytearray(len(data))
    key = KEY0
    for o in range(0, len(data), SECTOR):
        chunk = data[o:o + SECTOR]
        mask = (key.to_bytes(4, "big") * (SECTOR // 4))[:len(chunk)]
        out[o:o + len(chunk)] = (int.from_bytes(chunk, "big")
                                 ^ int.from_bytes(mask, "big")).to_bytes(len(chunk), "big")
        key = (key + KEY_STEP) & 0xFFFFFFFF
    return bytes(out)


# ── container ──────────────────────────────────────────────────────────────
class Entry:
    def __init__(self, index, rec):
        self.index = index
        self.sector, self.length = struct.unpack(">II", rec[:8])
        self.name = rec[8:19].split(b"\0")[0].decode("latin1")

    @property
    def offset(self):
        return self.sector * SECTOR


def parse_container(buf):
    """Return (declared_total_len, [Entry]) or raise ValueError."""
    if buf[:len(MAGIC)] != MAGIC:
        raise ValueError("not a UNITY FILE container")
    total, count = struct.unpack(">II", buf[0x20:0x28])
    if total != len(buf):
        raise ValueError(f"header length 0x{total:X} != buffer length 0x{len(buf):X}")
    if count > (CONTAINER_HDR - 0x40) // ENTRY_SIZE:
        raise ValueError(f"implausible entry count {count}")
    entries = []
    for i in range(count):
        e = Entry(i, buf[0x40 + i * ENTRY_SIZE:0x40 + (i + 1) * ENTRY_SIZE])
        if e.offset + e.length > len(buf):
            raise ValueError(f"entry {e.name} runs past end of container")
        entries.append(e)
    return total, entries


def is_container(blob):
    return blob[:len(MAGIC)] == MAGIC


def walk(buf, path=""):
    """Yield (path, Entry, plaintext, nested) for every leaf; nested containers recurse."""
    _, entries = parse_container(buf)
    for e in entries:
        raw = buf[e.offset:e.offset + e.length]
        full = f"{path}/{e.name}"
        if is_container(raw):
            yield (full, e, raw, True)
            yield from walk(raw, full)
        else:
            yield (full, e, xor_blocks(raw), False)


def unpack(frm):
    """-> {image_name: plaintext}, in container order."""
    return {p.rsplit("/", 1)[1]: data for p, _, data, nested in walk(frm) if not nested}


def repack(frm, images):
    """Re-encrypt `images` into a copy of `frm`. Every image must keep its length."""
    out = bytearray(frm)

    def apply(buf_off, buf):
        _, entries = parse_container(buf)
        for e in entries:
            raw = buf[e.offset:e.offset + e.length]
            if is_container(raw):
                sub = apply(buf_off + e.offset, raw)
                out[buf_off + e.offset:buf_off + e.offset + e.length] = sub
                continue
            if e.name not in images:
                continue
            new = images[e.name]
            if len(new) != e.length:
                raise ValueError(f"{e.name}: length {len(new)} != {e.length} "
                                 "(resizing is not supported)")
            out[buf_off + e.offset:buf_off + e.offset + e.length] = xor_blocks(new)
        return bytes(out[buf_off:buf_off + len(buf)])

    apply(0, frm)
    return bytes(out)


# ── update header ──────────────────────────────────────────────────────────
def _s(b):
    return bytes(x & 0x7F for x in b).split(b"\0")[0].decode("latin1")


def update_header(img):
    u32 = lambda o: struct.unpack_from(">I", img, o)[0]
    u16 = lambda o: struct.unpack_from(">H", img, o)[0]
    return {
        "ident": _s(img[0x00:0x10]),
        "type": img[0x10],
        "version": f"{u16(0x12) >> 8}.{u16(0x12) & 0xFF:02d}",
        "date": f"{u32(0x18):08X}",
        "time": f"{u32(0x1C):08X}",
        "pLoadAddr": u32(0x20),
        "pUpdateStartAddr": u32(0x160),
        "nUpdateTotalSize": u32(0x164),
        "nUpdateFileSize": u32(0x168),
        "nUpdateCsum": u32(0x16C),
        "bExecVerify": u32(0x170),
        "bCheckHeader": u32(0x174),
        "bCompareCsum": u32(0x178),
        "bCheckRomSize": u32(0x17C),
        "sBuildName": _s(img[0x180:0x190]),
        "nRevision": u32(0x190),
        "sUpdateIcode": _s(img[0x1F0:0x200]),
    }


# ── A: drive ───────────────────────────────────────────────────────────────
def afs_entries(img):
    """[(name, file_offset_in_image, size)]; empty name terminates the table."""
    dir_off = struct.unpack_from(">I", img, AFS_DIR_PTR)[0] + IMAGE_HDR
    out = []
    while dir_off + AFS_ENTRY <= len(img):
        rec = img[dir_off:dir_off + AFS_ENTRY]
        name = rec[:32].split(b"\0")[0].decode("latin1")
        if not name:
            break
        off, size = struct.unpack(">II", rec[32:40])
        if off + IMAGE_HDR + size > len(img):
            raise ValueError(f"{name}: extends past image end")
        out.append((name, off + IMAGE_HDR, size))
        dir_off += AFS_ENTRY
    return out


# ── commands ───────────────────────────────────────────────────────────────
def load(path):
    frm = Path(path).read_bytes()
    return frm, unpack(frm)


def cmd_info(a):
    frm, images = load(a.frm)
    print(f"{a.frm}: {len(frm):,} bytes  sha256 {hashlib.sha256(frm).hexdigest()}")
    for path, e, data, nested in walk(frm):
        pad = "  " * path.count("/")
        if nested:
            print(f"{pad}{e.name}  [container] sector {e.sector} len 0x{e.length:X}")
            continue
        print(f"{pad}{e.name}  [encrypted] sector {e.sector} len 0x{e.length:X}")
        for k, v in update_header(data).items():
            val = f"0x{v:08X}" if isinstance(v, int) and k.startswith(("p", "nUpdate")) else v
            print(f"{pad}    {k:18s} {val}")
        if e.name == RTOS_IMAGE:
            print(f"{pad}    {'ram base':18s} 0x{RTOS_BASE:08X} (+ file offset)")


def cmd_ls(a):
    _, images = load(a.frm)
    for name, off, size in afs_entries(images[AFS_IMAGE]):
        print(f"{size:>9,d}  0x{off:06X}  {name}")


SHELL_REC = 36  # name[32] + u32be handler (Thumb, odd address)


def _shell_rec(rtos, o):
    if o < 0 or o + SHELL_REC > len(rtos):
        return None
    raw = rtos[o:o + 32]
    name = raw.split(b"\0")[0]
    handler = struct.unpack_from(">I", rtos, o + 32)[0]
    if (name and all(32 <= c < 127 for c in name) and raw[len(name):] == b"\0" * (32 - len(name))
            and RTOS_BASE <= handler < RTOS_BASE + len(rtos) and handler & 1):
        return name.decode(), handler
    return None


def shell_commands(rtos):
    """Debug-shell command table of the RTOS image: [(name, handler_addr)]."""
    o = rtos.find(b"syscom\0")
    while o >= 0 and not _shell_rec(rtos, o):
        o = rtos.find(b"syscom\0", o + 1)
    if o < 0:
        raise ValueError("shell command table not found")
    while _shell_rec(rtos, o - SHELL_REC):
        o -= SHELL_REC
    out = []
    while _shell_rec(rtos, o):
        out.append(_shell_rec(rtos, o))
        o += SHELL_REC
    return out


def cmd_shell(a):
    _, images = load(a.frm)
    print("# base shell table (RTOS image)")
    for name, h in shell_commands(images[RTOS_IMAGE]):
        print(f"{name:20s} 0x{h:08X}")
    print("# A:/command.txt (extended table)")
    for name, off, size in afs_entries(images[AFS_IMAGE]):
        if name == "A:/command.txt":
            text = images[AFS_IMAGE][off:off + size].decode("latin1")
            for line in text.splitlines():
                if "," in line:
                    n, h = line.split(",", 1)
                    print(f"{n.strip():32s} {h.strip()}")


def cmd_extract(a):
    _, images = load(a.frm)
    out = Path(a.outdir)
    out.mkdir(parents=True, exist_ok=True)
    for name, data in images.items():
        (out / f"{name}.bin").write_bytes(data)
        print(f"{name}.bin  {len(data):,} bytes")
    afs = out / "afs"
    for name, off, size in afs_entries(images[AFS_IMAGE]):
        rel = name.split(":", 1)[1].lstrip("/")
        dst = afs / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(images[AFS_IMAGE][off:off + size])
    print(f"afs/  {len(afs_entries(images[AFS_IMAGE]))} files")


def cmd_verify(a):
    frm, images = load(a.frm)
    again = repack(frm, images)
    ok = again == frm
    print(f"images: {', '.join(f'{k} ({len(v):,} B)' for k, v in images.items())}")
    print(f"round trip: {'byte-identical' if ok else 'MISMATCH'}")
    for name, img in images.items():
        h = update_header(img)
        size_ok = h["nUpdateFileSize"] == len(img) and h["nUpdateTotalSize"] == len(img) - IMAGE_HDR
        print(f"{name}: header sizes {'consistent' if size_ok else 'INCONSISTENT'} "
              f"(file 0x{h['nUpdateFileSize']:X}, total 0x{h['nUpdateTotalSize']:X}); "
              f"nUpdateCsum 0x{h['nUpdateCsum']:08X} not verifiable (algorithm unknown)")
    afs = afs_entries(images[AFS_IMAGE])
    print(f"A: drive: {len(afs)} files, all inside image")
    return 0 if ok else 1


def cmd_repack(a):
    frm = Path(a.frm).read_bytes()
    images = {p.name[:-4]: p.read_bytes() for p in Path(a.imgdir).glob("*.bin")}
    out = repack(frm, images)
    Path(a.out).write_bytes(out)
    changed = unpack(out) != unpack(frm)
    print(f"wrote {a.out} ({len(out):,} B){' - payload CHANGED, nUpdateCsum NOT recomputed: do not flash' if changed else ''}")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    for name, fn, args in (("info", cmd_info, ["frm"]), ("ls", cmd_ls, ["frm"]),
                           ("shell", cmd_shell, ["frm"]),
                           ("extract", cmd_extract, ["frm", "outdir"]),
                           ("verify", cmd_verify, ["frm"]),
                           ("repack", cmd_repack, ["frm", "imgdir", "out"])):
        sp = sub.add_parser(name)
        for x in args:
            sp.add_argument(x)
        sp.set_defaults(fn=fn)
    a = p.parse_args(argv)
    sys.exit(a.fn(a) or 0)


if __name__ == "__main__":
    main()
