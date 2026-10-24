#!/usr/bin/env python3
"""rate.py <game.log> [<skip>]: for each watch tag of the input test channel's log, the view's yaw rate (a least-
squares fit of yaw on time, in yaw units and degrees per second), the mean horizontal speed and the absolute mouse
cursor's rate (pixels per second), over the watch lines after the first <skip> (default 20). Where the line has the
velocity and the acceleration, also the direction of each relative to the view (degrees, 0 ahead, positive to the
right; the mean of the per-tick vectors in the view's frame) and the share of ticks walking."""
import math
import re
import sys

NUM = r"(-?[\d.]+)"
WATCH = re.compile(r"NSDLInputTest: watch (\S+) (\d+) ([\d.]+) (-?\d+) (-?\d+) ([\d.]+) " + NUM + " " + NUM
                   + r"(?: " + " ".join([NUM] * 6) + r" (\d))?")


def direction(pts, k):
    """The direction of the vectors (p[k], p[k+1]) relative to each tick's view yaw, in degrees."""
    fwd = right = 0.0
    for p in pts:
        yaw = p[1] * 2 * math.pi / 65536
        x, y = p[k], p[k + 1]
        fwd += x * math.cos(yaw) + y * math.sin(yaw)
        right += -x * math.sin(yaw) + y * math.cos(yaw)
    return math.degrees(math.atan2(right, fwd)) if fwd or right else float("nan")


def main():
    skip = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    tags = {}
    for line in open(sys.argv[1], errors="replace"):
        if line.startswith("Log: "):
            continue
        m = WATCH.match(line)
        if m:
            p = [float(m.group(3)), int(m.group(4)), float(m.group(6)), float(m.group(7)), float(m.group(8))]
            if m.group(9) is not None:
                p += [float(m.group(g)) for g in range(11, 15)] + [int(m.group(15))]
            tags.setdefault(m.group(1), []).append(p)
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
        line = (f"{tag}: {rate:.0f} yaw/s ({rate * 360 / 65536:.1f} deg/s), speed {speed:.0f}, "
                f"cursor {slope(3):.0f},{slope(4):.0f} px/s, {n} ticks over {span:.2f} s")
        if len(pts[0]) > 5:
            walking = sum(p[9] for p in pts) / n
            line += (f", move {direction(pts, 5):.1f} deg (accel {direction(pts, 7):.1f} deg), "
                     f"walking {walking:.2f}")
        print(line)


if __name__ == "__main__":
    main()
