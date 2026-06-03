# SafraSense Implementation Notes

This file records implementation details, fragile constraints, and debugging
patterns that are easy to miss when changing SafraSense firmware behavior. Keep
new notes here when a feature has non-obvious memory, timing, browser, protocol,
or hardware constraints that future agents should not rediscover from scratch.

When adding a note, prefer concrete symptoms, causes, safe patterns, and exact
files or commands over broad advice.

## Documentation module (src/docs/)

All user-facing documentation lives in `src/docs/docs.cpp` as PROGMEM strings.
Four accordion sections (SafraSense, Raiznet, Hidroponia, Glossário) are served
from the same source to both the local dashboard (`/docs`) and the captive portal
(`/docs`). The shared API is:

- `appendDocsContent(String& out, Language lang)` — renders a TOC block followed
  by the four accordion sections; both portals call this and wrap it in their own
  shell HTML.
- `buildDocsPortalPage(Language lang)` — complete standalone HTML page with inline
  CSS and JS for the captive portal.

**i18n structure.** Content is organized around `struct DocLang`, one instance per
language (`DOCS_PT`, `DOCS_EN`, future `DOCS_ES`). Each instance holds:
- Short navigation strings: `page_title`, `page_subtitle`, `back_link`, `toc_title`,
  `s1_title`–`s4_title`.
- Body pointers: `s1_body`–`s4_body`, each pointing to a `static const char[]
  PROGMEM` array with inner HTML.

`getDocLang(Language)` returns the right struct via a switch. `LANG_PT` returns
`DOCS_PT`, `LANG_EN` returns `DOCS_EN`, and untranslated languages currently fall
back to `DOCS_EN`. To add a translation: add a `DocLang` instance with the
translated strings and bodies, then add a `case LANG_XX: return DOCS_XX;`.

**TOC and anchor navigation.** `appendDocsContent()` renders a `.doc-toc` box
before the sections. Each `<details>` element gets an `id` (`doc-s1`…`doc-s4`).
A small inline script (in both `DOCS_THEME_JS` and `DOCS_FOOTER_HTML`) listens
to clicks on `.doc-toc a` and sets `details.open = true` so the target section
opens when navigated to via anchor link.

**Adding content:** edit only `docs.cpp`. No changes to `http_local.cpp` or
`wifi_setup.cpp` are needed unless navigation or shell structure changes.

**CSS for the docs sections** is in:
- `LOCAL_PORTAL_CSS` in `http_local.cpp` (`.doc-section`, `.doc-body`, `.doc-toc`,
  etc.) — uses local portal CSS variables (`--primary`, `--bg-inset`).
- `DOCS_PORTAL_CSS` in `docs.cpp` (standalone version for the captive portal) —
  defines its own CSS variables and mirrors all doc-specific classes.

Keep both in sync when adding new doc-specific CSS classes. The `.doc-toc` styles
intentionally differ slightly (background token) between the two portals.

Flash cost at time of writing: ~38 KB above the pre-docs baseline (74.9 % of
1,966,080 bytes). The glossary section alone accounts for ~18 KB of that.

## Captive portal HTML size

The WiFiManager captive portal is sensitive to large inline HTML. The identity
section disappeared more than once when the mnemonic QR code was embedded
directly in the page:

- Inline SVG QR code made the page too large.
- Inline QR bit strings in `data-*` attributes also made the page fragile.
- Large identity HTML inside a `WiFiManagerParameter` made the section vanish.
- Large inline JavaScript in `setCustomHeadElement()` also made the page fragile.

Keep the initial HTML small. The current pattern is:

- Put only a small `<div id="identity-root"></div>` placeholder in
  `headerHtml`.
- Serve the full identity section from `/identity/section?lang=...`.
- Keep the custom head to CSS plus `<script src="/portal.js"></script>`.
- Serve the large portal JavaScript from `/portal.js`.
- Render an empty QR `<canvas>` in the endpoint-rendered identity section.
- Fetch `/identity/current?lang=...` after the page loads.
- Return only JSON with `mnemonic`, `qrSize`, and compact `qrBits`.
- Draw the QR in browser JavaScript.
- Generate the smallest QR version that fits, currently trying versions 6
  through 12 with low error correction and explicit byte-capacity checks before
  calling the QR library.

