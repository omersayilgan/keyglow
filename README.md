# Keyglow

![Keyglow showing the wave effect](screenshot.png)

Colour, effect, and brightness for the RGB keyboard backlight on laptops with
an ITE 8291 (rev 0.02, four-zone) controller, such as many Clevo-based
machines. One window, driven from the keyboard, following the Omarchy theme.

The effects run on the keyboard controller itself, so they keep going with the
window closed. A small user service puts them back after the lock screen,
suspend, and Omarchy's keyboard brightness keys, which make the driver repaint
in plain colour. The same service:

- **Follows the Omarchy theme.** With *Match the Omarchy theme* on, the
  keyboard takes the theme's accent colour, and takes the new one whenever
  the theme changes.

Every colour, picked or from the theme, is shown at full saturation: a
washed-out colour lights all three LEDs in each zone, and their mismatch
shows as an uneven tint. Grey theme accents become white.
- **Saves battery.** While unplugged, the light can stay as is, dim to a
  set level, or turn off. Plugging in brings back the saved level. A level
  picked by hand while unplugged holds until the power changes.

**Calibrate zones** evens out the four LED zones, whose LEDs aren't equally
strong: a mixed colour like orange can drift toward red in one zone and
yellow in another. It shows pure red, green, and blue in turn, and each
zone's matching slider is set by eye until the zones are equally bright; the
correction then applies to every colour and effect.

**Profiles** save a look (effect, colour, brightness, speed, theme matching)
under a name, to switch back to it from the window or a keybinding.

## Needs

- The `ite_8291` driver from `tuxedo-drivers-nocompatcheck-dkms` (in the
  Omarchy repository), which exposes the zones as `rgb:kbd_backlight*`.

## Install

```sh
bin/install
systemctl --user start keyglow   # once; it starts on its own from the next login
```

The package installs a udev rule that gives the logged-in session access to
the controller (`uaccess`), so nothing runs as root.

## Effects

| Effect | Colour | What it does |
|---|---|---|
| Static | yes | One colour on every key |
| Breathing | yes | Slow pulse |
| Wave | no | Colours sweep across the zones |
| Rainbow | no | All keys cycle through the hues |

## Shortcuts

| Key | Action |
|---|---|
| `1` – `4` | Pick an effect |
| `Space` | Turn the light on or off |
| `←` `→` | Brightness down or up |
| `[` `]` | Slower or faster |
| `C` | Next preset colour |
| `T` | Match the Omarchy theme on or off |
| `P` | Next profile |
| `Ctrl+S` | Save the current look as a profile |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / redo |
| `?` | Show the shortcuts |
| `Q` | Quit |

## Command line

```sh
keyglow status --json
keyglow set --mode breathing --color '#9b4dff' --brightness 60 --speed 4
keyglow set --toggle
keyglow set --follow-theme on
keyglow set --battery-saver dim --battery-brightness 20
keyglow set --zone-trim 2:100,85,100   # zone 2: 85% green
keyglow profile save Night
keyglow profile next     # good on a keybinding
keyglow profile list --json
keyglow apply        # send the saved lighting again
keyglow daemon       # what keyglow.service runs
```

Each command exits 0 on success, or 1 with the reason on stderr. The state
lives in `~/.config/keyglow/state.json` and profiles in
`~/.config/keyglow/profiles.json`.

## Develop

```sh
bin/build   # build/keyglow
bin/test    # Qt Test, offscreen, against a fake sysfs tree
```
