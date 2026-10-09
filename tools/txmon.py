#!/usr/bin/env python3
"""
txmon.py: klausās TX seriālo portu, pats atkodē katru kadru no bitiem
un nosaka, kādus datus TX izstaroja. Ja dots arī RX ports, salīdzina
nosūtīto ar saņemto un skaita statistiku.

  python3 tools/txmon.py --tx /dev/serial/by-id/...TX...
  python3 tools/txmon.py --tx ... --rx ... --auto 20 --csv data/tests.csv

Bez --auto: ieraksti vārdu terminālī un Enter, tas aiziet uz TX.
Komandas (!b, !t, !r 3 ...) arī tiek pārsūtītas uz TX.
"""
import argparse
import csv
import queue
import random
import string
import sys
import threading
import time

import serial

EOT = 0x04


def reader(name, port, q, stop):
    while not stop.is_set():
        try:
            raw = port.readline()
        except serial.SerialException as e:
            q.put((time.time(), name, f"!ERR {e}"))
            return
        if raw:
            q.put((time.time(), name, raw.decode("ascii", "replace").strip()))


def stdin_reader(q):
    for line in sys.stdin:
        q.put((time.time(), "IN", line.rstrip("\r\n")))


def decode_bits(bits):
    """10 biti: starts, 8 dati (MSB pirmais), pāra paritāte. -> (baits, kļūda)"""
    if len(bits) != 10 or set(bits) - {"0", "1"}:
        return None, "formats"
    if bits[0] != "1":
        return None, "nav starta"
    b = int(bits[1:9], 2)
    if bin(b).count("1") % 2 != int(bits[9]):
        return None, "paritate"
    return b, None


def show(c):
    return c if 32 <= ord(c) < 127 else f"\\x{ord(c):02x}"


class Word:
    def __init__(self, t, text):
        self.t = t
        self.sent = text          # ko TX teica, ka sūta
        self.data = []            # ko mēs atkodējām no bitiem
        self.eot = False
        self.sum_frame = None
        self.tx_sum = None
        self.errors = []
        self.done_t = None
        self.rx = ""
        self.rx_status = None

    @property
    def decoded(self):
        return "".join(chr(d) for d in self.data)

    @property
    def bits_ok(self):
        return (not self.errors and self.decoded == self.sent
                and self.sum_frame == (sum(self.data) & 0x7F) == self.tx_sum)


