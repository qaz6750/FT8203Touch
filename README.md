# Focal Tech FT8203 Touch Controller Driver for Windows

KMDF HID miniport driver for FocalTech FT8203 touch controller (I2C), targeting Samsung Galaxy Tab S7 FE Wi-Fi (SM-T733).

Originally forked from [theR4K/SynapticsTouch](https://github.com/theR4K/SynapticsTouch) → [gus33000/FocalTechTouch](https://github.com/gus33000/FocalTechTouch), then rewritten for FT8203.

## Features
- Multi-touch (up to 10 points)
- Palm rejection — listens for the `WacomPenActive` shared kernel event from the Wacom pen driver, suppresses all touch input while pen is in proximity

## Disclaimer
Work in progress. Debug code present, comments may be incomplete.
