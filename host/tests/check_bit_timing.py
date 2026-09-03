"""Nachbildung von timing_for_bitrate() aus slcan.c, um die resultierenden
Bit-Timings zu pruefen, bevor sie auf Hardware landen."""

PCLK1 = 36_000_000
SP_TOL = 30


def timing_for_bitrate(bitrate):
    target = 750 if bitrate >= 800_000 else 875
    best = None
    if bitrate < 5000 or bitrate > 1_000_000:
        return None
    for ntq in range(25, 7, -1):
        brp = (PCLK1 + (ntq * bitrate) // 2) // (ntq * bitrate)
        if brp < 1 or brp > 1024:
            continue
        actual = PCLK1 // (brp * ntq)
        diff = abs(actual - bitrate)
        if diff * 1000 > bitrate:
            continue
        t1 = (ntq * target + 500) // 1000
        if t1 < 2:
            t1 = 2
        t1 -= 1
        if t1 > 16:
            t1 = 16
        t2 = ntq - 1 - t1
        if t2 < 1 or t2 > 8:
            continue
        sp = ((t1 + 1) * 1000) // ntq
        err = abs(sp - target)
        if best is None or err < best[0]:
            best = (err, ntq, brp, t1, t2)
        if err <= SP_TOL:
            break
    return best


NAMES = [10_000, 20_000, 50_000, 100_000, 125_000,
         250_000, 500_000, 800_000, 1_000_000, 83_333]

print(f"{'Cmd':<4}{'soll':>10}{'BRP':>6}{'BS1':>5}{'BS2':>5}{'ntq':>5}"
      f"{'ist':>10}{'Fehler':>9}{'SP':>8}")
fail = 0
for i, br in enumerate(NAMES):
    r = timing_for_bitrate(br)
    if r is None:
        print(f"S{i}  {br:>10}  KEINE LOESUNG")
        fail += 1
        continue
    err, ntq, brp, t1, t2 = r
    actual = PCLK1 / (brp * ntq)
    sp = (1 + t1) / ntq * 100
    rel = abs(actual - br) / br * 100
    flag = "" if rel < 0.1 else "  <-- ZU UNGENAU"
    if rel >= 0.1:
        fail += 1
    if not (1 <= t1 <= 16 and 1 <= t2 <= 8 and 1 <= brp <= 1024):
        flag += "  <-- REGISTERBEREICH"
        fail += 1
    print(f"S{i:<3}{br:>10}{brp:>6}{t1:>5}{t2:>5}{ntq:>5}"
          f"{actual:>10.1f}{rel:>8.3f}%{sp:>7.1f}%{flag}")

print()
for br in (750_000, 400_000, 33_333, 95_238, 5000, 1_000_001):
    r = timing_for_bitrate(br)
    if r is None:
        print(f"B{br}: keine Loesung (erwartet fuer krumme/ungueltige Raten)")
        continue
    err, ntq, brp, t1, t2 = r
    actual = PCLK1 / (brp * ntq)
    print(f"B{br}: BRP={brp} BS1={t1} BS2={t2} ntq={ntq} -> {actual:.1f} "
          f"({abs(actual-br)/br*100:.3f}%), SP={(1+t1)/ntq*100:.1f}%")

print()
print("FEHLER" if fail else "alle Standardbitraten exakt und im Registerbereich")
