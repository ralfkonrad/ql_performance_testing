<!--
SPDX-FileCopyrightText: 2026 Ralf Konrad Eckel
SPDX-License-Identifier: MIT
-->

# Changing QuantLib

Read this when you change `external/QuantLib` itself to make it faster. This is
the only task allowed to edit a submodule. Every other task treats
`external/QuantLib` as read-only, and `external/benchmark` is read-only always.

## 1. Where the Change Lives

- Edit QuantLib in the submodule of a sibling `ql_performance_testing` worktree,
  never in the `master` worktree. Create the worktree first, then run
  `git submodule update --init external/QuantLib` inside it.
- Inside the submodule, work on a topic branch of `ralfkonrad/QuantLib` and push
  it to `origin`. Never push to `upstream`, and never open a QuantLib pull
  request: the maintainer opens it against `lballabio/QuantLib` themselves.
- QuantLib's own rules apply in there. Read `external/QuantLib/AGENTS.md` before
  editing: a new file is registered in CMake, Autotools and the Visual Studio
  solution.

## 2. Formatting Changed Lines Only

Never run clang-format over a whole QuantLib file. The tree is not clang-formatted
throughout, so a full pass buries the change under formatting noise. Format the
lines you changed, and nothing else, with QuantLib's own `.clang-format`.

`git clang-format` and `clang-format-diff.py` do not work here. Our root
`.clang-format-ignore` excludes `external/**`, so `git clang-format` reports "no
modified files to format", and `clang-format-diff.py` emits a diff that deletes
the whole file. Leave the ignore file alone, since it is what stops a stray
`clang-format -i` from rewriting the submodule. Instead, feed clang-format on
stdin, without a file name, with one `--lines` per hunk. Run this from
`external/QuantLib`, with `base` set to the commit your branch started from:

```bash
for f in $(git diff --name-only --diff-filter=AM "$base" -- '*.cpp' '*.hpp'); do
    lines=$(git diff -U0 "$base" -- "$f" |
        sed -nE 's/^@@ -[0-9,]+ \+([0-9]+)(,([0-9]+))? @@.*/\1 \3/p' |
        awk '{ n = ($2 == "" ? 1 : $2); if (n) printf " --lines=%d:%d", $1, $1 + n - 1 }')
    [ -n "$lines" ] && clang-format --style=file:.clang-format $lines < "$f" > "$f.fmt" && mv "$f.fmt" "$f"
done
```

`--lines` reflows the whole statement a changed line belongs to, so read the diff
afterwards: an untouched line that moved belongs to that statement, not to a
stray full-file pass.

## 3. Moving the Pointer

The pointer moves only on a `ql_performance_testing` feature branch, and only to
a commit already pushed to `ralfkonrad/QuantLib`, so a fresh clone can check it
out. Never on `master`. Commit the bump on its own, apart from the benchmark
that measures it.
