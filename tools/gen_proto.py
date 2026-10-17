#!/usr/bin/env python3
"""Regenerate protocol code from proto/bridge.proto.

    tools/.venv/Scripts/python tools/gen_proto.py

Outputs components/protocol/bridge.pb.{c,h} (nanopb) and tools/bridge_pb2.py.
"""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROTO_DIR = ROOT / "proto"
PROTO = PROTO_DIR / "bridge.proto"


def run(*args):
    subprocess.run([sys.executable, *args], check=True, cwd=ROOT)


def main():
    run("-m", "nanopb.generator.nanopb_generator",
        "-I", str(PROTO_DIR),
        "-f", str(PROTO_DIR / "bridge.options"),
        "-D", str(ROOT / "components" / "protocol"),
        str(PROTO))
    run("-m", "grpc_tools.protoc",
        "-I", str(PROTO_DIR),
        "--python_out", str(ROOT / "tools"),
        str(PROTO))


if __name__ == "__main__":
    main()
