# Design Document: Nano Server C demo site

> Source of truth for the visual and structural design of the demo site in `bin/`.
> Read this fully before making a design change, and update it after every change.

## 1. Brief

- **Subject:** The demo site served by Nano Server C, a static file server written in C.
- **Concept:** "Ye Olde Webbe Server": what a medieval castle's homepage would have looked like on the 1990s web, drawn with 1980s BBS-style ASCII art.
- **Audience:** Developers who build the server, open `http://localhost:8080/index.html` and want to see what it does.
- **Primary goal:** One simple page per HTTP feature the server implements, each with a button that sends a real request and shows the real answer.
- **Voice:** Plain, modern English. The medieval theme is visual; the words stay easy to read.
- **Originality level:** 4, Original.

## 2. Hard constraints from the server

| Constraint | Consequence |
| --- | --- |
| Only `.html`, `.htm`, `.css`, `.json` and `.png` get a real `Content-Type`. Everything else is `text/plain`. | No `.js`, `.gif`, `.svg`, font or `.ico` files. Scripts are inline, the favicon is a `data:` URI, textures are PNG. |
| `GET /` returns 404, and query strings become part of the file name. | Link to `index.html` explicitly. No `?v=` cache busting. |
| Paths resolve against the folder where the server was started. | Every link is **relative**, so the site works from `bin/` and from the project root. |
| One client at a time, one request per connection. | Few requests per page: one stylesheet, two small textures. Demos send requests one after another. |
| A client that leaves mid-transfer makes the server log `Broken pipe`. | Scripts read every response body completely and never cancel a fetch. |
| Every byte should come from the C server. | No CDNs, no web fonts, no frameworks, no build step. |

Tokens are CSS custom properties in `bin/styles.css`. Components are plain CSS classes.

## 3. Color tokens

One light scheme only: 1990s pages had no dark mode, and parchment is the point.

| Token | Hex | Role |
| --- | --- | --- |
| `--parchment` | `#f1e5c5` | Page background, under `images/parchment.png` |
| `--vellum` | `#e6d5a8` | Scrolls, table heads, the map, boxes |
| `--ink` | `#2a1b0e` | Body text |
| `--faded` | `#5c4529` | Secondary text, motto, ASCII castle |
| `--rubric` | `#9a1c14` | Headings: the red ink of medieval manuscripts |
| `--lapis` | `#1c3f94` | Links (1990s blue) |
| `--lapis-visited` | `#5b2a6e` | Visited links (1990s purple) |
| `--verdant` | `#1d5e26` | 2xx results in tables |
| `--gold` | `#b8860b` | Illuminated initials, frames, ornaments. Never text. |
| `--stone`, `--stone-light` | `#8c8473`, `#d9d0b8` | Bevelled frames and buttons |
| `--wax`, `--wax-black`, `--wax-text` | `#8e1515`, `#2b2b2b`, `#f6e7c1` | Wax seals for 2xx and 4xx |
| `--night`, `--led` | `#1c1209`, `#f0c040` | Marquee, hit counter |

Body background: `--wall` under `images/stone.png`. Contrast: every text color is at least 4.9:1 on `--parchment` and `--vellum`, including the darkest spots of the parchment texture. The lowest is `--verdant` on the darkest vellum (4.95:1).

## 4. Typography

| Role | Family | Usage |
| --- | --- | --- |
| Display | `--font-blackletter`: "Old English Text MT", other blackletter fonts, then Times | `h1`, `h2`, map and ring titles, illuminated initials |
| Body | `--font-serif`: Times New Roman, Liberation Serif, DejaVu Serif | All running text, the 1990s browser default |
| Code | `--font-mono`: Courier New, Liberation Mono | Scrolls, inline code, marquee, counter |
| Art | `--font-art`: DejaVu Sans Mono, Consolas, Courier New | The block-character banner and the castle: these glyphs must line up |

The blackletter stack works like a 1996 `<font face>`: it shows where the font is installed (Windows has Old English Text MT) and falls back to Times elsewhere. The layout must look right with Times.

Scale: `--step--1` 0.9375rem, `--step-0` 1.125rem (body), `--step-1` 1.375rem, `--step-2` up to 2rem, `--step-3` up to 3.2rem. ASCII art scales with `min(…, Nvw)` so it never overflows.

## 5. Spacing and layout

- Spacing scale: `--space-1` to `--space-6` (0.25rem to 2.25rem).
- Frame: `.page`, max `54rem` (an 800 × 600 screen), parchment inside an `8px ridge` stone border.
- At 46rem and above: a `12rem` map on the left, content on the right. Below that, the map sits on top in two columns.
- 1990s borders everywhere: `ridge` for frames, `inset`/`outset` for table cells and buttons, `double` gold for illumination. No rounded cards, except scrolls and seals.
- Touch targets: `--tap` (44px) for map links, ring links, buttons and inputs.

## 6. Signature element

**What:** HTTP messages written on **scrolls** (`.scroll`, vellum with wooden ends) and the status code pressed into a **wax seal** (`.seal`: red for 2xx, black for 4xx).

**Why:** The site exists to show HTTP answers. A sealed scroll is the medieval version of a response: a verdict on the outside, the message inside.

