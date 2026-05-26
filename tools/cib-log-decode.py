#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///

from __future__ import annotations

import argparse
import itertools
import json
import os
from collections.abc import Iterable, Iterator
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path
import sys
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
CIB_TOOLS = REPO_ROOT / "vendor" / "compile-time-init-build" / "tools"
sys.path.insert(0, str(CIB_TOOLS))

import mipi_messages as mipi  # noqa: E402


JsonObject = dict[str, Any]
MessageMap = dict[int, JsonObject]
ModuleMap = dict[int, str]

MSG_TYPES: dict[int, type[Any]] = {
    1: mipi.Short32,
    3: mipi.Catalog,
    7: mipi.Short64,
}


class CountingReader(Iterator[int]):
    def __init__(self, iterable: Iterable[int]) -> None:
        self._iter = iter(iterable)
        self.offset = 0

    def __iter__(self) -> CountingReader:
        return self

    def __next__(self) -> int:
        value = next(self._iter)
        self.offset += 1
        return value


def fd_reader(fd: int, chunk_size: int = 4096) -> Iterator[int]:
    while True:
        chunk = os.read(fd, chunk_size)
        if not chunk:
            break
        for byte in chunk:
            yield byte


@contextmanager
def input_reader(path: str) -> Iterator[Iterator[int]]:
    if path == "-":
        yield fd_reader(sys.stdin.buffer.fileno())
        return

    with open(path, "rb") as infile:
        yield fd_reader(infile.fileno())


def construct_msg(
    msg_type: int,
    first_byte: int,
    reader: Iterator[int],
    messages: MessageMap,
    modules: ModuleMap,
    db: JsonObject,
) -> Any:
    if msg_type not in MSG_TYPES:
        raise ValueError(f"unknown message type: {msg_type}")

    seq = itertools.chain([first_byte], reader)
    return MSG_TYPES[msg_type](seq, messages, modules, db)


def read_logs(reader: Iterable[int], db: JsonObject) -> Iterator[str]:
    counted = CountingReader(reader)
    messages: MessageMap = {message["id"]: message for message in db["messages"]}
    modules: ModuleMap = {module["id"]: module["string"] for module in db["modules"]}

    while True:
        try:
            first_byte = next(counted)
        except StopIteration:
            break

        offset = counted.offset - 1
        msg_type = first_byte & 0xF
        try:
            msg = construct_msg(
                msg_type, first_byte, counted, messages, modules, db
            )
        except Exception as exc:
            raise RuntimeError(f"decode failed near byte offset {offset}: {exc}") from exc

        yield str(msg)


def parse_cmdline() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Decode CIB binary log packets from a file or stdin."
    )
    parser.add_argument(
        "--input",
        required=True,
        help="Input binary log stream, or '-' for stdin.",
    )
    parser.add_argument(
        "--json",
        required=True,
        help="Generated CIB string catalog JSON, usually build/log/strings.json.",
    )
    parser.add_argument(
        "--output",
        help="Output text file. Defaults to stdout.",
    )
    parser.add_argument(
        "--host-time",
        action="store_true",
        help="Prefix each decoded line with the receiver's local timestamp.",
    )
    parser.add_argument(
        "--no-host-time",
        action="store_false",
        dest="host_time",
        help="Do not prefix receiver timestamps.",
    )
    parser.set_defaults(host_time=False)
    return parser.parse_args()


def format_line(line: str, host_time: bool) -> str:
    if not host_time:
        return line

    timestamp = datetime.now().astimezone().isoformat(timespec="milliseconds")
    return f"{timestamp} {line}"


def main() -> int:
    args = parse_cmdline()

    with open(args.json, "r", encoding="utf-8") as json_file:
        db = json.load(json_file)

    with input_reader(args.input) as reader:
        if args.output:
            with open(args.output, "w", encoding="utf-8") as outfile:
                for line in read_logs(reader, db):
                    print(format_line(line, args.host_time), file=outfile, flush=True)
        else:
            for line in read_logs(reader, db):
                print(format_line(line, args.host_time), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
