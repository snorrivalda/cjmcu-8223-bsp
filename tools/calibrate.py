#!/usr/bin/env python3
"""Fit the tilt -> specific gravity polynomial for a brew pill.

Measure the pill's tilt (from brewpill_receiver.py) in a few liquids of
known gravity, e.g. plain water (1.000) and sugar solutions checked with a
regular hydrometer or refractometer.  Put them in a CSV file with two
columns, tilt in degrees and SG:

    tilt,sg
    25.1,1.000
    38.4,1.020
    51.0,1.040
    63.9,1.060

then run:

    python3 calibrate.py points.csv            # cubic fit (default)
    python3 calibrate.py points.csv --degree 2

It prints the syscfg lines to paste into your target (or apps/brewpill)
and the matching --poly argument for brewpill_receiver.py.
No third-party packages are needed.
"""

import argparse
import csv
import sys


def solve(a, b):
    """Solve a*x = b by Gaussian elimination with partial pivoting."""
    n = len(b)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for col in range(n):
        piv = max(range(col, n), key=lambda r: abs(m[r][col]))
        if abs(m[piv][col]) < 1e-18:
            raise ValueError("points do not determine the polynomial; "
                         "use more points or a lower --degree")
        m[col], m[piv] = m[piv], m[col]
        for r in range(n):
            if r != col:
                f = m[r][col] / m[col][col]
                for c in range(col, n + 1):
                    m[r][c] -= f * m[col][c]
    return [m[i][n] / m[i][i] for i in range(n)]


def fit(points, degree):
    """Least-squares polynomial fit; returns [C0, C1, ...]."""
    n = degree + 1
    ata = [[sum(t ** (i + j) for t, _ in points) for j in range(n)]
           for i in range(n)]
    atb = [sum(sg * t ** i for t, sg in points) for i in range(n)]
    return solve(ata, atb)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("csv", help="CSV file with tilt,sg columns")
    p.add_argument("--degree", type=int, default=3, choices=(1, 2, 3))
    args = p.parse_args()

    points = []
    with open(args.csv, newline="") as f:
        for row in csv.reader(f):
            try:
                points.append((float(row[0]), float(row[1])))
            except (ValueError, IndexError):
                continue  # header or blank line

    if len(points) < args.degree + 1:
        sys.exit(f"need at least {args.degree + 1} points for a degree "
                 f"{args.degree} fit, got {len(points)}")

    coeffs = fit(points, args.degree)
    coeffs += [0.0] * (4 - len(coeffs))

    print("Fit residuals:")
    for t, sg in points:
        pred = sum(c * t ** i for i, c in enumerate(coeffs))
        print(f"  tilt {t:6.2f}  SG {sg:.4f}  fit {pred:.4f}  "
              f"error {pred - sg:+.4f}")

    print("\nsyscfg.vals for the brewpill target:")
    for i, c in enumerate(coeffs):
        print(f"    BREWPILL_SG_C{i}: {c:.10g}")

    print("\nOr convert on the receiver instead:")
    print("    python3 brewpill_receiver.py --poly " +
          " ".join(f"{c:.10g}" for c in coeffs))


if __name__ == "__main__":
    main()
