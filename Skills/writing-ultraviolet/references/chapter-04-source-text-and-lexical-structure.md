# Chapter 4: Source Text and Lexical Structure

## Load When

Use for Unicode source loading, identifiers, literals, comments, operators, punctuation, logical lines, keywords, and lexical diagnostics.

## Authoring Rules

- Use Unicode XID identifiers only when there is a clear project reason; repo style prefers ASCII names.
- Preserve keyword spelling exactly.
- Use newlines as the normal statement terminator; use semicolons only where the grammar or readability justifies it.
- Keep string and char escapes explicit and durable.

## Syntax Forms

```ultraviolet
internal procedure lexicalLiteralReference() -> bool {
    let name: string@View = "ultraviolet"
    let count: i32 = 3
    let newline: char = '\n'
    return name == "ultraviolet" &&
        count == 3 &&
        newline == '\n'
}
```

## Static Semantics

Identifier equality uses normalized keys. Reserved words cannot be ordinary identifiers.

## Runtime and Lowering Notes

Lexical loading rejects invalid UTF-8 before parse and module processing.

## Diagnostics

Use for BOM handling, invalid UTF-8, invalid escapes, malformed literals, keyword misuse, and logical-line issues.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/SourceText/**`

## Spec Fallback

Open `SPECIFICATION.md` chapter `4` and Appendix B lexical grammar for exact token rules.
