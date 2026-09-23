# `.agents/` — On-Demand Guides for AI Agents

[`AGENTS.md`](../AGENTS.md) at the repository root is loaded into an agent's
context at the start of _every_ session, so it is deliberately kept short: hard
constraints, coding conventions, build and test entry points, pitfalls, and
pointers.

The files here hold what is only needed for a _specific_ task — adding a pricing
engine, running a benchmark, reading the CI map. `AGENTS.md` links to them; an
agent reads one only when the task calls for it, so they cost no context
otherwise.

## Contents

| File                                                     | Read it when                                                                                                                                                      |
| -------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [`build-and-test.md`](build-and-test.md)                 | You are configuring a build, picking options, running a single test case, running clang-tidy or clang-format locally, or checking which workflow covers a change. |
| [`extending-rke-ql-ext.md`](extending-rke-ql-ext.md)     | You are adding an instrument, payoff, pricing engine, or test under `src/rke/`.                                                                                       |
| [`benchmarking.md`](benchmarking.md)                     | You are adding, changing, or running a benchmark.                                                                                                                 |
| [`maintaining-agent-docs.md`](maintaining-agent-docs.md) | You are editing `AGENTS.md` or one of the files above.                                                                                                            |

## Why this directory

There is no cross-agent standard location for supplementary instruction files.
The [`AGENTS.md` convention](https://agents.md/) specifies only the file itself,
plus nested `AGENTS.md` files — which are path-scoped rather than task-scoped, so
they do not fit "how to add a pricing engine". Task-scoped, load-on-demand
instructions _are_ standardized by the
[Agent Skills specification](https://agentskills.io/specification), but the
directories agents discover skills in remain vendor-specific: `.claude/skills/`
for [Claude Code][cc-skills], `.agents/skills/` and `.opencode/skills/` for
[opencode][oc-skills].

Plain Markdown referenced by relative path from `AGENTS.md` is the option that
works with every agent that can read a file, and with humans, without depending
on any one vendor's discovery rules. `.agents/` borrows its name from the
vendor-neutral skills path. If a guide later warrants automatic activation, it
can be promoted to a `SKILL.md` without changing anything here.

The `CLAUDE.md` → `AGENTS.md` indirection follows the
[documented recommendation][cc-memory] rather than duplicating content.

The layout follows [QuantLib's own agent docs][ql-pr], so that an agent moving
between this repository and the upstream fork in `external/QuantLib` finds the
same structure. What belongs in these files, and what belongs in `AGENTS.md`
instead, is [`maintaining-agent-docs.md`](maintaining-agent-docs.md).

[cc-skills]: https://code.claude.com/docs/en/skills
[cc-memory]: https://code.claude.com/docs/en/memory#agents-md
[oc-skills]: https://opencode.ai/docs/skills/
[ql-pr]: https://github.com/lballabio/QuantLib/pull/2498
