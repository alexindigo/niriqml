"""Extract displayed QML verbatim and lint it with the selected Qt tool.

Run only inside the assigned project guest. Output must be disk-backed.
"""
import argparse
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qmllint", required=True)
    parser.add_argument("--imports", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    output = pathlib.Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    paths = list(sorted((ROOT / "examples").glob("*.qml")))
    for name in ("README.md", "API.md", "docs/REQUESTS.md"):
        text = (ROOT / name).read_text()
        for i, source in enumerate(re.findall(r"```qml\n([\s\S]*?)\n```", text), 1):
            path = output / (name.replace("/", "-").replace(".md", "") + f"-{i}.qml")
            path.write_text(source + "\n")
            paths.append(path)
    paths.append(ROOT / "tests/test_examples.qml")
    print(f"Linting {len(paths)} example/snippet/test QML files", flush=True)
    return subprocess.run([args.qmllint, "-I", args.imports, *map(str, paths)], timeout=120).returncode


if __name__ == "__main__":
    raise SystemExit(main())
