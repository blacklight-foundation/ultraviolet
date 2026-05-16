# Appendix C: AST Form Index

## Load When

Use when mapping syntax to AST owner areas or diagnosing parser versus semantic owner boundaries.

## Authoring Rules

- Use AST ownership to identify the canonical compiler subsystem for a defect.
- Do not infer semantics from AST names alone; open the chapter that owns the form.

## Syntax Forms

No source forms are defined here.

## Static Semantics

AST forms connect parser output to feature-owner semantics.

## Runtime and Lowering Notes

AST form ownership helps trace lowering and runtime bugs to the right chapter.

## Diagnostics

Use when a diagnostic appears to be emitted from the wrong owner surface.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Parsing/**`
- `HelloUltraviolet/Source/Reference/Lowering/**`

## Spec Fallback

Open `SPECIFICATION.md` Appendix C for the AST form index.