def open_port(path, name):
    try:
        return serial.Serial(path, 9600, timeout=0.2)
    except serial.SerialException as e:
        import glob
        print(f"{name} portu nevar atvērt: {path}")
        print(f"  {e.strerror if hasattr(e, 'strerror') and e.strerror else e}")
        found = sorted(glob.glob("/dev/serial/by-id/*"))
        print("  Šobrīd pieslēgts:" if found else "  Neviena plate nav pieslēgta.")
        for p in found:
            print("   ", p)
        sys.exit(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tx", required=True, help="TX ports")
    ap.add_argument("--rx", help="RX ports (nav obligāts)")
    ap.add_argument("--auto", type=int, default=0, help="nosūtīt N nejaušus vārdus")
    ap.add_argument("--len", type=int, default=4, help="vārda garums --auto režīmā")
    ap.add_argument("--gap", type=float, default=3.0, help="pauze starp vārdiem, s")
    ap.add_argument("--csv", help="rezultātu CSV")
    ap.add_argument("--log", help="visas rindas ar PC laiku")
    ap.add_argument("--repeat", type=int, default=3, help="TX atkārtojumi (!r)")
    ap.add_argument("-q", "--quiet", action="store_true", help="nerādīt katru kadru")
    a = ap.parse_args()

    q, stop = queue.Queue(), threading.Event()
    tx = open_port(a.tx, "TX")
    threading.Thread(target=reader, args=("TX", tx, q, stop), daemon=True).start()
    rx = None
    if a.rx:
        rx = open_port(a.rx, "RX")
        threading.Thread(target=reader, args=("RX", rx, q, stop), daemon=True).start()
    if not a.auto:
        threading.Thread(target=stdin_reader, args=(q,), daemon=True).start()

    log = open(a.log, "a") if a.log else None
    words, cur = [], None
    ready = False
    t_open = time.time()
    auto_left, next_send, busy = a.auto, 0.0, False
    rx_buf = ""
    levels = []            # RX: starta līmenis, fons, slieksnis katram kadram
    groups = []            # katrs TX kadrs: laiks, sūtītais baits, RX kopijas
    NAMES = ["seq", "D6", "D5", "D4", "D3", "D2", "D1", "D0", "par"]

    print("Gaidu TX (atverot portu, Uno pārstartējas)...")
    try:
        while True:
            now = time.time()

            # automātiskā sūtīšana
            if a.auto and (ready or now - t_open > 3) and not busy and now >= next_send:
                if auto_left == 0:
                    if not words or words[-1].rx_status or not rx or now - words[-1].done_t > 15:
                        break
                else:
                    w = "".join(random.choice(string.ascii_uppercase) for _ in range(a.len))
                    tx.write((w + "\n").encode())
                    auto_left -= 1
                    busy = True

            try:
                t, src, line = q.get(timeout=0.1)
            except queue.Empty:
                continue
            if log:
                log.write(f"{t:.3f}\t{src}\t{line}\n")
                log.flush()

            if src == "IN":
                tx.write((line + "\n").encode())
                continue

            if src == "TX":
                if line.startswith("@R"):
                    ready = True
                    _, bit, freq, rep = line.split(",")
                    print(f"TX gatavs: BIT={bit} ms, FREQ={freq} Hz, atkartojumi={rep}")
                elif line.startswith("@W"):
                    _, _, text = line.split(",", 2)
                    cur = Word(t, text)
                    words.append(cur)
                    busy = True
                    print(f"\n>>> TX sāk: '{text}'")
                elif line.startswith("@F"):
                    _, _, i, total, hx, seq, bits = line.split(",")
                    b, err = decode_bits(bits)
                    if b is not None:
                        groups.append({"t": t, "i": i, "b": b, "copies": []})
                    msg = f"  kadrs {i}/{total}  biti {bits[0]} {bits[1]} {bits[2:9]} {bits[9]}  "
                    if err:
                        msg += f"KLUDA: {err}"
                        if cur: cur.errors.append(f"{i}:{err}")
                    else:
                        d, s = b & 0x7F, b >> 7
                        if d != int(hx, 16) or s != int(seq):
                            msg += "nesakrit ar TX teikto"
                            if cur: cur.errors.append(f"{i}:nesakrit")
                        what = "EOT" if (d == EOT and cur and not cur.eot) else (
                            f"summa {d}" if cur and cur.eot else f"'{show(chr(d))}'")
                        msg += f"-> seq {s}, dati 0x{d:02X} {what}"
                        if cur:
                            if cur.eot:
                                cur.sum_frame = d
                            elif d == EOT:
                                cur.eot = True
                            else:
                                cur.data.append(d)
                    if not a.quiet:
                        print(msg)
                elif line.startswith("@S"):
                    _, _, s, ms = line.split(",")
                    busy, next_send = False, t + a.gap
                    if cur:
                        cur.tx_sum, cur.done_t = int(s), t
                        print(f"<<< TX beidza {int(ms)/1000:.1f} s. No bitiem atkodēts: "
                              f"'{cur.decoded}', summa {cur.sum_frame} "
                              f"({'pareiza' if cur.bits_ok else 'NEPAREIZA ' + str(cur.errors)})")
                        cur = None
                elif line.startswith("!ERR"):
                    print("TX ports pazuda:", line)
                    break
                else:
                    print("  TX:", line)

            if src == "RX" and line.startswith("@B"):
                p = line.split(",")
                hx, par = p[1], p[2]
                lvl = f"  [starts {p[3]}, fons {p[4]}, TH {p[5]}]" if len(p) >= 6 else ""
                levels.append(tuple(int(x) for x in p[3:6]) if len(p) >= 6 else None)
                got = int(hx, 16)
                g = next((g for g in reversed(groups) if t - g["t"] > 0.4), None)
                if g is None or t - g["t"] > a.repeat * 0.6 + 1.0:
                    print(f"  RX kadrs 0x{got:02X} bez TX kadra (viltus starts?)")
                    continue
                g["copies"].append((got, int(par)))
                diff = got ^ g["b"]
                if not diff:
                    verdict = "sakrīt"
                else:
                    flips = [f"{NAMES[k]} {'1->0' if (g['b'] >> (7-k)) & 1 else '0->1'}"
                             for k in range(8) if (diff >> (7-k)) & 1]
                    verdict = "APGRIEZTI: " + ", ".join(flips)
                    verdict += "  (paritāte pamanīja)" if not int(par) else "  (PARITĀTE NEPAMANĪJA)"
                if not a.quiet or diff:
                    print(f"    RX kopija {len(g['copies'])}: 0x{got:02X} pret 0x{g['b']:02X}  {verdict}{lvl}")
                continue

            if src == "RX":
                target = next((w for w in reversed(words) if w.rx_status is None), None)
                if line.startswith("RX ") and len(line) >= 4:
                    rx_buf += line[3]
                elif line in ("OK", "KLUDA", "NEPILNS"):
                    if target:
                        target.rx, target.rx_status = rx_buf, line
                        same = rx_buf == target.sent
                        print(f"=== RX: '{rx_buf}' {line}  "
                              f"{'SAKRIT ar nosutito' if same else 'NESAKRIT ar ' + repr(target.sent)}")
                    else:
                        print(f"=== RX: '{rx_buf}' {line} (nav atbilstoša TX vārda)")
                    rx_buf = ""
                elif line.startswith("!ERR"):
                    print("RX ports pazuda:", line)
                    rx = None
                elif not line.lstrip("-").isdigit():
                    print("  RX:", line)

    except KeyboardInterrupt:
        pass
    finally:
        stop.set()

    # kopsavilkums
    done = [w for w in words if w.done_t]
    if not done:
        return
    print("\n--- kopsavilkums ---")
    print(f"nosūtīti: {len(done)}, biti pareizi: {sum(w.bits_ok for w in done)}")
    if a.rx:
        ok = sum(w.rx_status == "OK" and w.rx == w.sent for w in done)
        bad = sum(w.rx_status == "KLUDA" for w in done)
        part = sum(w.rx_status == "NEPILNS" for w in done)
        lost = sum(w.rx_status is None for w in done)
        print(f"RX: OK un sakrīt {ok}, KLUDA {bad}, NEPILNS {part}, nekas {lost}  "
              f"-> {100*ok/len(done):.0f}%")
    if a.rx and groups:
        exp = len(groups) * a.repeat
        cop = [(g["b"], c) for g in groups for c in g["copies"]]
        exact = sum(b == got for b, (got, p) in cop)
        caught = sum(b != got and not p for b, (got, p) in cop)
        sneaky = sum(b != got and p for b, (got, p) in cop)
        pos = [0] * 8
        up = down = 0
        for b, (got, p) in cop:
            d = b ^ got
            for k in range(8):
                if (d >> (7 - k)) & 1:
                    pos[k] += 1
                    if (b >> (7 - k)) & 1:
                        down += 1
                    else:
                        up += 1
        none = sum(not g["copies"] for g in groups)
        print(f"\nkadri: TX {len(groups)} x {a.repeat} = {exp} kopijas, RX saņēma {len(cop)} "
              f"({100*len(cop)/exp:.0f}%), no tām precīzas {exact}")
        print(f"bojātas: paritāte pamanīja {caught}, paritāte NEpamanīja {sneaky}; "
              f"kadri bez nevienas kopijas: {none}")
        if up or down:
            print("apgriezti biti: " + "  ".join(f"{n}:{c}" for n, c in zip(NAMES, pos)))
            ones = sum(bin(b).count("1") for b, _ in cop)
            zeros = 8 * len(cop) - ones
            print(f"1->0 {down} reizes, 0->1 {up} reizes")
            print(f"=> sūtīts 1, RX redz 0: {100*down/max(1,ones):.0f}%   "
                  f"sūtīts 0, RX redz 1: {100*up/max(1,zeros):.0f}%")
            if down > 2 * up:
                print("=> RX palaiž garām vieniniekus: signāls vājš vai slieksnis par augstu (MY_TH uz leju)")
            elif up > 2 * down:
                print("=> RX redz vieniniekus, kur to nav: troksnis vai slieksnis par zemu (MY_TH uz augšu)")
            if sum(pos[5:]) > 2 * max(1, sum(pos[:3])):
                print("=> kļūdas kadra beigās: starts pamanīts par vēlu (sk. linkdiag.py)")
        lv = [x for x in levels if x]
        if lv:
            md = lambda k: sorted(x[k] for x in lv)[len(lv) // 2]
            print(f"RX līmeņi (mediāna): starta bits {md(0)}, fons {md(1)}, slieksnis {md(2)}"
                  f"  -> signāls/fons {md(0) / max(1, md(1)):.1f}x")
        if len(cop) < 0.8 * exp:
            print("=> daudz kopiju pazūd pavisam: starta bits netiek pamanīts (MY_TH par augstu vai vājš signāls)")

    if a.csv:
        new = False
        try:
            open(a.csv).close()
        except FileNotFoundError:
            new = True
        with open(a.csv, "a", newline="") as f:
            wr = csv.writer(f)
            if new:
                wr.writerow(["pc_laiks", "nosutits", "atkodets_no_bitiem", "summa",
                             "biti_ok", "rx", "rx_statuss", "sakrit", "kludas"])
            for w in done:
                wr.writerow([f"{w.t:.3f}", w.sent, w.decoded, w.tx_sum, int(w.bits_ok),
                             w.rx, w.rx_status or "", int(w.rx == w.sent),
                             ";".join(w.errors)])
        print("saglabāts:", a.csv)


if __name__ == "__main__":
    main()