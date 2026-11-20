# Adaptive Sampling and Low-Power Environmental Monitoring System Using ESP32 and BME280

Project documents, wiring guide, and a first test sketch for a single-node environmental monitor.
An ESP32 reads a BME280 over I2C and compares fixed-rate sampling with a lightweight adaptive sampling rule.

## Status

- Chapters 1 to 3 and Appendix A: draft
- Chapters 4 and 5 (measurements, conclusion): not written yet

## Structure

| Folder | Content |
|---|---|
| `docs/` | Chapters 1 to 3, Appendix A (component selection details), progress-report slides |
| `figures/` | Figures 3.1 to 3.7 used in Chapter 3 (PNG) |
| `hardware/` | `wiring.md`: how to connect the ESP32 and the BME280 on a breadboard |
| `firmware/i2c_check/` | Arduino sketch that scans the I2C bus and reads the BME280 chip ID |
| `qa/` | Question bank and suggested answers for the oral defense (Vietnamese) |

## Notes

- Values in square brackets, such as `[10 s]` and `[X %]`, are placeholders. Replace them with your measured data from the actual circuit and firmware.
- If a value has not been measured yet, write "planned" or "target" instead of guessing.
- Keep your final answers in your own words during the oral defense.
