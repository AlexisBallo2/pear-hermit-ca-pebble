# Pear Hermit

An analog watchface for Pebble Time 2 (Emery) with activity tracking and full color customization.

![Pear Hermit Showcase](screenshots/pear-hermit-showcase.gif)

## Features

- Analog clock with hour numbers (1-12) in ZenDots font arranged in a rounded-rectangle layout
- Configurable second hand (per-second tick)
- Two hand rendering modes: opaque (filled) and transparent (outlined)
- Activity tracker with 7 display types, cycled by shaking the watch:
  - Step count
  - Active time (H:MM)
  - Calories burned
  - Distance walked (miles or kilometers)
  - Heart rate
  - Battery level
  - Digital time (12h/24h)
- Date display in a bordered box
- Fully configurable via phone settings (Clay):
  - Background color (64-color palette)
  - Dial / second hand color
  - Hour/minute hands color
  - Transparent hands toggle
  - Second hand toggle
  - Tracker text size (Normal / Large)
  - Distance units (Miles / Kilometers)
- All settings persist across reboots

## Building

Requires the Pebble SDK.

```
pebble build
pebble install --emulator emery
```

## Screenshots

| Default | Navy / Yellow | Forest | Inverted |
|---------|--------------|--------|----------|
| ![](screenshots/shot1.png) | ![](screenshots/shot2.png) | ![](screenshots/shot3.png) | ![](screenshots/shot4.png) |

## License

MIT
