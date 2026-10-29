# RACING LEGENDS

A fun, top-down 3D drift racer for the Nintendo Wii (homebrew). Play it in Dolphin or on a real Wii with the **Classic Controller**.

Drift through the corners, chain nitro boosts, and spend your winnings in the garage — the later tracks need a faster car.

## Controls (Classic Controller)

| Button | Does |
| --- | --- |
| **R / ZR** | accelerate (the analog R trigger works as a throttle) |
| **L / ZL** | brake / reverse — **hold it while steering to drift** |
| Left stick / D-Pad | steer |
| **A** | nitro boost (uses one tank) |
| **B** | hop — hop then hold L to start a drift at lower speed |
| **X** | change camera (follow / fixed top-down / close chase) |
| **Y** | horn |
| + | pause |
| − | put your car back on the track |
| HOME | exit |

A GameCube controller also works (A nitro, B hop, X camera, Y horn, R/L analog triggers, START pause, Z reset).

## How it plays

- **Drifting** charges up as you slide: blue sparks, then orange, then purple. Let go of L for a mini-turbo — the bigger the drift, the bigger the boost — and some nitro back in the tank.
- **Nitro** comes in tanks. Drifts and slipstreaming right behind a rival refill them.
- **Rocket start**: hit the throttle right as the lights go green.
- Finish in the **top 3** to unlock the next track. Every race pays credits (plus a drift bonus).

## The garage

Upgrades are what make the car faster: **Engine** (top speed), **Turbo** (acceleration), **Tires** (grip and handling), **Nitro Tank** (more boosts), **Nitro Power** (stronger boosts) and **Drift Kit** (drifts charge faster and bank more nitro). Four cars to drive (Hornet RS, Thunder V8, Baja Buggy, Apex GT), twelve paint colors, accent colors and liveries.

## The tracks

1. **Pinewood Lake** — a sunny forest circuit by a lake (asphalt)
2. **Red Rock Canyon** — a dusty desert run between red cliffs (dirt)
3. **Frostpeak Pass** — snowbound mountain roads (snow, slippery)
4. **Neon Harbor** — city streets at night: street lamps, neon, the harbor (asphalt, night)
5. **Lantern Valley** — a midnight dirt rally lit by lanterns, campfires and floodlights (dirt, night)

At night every car runs its headlights.

## Sound

No music, just racing: engines (a rally four, a big V8 and a buggy flat-twin, each with on/off-throttle layers and a 5-speed gearbox), tire squeal, gravel, nitro, wind, barrier scrapes, impacts, backfires, the turbo blow-off, countdown beeps and the crowd. All of it is synthesized when the game starts.

## Saving

Credits, upgrades, cars, paint, unlocked tracks, records and options save to `sd:/apps/racinglegends/save.dat`. In Dolphin this needs the virtual SD card (*Config > Wii > SD Card*, on by default in recent versions).

## Building

With devkitPro installed: `make` builds `boot.dol` and an `apps/racinglegends/` folder for a real Wii's SD card.

Pushed to GitHub, the Actions workflow builds the game in the devkitPro container and publishes it as `racinglegends_v1.dol` on the **v1** release.

`sh tools/run_tests.sh shots|bots|reel` runs the native test harness (software renderer + bot drivers) — no Wii needed.
