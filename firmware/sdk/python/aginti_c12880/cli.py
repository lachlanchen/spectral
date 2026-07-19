from __future__ import annotations

import argparse
import time

from .client import SpectrometerClient


def main() -> None:
    parser = argparse.ArgumentParser(description="AgInTi C12880MA firmware client")
    parser.add_argument("--port", required=True)
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("identity")
    stream = sub.add_parser("stream")
    stream.add_argument("--clock-hz", type=int, default=1_000_000)
    stream.add_argument("--exposure-us", type=float, default=1000.0)
    stream.add_argument("--seconds", type=float, default=5.0)
    args = parser.parse_args()

    with SpectrometerClient(args.port) as device:
        if args.action == "identity":
            print(device.identity())
            return
        exposure_clocks = max(11, round(args.exposure_us * args.clock_hz / 1_000_000))
        device.configure(args.clock_hz, exposure_clocks)
        time.sleep(0.05)
        device.start()
        started = time.monotonic()
        count = 0
        try:
            for frame in device.frames(args.seconds):
                count += 1
                if count % 100 == 0:
                    elapsed = time.monotonic() - started
                    print(f"frames={count} fps={count / elapsed:.1f} "
                          f"seq={frame.sequence} drops={frame.dropped_frames} "
                          f"peak={int(frame.pixels.max())}")
        finally:
            device.stop()


if __name__ == "__main__":
    main()

