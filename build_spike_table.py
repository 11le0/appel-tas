"""
Builds spike_correlation_table.json from spikes.txt.

spikes.txt format (one row per combo, comma-separated):
  <playerDir>(player dir),<spikeDir>(spike dir),spike <id>,<Costume>,<130*129 chars of 0/1>

Grid layout per row: 129 rows (dy) x 130 cols (dx), row-major.
  col index c -> dx = c - DX_ORIGIN
  row index r -> dy = r - DY_ORIGIN
DX_ORIGIN/DY_ORIGIN default to floor(W/2)/floor(H/2) -- confirm against the
reference (slow-path) engine before shipping; see calibrate_origin.py.

RLE scheme is IDENTICAL to spikeHitboxes.json / decode_binary_rle() in spikes.c:
  "<startBit>|<run1> <run2> <run3> ..."
"""
import json, sys

W, H = 130, 129
DX_ORIGIN, DY_ORIGIN = 65, 64

def encode_rle(bits: str) -> str:
    if not bits:
        return ""
    cur = bits[0]
    counts = []
    run = 0
    for ch in bits:
        if ch == cur:
            run += 1
        else:
            counts.append(run)
            cur = ch
            run = 1
    counts.append(run)
    return f"{bits[0]}|{' '.join(map(str, counts))}"

def decode_rle(enc: str) -> str:
    start, counts_s = enc.split('|')
    cur = start
    out = []
    for c in counts_s.split(' '):
        out.append(cur * int(c))
        cur = '1' if cur == '0' else '0'
    return ''.join(out)

def main(src="spikes.txt", dst="spike_correlation_table.json"):
    table = {}
    n_rows = 0
    with open(src) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            pdir_s, sdir_s, sid_s, costume, bits = line.split(',', 4)
            pdir = float(pdir_s.split('(')[0])
            sdir = int(sdir_s.split('(')[0])
            sid = int(sid_s.split(' ')[1])
            assert len(bits) == W * H, f"bad row length {len(bits)} for {line[:40]}"

            enc = encode_rle(bits)
            assert decode_rle(enc) == bits, "RLE round-trip failed"

            key = f"{pdir}|{sdir}|{sid}|{costume}"
            table[key] = enc
            n_rows += 1

    out = {
        "width": W,
        "height": H,
        "dx_origin": DX_ORIGIN,
        "dy_origin": DY_ORIGIN,
        "table": table,
    }
    with open(dst, "w") as f:
        json.dump(out, f, separators=(',', ':'))

    print(f"wrote {n_rows} entries to {dst}")

if __name__ == "__main__":
    main(*sys.argv[1:])
