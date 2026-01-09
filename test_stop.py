import subprocess
import time

def test_stop():
    proc = subprocess.Popen(
        ['./Aether-C.exe'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    commands = [
        "uci",
        "setoption name Threads value 4",
        "position startpos",
        "go infinite"
    ]

    for cmd in commands:
        proc.stdin.write(cmd + "\n")
        proc.stdin.flush()

    # Let it run for 1 second
    time.sleep(1)

    # Stop
    print("Sending stop...")
    proc.stdin.write("stop\n")
    proc.stdin.flush()

    # Read output until bestmove
    while True:
        line = proc.stdout.readline()
        if not line: break
        print("Engine:", line.strip())
        if "bestmove" in line:
            print("Received bestmove!")
            break

    # Run another search
    print("Sending go depth 5...")
    proc.stdin.write("go depth 5\n")
    proc.stdin.flush()

    while True:
        line = proc.stdout.readline()
        if not line: break
        print("Engine:", line.strip())
        if "bestmove" in line:
            print("Received bestmove 2!")
            break

    proc.stdin.write("quit\n")
    proc.stdin.flush()
    proc.wait()

if __name__ == "__main__":
    test_stop()
