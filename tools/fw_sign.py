#!/usr/bin/env python3
"""
AFi-R firmware signing and verification tool.

Reimplements the signature verification logic of uh-fw-tool, and adds the
ability to sign firmware with your own RSA-2048 private key.

Firmware format (UBNT):
  [UBNT header: 4-byte magic "UBNT" + 252-byte version + 4-byte header CRC]
  [PART entries: 4-byte magic "PART" + 16-byte name + 32-byte metadata
                 + 4-byte partition size + data + 4-byte data CRC]
  ...
  [ENDS block: 4-byte magic "ENDS" + 256-byte RSA-SHA1 signature]
  [4-byte trailer: 00000000]

Signature covers all bytes from start of file up to (but not including)
the "ENDS" magic.

The manufacturer's uh-fw-tool stores the public key in the binary at a
fixed offset, XOR-obfuscated with the 4-byte key 0x8193e0c4. See
patch_uh_fw_tool() for replacing it with your own key.

Usage:
  # Generate a signing keypair
  ./fw_sign.py genkey my_key

  # Verify a firmware image against the manufacturer's embedded key
  ./fw_sign.py verify firmware.bin

  # Verify against your own public key
  ./fw_sign.py verify firmware.bin --pubkey my_key.pub.pem

  # Sign a firmware image (replaces existing signature)
  ./fw_sign.py sign firmware.bin signed_firmware.bin --privkey my_key.pem

  # Patch uh-fw-tool to use your public key instead of the manufacturer's
  ./fw_sign.py patch-uhfwtool uh-fw-tool uh-fw-tool_patched --pubkey my_key.pub.pem

  # Show firmware info (partitions, CRCs, signature)
  ./fw_sign.py info firmware.bin
"""

import argparse
import base64
import hashlib
import os
import struct
import subprocess
import sys


# XOR key used by uh-fw-tool to obfuscate the embedded public key
UH_FW_TOOL_XOR_KEY = bytes([0x81, 0x93, 0xe0, 0xc4])

# File offset where the XOR-encoded public key is stored in uh-fw-tool.
# This is in the data section (vaddr 0x416000) and is consistent across
# firmware versions 2.0.0 through 4.0.3.
UH_FW_TOOL_KEY_FILE_OFFSET = 0x6000

# Length of base64-encoded RSA-2048 SubjectPublicKeyInfo (no PEM headers)
UH_FW_TOOL_KEY_B64_LEN = 392


def xor_encode(data: bytes, key: bytes) -> bytes:
    return bytes(data[i] ^ key[i % len(key)] for i in range(len(data)))


def extract_pubkey_from_uhfwtool(binary: bytes) -> str:
    """Extract and decode the manufacturer's public key from a uh-fw-tool binary."""
    encoded = binary[UH_FW_TOOL_KEY_FILE_OFFSET:
                     UH_FW_TOOL_KEY_FILE_OFFSET + UH_FW_TOOL_KEY_B64_LEN]
    decoded = xor_encode(encoded, UH_FW_TOOL_XOR_KEY)
    b64_str = decoded.decode('ascii')
    lines = [b64_str[i:i+64] for i in range(0, len(b64_str), 64)]
    return (
        "-----BEGIN PUBLIC KEY-----\n"
        + "\n".join(lines)
        + "\n-----END PUBLIC KEY-----\n"
    )


def replace_pubkey_in_uhfwtool(binary: bytes, pem_pubkey: str) -> bytes:
    """Replace the embedded public key in a uh-fw-tool binary."""
    # Extract base64 content from PEM
    lines = pem_pubkey.strip().splitlines()
    b64_lines = [l for l in lines if not l.startswith("-----")]
    b64_str = "".join(b64_lines)

    # Validate: must decode to a valid DER RSA-2048 public key (294 bytes)
    der = base64.b64decode(b64_str)
    if len(der) != 294:
        raise ValueError(
            f"Expected 294-byte DER key (RSA-2048), got {len(der)} bytes. "
            "Only RSA-2048 keys are supported."
        )

    if len(b64_str) != UH_FW_TOOL_KEY_B64_LEN:
        raise ValueError(
            f"Base64 length mismatch: expected {UH_FW_TOOL_KEY_B64_LEN}, "
            f"got {len(b64_str)}"
        )

    # XOR-encode and patch
    encoded = xor_encode(b64_str.encode('ascii'), UH_FW_TOOL_XOR_KEY)
    patched = bytearray(binary)
    patched[UH_FW_TOOL_KEY_FILE_OFFSET:
            UH_FW_TOOL_KEY_FILE_OFFSET + len(encoded)] = encoded
    return bytes(patched)


