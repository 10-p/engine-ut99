#!/usr/bin/env python3
"""rate.py <game.log> [<skip>]: for each watch tag of the input test channel's log, the view's yaw rate (a least-
squares fit of yaw on time, in yaw units and degrees per second), the mean horizontal speed and the absolute mouse
cursor's rate (pixels per second), over the watch lines after the first <skip> (default 20)."""
import re
import sys

WATCH = re.compile(r"NSDLInputTest: watch (\S+) (\d+) ([\d.]+) (-?\d+) (-?\d+) ([\d.]+) (-?[\d.]+) (-?[\d.]+)")


def main():
    skip = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    tags = {}
    for line in open(sys.argv[1], errors="replace"):
        if line.startswith("Log: "):
            continue
        m = WATCH.match(line)
        if m:
            tags.setdefault(m.group(1), []).append(
                (float(m.group(3)), int(m.group(4)), float(m.group(6)), float(m.group(7)), float(m.group(8))))
    for tag, pts in tags.items():
        pts = pts[skip:]
        n = len(pts)
        if n < 10:
            print(f"{tag}: too few watch lines ({n})")
            continue
        mt = sum(p[0] for p in pts) / n
        sxx = sum((p[0] - mt) ** 2 for p in pts)

        def slope(k):
            mk = sum(p[k] for p in pts) / n
            return sum((p[0] - mt) * (p[k] - mk) for p in pts) / sxx

        rate = slope(1)
        span = pts[-1][0] - pts[0][0]
        speed = sum(p[2] for p in pts) / n
        print(f"{tag}: {rate:.0f} yaw/s ({rate * 360 / 65536:.1f} deg/s), speed {speed:.0f}, "
              f"cursor {slope(3):.0f},{slope(4):.0f} px/s, {n} ticks over {span:.2f} s")


if __name__ == "__main__":
    main()
