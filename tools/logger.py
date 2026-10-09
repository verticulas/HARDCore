import sys, time, serial

port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM1'
out  = sys.argv[2] if len(sys.argv) > 2 else 'data/rx.csv'

s = serial.Serial(port, 9600)
try:
    with open(out, 'a') as f:
        while True:
            line = s.readline().decode(errors='replace').strip()
            f.write(f'{time.time():.3f},{line}\n'); f.flush()
            print(line)
except KeyboardInterrupt:
    print(f'\nSaglabāts: {out}')