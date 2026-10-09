#!/usr/bin/env python3
"""
viz.py: parāda, ko RX dara ar neapstrādātajiem datiem, soli pa solim.

  1. ADC paraugi starta bitā: smailes katrā tona frontē, 24 paraugi periodā
  2. reizināšana ar sin/cos un I, Q summēšanās vienā 5 ms blokā
  3. I/Q plakne: katrs bloks ir punkts; tonis veido gredzenu, klusums ir centrā
  4. amplitūda pa visu kadru, bitu logi, sliekšņi un lēmumi
  5. (simulācijā) vecais "lielākā lēciena" mērījums pret jauno

Režīmi:
  python3 tools/viz.py --sim                          # bez platēm, fizikālais modelis
  python3 tools/viz.py --sim --byte 4B --dist 2       # cits baits, vājāks signāls
  python3 tools/viz.py --rx $RX --tx $TX --byte 4B    # īsti dati (RX MODE=3)
  python3 tools/viz.py --rx $RX --expect 55           # TX bāka jau ieslēgta
  python3 tools/viz.py --load data/raw.jsonl          # saglabātie kadri
  ... --save attels.png                               # saglabāt attēlu

Vajag: python3 -m pip install numpy matplotlib pyserial
"""
import argparse
import json
import sys
import time

import numpy as np
import matplotlib.pyplot as plt

