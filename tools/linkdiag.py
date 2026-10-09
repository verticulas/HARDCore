#!/usr/bin/env python3
"""
linkdiag.py: kur pazūd biti un kādi iestatījumi tos atgūst.

RX jābūt MODE=2 (diagnostika, 115200 bodi). Katram kadram RX atsūta
50 vērtības: lielāko ADC lēcienu katrā 10 ms logā kopš starta bita.
Skripts zina, kas TIKA sūtīts, un salīdzina.

Ko sūtīja, var zināt divos veidos:
  --expect 55     TX ir bākas režīmā ar zināmu baitu (!b vai !b A3)
  --tx PORTS      TX pie tā paša datora, ņem bitus no @F rindām

  python3 tools/linkdiag.py --rx $RX --expect 55 --n 100 --save data/diag.jsonl
  python3 tools/linkdiag.py --rx $RX --tx $TX --save data/diag.jsonl
  python3 tools/linkdiag.py --load data/diag.jsonl          # analīze bez platēm
"""
import argparse
import bisect
import json
import queue
import sys
import threading
import time

BIT = 50        # ms, jāsakrīt ar saite.h
SLOT = 10       # ms, jāsakrīt ar reciever.cpp
NSLOT = 50
FW_O, FW_W = 10, 30   # saite.h: BIT*(i+1)+10, measure(30)


def parity(b):
    return bin(b).count("1") % 2


def bits_of(byte):
    """9 biti pēc starta: 8 dati + paritāte, kā raida saite::sendByte"""
    return format(byte, "08b") + str(parity(byte))


