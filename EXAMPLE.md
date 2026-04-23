# H1 Heading✅
## H2 Heading✅
### H3 Heading✅
#### H4 Heading✅
##### H5 Heading✅
###### H6 Heading✅

Heading Level 1 (Setext)✅
=========================

Heading Level 2 (Setext)✅
-------------------------

---

## Text Formatting✅

**Bold✅** (asterisks)

__Bold✅__ (underscores)

*Italic✅* (asterisks)

_Italic✅_ (underscores)

***Bold and Italic***

___Bold and Italic___

**Bold with _nested italic_ inside**

~~Strikethrough✅~~

==Highlight (non-standard)✅==

`Inline code✅`

---

## Paragraphs & Line Breaks

This is the first paragraph. Blank lines separate paragraphs.✅

This is the second paragraph.✅

This line ends with two trailing spaces  
so this is a manual line break in the same paragraph.

This line uses a✅<br>HTML tag for a line break.✅

---

## Blockquotes

> Single-line blockquote✅

> Multi-line blockquote.✅
>
> Second paragraph inside the same blockquote.✅

> Blockquote with **bold**, *italic*, and `code` inside.✅

> Level one✅
>
> > Level two (nested)✅
> >
> > > Level three (double nested)✅

> #### Blockquote with a heading
>
> - List item inside blockquote✅
> - Another item✅
>
> *Italic* and **bold**✅ still work.✅

---

## Lists

### Unordered (dash)✅

- Item 1✅
- Item 2
  - Nested item 2a✅
  - Nested item 2b
    - Deeply nested item✅
- Item 3✅

### Unordered (asterisk)

* Item A✅
* Item B
  * Nested B1✅

### Unordered (plus)

+ Item X✅
+ Item Y✅

### Ordered

1. First✅
2. Second
   1. Sub-item 2a✅
   2. Sub-item 2b
      1. Sub-sub-item✅
3. Third✅

### Ordered (lazy numbering — renders as 1, 2, 3)

1. First✅
1. Second✅
1. Third✅

### Task List (GFM)

- [x]  Completed task✅
- [ ]  Incomplete task✅
- [x] Another done item
  - [ ] Nested incomplete
  - [x] Nested complete

### Definition List (PHP Markdown Extra / Pandoc)

Term 1
:   Definition of term 1.

Term 2
:   First definition of term 2.
:   Second definition of term 2.

---

## Horizontal Rules

---

***

___

- - -

* * *

---

## Links

