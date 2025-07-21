#!/usr/bin/env python3

import os
import subprocess
import sys

# Directories to include for clang-tidy checks
PROJECT_DIRS = ["rke", "benchmark"]
BUILD_DIR = "build/clang-tidy"  # Change if your build directory differs
CLANG_TIDY = "clang-tidy"

# File extensions to check
SOURCE_EXTENSIONS = (".cpp", ".cc", ".cxx", ".c", ".h", ".hpp")


def find_project_files():
    files = []
    for directory in PROJECT_DIRS:
        if not os.path.isdir(directory):
            continue
        for root, _, filenames in os.walk(directory):
            for filename in filenames:
                if filename.endswith(SOURCE_EXTENSIONS):
                    files.append(os.path.join(root, filename))
    return files


def run_clang_tidy(files):
    failed = []
    for file in files:
        print(f"Running {CLANG_TIDY} on {file}")
        cmd = [CLANG_TIDY, file, "-p", BUILD_DIR, "--quiet", "--fix"]
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode != 0:
            print(f"\033[91mFAILED: {file}\033[0m")
            failed.append(file)
        print(result.stdout.decode())
        if result.stderr:
            print(result.stderr.decode())
    if failed:
        print(f"\n{len(failed)} file(s) failed clang-tidy checks.")
        sys.exit(1)
    else:
        print("\nAll files passed clang-tidy checks.")


if __name__ == "__main__":
    files = find_project_files()
    if not files:
        print("No source or header files found in the project directories.")
        sys.exit(0)
    run_clang_tidy(files)