**Where:** Every chamber's demo (`.verdict` = seal + scroll), and the two seals at the top of Chamber III.

Supporting details, from the two eras: the block-character banner, the ASCII castle and the ASCII sword dividers (1980s BBS); the marquee, hit counter, "under construction" box, 88 × 31 badges and webring-style "More Chambers" box (1990s). Don't add more motifs.

## 7. Motion

Only the marquee moves, and only under `prefers-reduced-motion: no-preference`. It pauses on hover. With reduced motion it is a static paragraph.

## 8. Pages and components

| Page | Feature | Demo |
| --- | --- | --- |
| `index.html`, Home | Overview | Hit counter (this browser only, `localStorage`) |
| `pages/request.html`, 1. Request line | Request line, path → file | Free-text path field and presets |
| `pages/methods.html`, 2. Methods | HTTP methods | Sends all 7 methods plus `BREW` |
| `pages/status.html`, 3. Status codes | Status codes 200 and 404 | Asks for an existing and a missing file |
| `pages/headers.html`, 4. Headers | Response headers, `Connection: close` | Fetches the page and prints its head |
| `pages/mime.html`, 5. Content types | `Content-Type` by extension | Fetches five files of different types |
| `pages/streaming.html`, 6. Streaming | `Content-Length`, 4 KB streaming | Fetches the 2 MB PNG and counts pieces |

Every page shares the same frame: `.banner`, `.map` (with `aria-current`), `.ring`, `.colophon`. Copy it exactly from an existing page. Each chamber ends with a `.forge` box naming the C functions behind the feature.

| Class | What it is |
| --- | --- |
| `.art`, `.art--castle`, `.sword` | ASCII art, always `aria-hidden` |
| `.lead` | First paragraph; `::first-letter` is the illuminated initial |
| `.scroll`, `.scroll--fixed`, `.scroll__caption` | HTTP messages and code; `--fixed` for column-aligned diagrams |
| `.verdict`, `.seal`, `.seal--error`, `.seal--waiting` | Demo result: seal plus scroll |
| `.button`, `.controls`, `.field` | Bevelled 1996 buttons and inset inputs |
| `.ledger`, `.table-wrap` | `<table border>` style tables, scrollable on phones |
| `.marquee`, `.construction`, `.counter`, `.badges`, `.ring` | 1990s homepage furniture |
| `.marginalia`, `.portrait`, `.forge` | Side notes, framed picture, "Forged in" code reference |

Demo scripts share the same helpers (`summon`, `head`, `stamp`): read the whole body, rebuild the head in the server's header order, press the seal. Elements that need JavaScript start `hidden` with `data-needs-script`, and a `<noscript>` note explains why.

## 9. Copy voice

Plain, modern English everywhere: page titles name the feature ("Status Codes", not a themed name). Technical facts stay exact: real header names, real byte counts, real function names in `<code>`. Buttons start with ⚔ and say what they do. No exclamation marks, except the one in the home page greeting.

## 10. Decision log

| Date | Decision | Rationale |
| --- | --- | --- |
| 2026-10-08 | First redesign: "the page is the response", level 4. | Replaced a generic beige retro template with a design built from HTTP message anatomy. |
| 2026-10-08 | Probe tour, `bin/samples/`, relative links. | Visitors can test each MIME type and the 404 from the page itself. |
| 2026-10-08 | **Redesign** to "Ye Olde Webbe Server". The previous direction is archived below. | The user wanted an old-school 1980s–1990s web style with a medieval theme, and one simple page per HTTP feature instead of a single long page. |
| 2026-10-08 | ASCII art from `toilet -f pagga` for the banner, hand-drawn castle and swords. | No blackletter font is installed on the target system, and web fonts would be served as `text/plain`. Block-character art is authentic to 1980s BBSes and needs no font. |
| 2026-10-08 | Generated `images/parchment.png` and `images/stone.png` (seamless tiles). | Tiled backgrounds are the 1990s look, and PNG is the only image type the server labels correctly. |
| 2026-10-08 | Switched all copy to plain English, renamed the chambers after their feature, and removed the tutorial page. | The mock-archaic English was hard to read, and the C library tutorial had nothing to do with HTTP. |

<details>
<summary>Previous direction: "The page is the response" (archived)</summary>

Cool gray paper with the T568B Ethernet pair colors, system monospace for protocol and labels, system sans for prose, light and dark schemes. Signature: each page opened with the anatomy of the HTTP response that delivered it (request line, giant status line, headers with `\r\n`, a double rule for the empty line). Tokens:

| Token | Light | Dark |
| --- | --- | --- |
| `--paper` | `#eef0f2` | `#0e1014` |
| `--surface` | `#ffffff` | `#161920` |
| `--sunk` | `#e2e5e9` | `#1d2129` |
| `--ink` | `#111317` | `#e7e9ec` |
| `--muted` | `#555b64` | `#9ba2ad` |
| `--accent` | `#a63909` | `#ff8a4c` |
| `--link` | `#1a4fd6` | `#8ab0ff` |
| `--ok` | `#1d6a2e` | `#6bd07f` |
| `--error` | `#c0262d` | `#ff7b7b` |

</details>
