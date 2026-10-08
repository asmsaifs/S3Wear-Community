#!/usr/bin/env python3
"""Encode protocol golden vectors: protocol/testvectors/<message>_<case>.json -> .bin.

The .json is the proto3 JSON form of one message; <message> is the snake_case
name of a message in package s3w.v1 (longest match wins, so hello_ack_x is a
HelloAck and hello_x a Hello). Encoding uses Google's Python protobuf, an
implementation independent of both nanopb (watch) and Wire (phone).

Usage: gen_testvectors.py --proto-dir DIR --vectors-dir DIR [--check]
"""

import argparse
import importlib
import pathlib
import re
import sys
import tempfile

from google.protobuf import json_format
from grpc_tools import protoc


def snake(name: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


def load_messages(proto_dir: pathlib.Path, out_dir: pathlib.Path) -> dict:
    protos = sorted(proto_dir.glob("*.proto"))
    args = ["protoc", f"-I{proto_dir}", f"-I{protoc.__file__.rsplit('/', 1)[0]}/_proto",
            f"--python_out={out_dir}"] + [str(p) for p in protos]
    if protoc.main(args) != 0:
        sys.exit("protoc failed")
    sys.path.insert(0, str(out_dir))
    messages = {}
    for p in protos:
        module = importlib.import_module(p.stem + "_pb2")
        for name, desc in module.DESCRIPTOR.message_types_by_name.items():
            messages[snake(name)] = getattr(module, name)
    return messages


def message_for(stem: str, messages: dict):
    matches = [k for k in messages if stem == k or stem.startswith(k + "_")]
    if not matches:
        sys.exit(f"{stem}.json: no message named like '{stem}'")
    return messages[max(matches, key=len)]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--proto-dir", required=True, type=pathlib.Path)
    ap.add_argument("--vectors-dir", required=True, type=pathlib.Path)
    ap.add_argument("--check", action="store_true", help="fail if a .bin is missing or stale")
    a = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        messages = load_messages(a.proto_dir, pathlib.Path(tmp))
        stale = []
        jsons = sorted(a.vectors_dir.glob("*.json"))
        for j in jsons:
            cls = message_for(j.stem, messages)
            msg = json_format.Parse(j.read_text(encoding="utf-8"), cls())
            data = msg.SerializeToString(deterministic=True)
            b = j.with_suffix(".bin")
            if a.check:
                if not b.exists() or b.read_bytes() != data:
                    stale.append(b.name)
            elif not b.exists() or b.read_bytes() != data:
                b.write_bytes(data)
                print(f"wrote {b.name} ({cls.DESCRIPTOR.full_name}, {len(data)} bytes)")
        orphans = [b.name for b in a.vectors_dir.glob("*.bin") if not b.with_suffix(".json").exists()]
        if orphans:
            sys.exit(f"vectors without .json: {', '.join(sorted(orphans))}")
        if stale:
            sys.exit(f"stale vectors (run tools/gen_proto.sh): {', '.join(stale)}")
        print(f"{len(jsons)} golden vectors OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