Do not move the QR matrix, full identity section, or large JavaScript back into
`headerHtml` or `setCustomHeadElement()` unless the whole portal is re-tested on
the ESP.

## Seed generation timing

The mnemonic draft must not be generated before the Wi-Fi captive portal is
active. ESP32 random quality depends on hardware entropy availability, and the
RF subsystem being active is the condition we rely on here.

Current flow:

- `setupWifi()` loads identity state but clears the portal draft mnemonic.
- `/identity/current` generates the first draft seed only when the portal page
  requests it.
- `/identity/reroll` generates a new draft seed on demand.
- The draft is persisted only when the user saves the Wi-Fi configuration.

Keep `generateOwnerIdentity()` out of early setup paths unless a new entropy
source is explicitly enabled and documented.

## Persistence boundary

`generateOwnerIdentity()` and `importOwnerIdentity()` should not write to NVS by
themselves. They only mutate the in-memory `DeviceIdentity`.

Persist identity with `saveIdentity(id)` only after the user confirms and saves
the initial configuration. This is what makes reset/configuration startup create
a fresh draft while still saving the final chosen identity.

## Language-specific wordlists

The identity screen needs to follow the selected portal language, including the
BIP-39 wordlist. The browser stores the selected language and calls
`/identity/current?lang=...`; the firmware regenerates the draft if the language
changes.

Be careful when changing this: regenerating on language change also changes the
underlying entropy and words, not just their display labels.

## Save modal and button behavior

Only the real Wi-Fi save submit should open the save confirmation modal. Identity
buttons such as copy, save QR, and reroll are `type="button"` and must not be
treated as submit actions.

The button press effect is a brief scale-down transform. Avoid reintroducing
active color changes on the identity screen unless the whole button style is
reviewed across the portal.

## QR save behavior on mobile

Android can download the generated QR image from the button action. iOS Safari
usually opens the generated image in a new tab; the user can then long-press and
save it. This is expected browser behavior for the current no-backend-download
approach.

## QR recovery import

The recovery tab keeps QR decoding out of the initial HTML. The browser loads the
chosen image, draws it to a temporary canvas, resizes it to at most 240 px on the
longest side, thresholds it to a 1-bit bitmap, Base64-encodes the packed bitmap,
and posts it to `/identity/decode-qr?w=...&h=...`.

The firmware decodes the Base64 body, expands the packed bitmap into the
vendored `quirc` decoder buffer through `src/identity/qr_decode.cpp`, and then
runs QR detection. Keep the 240 px limit unless RAM usage is re-tested on the
ESP32. The `WebServer` plain body path stores request data as a `String`, so do
not send raw binary bitmap bytes directly; `0x00` bytes can truncate the body.

Firmware logs are controlled by `src/logging/logging.h` and are disabled by
default. Set `SAFRASENSE_LOG_LEVEL` at build time to enable them: `1` for errors,
`2` for warnings, `3` for info, and `4` for debug details such as payload size,
heap, and decoder state. Log tags are module names such as `qr`. Do not log
mnemonic words or QR payload contents.

## Recovery word validation and suggestions

Manual identity recovery is intentionally endpoint-driven. Do not load BIP-39
wordlists into the portal HTML or browser JavaScript; keep the wordlists in
firmware and query them from `src/identity/identity.cpp`.

Current behavior:

- `/identity/validate` returns `complete`, `partial`, word count, missing count,
  and up to 6 `suggestions` for the current prefix.
- Validation is case-insensitive; imported mnemonics are normalized to lowercase
  before saving.
- Empty import text is allowed only while advanced Raiznet connectivity is
  disabled. When enabled, the identity block controls whether saving is allowed.
- Compatible but incomplete input is yellow and keeps save disabled until 12
  words are complete.
- A word or prefix outside the selected language wordlist is red and blocks
  save.
- Duplicate mnemonic words are invalid and show a duplicate-word error.
- Validation/autocomplete requests are debounced in the browser. The current
  debounce is 120 ms.
- The browser autocompletes the active word only when the endpoint has exactly
  one suggestion and the current typed prefix is still compatible with the
  prefix that triggered the request. This prevents stale responses from
  replacing a word after the user has erased or changed its beginning.
- Backspace removes the whole current word only when that word is already a
  complete valid word. Invalid or incomplete words delete one character at a
  time so the user can correct typos.
