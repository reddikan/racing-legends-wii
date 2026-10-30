# RACING LEGENDS

A top-down 3D hover racer for the Nintendo Wii (homebrew). Play it in Dolphin or on a real Wii with the **Classic Controller**.

Glide anti-gravity craft along elevated neon decks, lean hard into the corners, fire your boost, and spend your winnings in the garage. The later tracks need a faster craft. The look is a grungy, glowing miniature: tilt-shift blur, film grain, RGB split, soft focus and neon bloom.

## Controls (Classic Controller)

| Button | Does |
| --- | --- |
| **A** | accelerate |
| **B** | boost — unlimited, but it recharges after every use |
| Left stick / D-Pad | steer |
| **L / ZL** | lean left |
| **R / ZR** | lean right |
| **Y** | brake / reverse |
| **X** | change camera (follow / fixed top-down / close chase) |
| + | pause |
| HOME | exit |

Leaning works like F-Zero. Hold L or R with the stick pushed the same way and the craft turns much sharper. Press L or R without steering and it strafes sideways, to dodge, line up a boost pad or bump a rival.

A GameCube controller also works (A accelerate, B boost, L / R lean, Y brake, X camera, START pause).

## How it plays

- **Steering** is locked in: the craft goes where it points and keeps its speed through the turn.
- **Boost** never runs out. Each use empties the gauge, which then recharges. The HUD shows **BOOST READY** when you can fire again. Slipstreaming right behind a rival speeds up the recharge, and the Recharge upgrade shortens it.
- **Boost pads** on the straights give a quick burst of speed.
- **88 MPH**: hit 88 and the world lights up around you.
- **Rocket start**: press A just before the lights go green.
- Rivals who fall behind you find extra pace, so a lead is never safe.
- Finish in the **top 3** to unlock the next track. Every race pays credits, plus a bonus for every lap without a single bump.

## The garage

Upgrades are what make your craft faster:

- **Engine**: top speed
- **Thrusters**: acceleration
- **Mag Grip**: grip and handling
- **Recharge**: the boost comes back sooner
- **Boost Power**: stronger boosts
- **Air Brakes**: sharper leaning turns and faster strafes

There are three craft to buy (**Comet**, **Viper**, **Titan**), plus paint colors, accents and liveries.

### The hidden craft

On the title screen, enter **Up, Up, Down, Down, Left, Right, Left, Right, B, A** to unlock **FUTURE 88**, a liquid-silver arrowhead with neon-blue light trails.

## The tracks

1. **Skyline Sprint**: Orbital Gardens, a sky-garden circuit above the trees
2. **Mesa Prime**: dusk on a red planet
3. **Cryo Station**: an ice planet ring
4. **Neon Metro**: a megacity at night
5. **Magma Rift**: a volcanic moon at night

At night every craft runs its headlights.

## Sound

There's no music, just racing sounds: hover turbines (a different voice for each craft), the air brakes' hiss, the boost roar and its recharge chime, boost pads, energy-wall scrapes, impacts, the 88 mph sonic boom, countdown beeps and the crowd. All of it is synthesized when the game starts.

## Saving

Credits, upgrades, craft, paint, unlocked tracks, records and options save to `sd:/apps/racinglegends/save.dat`. In Dolphin this needs the virtual SD card (*Config > Wii > SD Card*, on by default in recent versions). A v1 save carries over: the track records reset, and the price of a v1-only car is refunded.

## Building

With devkitPro installed, `make` builds `boot.dol` and an `apps/racinglegends/` folder for a real Wii's SD card.

When pushed to GitHub, the Actions workflow builds the game in the devkitPro container. It publishes the result as `racinglegends_v4.dol` on the **v4** release.

`sh tools/run_tests.sh shots|bots|reel|fx|strafe` runs the native test harness (a software renderer plus bot drivers). No Wii is needed.

The HUD font is Bebas Neue by Dharma Type, under the SIL Open Font License (`tools/fonts/OFL.txt`).
