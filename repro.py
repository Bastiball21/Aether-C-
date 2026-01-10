import subprocess
import time

def run_test():
    engine = subprocess.Popen(
        ['./Aether-C.exe'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=0
    )

    commands = [
        "uci",
        "isready",
        "position startpos",
        "go depth 12"
    ]

    for cmd in commands:
        engine.stdin.write(cmd + "\n")
        engine.stdin.flush()

    output = ""
    while True:
        line = engine.stdout.readline()
        if not line:
            break
        output += line
        print(line.strip())
        if "bestmove" in line:
            break

    engine.terminate()

if __name__ == "__main__":
    run_test()
