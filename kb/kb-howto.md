---
type: Playbook
title: How to maintain this knowledge base
description: Rules for agents and people adding to the OKF bundle in kb/.
tags: [meta, process]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# Rules

1. **Record as you go.** Every finished step, measurement, finding, decision
   or paper read gets a concept or an update to one, in the same commit as
   the work. Add a line to [the log](/log.md).
2. **One concept per file.** Frontmatter needs `type`; add `title`,
   `description`, `tags`, `status`, `generated`. Use `sources` for where a
   claim comes from (papers, commits, measurements) and footnotes keyed to
   `sources[].id` for specific claims.
3. **Public-safe.** The fork is public. No personal names, email addresses,
   local machine paths or private project names. Cite papers by title,
   venue and DOI or URL, never by a local file path.
4. **Measured, not guessed.** Put numbers with their conditions (GPU,
   resolution, frames, commit). If something wasn't tested, say so.
5. **Trust.** `generated.by` is the writer (`claude_code/<model>` for the
   agent, `human:<id>` for a person). Add `verified` only when someone or a
   process checked the content. Use `status: draft` for ideas not yet
   tried, `deprecated` for things proven wrong (keep them: they stop the
   same mistake twice).
6. **Link, don't copy.** Runbooks (CUSTOM_BUILD.md, DDGI.md, ...) stay the
   source for procedures; concepts link to them.
7. **Index.** Keep each directory's `index.md` listing its concepts with
   their descriptions.
8. **Mistakes and successes.** When something goes wrong (a false claim,
   a wasted rebuild, a test that measured the wrong thing, a correction
   from the user), add a `Mistake` in [mistakes/](/mistakes/): what
   happened, why, and the rule. When an approach works clearly better than
   what came before, add a `Success` in [successes/](/successes/): what,
   why it worked, when to reuse it. Read both before testing or measuring.

# Types used here

| Type | For |
| --- | --- |
| `Overview` | A system or area of the project |
| `Finding` | Something learned from a test or investigation |
| `Decision` | A choice made and why |
| `Proposal` | An improvement idea, not yet built |
| `Reference` | A paper or talk, summarized for this project |
| `Playbook` | A procedure |
| `Technique` | A testing or debugging method that worked |
| `Mistake` | Something an agent got wrong, why, and the rule that prevents it |
| `Success` | An approach that worked, why, and when to reuse it |
