# COD scripts

`haeuser.cod` (buildings, terrain, production), `figuren.cod` (figures, units, ships) are
programs in a small description language. All values for buildings,
production chains, costs, graphics and units come from them. `Text.cod` / `Editor.cod` contain texts in
a different, simpler format (see below).

Status: ✅ fully specified and implemented (`engine/assets/cod_parser.*`),
tested against `haeuser.cod` and `figuren.cod`.

## 1. Encoding

Every byte is stored negated: `stored = (-plaintext) & 0xFF`. Text is Windows-1252
with CRLF. Every file whose name ends in `.cod` is decoded this way, the text files too.

## 2. Loading sequence

`haeuser.cod` and `figuren.cod` are read the same way:

1. Clear the symbol table. **Every file starts with an empty table**;
   constants from `haeuser.cod` are not visible in `figuren.cod`.
2. Enter predefined constants: object types (`HAUS=1`,
   `HAUS_PRODTYP=2`, `HAUS_BAUKOST=3`, `BGRUPPE=5`, `BGRUPPE_WARE=6`, `BAUINFRA=7` or `FIGUR=1`,
   `ANIM=2`, `FORMATION=3`), sizes (`MAXHAUS=500`, `MAXFIGUR=119`), building kinds (`BODEN=11`,
   `MEER=19`, …), goods (`NOWARE`…), sounds (`WAV_*`), infrastructure levels (`INFRA_*`) and much more
   – 398 entries for houses, 287 for figures (the complete lists: `engine/assets/src/cod_predefined.cpp`).
3. Parse the file line by line; the result is the target symbol of the line.
4. Reading the objects only handles the structural keywords `Objekt`, `EndObj`, `Nummer`,
   `ObjFill` (section 5) and take known field names (`Id`, `Gfx`, `Kind`, `Size`, …) into
   their tables. Unknown fields are ignored.

## 3. Symbols

At most **650 symbols** per file (names case-sensitive, max. 31 characters). Each symbol has
**up to 8 values**, each a number or a string (up to 63 characters), and remembers how many values
its last assignment set. Numbers are always kept twice (integer and
floating point) and computed separately: `1.3` gives integer 1 (truncated) and floating point 1.3.
An unknown identifier is created with the value 0 on first use – without an error message.

## 4. Syntax of a line

A statement ends at the end of the line (`\n`) or the end of the file. Character by character:

