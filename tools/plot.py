import sys, matplotlib.pyplot as plt
t, v = [], []
for line in open(sys.argv[1]):
    p = line.strip().split(',')
    if len(p) == 3 and p[1].isdigit() and p[2].isdigit():
        t.append(int(p[1]) / 1000); v.append(int(p[2]))
plt.plot(t, v); plt.xlabel('s'); plt.ylabel('līmenis'); plt.show()