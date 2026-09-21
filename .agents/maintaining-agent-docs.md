# Maintaining the Agent Guides

Companion to [`AGENTS.md`](../AGENTS.md). Read this when you are editing
`AGENTS.md` itself or a file in this directory — not when you are working on the
code.

## 1. Instructions, Not Overviews

Write what an agent should _do_ or _avoid_, not what the repository _contains_.
Directory trees, file inventories and "what lives where" tours do not measurably
help an agent find the right file, they go stale, and `fd` and `rg` answer the
same question on demand. Prefer a rule an agent cannot derive from the codebase
in one command.

The evidence is [Gloaguen et al., _Evaluating AGENTS.md_][paper]. Across
SWE-bench and a purpose-built benchmark of repositories carrying
developer-written context files, agents reliably _followed_ instructions given in
those files, but repository overviews did not improve their results — while the
files raised inference cost by over 20%.

Two caveats before treating that as settled. The overview finding is "does not
earn its tokens" rather than "actively harmful": removing the overview section
changed accuracy insignificantly in the authors' own ablation. And the study is
Python-only, which the authors flag first under Limitations — for a
well-represented language, tooling knowledge may already sit in the model's
weights. The submodule boundary, the hand-maintained CMake file lists, the
regression-lock convention in the test suite and the lazy-evaluation trap in a
benchmark loop are the opposite case, and are worth writing down.

## 2. Smallest Scope That Works

`AGENTS.md` is loaded at the start of _every_ session, whatever the task, so a
line there is paid for by every agent forever. A line here is paid for only by
the agent that needed it.

- Needed on _every_ task → `AGENTS.md`.
- Needed for _one kind_ of task → a file in this directory, plus a row in the
  `AGENTS.md` task-guide table so an agent knows when to load it.

Corollary: keep external links out of `AGENTS.md`. An agent that finds a URL in
its always-loaded context may fetch it before doing the work it was asked to do.
Links belong in these on-demand files, where the reader has already chosen the
topic.

## 3. Accuracy and Cadence

Verify a claim against the repository before writing it down, and prefer a claim
you have actually run to one you have read. A guide that confidently states
something false is worse than no guide, because an agent has no reason to doubt
it. Numbers in these files — a pinned regression value, a tolerance, a measured
residual — are copied from the code, never from memory.

Update `AGENTS.md` and these guides when

- a preset, option or target name changes,
- a workflow is added, retriggered differently, or made to gate pull requests,
- the `external/` submodules are bumped to a QuantLib release that changes an
  API these files name,
- or a convention in `.clang-format` or `.clang-tidy` changes.

[paper]: https://arxiv.org/abs/2602.11988