| Character | Effect |
|---|---|
| letter `A–Z a–z` | identifier `[A-Za-z][A-Za-z0-9_]*`, optionally `[n]` (value index). The **first** identifier of the line is the target; every further one is replaced by the value of the symbol. |
| digit | number `[0-9]+` or `[0-9]+.[0-9]*` (then floating point); a `c` right after the digits is dropped (`249c`: the History Edition's mark for a palette colour, in its TOOLS.INC and MARKT.GAD) |
| `$` | hexadecimal number, e.g. `$1F` |
| `%` | binary number, e.g. `%0101` |
| `"…"` | string (without escapes, may contain line breaks) |
| `:` or `=` | assignment begins; operator = assign |
| `,` | next value (index + 1); operator = assign |
| `+ - * / \| &` | operator for the following values |
| `@` | relative assignment (see below) |
| `;` | comment to the end of the line |
| anything else | is ignored (space, tab, CR, brackets, …) |

Peculiarities that the reimplementation reproduces exactly:

- Numbers above 32 bits stop at 2147483647 (`strtol()` of the original with a 32-bit `long`),
  the same on every platform.
- Numbers with a decimal point are read without the C library, so the locale does not matter: up
  to 15 significant digits they round like `strtod()`, further digits are dropped. The integer value
  is the number truncated toward zero and clamped to 32 bits (`ftol()`).
- **No operator precedence**: values are combined strictly from left to right.
  `A = 2+3*4` gives 20.
- An operator stays active until another one comes (`A: 1 + 2 3` = 6).
- After an operator character, all characters up to the next letter, the next digit
  or the next punctuation character are skipped – **including line breaks**. An operator at the end of a line
  therefore continues the statement on the next line.
- `-5` is not a negative number but "minus 5": without `@` the target is first set to 0,
  so the result is still −5.
- `|` and `&` compute with the integers; the floating-point field is then set from the integer.
- Integer division by 0 crashes the original (the reimplementation reports it and
  keeps the value).
- String plus string concatenates; other operators on strings have no effect.

### Evaluation

At the end of the line the target is recomputed:

1. Number of values = number of commas + 1.
2. Without `@` the target value is first set to 0; with `@` the computation continues **from the previous value of the
   symbol**. `@Gfx: +40` thus means "last `Gfx` value + 40", no matter which
   record it was in.
3. A `,` moves to the next value slot (without `@` this one is also set to 0).

## 5. Objects and records

| Statement | Effect |
|---|---|
| `Objekt: TYP` | new, nested block of type `TYP`; `Nummer` becomes 0 |
| `EndObj` | close the block; `Nummer` gets back the value of the outer block |
| `Nummer: n` / `@Nummer: +1` | selects record n within the block |
| `ObjFill: q` | copies the complete record q (incl. nested blocks) into the current one |
| `ObjFill: q, n` | copies record q into the n records starting at the current one (`ObjFill: 0,MAXHAUS` = default values for all) |
| `Feld: Werte` | sets a property of the current record |

Nested blocks (`HAUS_PRODTYP`, `HAUS_BAUKOST` in `HAUS`) belong to the current record of the
outer block. Fields set outside any block are global values (`Nahrung: 1.3`,
`Soldat: …`). Some fields are lists: `figuren.cod` contains, for example, one `Soldat:` line per
unit type; the reimplementation therefore keeps all assignments in order (`Object::history`).

`ObjFill` copies records, but **no symbols**. Subsequent `@Feld` assignments still refer
to the last written symbol value, not to the copied record.

Example from `haeuser.cod` (shortened):

```
Objekt: HAUS
  @Nummer:    0          ; default values in record 0
  Kind:       UNUSED
  Size:       1, 1
  ObjFill:    0,MAXHAUS  ; ... copy into all 500 records

  @Nummer:    +1
  HAUSWACHS = Nummer     ; remember the number of this record
  Id:         IDWEIDE+0
  Gfx:        GFXBODEN+4

  @Nummer:    +1
  ObjFill:    HAUSWACHS  ; copy record HAUSWACHS ...
  @Id:        +1         ; ... and change Id/Gfx relative to the last value
  @Gfx:       -36
```

## 6. Result for the original files

| File | Symbols | Records | Notable |
|---|---|---|---|
| `haeuser.cod` | 617 of max. 650 | 500 `HAUS` (496 distinct `Id`), 30 `BAUINFRA`, 5 `BGRUPPE` | `WAV_BAECKER`, `FAHNEKONTOR` undefined → 0 (a data patch corrects both, D-001, D-002) |
| `figuren.cod` | 371 | 119 `FIGUR`, 3 `FORMATION` | `FIGTYP_KANONTURM`, `FIGTYP_PIRATSHIP` undefined → 0 |

Cross-check: all tile IDs of the island template `NORD/big.scp` (+20000) exist as an `Id` in
`haeuser.cod`; their `Gfx` values together with `GFX/STADTFLD.BSH` produce a consistent island.

## 7. Text.cod / Editor.cod

Different format: the decoded file is split into
sections `[NAME]`, each followed by one text per line. **Open:** mapping of the lines to the places
where they are used.

## Tools

```sh
o1602-tool cod      haeuser.cod haeuser.txt     # decrypt only (UTF-8)
o1602-tool cod-dump haeuser.cod --type HAUS     # interpret and print the records
o1602-tool cod-dump figuren.cod                 # predefinitions are recognised by the file name
```
