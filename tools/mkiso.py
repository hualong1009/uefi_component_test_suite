#!/usr/bin/env python3
"""mkiso.py - wraps build/BOOTX64.EFI into a bootable UEFI ISO.

The ISO is a plain ISO 9660 image with two ways in:

  * an El Torito no-emulation entry with platform id 0xEF whose boot image is a
    small FAT16 volume holding EFI/BOOT/BOOTX64.EFI (this is what UEFI firmware
    mounts, per the UEFI spec the EFI boot image must be a FAT filesystem),
  * the same EFI/BOOT/BOOTX64.EFI file in the ISO 9660 tree, for firmware that
    falls back to the filesystem instead of the boot catalog.

Usage:
    python tools/mkiso.py [--efi build/BOOTX64.EFI] [--out build/UCTS.iso]
"""

import argparse
import hashlib
import io
import os
import struct
import sys

try:
    import pycdlib
except ImportError:
    sys.exit("pycdlib is required: python -m pip install pycdlib")

SECTOR = 512
CLUSTER_SECTORS = 1
CLUSTER_BYTES = SECTOR * CLUSTER_SECTORS
ROOT_ENTRIES = 512
TOTAL_SECTORS = 8192               # 4 MB volume, safely inside the FAT16 range
RESERVED_SECTORS = 1
FAT_COUNT = 2
MEDIA_DESCRIPTOR = 0xF8
FIXED_DATE = (2026, 1, 1, 0, 0, 0)

STARTUP_NSH = r"""@echo -off
if exist fs0:\EFI\BOOT\BOOTX64.EFI then
  fs0:\EFI\BOOT\BOOTX64.EFI
endif
if exist fs1:\EFI\BOOT\BOOTX64.EFI then
  fs1:\EFI\BOOT\BOOTX64.EFI
endif
"""