def parse_firmware(data: bytes) -> dict:
    """Parse a UBNT firmware image and return its structure."""
    info = {"size": len(data), "partitions": []}

    if len(data) < 260:
        raise ValueError("File too small to be a valid firmware image")

    # UBNT header
    magic = data[0:4]
    if magic != b'UBNT':
        raise ValueError(f"Invalid magic: {magic.hex()} (expected 'UBNT')")

    version_raw = data[4:256]
    null_idx = version_raw.find(b'\x00')
    info["version"] = version_raw[:null_idx].decode('ascii', errors='replace') if null_idx > 0 else ""
    info["header_crc"] = struct.unpack('>I', data[256:260])[0]

    # Find ENDS marker
    ends_idx = data.rfind(b'ENDS')
    if ends_idx < 0:
        info["signed"] = False
        info["ends_offset"] = None
        info["signature"] = None
    else:
        info["signed"] = True
        info["ends_offset"] = ends_idx
        info["signature"] = data[ends_idx + 4:ends_idx + 4 + 256]
        info["signed_data"] = data[:ends_idx]

    # Parse PART entries
    offset = 260  # after UBNT header + CRC
    while offset < (ends_idx if ends_idx else len(data)):
        if offset + 4 > len(data):
            break
        part_magic = data[offset:offset + 4]
        if part_magic != b'PART':
            offset += 1
            continue

        if offset + 56 > len(data):
            break

        name_raw = data[offset + 4:offset + 20]
        null_idx = name_raw.find(b'\x00')
        name = name_raw[:null_idx].decode('ascii', errors='replace') if null_idx > 0 else ""

        part_size = struct.unpack('>I', data[offset + 48:offset + 52])[0]

        # Data starts after 56-byte header
        data_start = offset + 56
        data_end = data_start + part_size

        # CRC32 follows partition data
        crc_offset = data_end
        part_crc = struct.unpack('>I', data[crc_offset:crc_offset + 4])[0] if crc_offset + 4 <= len(data) else None

        info["partitions"].append({
            "name": name,
            "header_offset": offset,
            "data_offset": data_start,
            "size": part_size,
            "crc_offset": crc_offset,
            "crc": part_crc,
        })

        offset = crc_offset + 4

    return info


def verify_firmware(fw_data: bytes, pubkey_pem: str) -> bool:
    """Verify firmware RSA-SHA1 signature using openssl CLI."""
    info = parse_firmware(fw_data)
    if not info["signed"]:
        print("ERROR: Firmware is not signed (no ENDS marker found)")
        return False

    # Write temp files for openssl
    import tempfile
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(info["signed_data"])
        data_path = f.name
    with tempfile.NamedTemporaryFile(suffix='.sig', delete=False) as f:
        f.write(info["signature"])
        sig_path = f.name
    with tempfile.NamedTemporaryFile(suffix='.pem', mode='w', delete=False) as f:
        f.write(pubkey_pem)
        key_path = f.name

    try:
        result = subprocess.run(
            ['openssl', 'dgst', '-sha1', '-verify', key_path,
             '-signature', sig_path, data_path],
            capture_output=True
        )
        ok = result.returncode == 0
        return ok
    finally:
        os.unlink(data_path)
        os.unlink(sig_path)
        os.unlink(key_path)