- Suggestion chips are rendered in a fixed-height row above the textarea. The
  browser measures available width and only displays the chips that fit on one
  row, so the layout does not jump or clip a second line.
- Tapping a suggestion chip replaces only the active word and appends a trailing
  space.

Known continuation work:

- Keep suggestion requests debounced; the current validation debounce is in the
  portal JavaScript inside `setupIdentityBackupActions()`.
- Re-test Japanese and Chinese carefully before changing tokenization. The
  current implementation assumes words are separated by spaces, matching the QR
  and displayed mnemonic format used by this portal.

## Advanced Raiznet connectivity section

The initial Wi-Fi page now has a general `Configuração Inicial` title and a
`Configurações avançadas` section with the checkbox `Conectar a servidores
raiznet`.

When the checkbox is unchecked:

- The identity UI and server fields are hidden.
- The hidden server fields are disabled so they are not submitted.
- Identity validation is cleared and ignored.
- Saving Wi-Fi settings uses the simple save confirmation and must not be
  blocked by invalid or incomplete identity text.

When the checkbox is checked:

- The identity section is fetched from `/identity/section?lang=...`.
- `Nome do Sensor` stays outside the advanced section and remains visible even
  when Raiznet connectivity is disabled.
- Server settings are stored in hidden JSON inputs (`ext_servers` and
  `loc_servers`) so the portal can manage multiple entries without adding many
  WiFiManager parameters.
- Inside the advanced body, server settings are shown first under the
  `Servidores` subsection. That subsection contains `Lista de servidores
  externos` and `Lista de servidores locais` areas. The Arateki action is
  disabled while the Arateki server chip is present and is re-enabled only after
  that chip is removed.
- The mnemonic card is shown below under `Identificação`.
- Switching the identity tab from `Recuperar` back to `Criar` clears the pending
  recovery validation state, because the generated words in `Criar` are the
  active identity source.
- Identity validation and the master-key save warning become active.

The DOM manipulation here is intentionally explicit. WiFiManager renders custom
parameters with extra `<br>` elements and applies default padding to `div`
elements. The portal keeps the server UI browser-side, renders chips from the
hidden JSON values, and the CSS resets lateral padding on `.advanced-section`,
`.advanced-body`, `.advanced-subsection`, `.advanced-fields`, and
`.advanced-field`. Without these details, subsection widths can drift and the
advanced section can look narrower than the rest of the form.

Do not move the identity block and server fields back into a large static
`headerHtml` block. Keep the initial HTML small and let the browser assemble the
advanced section after the Wi-Fi page loads.

`/portal.js` must be served with `send_P(...)`, not `send(...)`. The portal
script is large enough that `send(...)` can force a full `String` allocation in
RAM; when that allocation fails, the browser receives no custom script and the
portal falls back to raw WiFiManager behavior: no persistent header, no custom
language handling, no loading overlay, no custom Wi-Fi select, and broken
translations.

Future HTML/JS/CSS work:

- Prefer `PROGMEM` plus `send_P(...)` for large static HTML, JS, or CSS,
  especially inside the captive portal.
- It is fine to keep small dynamic JSON or short status responses using
  `send(...)`.
- Do not convert dynamic pages mechanically. If a page mixes static markup with
  runtime values, split static chunks from dynamic values or leave it alone
  until that page is actively being changed.
- Watch `http_local.cpp` `/config` if that page grows; it currently builds a
  full HTML page in a `String`.
- Watch `/identity/current`, `/identity/reroll`, and `/identity/decode-qr` for
  heap pressure if QR payloads or response bodies grow. Their risk is dynamic
  payload size, not static asset delivery.

Connected local portal:

- The connected dashboard at `/` serves its reusable visual layer from
  `/local.css` and its dashboard behavior from `/dashboard.js`, both stored in
  `PROGMEM` and sent with `send_P(...)`.
- Keep new local-portal pages on the same tokens/classes before adding page
  specific CSS. The base vocabulary intentionally mirrors `apps/prototype`
  (`eyebrow`, serif/mono text, line-based metric cards, compact side chrome)
  while staying close to the SafraSense captive-portal colors.
