# Design decision records

[Documentation index](../../README.md)

Use this directory for consequential choices that need their reasoning preserved: object identity, containment, authority enforcement, message acceptance, image compatibility, execution ordering, or spatial semantics.

The existing agreements and open questions are recorded in [architecture.md](../architecture.md). They have not been retroactively converted into decision records. Do not invent approval dates or label an agent proposal as an accepted user decision.

## Naming and lifecycle

Name a record `NNNN-short-subject.md`, beginning with `0001`. Use one focused decision per record. Status is proposed, accepted, rejected, or superseded. Link superseded records to their replacements; preserve their historical rationale.

Accepted means explicit user direction or an engineering decision within delegated scope, with that basis stated. Document provisional implementation defaults as provisional; they do not automatically settle language semantics.

## Record contents

- **Title and status:** the concrete decision and whether it is settled.
- **Context:** the problem and relevant constraints.
- **Decision:** exact rules and boundaries; identify approval or delegated engineering scope.
- **Alternatives:** realistic options and why they were not selected.
- **Consequences:** tradeoffs, failure cases, migration implications, and unresolved questions.
- **Evidence:** implementation, tests, examples, and related documents.

Link the record from the affected architecture/reference page. If implementation is pending, state that clearly and link the plan rather than implying the feature exists.