# --------------------------------------------------------------- FAT16 image
def fat_datetime(year=2026, month=1, day=1, hour=0, minute=0, second=0):
    dos_date = ((year - 1980) << 9) | (month << 5) | day
    dos_time = (hour << 11) | (minute << 5) | (second // 2)
    return dos_time, dos_date


def short_entry(name, attr, cluster, size):
    """32 byte directory entry, 8.3 short name only.

    The on disk name is the base name padded to 8 characters followed by the
    extension padded to 3 - a plain 11 character pad of "BOOTX64.EFI" would read
    back as "BOOTX64..EFI".
    """
    if name == ".":
        raw = b"." + b" " * 10
    elif name == "..":
        raw = b".." + b" " * 9
    else:
        base, _, ext = name.partition(".")
        raw = (base.upper().ljust(8)[:8] + ext.upper().ljust(3)[:3]).encode("ascii")
    dos_time, dos_date = fat_datetime(*FIXED_DATE)
    return struct.pack(
        "<11sBBBHHHHHHHI",
        raw, attr, 0, 0, dos_time, dos_date, dos_date,
        (cluster >> 16) & 0xFFFF, dos_time, dos_date, cluster & 0xFFFF, size,
    )


def build_fat(files):
    """files: list of (components, bytes); components is e.g. ('EFI','BOOT','BOOTX64.EFI').

    Returns a bytes object holding a FAT16 volume. Only short names are used, so
    the volume is readable by every firmware FAT driver without LFN support.
    """
    # ---- tree: nested dict, a directory is a dict and a file is a bytes leaf
    tree = {}

    def dir_node(parts):
        node = tree
        for part in parts:
            node = node.setdefault(part, {})
        return node

    for parts, data in files:
        dir_node(parts[:-1])[parts[-1]] = data

    dirs = [()]

    def collect(parts):
        node = dir_node(parts)
        for name, value in node.items():
            if isinstance(value, dict):
                dirs.append(parts + (name,))
                collect(parts + (name,))

    collect(())
    dirs.sort(key=len)

    # ---- cluster assignment: one cluster per subdirectory, a chain per file
    next_cluster = 2
    dir_clusters = {}
    for d in dirs:
        dir_clusters[d] = next_cluster
        next_cluster += 1

    fat = {0: 0xFFF8, 1: 0xFFFF}
    file_clusters = {}
    for parts, data in files:
        first = next_cluster
        count = max(1, (len(data) + CLUSTER_BYTES - 1) // CLUSTER_BYTES)
        for i in range(count):
            fat[next_cluster + i] = (next_cluster + i + 1) if i + 1 < count else 0xFFFF
        file_clusters[parts] = first
        next_cluster += count
    for d in dirs:
        fat[dir_clusters[d]] = 0xFFFF

    needed_clusters = next_cluster - 2

    # The FAT type is decided from the cluster count, not from the BPB string: a
    # volume with fewer than 4085 clusters is read as FAT12 and our two byte FAT
    # entries would be misinterpreted. Solve the geometry for a fixed size.
    root_sectors = (ROOT_ENTRIES * 32 + SECTOR - 1) // SECTOR
    total_sectors = TOTAL_SECTORS
    fat_sectors = 1
    for _ in range(16):
        clusters = total_sectors - RESERVED_SECTORS - FAT_COUNT * fat_sectors - root_sectors
        need = (2 * (clusters + 2) + SECTOR - 1) // SECTOR
        if need == fat_sectors:
            break
        fat_sectors = need
    if not 4085 <= clusters < 65525:
        raise SystemExit("cluster count %d is outside the FAT16 range" % clusters)
    if needed_clusters > clusters:
        raise SystemExit("boot volume too small: %d of %d clusters used"
                         % (needed_clusters, clusters))

    data_start = (RESERVED_SECTORS + FAT_COUNT * fat_sectors + root_sectors) * SECTOR

    img = bytearray(total_sectors * SECTOR)

    # ---- boot sector / BPB
    bpb = bytearray(SECTOR)
    bpb[0:3] = bytes((0xEB, 0x3C, 0x90))
    bpb[3:11] = b"MSWIN4.1"
    struct.pack_into("<HBHBHHBHHHII", bpb, 11,
                     SECTOR, CLUSTER_SECTORS, RESERVED_SECTORS, FAT_COUNT, ROOT_ENTRIES,
                     total_sectors, MEDIA_DESCRIPTOR, fat_sectors, 32, 64, 0, 0)
    bpb[36] = 0x80                       # drive number
    bpb[38] = 0x29                       # extended boot signature
    struct.pack_into("<I", bpb, 39, 0x55435453)   # volume id "UCTS"
    bpb[43:54] = b"UCTS EFI   "
    bpb[54:62] = b"FAT16   "
    bpb[510:512] = bytes((0x55, 0xAA))
    img[0:SECTOR] = bpb

    # ---- FAT copies
    fat_bytes = bytearray(fat_sectors * SECTOR)
    for c, val in fat.items():
        struct.pack_into("<H", fat_bytes, c * 2, val)
    for i in range(FAT_COUNT):
        off = (RESERVED_SECTORS + i * fat_sectors) * SECTOR
        img[off:off + len(fat_bytes)] = fat_bytes

    # ---- directory contents
    def dir_bytes(parts):
        entries = bytearray()
        if parts:
            # the root directory of a FAT16 volume has no dot entries
            entries += short_entry(".", 0x10, dir_clusters[parts], 0)
            parent = parts[:-1] if len(parts) > 1 else ()
            entries += short_entry("..", 0x10, dir_clusters[parent] if parent else 0, 0)
        node = dir_node(parts)
        for name in sorted(node):
            child = parts + (name,)
            if isinstance(node[name], dict):
                entries += short_entry(name.upper(), 0x10, dir_clusters[child], 0)
            else:
                entries += short_entry(name.upper(), 0x20, file_clusters[child],
                                       len(node[name]))
        if len(entries) > CLUSTER_BYTES:
            raise SystemExit("directory %s does not fit one cluster" % (parts,))
        raw = bytes(entries)
        return raw + bytes(CLUSTER_BYTES - len(raw))

    root_off = (RESERVED_SECTORS + FAT_COUNT * fat_sectors) * SECTOR
    root_entries = dir_bytes(())
    img[root_off:root_off + len(root_entries)] = root_entries

    for d in dirs[1:]:
        off = data_start + (dir_clusters[d] - 2) * CLUSTER_BYTES
        img[off:off + CLUSTER_BYTES] = dir_bytes(d)

    # ---- file data
    for parts, data in files:
        first = file_clusters[parts]
        count = max(1, (len(data) + CLUSTER_BYTES - 1) // CLUSTER_BYTES)
        blob = data + bytes(count * CLUSTER_BYTES - len(data))
        off = data_start + (first - 2) * CLUSTER_BYTES
        img[off:off + len(blob)] = blob

    return bytes(img)


# --------------------------------------------------------------- FAT read back
def fat_read(volume, parts):
    """Read one file back out of a FAT16 volume. Used to prove the image we just
    built is actually walkable instead of trusting the writer."""
    bps, spc, rsv, nfats, rootent = struct.unpack("<HBHBH", volume[11:19])
    total16, media, fat_sectors = struct.unpack("<HBH", volume[19:24])
    fat = volume[rsv * bps:(rsv + fat_sectors) * bps]

    def chain(first):
        out = bytearray()
        cluster = first
        seen = 0
        while 2 <= cluster < 0xFFF8:
            start = (rsv + nfats * fat_sectors) * bps + rootent * 32
            off = start + (cluster - 2) * spc * bps
            out += volume[off:off + spc * bps]
            cluster = struct.unpack("<H", fat[cluster * 2:cluster * 2 + 2])[0]
            seen += 1
            if seen > 0x10000:
                raise SystemExit("FAT chain loop")
        return bytes(out)

    dir_off = (rsv + nfats * fat_sectors) * bps
    dir_size = rootent * 32
    node = volume[dir_off:dir_off + dir_size]
    for index, part in enumerate(parts):
        want = (part.upper().split(".")[0].ljust(8)[:8]
                + (part.upper().split(".")[1] if "." in part else "").ljust(3)[:3])
        found = None
        for i in range(0, len(node), 32):
            entry = node[i:i + 32]
            if entry[0] in (0x00, 0xE5):
                if entry[0] == 0x00:
                    break
                continue
            if entry[11] == 0x0F:
                continue
            if entry[:11].decode("ascii", "replace") == want:
                found = entry
                break
        if found is None:
            raise SystemExit("FAT self check: %s not found" % "/".join(parts[:index + 1]))
        cluster = struct.unpack("<H", found[26:28])[0]
        size = struct.unpack("<I", found[28:32])[0]
        data = chain(cluster)
        if index == len(parts) - 1:
            return data[:size], size
        node = data                   # walk into the next directory cluster
    raise SystemExit("FAT self check: empty path")


# ----------------------------------------------------------------- ISO build
def build_iso(efi_path, out_path, vol_ident="UCTS"):
    with io.open(efi_path, "rb") as fh:
        efi = fh.read()

    fat = build_fat([(("EFI", "BOOT", "BOOTX64.EFI"), efi)])
    readback, size = fat_read(fat, ("EFI", "BOOT", "BOOTX64.EFI"))
    if readback != efi or size != len(efi):
        raise SystemExit("boot image self check failed: read back %d of %d bytes"
                         % (len(readback), len(efi)))

    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=3, vol_ident=vol_ident, joliet=3, rock_ridge="1.09",
            sys_ident="UEFI")
    # If a firmware has no El Torito support it drops to the UEFI shell, which
    # runs startup.nsh from the ISO filesystem. Same binary, same entry point.
    startup = STARTUP_NSH.encode("ascii")
    iso.add_fp(io.BytesIO(startup), len(startup), iso_path="/STARTUP.NSH;1",
               rr_name="startup.nsh", joliet_path="/STARTUP.NSH")
    iso.add_directory(iso_path="/EFI", rr_name="EFI", joliet_path="/EFI")
    iso.add_directory(iso_path="/EFI/BOOT", rr_name="BOOT", joliet_path="/EFI/BOOT")
    # filesystem copy, for firmware that scans the ISO 9660 tree
    iso.add_fp(io.BytesIO(efi), len(efi), iso_path="/EFI/BOOT/BOOTX64.EFI;1",
               rr_name="BOOTX64.EFI", joliet_path="/EFI/BOOT/BOOTX64.EFI")
    # El Torito boot image: the FAT volume the firmware mounts
    iso.add_fp(io.BytesIO(fat), len(fat), iso_path="/EFI/BOOT/EFIBOOT.IMG;1",
               rr_name="EFIBOOT.IMG", joliet_path="/EFI/BOOT/EFIBOOT.IMG")
    # the boot catalog records the image size in 512 byte sectors; EDK2 sizes the
    # El Torito partition from it, so zero here means a zero length boot device
    iso.add_eltorito("/EFI/BOOT/EFIBOOT.IMG;1", media_name="noemul",
                     boot_load_size=len(fat) // SECTOR, efi=True, platform_id=0xEF)
    iso.write(out_path)
    iso.close()
    return len(efi), len(fat)


# -------------------------------------------------------------- ISO validate
def _both32(buf, off):
    """ISO 9660 stores numbers both-endian; return the little endian value."""
    le = struct.unpack("<I", buf[off:off + 4])[0]
    be = struct.unpack(">I", buf[off + 4:off + 8])[0]
    return le, (le == be)


def _both16(buf, off):
    le = struct.unpack("<H", buf[off:off + 2])[0]
    be = struct.unpack(">H", buf[off + 2:off + 4])[0]
    return le, (le == be)


def verify_iso(path, expected_efi=None):
    """Structural check of an ISO 9660 + El Torito image, no external tools.

    Prints one line per check and returns the number of failures. Anything that
    a Linux mount or a UEFI firmware would trip over shows up here.
    """
    data = io.open(path, "rb").read()
    problems = 0

    def check(ok, text, detail=""):
        nonlocal problems
        print(("  ok   " if ok else "  FAIL ") + text + ((" - " + detail) if detail else ""))
        if not ok:
            problems += 1

    print("iso: %s (%d bytes, %d blocks of 2048)"
          % (path, len(data), len(data) // 2048))
    check(len(data) % 2048 == 0, "size is a multiple of 2048")
    blocks = len(data) // 2048

    # ---- volume descriptors
    pvd = boot_rec = joliet = None
    sector = 16
    end = None
    while sector < 16 + 32:
        vd = data[sector * 2048:(sector + 1) * 2048]
        if len(vd) < 2048 or vd[0] == 0xFF:
            end = sector
            break
        if vd[1:6] != b"CD001":
            break
        if vd[0] == 0 and boot_rec is None:
            boot_rec = vd
        elif vd[0] == 1 and pvd is None:
            pvd = vd
        elif vd[0] == 2 and joliet is None and vd[88:91] in (b"%/E", b"%/C", b"%/@"):
            joliet = vd
        sector += 1

    check(pvd is not None, "primary volume descriptor at block 16")
    check(end is not None, "volume descriptor set terminator", "block %s" % end)
    check(boot_rec is not None, "El Torito boot record descriptor")
    check(joliet is not None, "Joliet supplementary descriptor")
    if pvd is None:
        return problems

    lba, lba_ok = _both16(pvd, 128)
    check(lba == 2048 and lba_ok, "logical block size", str(lba))
    space, space_ok = _both32(pvd, 80)
    # hybrid images carry partitions after the ISO 9660 volume, so a volume that
    # is smaller than the file is fine; one that is larger is truncation
    check(space <= blocks and space_ok, "volume space size fits inside the file",
          "%d blocks of %d" % (space, blocks))
    set_size, _ = _both16(pvd, 120)
    check(set_size == 1, "volume set size", str(set_size))

    # ---- root directory record: extent + length
    root = pvd[156:156 + 34]
    root_len = root[0]
    root_ext = struct.unpack("<I", root[2:6])[0]
    root_size = struct.unpack("<I", root[10:14])[0]
    check(root_len == 34 and root_ext < blocks and root_size > 0,
          "root directory record", "extent %d, %d bytes" % (root_ext, root_size))

    # ---- walk the tree, every record must live inside the volume
    seen = [0]

    def walk(extent, length, depth=0):
        buf = data[extent * 2048:extent * 2048 + length]
        pos = 0
        while pos < len(buf) and buf[pos]:
            rec_len = buf[pos]
            if rec_len == 0 or pos + rec_len > len(buf):
                break
            rec = buf[pos:pos + rec_len]
            size = struct.unpack("<I", rec[10:14])[0]
            ext = struct.unpack("<I", rec[2:6])[0]
            flags = rec[25]
            name_len = rec[32]
            name = rec[33:33 + name_len]
            if name not in (bytes((0,)), bytes((1,))):
                seen[0] += 1
                inside = ext + (size + 2047) // 2048 <= blocks
                if not inside:
                    check(False, "record inside volume",
                          "%r extent %d size %d" % (name, ext, size))
                if flags & 0x02 and depth < 8:
                    walk(ext, size, depth + 1)
            pos += rec_len

    walk(root_ext, root_size)
    check(seen[0] > 0, "directory tree walked", "%d records" % seen[0])

    # ---- El Torito boot catalog
    if boot_rec is not None:
        sysid = boot_rec[7:39].strip(bytes((0x00,)) + b" ")
        check(sysid == b"EL TORITO SPECIFICATION", "boot record system id", repr(sysid))
        cat_lba = struct.unpack("<I", boot_rec[0x47:0x4B])[0]
        check(cat_lba < blocks, "boot catalog inside volume", "block %d" % cat_lba)
        cat = data[cat_lba * 2048:(cat_lba + 1) * 2048]
        check(cat[0] == 0x01 and cat[30:32] == bytes((0x55, 0xAA)) and cat[1] == 0xEF,
              "boot catalog header", "platform 0x%02x" % cat[1])
        total = sum(struct.unpack("<16H", cat[:32])) & 0xFFFF
        check(total == 0, "boot catalog checksum")
        entries = data[cat_lba * 2048 + 32:cat_lba * 2048 + 64]
        count = struct.unpack("<H", entries[6:8])[0]
        rba = struct.unpack("<I", entries[8:12])[0]
        check(entries[0] == 0x88 and entries[1] == 0, "boot entry: bootable, no emulation")
        check(0 < count <= 0xFFFF and rba < blocks, "boot entry image size and lba",
              "%d sectors of 512 at block %d" % (count, rba))
        image = data[rba * 2048:rba * 2048 + count * 512]
        if image[54:59] != b"FAT16":
            # a foreign image (FAT12/FAT32, disk image, ...) - nothing to check
            print("  skip boot image FAT16 check - not a FAT16 volume")
        else:
            try:
                efi, size = fat_read(image, ("EFI", "BOOT", "BOOTX64.EFI"))
                check(len(efi) == size, "boot image FAT volume holds EFI/BOOT/BOOTX64.EFI",
                      "%d bytes" % size)
                print("  sha256 boot image EFI: " + hashlib.sha256(efi).hexdigest())
                if expected_efi and os.path.isfile(expected_efi):
                    with io.open(expected_efi, "rb") as fh:
                        want = fh.read()
                    check(efi == want, "boot image matches %s" % expected_efi)
            except (SystemExit, struct.error) as exc:
                check(False, "boot image FAT volume readable", str(exc))

    with io.open(path, "rb") as fh:
        digest = hashlib.sha256(fh.read()).hexdigest()
    print("  sha256 iso: " + digest)
    print("  %s" % ("all checks passed" if problems == 0 else "%d check(s) failed" % problems))
    return problems


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    # tools/build.sh may put the binary anywhere; honour $OUT when it is set
    default_dir = os.environ.get("OUT") or os.path.join(root, "build")
    ap.add_argument("--efi", default=os.path.join(default_dir, "BOOTX64.EFI"))
    ap.add_argument("--out", default=os.path.join(default_dir, "UCTS.iso"))
    ap.add_argument("--verify", metavar="ISO",
                    help="only validate an existing ISO image, do not build")
    args = ap.parse_args()

    if args.verify:
        sys.exit(1 if verify_iso(args.verify, args.efi) else 0)

    if not os.path.isfile(args.efi):
        sys.exit("EFI binary not found: %s (build it first)" % args.efi)

    efi_len, fat_len = build_iso(args.efi, args.out)
    print("efi binary : %d bytes" % efi_len)
    print("boot image : %d bytes (FAT16 with EFI/BOOT/BOOTX64.EFI)" % fat_len)
    print("iso        : %s (%d bytes)" % (args.out, os.path.getsize(args.out)))
    return verify_iso(args.out, args.efi)


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
