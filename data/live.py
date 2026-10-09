import sys, time, collections, serial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM0'
out  = sys.argv[2] if len(sys.argv) > 2 else None
TH   = int(sys.argv[3]) if len(sys.argv) > 3 else None
WIN  = 10.0                                   # sekundes ekrānā

s = serial.Serial(port, 9600, timeout=0)
f = open(out, 'a') if out else None
t, v = collections.deque(), collections.deque()
buf = b''

fig, ax = plt.subplots()
line, = ax.plot([], [], lw=1)
if TH is not None:
    ax.axhline(TH, ls='--', color='red')
ax.set_xlabel('s'); ax.set_ylabel('līmenis')

def update(_):
    global buf
    buf += s.read(s.in_waiting or 1)
    while b'\n' in buf:
        raw, buf = buf.split(b'\n', 1)
        txt = raw.decode(errors='replace').strip()
        if f: f.write(f'{time.time():.3f},{txt}\n')
        p = txt.split(',')
        if len(p) == 2 and p[0].isdigit() and p[1].isdigit():
            tm = int(p[0]) / 1000
            if t and tm < t[-1]:              # Uno pārstartējās
                t.clear(); v.clear()
            t.append(tm); v.append(int(p[1]))
    while t and t[-1] - t[0] > WIN:
        t.popleft(); v.popleft()
    if t:
        line.set_data(t, v)
        ax.set_xlim(t[-1] - WIN, t[-1])
        ax.set_ylim(0, max(50, max(v) * 1.1))
    if f: f.flush()
    return line,

ani = FuncAnimation(fig, update, interval=50, cache_frame_data=False)
plt.show()
if f: f.close()