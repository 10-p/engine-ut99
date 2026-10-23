#!/usr/bin/env python3
"""rate.py <game.log> [<skip>]: for each watch tag of the input test channel's log, the view's yaw rate (a least-
squares fit of yaw on time, in yaw units and degrees per second) and the mean horizontal speed, over the watch lines
after the first <skip> (default 20)."""
import re
import sys

WATCH = re.compile(r"NSDLInputTest: watch (\S+) (\d+) ([\d.]+) (-?\d+) (-?\d+) ([\d.]+)")


def main():
    skip = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    tags = {}
    for line in open(sys.argv[1], errors="replace"):
        if line.startswith("Log: "):
            continue
        m = WATCH.match(line)
        if m:
            tags.setdefault(m.group(1), []).append((float(m.group(3)), int(m.group(4)), float(m.group(6))))
    for tag, pts in tags.items():
        pts = pts[skip:]
        n = len(pts)
        if n < 10:
            print(f"{tag}: too few watch lines ({n})")
            continue
        mt = sum(p[0] for p in pts) / n
        my = sum(p[1] for p in pts) / n
        sxx = sum((p[0] - mt) ** 2 for p in pts)
        rate = sum((p[0] - mt) * (p[1] - my) for p in pts) / sxx
        span = pts[-1][0] - pts[0][0]
        speed = sum(p[2] for p in pts) / n
        print(f"{tag}: {rate:.0f} yaw/s ({rate * 360 / 65536:.1f} deg/s), speed {speed:.0f}, "
              f"{n} ticks over {span:.2f} s")


if __name__ == "__main__":
    main()