def bitval(v, j, o, w):
    """bita j (0..7 dati, 8 paritāte) vērtība ar nobīdi o un logu w"""
    s0 = (BIT * (j + 1) + o) // SLOT
    return max(v[s0:s0 + w // SLOT])


def decode(v, o, w, th):
    return "".join("1" if bitval(v, j, o, w) > th else "0" for j in range(9))


def best_threshold(ones, zeros):
    """slieksnis ar vismazāk kļūdām; bits = 1, ja vērtība > TH"""
    ones, zeros = sorted(ones), sorted(zeros)
    cands = sorted(set(ones) | set(zeros) | {0})
    best = None
    for th in cands:
        err = bisect.bisect_right(ones, th) + (len(zeros) - bisect.bisect_right(zeros, th))
        if best is None or err < best[0]:
            best = (err, th)
    err, th = best
    # vidus starp th un nākamo vērtību, lai būtu rezerve abos virzienos
    nxt = [x for x in cands if x > th]
    if nxt:
        th = (th + nxt[0]) // 2
    return th, err


def hist(ones, zeros, width=40):
    if not ones and not zeros:
        return
    hi = max(ones + zeros + [1])
    bins = 16
    step = max(1, (hi + bins) // bins)
    c1, c0 = [0] * bins, [0] * bins
    for x in ones:
        c1[min(bins - 1, x // step)] += 1
    for x in zeros:
        c0[min(bins - 1, x // step)] += 1
    m = max(c1 + c0 + [1])
    print(f"  {'vertiba':>9}  {'sutits 0 (.)':<{width}}  sutits 1 (#)")
    for i in range(bins):
        a = "." * round(width * c0[i] / m)
        b = "#" * round(width * c1[i] / m)
        print(f"  {i*step:4}-{(i+1)*step-1:<4}  {a:<{width}}  {b}")


# ---------------------------------------------------------------- savākšana

def collect(a):
    import serial
    q, stop = queue.Queue(), threading.Event()

    def reader(name, port):
        while not stop.is_set():
            try:
                raw = port.readline()
            except serial.SerialException:
                return
            if raw:
                q.put((time.time(), name, raw.decode("ascii", "replace").strip()))

    rx = serial.Serial(a.rx, 115200, timeout=0.2)
    threading.Thread(target=reader, args=("RX", rx), daemon=True).start()
    if a.tx:
        tx = serial.Serial(a.tx, 9600, timeout=0.2)
        threading.Thread(target=reader, args=("TX", tx), daemon=True).start()
    if a.th is not None:
        time.sleep(2.5)
        rx.write(f"t{a.th}\n".encode())

    frames, txf = [], []
    out = open(a.save, "a") if a.save else None
    print("Vācu kadrus. Ctrl+C, lai beigtu un analizētu.")
    try:
        while not a.n or len(frames) < a.n:
            try:
                t, src, line = q.get(timeout=0.5)
            except queue.Empty:
                continue
            if src == "TX" and line.startswith("@F"):
                p = line.split(",")
                txf.append((t, int(p[2]), p[6][1:]))      # pc laiks, kadra nr, 9 biti
            elif src == "TX" and line.startswith("@W"):
                print(f"\nTX sūta: {line.split(',', 2)[2]}")
            elif src == "RX" and line.startswith("@R"):
                print("RX diagnostikā:", line)
            elif src == "RX" and line.startswith("@C"):
                print("RX:", line)
            elif src == "RX" and line.startswith("@D"):
                _, _, w, vals = line.split(",", 3)
                v = [int(x) for x in vals.split()]
                if len(v) != NSLOT:
                    continue
                exp, grp = None, None
                if a.expect is not None:
                    exp = bits_of(a.expect)
                elif txf:
                    # RX rinda pienāk ~0,5 s pēc kadra sākuma; atkārtojumi ik 0,6 s
                    for tf, nr, bits in reversed(txf):
                        if 0.3 < t - tf < a.repeat * 0.6 + 0.8:
                            exp, grp = bits, f"{tf:.3f}"
                            break
                fr = {"t": t, "w": int(w), "v": v, "exp": exp, "grp": grp}
                frames.append(fr)
                if out:
                    out.write(json.dumps(fr) + "\n")
                    out.flush()
                live_line(len(frames), fr, a)
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
    return frames


def live_line(n, fr, a):
    v, exp = fr["v"], fr["exp"]
    th = a.th if a.th is not None else 33
    got = decode(v, FW_O, FW_W, th)
    vals = " ".join(f"{bitval(v, j, FW_O, FW_W):3}" for j in range(9))
    start = max(v[0:3])
    if exp is None:
        tag = "?"
    elif got == exp:
        tag = "ok"
    else:
        bad = "".join("^" if g != e else " " for g, e in zip(got, exp))
        tag = f"KLUDA  gaidits {exp}\n{'':>57}{bad}"
    print(f"#{n:<4} starts {start:3} | {vals} | {got} {tag}")


# ---------------------------------------------------------------- analīze

def analyse(frames, a):
    known = [f for f in frames if f["exp"]]
    print(f"\n=== {len(frames)} kadri, no tiem ar zināmu saturu {len(known)} ===")
    if not known:
        print("Nav ar ko salīdzināt: vajag --expect HH vai --tx PORTS.")
        return
    th_now = a.th if a.th is not None else 33

    # 1. kā ir tagad
    errs_pos = [0] * 9
    frame_bad = 0
    trans = {"0->1": [0, 0], "1->0": [0, 0], "vienads": [0, 0]}
    for f in known:
        got = decode(f["v"], FW_O, FW_W, th_now)
        exp = f["exp"]
        if got != exp:
            frame_bad += 1
        prev = "1"                                          # starta bits
        for j, (g, e) in enumerate(zip(got, exp)):
            k = "vienads" if e == prev else f"{prev}->{e}"
            trans[k][1] += 1
            if g != e:
                errs_pos[j] += 1
                trans[k][0] += 1
            prev = e
    nb = len(known) * 9
    print(f"\n1) Ar pašreizējiem iestatījumiem (nobīde {FW_O} ms, logs {FW_W} ms, TH {th_now}):")
    print(f"   bitu kļūdas {sum(errs_pos)}/{nb} = {100*sum(errs_pos)/nb:.1f}%, "
          f"bojāti kadri {frame_bad}/{len(known)} = {100*frame_bad/len(known):.0f}%")
    names = ["D7/seq", "D6", "D5", "D4", "D3", "D2", "D1", "D0", "par"]
    print("   kļūdas pa bitiem:  " + "  ".join(f"{n}:{e}" for n, e in zip(names, errs_pos)))
    print("   pēc pārejas:       " + "  ".join(
        f"{k} {e}/{t}" for k, (e, t) in trans.items() if t))

    # 2. vērtību sadalījums
    ones, zeros = [], []
    for f in known:
        for j, e in enumerate(f["exp"]):
            (ones if e == "1" else zeros).append(bitval(f["v"], j, FW_O, FW_W))
    print(f"\n2) Ko RX redz (pašreizējais logs). Sūtīts 1: min {min(ones) if ones else '-'}, "
          f"mediāna {sorted(ones)[len(ones)//2] if ones else '-'}. "
          f"Sūtīts 0: max {max(zeros) if zeros else '-'}, "
          f"mediāna {sorted(zeros)[len(zeros)//2] if zeros else '-'}.")
    hist(ones, zeros)
    starts = [max(f["v"][0:3]) for f in known]
    print(f"   starta bits: min {min(starts)}, mediāna {sorted(starts)[len(starts)//2]}")

    # 3. labākie iestatījumi
    print("\n3) Visi nobīdes, loga un sliekšņa varianti:")
    res = []
    for o in range(0, BIT, SLOT):
        for w in range(SLOT, BIT - o + 1, SLOT):
            on, ze = [], []
            for f in known:
                for j, e in enumerate(f["exp"]):
                    (on if e == "1" else ze).append(bitval(f["v"], j, o, w))
            th, err = best_threshold(on, ze)
            margin = (min(on) - max(ze)) if on and ze else 0
            res.append((err, -margin, o, w, th))
    res.sort()
    print(f"   {'nobide':>6} {'logs':>5} {'TH':>4} {'kludas':>7} {'rezerve':>8}")
    for err, nm, o, w, th in res[:6]:
        print(f"   {o:>6} {w:>5} {th:>4} {err:>7} {-nm:>8}")
    err, nm, o, w, th = res[0]
    margin = -nm

    # 4. balsošana starp atkārtojumiem
    groups = {}
    for f in known:
        if f["grp"]:
            groups.setdefault(f["grp"], []).append(f)
    if groups:
        first_ok = vote_ok = 0
        for g in groups.values():
            exp = g[0]["exp"]
            dec = [decode(f["v"], o, w, th) for f in g]
            good = [d for d in dec if parity(int(d[:8], 2)) == int(d[8])]
            first_ok += bool(good) and good[0] == exp
            vote = "".join("1" if sum(d[j] == "1" for d in dec) * 2 > len(dec) else "0"
                           for j in range(9))
            vote_ok += vote == exp
        n = len(groups)
        print(f"\n4) {n} kadru grupas (atkārtojumi). Ar labākajiem iestatījumiem:")
        print(f"   kā tagad (pirmā kopija ar pareizu paritāti): {first_ok}/{n}")
        print(f"   balsojot pa bitiem starp kopijām:            {vote_ok}/{n}")

    # 5. secinājumi
    print("\n5) Secinājumi:")
    if margin > 0:
        print(f"   Kanāls ir tīrs: ar nobīdi {o} ms un logu {w} ms sūtītie 1 un 0 nepārklājas,")
        print(f"   rezerve {margin}. Ieliec MY_TH = {th}.")
    else:
        print(f"   Sūtītie 1 un 0 pārklājas: labākajā gadījumā {err}/{nb} bitu kļūdas "
              f"({100*err/nb:.1f}%).")
        print("   Fizikā: tuvini elektrodus, pagarini paralēlo daļu, rokas nost no datora.")
        print("   Programmā: lielāks BIT (piem. 80) abās platēm, vairāk atkārtojumu (!r 5).")
    ch = []
    if o != FW_O:
        ch.append(f"'+ {FW_O}' (2 vietās) aizstāj ar '+ {o}'")
    if w != FW_W:
        ch.append(f"measure({FW_W}) (2 vietās) aizstāj ar measure({w})")
    if ch:
        print("   saite.h receiveByte(): " + ", ".join(ch) + f", MY_TH = {th}.")
    late = sum(errs_pos[5:]) > 2 * max(1, sum(errs_pos[:4]))
    if late and sum(errs_pos):
        print("   Kļūdas krājas kadra beigās: starts tiek pamanīts par vēlu vai pulksteņi aiziet.")
    if trans["1->0"][0] > 2 * max(1, trans["0->1"][0]):
        print("   Kļūdas pēc 1->0: 1 'aste' ielien nākamajā bitā. Lielāka nobīde vai mazāks logs.")
    if trans["0->1"][0] > 2 * max(1, trans["1->0"][0]):
        print("   Kļūdas pēc 0->1: signāls kāpj lēni. Lielāks logs.")
    weak = sum(s <= th for s in starts)
    if weak:
        print(f"   {weak} kadros starta bits ir zem sliekšņa: tie tika noķerti par vēlu.")


def main():
    global BIT
    ap = argparse.ArgumentParser()
    ap.add_argument("--rx", help="RX ports (MODE=2)")
    ap.add_argument("--tx", help="TX ports, ja TX sūta vārdus")
    ap.add_argument("--expect", type=lambda s: int(s, 16), help="bākas baits hex, piem. 55")
    ap.add_argument("--th", type=int, help="RX slieksnis (pašreizējais MY_TH)")
    ap.add_argument("--n", type=int, default=0, help="cik kadrus savākt")
    ap.add_argument("--repeat", type=int, default=3)
    ap.add_argument("--bit", type=int, default=BIT)
    ap.add_argument("--save", help="saglabāt kadrus .jsonl")
    ap.add_argument("--load", help="analizēt saglabātos kadrus")
    a = ap.parse_args()
    BIT = a.bit

    if a.load:
        frames = [json.loads(l) for l in open(a.load) if l.strip()]
        if a.expect is not None:
            for f in frames:
                f["exp"] = bits_of(a.expect)
    elif a.rx:
        frames = collect(a)
    else:
        ap.error("vajag --rx vai --load")
    analyse(frames, a)


if __name__ == "__main__":
    main()