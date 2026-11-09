# Controls

All bindings can be changed in **Controller Settings**. Bindings are shared by both editions; the action ids
are identical (`p1.a`, `p1.b`, `p1.c`, ...).

## Game

| Action | Keyboard (P1) | Keyboard (P2) | Controller |
|---|---|---|---|
| Up / Down / Left / Right | Arrow keys | W / S / A / D | D-pad, left stick |
| Button A – Shot | Z | J | A |
| Button B – Black Hole Bomb | X | K | B |
| Button C (see below) | C | L | Y |
| Start | 1 | 2 | Menu / Start |
| Insert coin | 5 | 6 | View / Back |
| Service credit | 9 | – | – |

Player 1 uses the first controller and Player 2 the second; the keyboard works alongside them.

Presets (Controller Settings › Preset):
- **Standard:** face buttons.
- **Shot on triggers:** Shot on RT, Bomb on LT.
- **Arcade stick / fight pad:** X / Y / B, or LCtrl / LAlt / Space.

### What the game reads (verified)

Both editions' input routines read three buttons per player, plus the stick, Start and coins. The board's
fourth button line is never read, so it isn't offered.

- **Button A** fires the main weapon.
- **Button B** fires the Black Hole Bomb; the bomb counter drops by one.
- **Button C:** in the **Extra Edition** it toggles an on-screen `[ PAUSE ]`. In the **Original** no effect
  was observed in play, so it's labelled "no observed function". It stays bindable, because the program
  reads it.

The test switch exists on the board, but the game's test menu doesn't open in this build (README, Known
issues), so it isn't offered.

## Frontend

| Action | Keyboard | Controller |
|---|---|---|
| Quick menu (pause) | Esc | View + Menu (configurable: L3+R3, LB+RB+Start, Guide) |
| Quick save / Quick load | F5 / F8 | bindable in Controller Settings › Frontend Hotkeys |
| Fast-forward (hold) | Tab | bindable |
| Rewind (hold, when enabled) | Backspace | bindable |
| Screenshot | F12 | bindable |
| Fullscreen | F11 | – |

In menus:
- Arrows or the D-pad move.
- Enter / A selects.
- Esc / B goes back.
- Q/E or LB/RB change pages.
- Left/Right adjusts values.
- X (or Delete) clears a binding or deletes a save.

The mouse works too, but it is never required.

When a menu closes, the game ignores every input until all buttons are released, so the button that closed
the menu never fires in the game. The same applies after loading a state or switching editions.