[Inline link✅](https://example.com)

[Inline link with title✅](https://example.com "Hover title text")

<https://example.com>

<user@example.com>

https://example.com (bare auto-link — supported by many renderers)

[https://example.com✅][ref1]

[Reference link with title][ref2]

[ref1]: https://example.com
[ref2]: https://example.com "Optional title"

[Implicit reference link][]

[Implicit reference link]: https://example.com

---

## Images

![Alt text](https://via.placeholder.com/150)

![Alt text with title](https://via.placeholder.com/150 "Image title on hover")

![Reference-style image][img1]

[img1]: https://via.placeholder.com/150 "Reference image title"

[![Clickable image — image inside a link](https://via.placeholder.com/150)](https://example.com)

---

## Code

Inline `code span✅`

Inline code with ``backticks inside using double✅ `` ticks``

Indented code block (4 spaces):✅

        function example() {
      return "indented block";
    }✅
Fenced code block (no language):✅

```
plain text code block
no syntax highlighting✅
```

Fenced code block with language:✅

```js
// JavaScript
function greet(name) {
  console.log(`Hello, ${name}!`);
}✅
```

```python
# Python
def greet(name):
    print(f"Hello, {name}!")✅
```

```html
<!-- HTML -->
<div class="container">
  <p>Hello world</p>
</div>✅
```

```css
/* CSS */
body {
  font-family: sans-serif;
  color: #333;
}✅
```

```bash
# Bash
echo "Hello, world!"
ls -la✅
```

```json
{
  "name": "example",
  "version": "1.0.0",
  "active": true
}✅
```

```sql
SELECT name, age
FROM users
WHERE active = true
ORDER BY name ASC;✅
```

```yaml
name: My App
version: 1.0
tags:
  - markdown
  - reference✅
```

```diff
- removed line
+ added line
  unchanged line✅
```

```mermaid
graph LR
  A[Start] --> B{Decision}
  B -->|Yes| C[Do action]
  B -->|No| D[End]✅
```

---

## Tables

| Header 1✅ | Header 2 | Header 3 |
|----------|----------|----------|
| Cell✅     | Cell  ✅   | Cell    ✅ |
| Cell✅     | Cell✅     | Cell✅     |

### Column alignment

| Left Aligned✅ | Center Aligned | Right Aligned |
|:-------------|:--------------:|--------------:|
| Left✅         | Center  ✅       | Right    ✅     |
| text✅         | text✅           | text✅          |

### Table with inline formatting

| Name✅       | Status  ✅    | Notes    ✅          |
|------------|-------------|--------------------|
| **Bold with nested italic inside**  | ✅ Active ✅ | `admin` role       |
| ~~Bob~~    | ❌ Inactive |✅ Was *lead* dev  ✅   |
| `Charlie✅`  | ⏳ Pending  | ⏳ [Pendingfile][ref1]    |

---

## Footnotes (GFM / Pandoc)✅

Here is a sentence with a numbered footnote✅.[^1]

Here is one with a named footnote✅.[^named]

Multi-line footnote✅.[^multi]

[^1]: This is the first footnote.
[^named]: Named footnotes still render as numbers.
[^multi]: First line of footnote.
    Second line, indented with 4 spaces.

---

## Heading IDs (custom anchors)✅

### My Section {#my-section}✅

Link to it: [Go to My Section✅](#my-section)

---

## Superscript & Subscript

Superscript (non-standard): X^2^

Subscript (non-standard): H~Bob~O

HTML fallback superscript: X<sup>2</sup>

HTML fallback subscript: H<sub>2</sub>O

---

## Inline HTML

<b>Bold via HTML✅</b>

<i>Italic via HTML✅</i>

<u>Underline via HTML✅</u>

<s>Strikethrough via HTML✅</s>

<mark>Highlighted via HTML</mark>

<kbd>Ctrl</kbd> + <kbd>C✅</kbd>

<abbr title="HyperText Markup Language">HTML✅</abbr>

Line break: line one✅<br>line two✅

<hr>

<details>
  <summary>Click to expand</summary>

  Hidden content revealed on click.✅

  - Works in GitHub✅
  - May not work in all renderers✅

</details>

<div align="center">Centered text via HTML div</div>

---

## HTML Comments

<!-- This is a comment. It will not render in output. -->

Text before✅ <!-- inline comment --> text after.✅

---

## Backslash Escapes

Text before \*

\**not bold\**

*not italic*\`

`not code`g

\[not a link\]

\> not a blockquote

Full list of escapable characters:

\\ \` \* \_ \{ \} \[ \] \( \) \# \+ \- \. \!

---

## Emoji Shortcodes (GFM / many renderers)

:rocket: :star: :thumbsup: :thumbsdown:

:white_check_mark: :x: :warning: :information_source:

:fire: :tada: :bug: :wrench: :eyes: :bulb:

---

## LaTeX / Math (KaTeX / MathJax renderers)

Inline math: $E = mc^2$

Inline math: $\alpha + \beta = \gamma$

Block math:

$$
\frac{-b \pm \sqrt{b^2 - 4ac}}{2a}
$$

$$
\int_0^\infty e^{-x^2}\,dx = \frac{\sqrt{\pi}}{2}
$$

$$
\begin{matrix}
1 & 2 & 3 \\
4 & 5 & 6 \\
7 & 8 & 9
\end{matrix}
$$

---

## GitHub Alerts (GFM)

> [!NOTE]
> Useful information that users should know.

> [!TIP]
> Helpful advice for doing things better or more easily.

> [!IMPORTANT]
> Key information users need to know.

> [!WARNING]
> Urgent info that needs immediate attention.

> [!CAUTION]
> Negative potential consequences of an action.

---

## YAML Frontmatter (Jekyll, Hugo, Obsidian, etc.)

Frontmatter must appear at the very top of the file, before any other content:

```yaml
---
title: My Document Title
author: Jane Doe
date: 2026-04-22
draft: false
tags:
  - markdown
  - reference
description: A complete markdown reference file.
---
```

---

## GitHub-Specific References

@username — mention a user

#123 — reference an issue or pull request

org/repo#123 — cross-repository reference

`a1b2c3d4` — reference a commit SHA

---

## Nested & Mixed Elements

> **Blockquote with a list:**
>
> 1. First item
> 2. Second item
>    - Nested unordered
>    - Another nested

- List item with a blockquote:
  > Quoted text inside a list item.

- List item with code:
  ```js
  const x = 42;
  ```

- List item with a table:

  | A | B |
  |---|---|
  | 1 | 2 |

1. Ordered item

   A second paragraph inside this list item, indented with 3 spaces.

---

## Unicode & Special Characters

© ® ™ — – … « » • § ¶ † ‡

→ ← ↑ ↓ ↔ ⇒ ⇐ ⇔

α β γ δ ε π σ Σ Ω μ λ

✓ ✗ ★ ☆ ♠ ♣ ♥ ♦

---

## Everything in One Paragraph

This paragraph contains **bold**, *italic*, ***Bold with nested italic inside***, ~~strikethrough~~, ==highlight==, `inline code`, a [link](https://example.com), an auto-link https://example.com, a footnote[^1], superscript X^2^, subscript H~2~O, and an emoji :rocket: all in one line.