- `/` and `/config` share the fixed local header from `/local.css`: brand on
  the left, `Inicio` / `Configuracoes` in the center, and the theme toggle on
  the right. Keep route-specific utility links inside `/config`, not in a
  sidebar or dashboard footer.
- The connected dashboard intentionally does not use cookies for language or
  theme. Theme uses `localStorage` first and then the browser
  `prefers-color-scheme`. Language uses `?lang=...` for server-rendered pages;
  when no query is present, the firmware reads `Accept-Language` and falls back
  to English. `LOCAL_NAV_JS` must preserve `lang` on internal links and forms so
  the next page can render in the selected language before client translation.
- The `/` dashboard intentionally does not repeat the metric readings in a
  second `Leituras atuais` section. After the status strip and metric cards, the
  page shows `Servidores` and `Sistema` only.
- On narrow mobile screens, keep the metric cards at two readings per row. The
  small-screen breakpoint reduces card padding and metric typography rather than
  collapsing to a single column.
- `/config` still has its older inline style and dynamic `String` assembly. Move
  it onto `/local.css` only when actively changing that page, because its saved
  values and form rows need careful escaping and naming preservation. Technical
  links such as `Status API`, `JSON`, and `Reconectar Wi-Fi` belong in `/config`,
  not in the dashboard.

## BIP-39 scope

The current mnemonic generation uses:

- `esp_fill_random(..., 16)` for 128 bits of entropy.
- SHA-256 of that entropy for the BIP-39 checksum.
- 12 indices of 11 bits into the selected 2048-word list.

One important limitation remains: `owner_private_key` is currently derived as
`SHA256(mnemonic)`. That is not the full BIP-39 mnemonic-to-seed derivation,
which would use PBKDF2-HMAC-SHA512. Do not describe the owner key derivation as
fully BIP-39 compliant until that is changed.

## Build/upload notes

PlatformIO is available at:

```sh
/home/yan/.platformio/penv/bin/pio
```

Build from this folder:

```sh
/home/yan/.platformio/penv/bin/pio run
```

Upload from this folder and let PlatformIO detect the port:

```sh
/home/yan/.platformio/penv/bin/pio run -t upload
```

For firmware/UI code changes in this project, the current workflow preference is
to run the build and then upload immediately after the adjustment succeeds. Do
not wait for a separate "suba" command unless the user explicitly asks to pause.

At this handoff, the last uploaded build was the local-portal mobile adjustment
with two metric cards per row on mobile. PlatformIO reported:

- RAM: `54,400 bytes` used from `327,680` (`16.6%`).
- Flash: `1,433,481 bytes` used from `1,966,080` (`72.9%`).
- Upload port: `/dev/ttyACM1`, MAC `00:70:07:26:7e:90`.

## Known Issues

**Telemetry Interval Drift:**
A bug has been reported where telemetry readings and sends occur approximately every 5 seconds, despite `TELEMETRY_INTERVAL_MS` being set to 60000 (60s). Initial attempts to fix this by ensuring `lastTelemetryMs` is initialized with `millis()` in `setup()` did not resolve the issue. The cause is currently unknown and further investigation was paused at user request.

**Metric Card Collapse:**
A bug has been identified where active/open metric cards (showing detailed help text) automatically collapse and close whenever a new sensor reading is received and updated on the dashboard. This occurs during the background refresh cycle. Investigation and fix were paused at user request.

**Sensor Initialization Delay:**
A significant delay has been observed in sensor readiness upon system startup. Sensors take a considerable amount of time to begin reporting valid data after the firmware starts. No repair was requested; this is documented for awareness.


## Forced telemetry cycle

The dashboard provides a "Fazer nova leitura" button that triggers an immediate
sensor read and data transmission, bypassing the standard interval.

- **Mechanism:** The web UI calls `POST /api/force-read`, which sets
  `gPendingAction = ACTION_FORCE_READ`.
- **Timer Reset:** In `main.cpp`, when `ACTION_FORCE_READ` is detected,
  `lastTelemetryMs` is reset to `millis() - TELEMETRY_INTERVAL_MS`. This forces
  the next iteration of the telemetry block to evaluate as true immediately.
- **Safety:** This reset only affects the sensor reading cycle. Independent
  subsystems (Wi-Fi, HTTP server, LED animations) remain unaffected as they do
  not rely on `lastTelemetryMs`.

