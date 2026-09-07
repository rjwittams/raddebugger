"""Drive an isolated RAD instance loaded with line_step.S, stopped at line 10.

Compile line_step.S with debug info (and dsymutil on macOS), create a project
targeting it with a source breakpoint at line 10, and pass the instance's PID.
Run again with SPECIAL_LINE=0xfeefee and SPECIAL_LINE=0xf00f00 to exercise the
same policies with CodeView's encodings, without requiring a PDB producer.
"""

import argparse
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sender")
    parser.add_argument("pid", type=int)
    parser.add_argument("--instruction-size", type=int, choices=(1, 4), default=4)
    parser.add_argument("--case", choices=("over", "into", "instruction", "breakpoint", "halt"), default="over")
    parser.add_argument("--exception", action="store_true", help="fixture built with HIDDEN_EXCEPTION")
    args = parser.parse_args()

    def ipc(command):
        return subprocess.check_output(
            [args.sender, "--ipc", f"--pid:{args.pid}", *command.split()],
            text=True, timeout=15,
        )

    def state():
        raw = ipc("state")
        def number(key):
            match = re.search(rf"^\s*{key}:\s*(0x[\da-f]+|\d+)", raw, re.M)
            return int(match[1], 0) if match else None
        return {"raw": raw, "stop": number("stop_count"), "running": number("running"),
                "line": number("line_num"), "ip": number("ip")}

    def stopped(command):
        before = state()["stop"]
        ipc(command)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            current = state()
            if (not current["running"] and current["stop"] > before
                    and (command != "restart" or current["ip"] != 0)):
                print(f"{command}: line={current['line']} ip={current['ip']:#x}", flush=True)
                return current
            time.sleep(0.02)
        raise AssertionError(f"{command} did not finish\n{state()['raw']}")

    def expect_line(current, line):
        assert current["line"] == line, f"expected line {line}\n{current['raw']}"

    current = stopped("restart")
    expect_line(current, 10)
    if args.case == "halt":
        # Fixture built with HIDDEN_LOOP: source stepping must remain haltable.
        expect_line(stopped("step_over_line"), 20)
        ipc("step_over_line")
        time.sleep(0.2)
        assert state()["running"], state()["raw"]
        current = stopped("halt")
        assert 'explanation: "Halted"' in current["raw"], current["raw"]
        time.sleep(0.2)
        assert not state()["running"], state()["raw"]
    elif args.case == "instruction":
        current = stopped("step_into_inst")
        assert current["line"] in (0, 0xfeefee, 0xf00f00), current["raw"]
    elif args.case == "breakpoint":
        address = current["ip"] + args.instruction_size
        reply = ipc(f"add_address_breakpoint {address:#x}")
        breakpoint_id = re.search(r"\$[\da-fA-F]+", reply)
        assert breakpoint_id, reply
        try:
            current = stopped("step_over_line")
            assert current["ip"] == address, current["raw"]
            assert "hit a breakpoint" in current["raw"], current["raw"]
        finally:
            ipc(f"remove_breakpoint {breakpoint_id[0]}")
    else:
        step = "step_over_line" if args.case == "over" else "step_into_line"
        for line in (20, 30):
            expect_line(stopped(step), line)
        if args.case == "into":
            expect_line(stopped(step), 40)
        expect_line(stopped(step), 50)
        current = stopped(step)
        reason = "exception" if args.exception else "hit a trap"
        assert reason in current["raw"], current["raw"]
        current = stopped("kill_all" if args.exception else "run")
        assert current["ip"] == 0, current["raw"]
    print(f"PASS: {args.case}")


if __name__ == "__main__":
    main()
