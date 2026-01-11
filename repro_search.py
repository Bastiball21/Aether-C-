import subprocess
import time

def run_bench():
    print("Running bench...")
    p = subprocess.Popen(["./Aether-C.exe", "bench"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    out, err = p.communicate()
    # Parse bench output
    for line in out.splitlines():
        if "Bench:" in line:
            print(line)
            return

def run_fixed_search():
    print("Running fixed search 1s...")
    p = subprocess.Popen(["./Aether-C.exe"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    cmds = "position startpos\ngo movetime 1000\nquit\n"
    out, err = p.communicate(input=cmds)
    # Parse info
    nodes = 0
    nps = 0
    depth = 0
    for line in out.splitlines():
        if line.startswith("info"):
            parts = line.split()
            if "nodes" in parts:
                nodes = int(parts[parts.index("nodes") + 1])
            if "nps" in parts:
                nps = int(parts[parts.index("nps") + 1])
            if "depth" in parts:
                depth = int(parts[parts.index("depth") + 1])

    print(f"Nodes: {nodes}, NPS: {nps}, Depth: {depth}")

if __name__ == "__main__":
    run_bench()
    run_fixed_search()