def sign_firmware(fw_data: bytes, privkey_pem_path: str) -> bytes:
    """Sign firmware with an RSA-2048 private key, replacing any existing signature."""
    info = parse_firmware(fw_data)

    # Strip existing signature block if present
    if info["signed"]:
        unsigned = info["signed_data"]
    else:
        unsigned = fw_data

    # Sign with openssl
    import tempfile
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(unsigned)
        data_path = f.name
    with tempfile.NamedTemporaryFile(suffix='.sig', delete=False) as f:
        sig_path = f.name

    try:
        result = subprocess.run(
            ['openssl', 'dgst', '-sha1', '-sign', privkey_pem_path,
             '-out', sig_path, data_path],
            capture_output=True
        )
        if result.returncode != 0:
            raise RuntimeError(f"openssl sign failed: {result.stderr.decode()}")

        with open(sig_path, 'rb') as f:
            signature = f.read()

        if len(signature) != 256:
            raise RuntimeError(
                f"Expected 256-byte signature (RSA-2048), got {len(signature)}. "
                "Make sure you're using an RSA-2048 key."
            )
    finally:
        os.unlink(data_path)
        os.unlink(sig_path)

    # Reassemble: unsigned data + ENDS + signature + 4-byte zero trailer
    return unsigned + b'ENDS' + signature + b'\x00\x00\x00\x00'


def generate_keypair(name: str):
    """Generate an RSA-2048 keypair for firmware signing."""
    privkey_path = f"{name}.pem"
    pubkey_path = f"{name}.pub.pem"

    # Generate private key
    result = subprocess.run(
        ['openssl', 'genrsa', '-out', privkey_path, '2048'],
        capture_output=True
    )
    if result.returncode != 0:
        raise RuntimeError(f"Key generation failed: {result.stderr.decode()}")

    # Extract public key
    result = subprocess.run(
        ['openssl', 'rsa', '-in', privkey_path, '-pubout', '-out', pubkey_path],
        capture_output=True
    )
    if result.returncode != 0:
        raise RuntimeError(f"Public key extraction failed: {result.stderr.decode()}")

    os.chmod(privkey_path, 0o600)
    print(f"Private key: {privkey_path}")
    print(f"Public key:  {pubkey_path}")


def cmd_info(args):
    with open(args.firmware, 'rb') as f:
        fw = f.read()
    info = parse_firmware(fw)

    print(f"Firmware: {args.firmware}")
    print(f"Size: {info['size']} bytes")
    print(f"Version: {info['version']}")
    print(f"Header CRC: {info['header_crc']:#010x}")
    print(f"Signed: {info['signed']}")
    if info['signed']:
        print(f"ENDS offset: {info['ends_offset']:#x}")
        print(f"Signature: {info['signature'][:16].hex()}...")

    print(f"\nPartitions ({len(info['partitions'])}):")
    for p in info["partitions"]:
        print(f"  {p['name']:16s}  offset={p['header_offset']:#08x}  "
              f"size={p['size']:#010x}  crc={p['crc']:#010x}")


def cmd_verify(args):
    with open(args.firmware, 'rb') as f:
        fw = f.read()

    if args.pubkey:
        with open(args.pubkey) as f:
            pubkey_pem = f.read()
    else:
        # Use manufacturer's embedded key (extracted from v3.1.2 uh-fw-tool)
        pubkey_pem = get_manufacturer_pubkey()

    ok = verify_firmware(fw, pubkey_pem)
    if ok:
        print(f"Verified OK: {args.firmware}")
    else:
        print(f"Verification FAILED: {args.firmware}")
        sys.exit(1)


def cmd_sign(args):
    with open(args.firmware, 'rb') as f:
        fw = f.read()

    signed = sign_firmware(fw, args.privkey)

    with open(args.output, 'wb') as f:
        f.write(signed)

    print(f"Signed firmware written to: {args.output}")

    # Verify if public key is available
    pubkey_path = args.privkey.replace('.pem', '.pub.pem')
    if os.path.exists(pubkey_path):
        with open(pubkey_path) as f:
            pubkey_pem = f.read()
        ok = verify_firmware(signed, pubkey_pem)
        if ok:
            print(f"Verification against {pubkey_path}: OK")
        else:
            print(f"WARNING: Verification against {pubkey_path}: FAILED")


def cmd_genkey(args):
    generate_keypair(args.name)


