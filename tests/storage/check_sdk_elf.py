"""Check that a real SDK executable's PT_INTERP contains only its path."""
import json
from pathlib import Path
import struct
import sys


def check(path):
    data = Path(path).read_bytes()
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    if header[0][:6] != b"\x7fELF\x02\x01":
        raise ValueError("requires little-endian ELF64")
    segments = [struct.unpack_from("<IIQQQQQQ", data, header[5] + i * header[9])
                for i in range(header[10])]
    interpreters = [segment for segment in segments if segment[0] == 3]
    if len(interpreters) != 1:
        raise ValueError("expected exactly one PT_INTERP")
    segment = interpreters[0]
    expected = b"/lib/ld-osinfo-x86-64.so\0"
    offset, size = segment[2], segment[5]
    if data[offset:offset + size] != expected or size != segment[6]:
        raise ValueError("PT_INTERP includes data beyond interpreter path")
    notes = [struct.unpack_from("<IIQQQQIIQQ", data, header[6] + i * header[11])
             for i in range(header[12])]
    notes = [section for section in notes if section[1] == 7 and section[5]]
    for section in notes:
        start, end = section[4], section[4] + section[5]
        if start < offset + size and offset < end:
            raise ValueError("note section overlaps PT_INTERP")
    return {"file": str(path), "status": "PASS", "interpreter_bytes": size,
            "note_sections_preserved": len(notes)}


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("usage: check_sdk_elf.py ELF...")
    for path in sys.argv[1:]:
        try:
            print(json.dumps(check(path)))
        except (OSError, ValueError, struct.error) as error:
            print(json.dumps({"file": path, "status": "FAIL", "error": str(error)}))
            raise SystemExit(1)
