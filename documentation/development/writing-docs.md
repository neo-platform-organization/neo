# Writing documentation

[Documentation index](../README.md)

## Choose a home

Choose the page's primary purpose: learning, reference, development, design, or planning. Use a short lowercase hyphenated filename. Introduce subject subdirectories only when multiple substantive pages need them; avoid empty placeholders and a miscellaneous folder.

A tutorial teaches one path through the system. A reference specifies exact behavior. A design page explains requirements, alternatives, and rationale. A development guide explains a task. A plan tracks intended work. Link between these instead of maintaining several competing descriptions of the same rule.

## State what is known

Mark proposals and unresolved choices explicitly. Describe implemented behavior in reference pages, with limitations and failure cases. Do not copy aspirational claims into an API guarantee. Tests provide evidence for implementation claims; documentation alone does not.

The whitepaper is preserved reference material. The architecture names agreed requirements separately from defaults. The language-design page remains exploratory. Maintain those distinctions when moving text between guides.

## Examples and navigation

- Use C for implementation examples and plain text for conceptual diagrams. Label actual neo source examples as executable or proposed.
- Assume commands run from the repository root unless another directory is explicitly stated.
- Use relative Markdown links to source and other documents so the checkout is portable.
- Add a documentation-index link near the top of new pages, with the correct relative path.
- Update the main documentation index and relevant README links when adding or moving a guide.
- Keep README.md concise; put detailed explanations here.

## Keeping docs current

When changing behavior, update the affected reference page, development limitations, and any tutorial that traces that behavior. When changing design, add or update a decision record and link it from the architecture. Update the implementation plan when milestones change.

For documentation-only changes, check links and paths. Run executable examples when commands or their assumptions change. Do not rerun unrelated runtime suites merely because prose moved.

## New page checklist

1. Does its location match its purpose and subject?
2. Does it distinguish implemented, agreed, proposed, and open behavior?
3. Are the examples consistent with the actual code or clearly marked as proposals?
4. Do links resolve from the page's new location?
5. Can a reader reach the page through the index?