def cmd_patch_uhfwtool(args):
    with open(args.binary, 'rb') as f:
        binary = f.read()

    # Show current key
    try:
        current_pem = extract_pubkey_from_uhfwtool(binary)
        print(f"Current embedded key:\n{current_pem}")
    except Exception as e:
        print(f"Warning: could not extract current key: {e}")

    with open(args.pubkey) as f:
        new_pubkey_pem = f.read()

    patched = replace_pubkey_in_uhfwtool(binary, new_pubkey_pem)

    with open(args.output, 'wb') as f:
        f.write(patched)

    # Verify the patch
    verify_pem = extract_pubkey_from_uhfwtool(patched)
    print(f"New embedded key:\n{verify_pem}")
    print(f"Patched binary written to: {args.output}")


def cmd_extract_key(args):
    with open(args.binary, 'rb') as f:
        binary = f.read()

    pem = extract_pubkey_from_uhfwtool(binary)
    if args.output:
        with open(args.output, 'w') as f:
            f.write(pem)
        print(f"Key written to: {args.output}")
    else:
        print(pem)


def get_manufacturer_pubkey() -> str:
    """Return the manufacturer's firmware signing public key (extracted from uh-fw-tool)."""
    return (
        "-----BEGIN PUBLIC KEY-----\n"
        "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAp3BfWLtQT9OrJHYsMKhj\n"
        "KJz9ZL6M41wPPHMcRul6kYBi94PwQqNfRUeQjIu+gY75ebcbqFZhLseyqa7GM7qk\n"
        "CbWW1fmkqeieHz3ONaD0CRpWs7vG5EzGj/XBjF+OriguYfpGAxizCNU8NzRZHaEB\n"
        "w+YEtTf2Nm5PWcHzxpTKffEHks49PHLHBc4zaWsRDX/bcoa3atO938EFqLIqBhnD\n"
        "CmkEf3O/ay8YeTs9aLHznFb9ebBVWUvK/tjHJ97LtQCic6iHJCEOtPNfqR9nA+kR\n"
        "reCHOYo8EreI4+ni5Wcin7LYzhCWGKfanTGunWKjmfyzEYWXTzLu40G2WpU7wGoo\n"
        "tQIDAQAB\n"
        "-----END PUBLIC KEY-----\n"
    )


def main():
    parser = argparse.ArgumentParser(
        description="AFi-R firmware signing and verification tool"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # info
    p = subparsers.add_parser("info", help="Show firmware image info")
    p.add_argument("firmware", help="Firmware .bin file")

    # verify
    p = subparsers.add_parser("verify", help="Verify firmware signature")
    p.add_argument("firmware", help="Firmware .bin file")
    p.add_argument("--pubkey", help="PEM public key file (default: manufacturer key)")

    # sign
    p = subparsers.add_parser("sign", help="Sign firmware with your private key")
    p.add_argument("firmware", help="Input firmware .bin file")
    p.add_argument("output", help="Output signed firmware .bin file")
    p.add_argument("--privkey", required=True, help="PEM private key file")

    # genkey
    p = subparsers.add_parser("genkey", help="Generate RSA-2048 keypair for signing")
    p.add_argument("name", help="Base name for key files (produces NAME.pem and NAME.pub.pem)")

    # patch-uhfwtool
    p = subparsers.add_parser(
        "patch-uhfwtool",
        help="Patch uh-fw-tool binary to use your public key"
    )
    p.add_argument("binary", help="Input uh-fw-tool binary")
    p.add_argument("output", help="Output patched binary")
    p.add_argument("--pubkey", required=True, help="Your PEM public key file")

    # extract-key
    p = subparsers.add_parser(
        "extract-key",
        help="Extract the embedded public key from a uh-fw-tool binary"
    )
    p.add_argument("binary", help="uh-fw-tool binary")
    p.add_argument("-o", "--output", help="Output PEM file (default: stdout)")

    args = parser.parse_args()

    commands = {
        "info": cmd_info,
        "verify": cmd_verify,
        "sign": cmd_sign,
        "genkey": cmd_genkey,
        "patch-uhfwtool": cmd_patch_uhfwtool,
        "extract-key": cmd_extract_key,
    }
    commands[args.command](args)


if __name__ == "__main__":
    main()
