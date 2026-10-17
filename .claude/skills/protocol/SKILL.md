---
name: protocol
description: How to change or update the phone<->dongle protocol. Use whenever touching proto/ (the keyling-hq/protocol submodule), bridge.proto/bridge.options, the generated components/protocol/bridge.pb.* or tools/bridge_pb2.py.
---

# The protocol submodule

`proto/` is a git submodule of https://github.com/keyling-hq/protocol (MIT). The firmware pins one
commit of it; the Android app pins its own. `bridge.proto` comments are the normative spec.

Generated code is committed and must never be edited by hand:
- `components/protocol/bridge.pb.c`, `bridge.pb.h` (nanopb)
- `tools/bridge_pb2.py` (Python, for `tools/client.py`)

Regenerate with `tools/.venv/Scripts/python tools/gen_proto.py`. The venv is gitignored; create it
with `py -3 -m venv tools/.venv` and `tools/.venv/Scripts/python -m pip install -r tools/requirements.txt`.
The generator writes LF line endings, so git may show a generated file as modified when only the
line endings differ: `git diff --stat` stays empty in that case and staging the file clears it.

## Changing the protocol

Edit inside `proto/`, but on a branch: a submodule checks out a detached HEAD and commits made
there are easily lost.

1. `git -C proto switch main` and `git -C proto pull`
2. Edit `proto/bridge.proto` (and `bridge.options` for any new size-bounded field) following the
   compatibility rules in `proto/README.md`: only add, never renumber or reuse; keep the oneof
   number ranges (1-9 core, 10-29 input, 30-49 system, 50+ apps).
3. Regenerate, then update `components/protocol/protocol.c` (and `tools/client.py`) for the change.
4. Build `hid` once and get the change tested on the dongle.
5. Commit and **push inside `proto/` first**, then commit in firmware: the new submodule pointer
   together with the generated files and the code using them, in one commit. Pushing firmware
   before the protocol commit exists on GitHub leaves every other clone unable to check it out.

## Picking up a protocol change made elsewhere

`git submodule update --remote proto`, regenerate, build, commit pointer + generated code together.

## After pulling firmware

`git pull` does not move the submodule's checkout. Run `git submodule update --init` (or set
`git config submodule.recurse true`, which this clone has) so `proto/` matches the pinned commit
before regenerating or building.