F_CPU = 16_000_000
FREQ_SET = 3170
F_TONE = F_CPU / 64 / (F_CPU // 64 // FREQ_SET)     # ko tone() reāli dod: 3205,13 Hz
FS = F_CPU / 16 / 13                                 # ADC brīvajā režīmā: 76923 Hz
BIT = 50                                             # ms
RAW_N, IQ_N, BLOCK = 480, 98, 384                    # kā reciever.cpp MODE 3
S24 = np.round(63 * np.sin(2 * np.pi * np.arange(24) / 24)).astype(int)
C24 = np.roll(S24, -6)                               # cos = sin + 90°
T_RAW = RAW_N / FS * 1000                            # ms
T_BLK = BLOCK / FS * 1000                            # ms


def parity(b):
    return bin(b).count("1") % 2


def frame_bits(b):
    return [1] + [int(c) for c in format(b, "08b")] + [parity(b)]


def mag(i, q):
    """kā firmware: max + 3/8 min ≈ sqrt(I²+Q²), tad mērogs measure() vienībās"""
    ai, aq = np.abs(i), np.abs(q)
    m = np.maximum(ai, aq) + np.minimum(ai, aq) * 3 / 8
    return m * 512 / 1512


# ---------------------------------------------------------------- simulācija

def simulate(byte, dist, noise, hum, seed=1):
    """
    Fizikālais modelis: TX taisnstūris caur C_m uz RX mezglu ar C_in || R.
    Rezultāts: tie paši dati, ko RX MODE 3 atsūtītu, plus pilnais ADC signāls
    vecā mērījuma salīdzināšanai.
    """
    rng = np.random.default_rng(seed)
    dt = 1e-6
    c_in, r = 15e-12, 2e6
    c_m = 0.5e-12 * np.log(65 / 0.25) / np.log(max(dist, 0.07) * 10 / 0.25)  # ~ 1/ln(d/r)
    k = c_m / (c_m + c_in)
    tau = r * (c_m + c_in)
    bits = frame_bits(byte)
    pre = 0.020                                       # 20 ms klusuma pirms kadra
    t = np.arange(0, pre + 0.56, dt)
    vtx = np.zeros_like(t)
    for i, bt in enumerate(bits):                     # tone() sāk no jauna katrā bitā
        if bt:
            a = pre + i * BIT / 1000
            m = (t >= a) & (t < a + BIT / 1000)
            vtx[m] = 5.0 * ((((t[m] - a) * F_TONE) % 1) < 0.5)
    dv = np.diff(vtx, prepend=0)
    alpha = np.exp(-dt / tau)
    # RC augstfrekvenču filtrs (C_m pret R): starp frontēm mezgls eksponenciāli dziest
    idx = np.flatnonzero(dv)
    node_vals = np.zeros_like(t)
    last_t, last_v = 0, 0.0
    for n in idx:
        seg = slice(last_t, n)
        node_vals[seg] = last_v * alpha ** np.arange(n - last_t)
        last_v = last_v * alpha ** (n - last_t) + k * dv[n]
        last_t = n
    node_vals[last_t:] = last_v * alpha ** np.arange(len(t) - last_t)
    node = node_vals
    node += hum * np.sin(2 * np.pi * 50 * t + 0.7)                     # 50 Hz
    node += rng.normal(0, noise * 4.88e-3, len(t))                     # baltais troksnis
    spikes = rng.random(len(t)) < 2e-5                                 # impulsu troksnis
    node[spikes] += rng.uniform(0.05, 0.25, spikes.sum())
    ts = np.arange(0, t[-1], 1 / FS)                                   # ADC paraugi
    adc = np.clip(np.round(np.interp(ts, t, node) / 4.88e-3), 0, 1023).astype(int)
    # RX pamana startu pirmajā 5 ms blokā, kur tonis jau ir
    det = int((pre + 0.004) * FS)
    raw = np.minimum(adc[det:det + RAW_N], 255)
    rest = np.minimum(adc[det + RAW_N:det + RAW_N + IQ_N * BLOCK], 511).reshape(IQ_N, BLOCK)
    ph = np.arange(BLOCK) % 24
    I = (rest * S24[ph]).sum(1) // 512
    Q = (rest * C24[ph]).sum(1) // 512
    quiet = np.minimum(adc[:(int(pre * FS) // BLOCK) * BLOCK], 511).reshape(-1, BLOCK)
    floor = float(np.mean(mag((quiet * S24[ph]).sum(1) // 512, (quiet * C24[ph]).sum(1) // 512)))
    info = dict(c_m=c_m, k=k, tau=tau, dist=dist)
    return dict(raw=raw.tolist(), I=I.tolist(), Q=Q.tolist(), v=None, floor=floor,
                sth=None, exp=byte, adc=adc, det=det, info=info, lead=4)


def old_metric(adc, det, ms=30):
    """vecais measure(): lielākā starpība starp blakus paraugiem logā"""
    out = []
    n = int(ms / 1000 * FS)
    step = int(T_BLK / 1000 * FS)
    for s in range(det + RAW_N, det + RAW_N + IQ_N * BLOCK, step):
        w = adc[s:s + n]
        out.append(np.max(np.diff(w)) if len(w) > 1 else 0)
    return np.array(out)


# ---------------------------------------------------------------- īsti dati

def capture(a):
    import serial
    rx = serial.Serial(a.rx, 115200, timeout=0.3)
    if a.tx:
        tx = serial.Serial(a.tx, 9600, timeout=0.3)
        time.sleep(2.5)                               # TX pārstartējas
        tx.write(f"!b {a.byte:02X}\n".encode())
        print(f"TX: bāka ar 0x{a.byte:02X}")
    print("Gaidu RX kadrus (RX jābūt MODE=3)...")
    frames, cur = [], {}
    out = open(a.out, "a") if a.out else None
    while len(frames) < a.n:
        line = rx.readline().decode("ascii", "replace").strip()
        if line.startswith("@LV,"):
            v, fl, sth = map(int, line[4:].split(","))
            cur = {"v": v, "floor": fl, "sth": sth}
        elif line.startswith("@RAW,"):
            cur["raw"] = [int(x) for x in line[5:].split()]
        elif line.startswith("@IQ,"):
            vals = [int(x) for x in line[4:].split()]
            cur["I"], cur["Q"] = vals[0::2], vals[1::2]
            if len(cur.get("raw", [])) == RAW_N and len(cur["I"]) == IQ_N:
                cur["exp"] = a.expect if a.expect is not None else a.byte if a.tx else None
                frames.append(cur)
                print(f"  kadrs {len(frames)}/{a.n}: starts {cur['v']}, fons {cur['floor']}, "
                      f"starta slieksnis {cur['sth']}")
                if out:
                    out.write(json.dumps(cur) + "\n")
                    out.flush()
            cur = {}
    if a.tx:
        tx.write(b"!b\n")                             # bāku izslēdz
    return frames


# ---------------------------------------------------------------- zīmēšana

def draw(fr, a, title):
    raw = np.array(fr["raw"])
    I, Q = np.array(fr["I"]), np.array(fr["Q"])
    m = mag(I, Q)
    tb = T_RAW + np.arange(IQ_N) * T_BLK                # bloka sākums, ms no detekcijas
    exp = fr.get("exp")

    sim = "adc" in fr
    fig = plt.figure(figsize=(15, 10 if sim else 9))
    gs = fig.add_gridspec(3 if sim else 2, 3, height_ratios=[1, 1.2, 0.9] if sim else [1, 1.2])
    fig.suptitle(title, fontsize=13)

    # 1. ADC paraugi
    ax = fig.add_subplot(gs[0, 0:2])
    tr = np.arange(RAW_N) / FS * 1000
    ax.plot(tr, raw, ".-", ms=3, lw=0.6, color="#1f77b4", label="ADC paraugs")
    per = 1000 / F_TONE
    for kk in range(int(T_RAW / per) + 1):
        ax.axvline(kk * per, color="#bbb", lw=0.5)
    ax.set_xlim(0, min(T_RAW, 8 * per))
    ax.set_xlabel("ms kopš starta pamanīšanas")
    ax.set_ylabel("ADC")
    ax.set_title(f"1. Neapstrādāti paraugi: {FS/1000:.1f} kHz, 24 uz tona periodu "
                 f"({F_TONE:.1f} Hz). Pelēkās līnijas: tona periodi")
    ax.legend(loc="upper right", fontsize=8)

    # 2. reizināšana un I, Q summa vienā blokā
    ax = fig.add_subplot(gs[0, 2])
    blk = np.minimum(raw[:BLOCK], 511)
    ph = np.arange(BLOCK) % 24
    ci, cq = np.cumsum(blk * S24[ph]) / 512, np.cumsum(blk * C24[ph]) / 512
    tt = np.arange(BLOCK) / FS * 1000
    ax.plot(tt, ci, label="I summa (×sin)")
    ax.plot(tt, cq, label="Q summa (×cos)")
    ax.plot(tt, mag(ci, cq) * 1512 / 512, "k--", lw=1, label="√(I²+Q²)")
    ax.set_xlabel("ms bloka iekšā")
    ax.set_title("2. Summēšana 5 ms blokā:\ntonis aug lineāri, troksnis ne")
    ax.legend(fontsize=8)

    # 3. I/Q plakne
    ax = fig.add_subplot(gs[1, 0])
    lead = fr.get("lead", 5)                          # cik ms pēc īstā starta RX to pamana
    bit_of_block = np.floor((tb + T_BLK / 2 + lead) / BIT).astype(int)
    if exp is not None:
        fb = frame_bits(exp)
        colors = ["#d62728" if 0 <= b < 10 and fb[b] else "#7f7f7f" for b in bit_of_block]
    else:
        colors = plt.cm.viridis(np.linspace(0, 1, IQ_N))
    ax.scatter(I, Q, c=colors, s=18)
    hyp = np.hypot(I, Q)
    if exp is not None and any(c == "#d62728" for c in colors):
        r = np.median(hyp[[c == "#d62728" for c in colors]])
    else:
        r = np.median(hyp[m > np.median(m)]) if np.any(m > 0) else 1
    ax.add_patch(plt.Circle((0, 0), r, fill=False, ls="--", color="#d62728", lw=0.8))
    ax.axhline(0, color="#ccc", lw=0.5)
    ax.axvline(0, color="#ccc", lw=0.5)
    ax.set_aspect("equal")
    ax.set_xlabel("I")
    ax.set_ylabel("Q")
    ax.set_title("3. Katrs 5 ms bloks ir punkts.\nSarkans: tonis (gredzens), pelēks: klusums")

    # 4. amplitūda pa kadru un lēmumi
    ax = fig.add_subplot(gs[1, 1:])
    ax.step(tb, m, where="post", color="#1f77b4", label="amplitūda (measure)")
    floor = fr.get("floor")
    if floor is None:
        floor = float(np.percentile(m, 20))
    sel = tb < 20                                     # firmware: measure(20) uzreiz pēc starta
    ref = float(np.mean(m[sel])) if np.any(sel) else float(m[0])
    bit_th = max(a.th, (ref + floor) / 2)
    ax.axhline(floor, color="#7f7f7f", ls=":", label=f"fons {floor:.0f}")
    ax.axhline(bit_th, color="#d62728", ls="--", label=f"bitu slieksnis {bit_th:.0f}")
    if fr.get("sth"):
        ax.axhline(fr["sth"], color="#ff7f0e", ls="-.", label=f"starta slieksnis {fr['sth']}")
    names = ["start", "seq", "D6", "D5", "D4", "D3", "D2", "D1", "D0", "par"]
    got = []
    for i in range(9):                                # 8 dati + paritāte
        w0, w1 = BIT * (i + 1) + 10, BIT * (i + 1) + 40
        sel = (tb >= w0 - 0.01) & (tb + T_BLK <= w1 + 2.5)
        val = float(np.mean(m[sel])) if np.any(sel) else 0
        bit = int(val > bit_th)
        got.append(bit)
        ax.axvspan(w0, w1, color="#2ca02c" if bit else "#cccccc", alpha=0.25)
    ymax = max(m.max() * 1.25, bit_th * 1.5, 10)
    ax.set_ylim(0, ymax)
    for i in range(10):
        x = i * BIT - lead
        ax.axvline(x, color="#eee", lw=0.6)
    for i in range(9):
        w0 = BIT * (i + 1) + 10
        txt = f"{names[i+1]}\n→{got[i]}"
        if exp is not None:
            e = frame_bits(exp)[i + 1]
            txt += f"\n({e})"
            col = "#2ca02c" if e == got[i] else "#d62728"
        else:
            col = "k"
        ax.text(w0 + 15, ymax * 0.97, txt, ha="center", va="top", fontsize=8, color=col)
    dec = int("".join(map(str, got[:8])), 2)
    verdict = ""
    if exp is not None:
        verdict = "  SAKRĪT" if dec == exp else f"  sūtīts 0x{exp:02X}"
    ax.set_title(f"4. Amplitūda pa kadru. Zaļie logi = RX lēma 1. "
                 f"Atkodēts 0x{dec:02X}{verdict}   (iekavās: kas sūtīts)")
    ax.set_xlabel("ms kopš starta pamanīšanas")
    ax.legend(loc="lower right", fontsize=8, framealpha=0.9)

    # 5. vecais pret jauno (tikai simulācijā)
    if sim:
        ax = fig.add_subplot(gs[2, :])
        old = old_metric(fr["adc"], fr["det"])
        nfloor_old = np.percentile(old, 20) or 1
        nfloor_new = np.percentile(m, 20) or 1
        ax.plot(tb, old / nfloor_old, color="#ff7f0e", label="vecais: lielākais lēciens 30 ms logā")
        ax.plot(tb, m / nfloor_new, color="#1f77b4", label="jaunais: 24 paraugu I/Q, 5 ms bloki")
        fb = frame_bits(exp)
        for i, bt in enumerate(fb):
            x0 = i * BIT - lead
            ax.axvspan(x0, x0 + BIT, color="#d62728" if bt else "white", alpha=0.08)
        ax.set_yscale("log")
        ax.set_ylabel("× virs sava fona")
        ax.set_xlabel("ms kopš starta pamanīšanas (sarkanīgs fons: sūtīts 1)")
        inf = fr["info"]
        ax.set_title(f"5. Abi mērījumi, katrs dalīts ar savu fonu. Modelis: d={inf['dist']} cm, "
                     f"C_m≈{inf['c_m']*1e12:.2f} pF, dalītājs {inf['k']*100:.1f}%, "
                     f"τ={inf['tau']*1e6:.0f} µs")
        ax.legend(fontsize=8)

    fig.tight_layout()
    return fig


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", action="store_true", help="fizikālā simulācija bez platēm")
    ap.add_argument("--rx", help="RX ports (MODE=3)")
    ap.add_argument("--tx", help="TX ports: ieslēdz bāku ar --byte")
    ap.add_argument("--byte", type=lambda s: int(s, 16), default=0x4B, help="sūtāmais baits hex")
    ap.add_argument("--expect", type=lambda s: int(s, 16), help="bākas baits, ja TX ieslēgts ar roku")
    ap.add_argument("--load", help="saglabātie kadri .jsonl")
    ap.add_argument("--out", help="saglabāt saņemtos kadrus .jsonl")
    ap.add_argument("--n", type=int, default=1, help="cik kadrus savākt")
    ap.add_argument("--th", type=int, default=10, help="MY_TH (minimālais slieksnis)")
    ap.add_argument("--dist", type=float, default=6.5, help="sim: attālums cm")
    ap.add_argument("--noise", type=float, default=1.5, help="sim: baltais troksnis, ADC vienības")
    ap.add_argument("--hum", type=float, default=0.02, help="sim: 50 Hz amplitūda, V")
    ap.add_argument("--save", help="saglabāt attēlu (png/svg/pdf)")
    a = ap.parse_args()

    if a.sim:
        fr = simulate(a.byte, a.dist, a.noise, a.hum)
        frames = [fr]
        title = f"Simulācija: baits 0x{a.byte:02X}, d = {a.dist} cm"
    elif a.load:
        frames = [json.loads(l) for l in open(a.load) if l.strip()]
        if a.expect is not None:
            for f in frames:
                f["exp"] = a.expect
        title = f"Saglabātie dati: {a.load}"
    elif a.rx:
        frames = capture(a)
        title = "Īsti RX dati"
    else:
        ap.error("vajag --sim, --rx vai --load")

    fr = frames[-1]
    fig = draw(fr, a, title + (f"  (kadrs {len(frames)}/{len(frames)})" if len(frames) > 1 else ""))
    if a.save:
        fig.savefig(a.save, dpi=130)
        print("saglabāts:", a.save)
    else:
        plt.show()


if __name__ == "__main__":
    main()
