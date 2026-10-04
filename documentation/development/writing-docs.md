# Documentation format

[VM documentation](../README.md)

Write formal usage, compilation, interface, and learning guides in Markdown here.
Language guides live in `neo/documentation/` so they can migrate with the language.
Plans, TODOs, decisions, and collaboration context live in the external `brain/`.
Keep relative links valid in a standalone checkout. Preserve both whitepapers.

C public interfaces should explain ownership, output values on failure, rights,
and lifetime in Doxygen comments. Use `/** ... */` or `///` for API documentation;
ordinary implementation comments explain reasoning. neo source comments use `//`.

[Doxygen](https://www.doxygen.nl/manual/markdown.html) combines Markdown guides and
C source documentation into an HTML manual. Its LaTeX output can produce PDF with
a TeX toolchain. This is the recommended single tool for both documentation sources.
[Pandoc](https://pandoc.org/getting-started.html) is useful for converting standalone
Markdown guides to HTML/PDF; it does not extract C API comments.

```sh
doxygen Doxyfile
pandoc documentation/development/build.md -s -o /tmp/neo-build.html
```

The Doxygen output directory is `generated-docs/`, separate from the VM build.
PDF generation requires the chosen external PDF engine. Tools are not installed
by the VM build.
