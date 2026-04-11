# Master Control Orchestration Server — Tron UI Theme

![accent](https://img.shields.io/badge/accent-#00F6FF-00f6ff?style=flat-square) ![type](https://img.shields.io/badge/type-Bahnschrift%20SemiCondensed-031018?style=flat-square) ![corner radius](https://img.shields.io/badge/corner%20radius-0-00aacc?style=flat-square)

The product is intentionally Tron-styled: cyan-on-blue-black, hard edges, 
Bahnschrift type, and motion as feedback rather than decoration. The shell and 
the browser admin UI share the same palette so a screenshot of one looks like a 
screenshot of the other.

---

## Palette

| Token | Hex | Use |
| --- | --- | --- |
| `ShellAccentBrush` | `#00F6FF` | Primary accent — borders, active states, CTAs |
| `ShellAccentBrightBrush` | `#7DFFFF` | Hover and pressed accent |
| `ShellHotGlowBrush` | `#2200F6FF` | Diffuse glow under hot elements |
| `TextPrimaryBrush` | `#E6FCFF` | Body and label text |
| `TextMutedBrush` | `#8CB7C4` | Secondary text |
| `TextFaintBrush` | `#6A8B95` | Disabled / placeholder |
| `SuccessBrush` | `#1CF2C1` | OK states |
| `WarningBrush` | `#FFC857` | Caution states, 4xx events |
| `DangerBrush` | `#FF6A80` | Failure, 5xx events |
| `PanelFillBrush` | `#E0060A10` | Section panels |
| `CardFillBrush` | `#E00A1018` | Cards inside panels |
| `CardEdgeBrush` | `#5A00E8FF` | Card outlines |

**Background gradient:** `#010205 → #031018 → #041C2A → #060A10`, top to bottom. 
Above it sits a 42px × 42px cyan grid at 8% opacity, mask-faded toward the bottom.

---

## Typography

| Style | Font | Size | Tracking |
| --- | --- | --- | --- |
| Hero | Bahnschrift SemiCondensed | 28pt | normal |
| Section header | Bahnschrift SemiCondensed | 17pt | 80% |
| Eyebrow | Bahnschrift SemiCondensed | 12pt | 160% |
| Body | Bahnschrift SemiCondensed | 14pt | normal |
| Data | Consolas | 13pt | normal |

---

## Hard rules

- **No corner radii.** Every `CornerRadius` in the shell and every `border-radius` in CSS is `0`. The product reads as Tron, not Fluent.
- **No oval shapes.** No pill buttons, no circular avatars. If something is round, it gets rebuilt.
- **Cyan border = interactive.** Anything the operator can act on has the accent border. Static text does not.
- **Motion ≠ decoration.** Animations exist to confirm that an action happened or that the surface is alive (live clock, accent pulse, focus outline). Pure decoration is forbidden.
- **Honor `prefers-reduced-motion`.** All motion is opt-out.

---

## Shell consumption

All tokens live in [`src/MasterControlShell/App.xaml`](../../src/MasterControlShell/App.xaml). 
Implicit `Style TargetType` entries override the Fluent defaults for `TextBlock`, 
`Button`, `ToggleSwitch`, `CheckBox`, `RadioButton`, `ComboBoxItem`, and `ListViewItem`, 
and Fluent theme brushes (`TextFillColorPrimaryBrush`, `ControlFillColorDefaultBrush`, 
`AccentFillColorDefaultBrush`, etc.) are remapped to the Tron palette so any 
unstyled control still renders correctly.

## Browser consumption

[`resources/web/styles.css`](../../resources/web/styles.css) mirrors the same 
tokens as CSS variables and adds a polish layer: focus-visible outlines, accent 
pulse animation, `<dialog>::backdrop` blur, and a reduced-motion media query that 
disables every animation when the operator prefers it.

---

See also: [Architecture](Architecture) · [Telemetry & Activity](Telemetry-and-Activity) · 
[Operations](Operations)
