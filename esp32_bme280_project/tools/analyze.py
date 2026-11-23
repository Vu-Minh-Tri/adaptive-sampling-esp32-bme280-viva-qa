"""Compare fixed-rate and adaptive logs.
  python analyze.py fixed.csv adaptive.csv [--plot out.png]
  python analyze.py --replay fixed.csv [--plot out.png]   # apply the adaptive rule to a fixed-rate log
Log columns: time_s,temp_c,hum_rh,press_hpa,mode,interval_s   (ERROR lines are ignored)."""
import sys
import numpy as np

TMIN, TMAX, DT, TH_T, TH_H = 10, 120, 10, 0.3, 2.0      # keep equal to the firmware

def load(path):
    rows = []
    for line in open(path, encoding="utf-8"):
        p = line.strip().split(",")
        if p and p[0].replace(".", "", 1).isdigit() and "ERROR" not in line:
            rows.append([float(p[0]), float(p[1]), float(p[2])])
    a = np.array(rows)
    return a[:, 0], a[:, 1], a[:, 2]

def replay(t, T, H):
    """Samples that the adaptive rule would take from a fixed-rate log."""
    idx, interval, nxt = [], TMIN, t[0]
    for i in range(len(t)):
        if t[i] < nxt: continue
        if idx:
            j = idx[-1]
            stable = abs(T[i] - T[j]) < TH_T and abs(H[i] - H[j]) < TH_H
            interval = min(interval + DT, TMAX) if stable else TMIN
        idx.append(i); nxt = t[i] + interval
    return np.array(idx)

def errors(tf, vf, ta, va):
    hold = np.array([va[np.searchsorted(ta, x, side="right") - 1] for x in tf])
    e = np.abs(vf - hold)
    return e.mean(), np.sqrt((e ** 2).mean()), e.max(), hold

def main():
    a = sys.argv[1:]
    plot = a[a.index("--plot") + 1] if "--plot" in a else None
    if "--plot" in a: del a[a.index("--plot"):a.index("--plot") + 2]
    if a[0] == "--replay":
        tf, Tf, Hf = load(a[1]); k = replay(tf, Tf, Hf); ta, Ta, Ha = tf[k], Tf[k], Hf[k]
    else:
        tf, Tf, Hf = load(a[0]); ta, Ta, Ha = load(a[1])
    nf, na = len(tf), len(ta)
    print(f"Readings: fixed {nf}, adaptive {na}, reduction R = {100 * (nf - na) / nf:.1f} %")
    for name, vf, va, tol in (("Temperature (C)", Tf, Ta, 0.5), ("Humidity (%RH)", Hf, Ha, 3.0)):
        mae, rmse, mx, hold = errors(tf, vf, ta, va)
        ok = 100 * (np.abs(vf - hold) <= tol).mean()
        print(f"{name}: MAE {mae:.3f}  RMSE {rmse:.3f}  max {mx:.3f}  within +-{tol}: {ok:.1f} %")
    if plot:
        import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
        _, _, _, hold = errors(tf, Tf, ta, Ta)
        fig, ax = plt.subplots(2, 1, figsize=(8, 5.2), sharex=True, gridspec_kw={"height_ratios": [3, 1]})
        ax[0].plot(tf, Tf, ".", color="#9E9E9E", ms=3, label=f"Fixed-rate ({nf} readings)")
        ax[0].step(tf, hold, where="post", color="#C62828", lw=1.2, label="Adaptive, held between samples")
        ax[0].plot(ta, Ta, "o", color="#C62828", ms=3.5, label=f"Adaptive ({na} readings)")
        ax[0].set_ylabel("Temperature (°C)"); ax[0].legend(loc="upper right", fontsize=8); ax[0].grid(alpha=.3)
        ax[1].step(ta[:-1], np.diff(ta), where="post", color="#1F4E9E"); ax[1].set_ylabel("Interval (s)")
        ax[1].set_xlabel("Time (s)"); ax[1].grid(alpha=.3)
        fig.tight_layout(); fig.savefig(plot, dpi=200)

if __name__ == "__main__":
    main()
