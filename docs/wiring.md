# Phase 1 wiring and electrical checks

This is a proposed connection reference, not a declaration that the assembly is safe to power. The physical sensor PCB marking is **JSN-SR20-Y3.0**. Y1 documents are not proof of Y3.0 behavior.

| Sensor connection | ESP32 / supply connection |
| --- | --- |
| 5V | Verified regulated 5V source compatible with the sensor |
| GND | Common supply and ESP32 ground |
| Trig | GPIO5, after confirming 3.3V logic is accepted and the sensor does not drive this line above ESP32 limits |
| Echo | 10k ohm upper resistor to divider node; node to GPIO18 |
| Divider node | 20k ohm lower resistor to common GND |

```text
Sensor ECHO ---- 10k ----+---- GPIO18
                        |
                       20k
                        |
                       GND
```

Never connect a possible 5V ECHO directly to the ESP32. GPIO numbers are not connector position numbers. Verify the exact development-board pin labels and pinout before wiring.

## Divider review before connection

The ideal equation is Vnode = Vecho * Rlower / (Rupper + Rlower).
At 5.00V, 10k/20k produces **3.33V**, not 3.00V. At 5.25V with 5% resistors in the adverse direction (9.5k upper, 21k lower), it produces about **3.61V**. Therefore this proposed divider has limited margin and is NOT automatically approved for use. Compare the worst case, including supply variation, actual resistor tolerances and transients, against the exact ESP32 electrical limits. An absolute maximum is not a normal operating target. A reviewed lower ratio (for example 10k/15k gives nominal 3.0V at 5V) or suitable level translator may be needed; verify both safe high voltage and adequate input-high level.

1. Disconnect all power. Identify resistors with the meter and check continuity, divider orientation, common ground and absence of shorts. Measure resistors out of circuit when parallel paths affect the reading.
2. Verify the supply separately and review its current capability. Do not connect USB and external 5V to the development board simultaneously without the board schematic and a reviewed power path. A separately powered sensor with shared ground is distinct from feeding external 5V into the board; review back-power paths and power sequencing too.
3. Keep the divider node disconnected from GPIO18 until its worst-case voltage is established. A multimeter may miss short ECHO peaks; use a scope or appropriate pulse measurement equipment if necessary. A low average voltage does not establish safe pulse amplitude.
4. Confirm the TRIG interface accepts 3.3V and does not expose GPIO5 to 5V. If translation is necessary, review it before connecting.
5. Only after these checks connect the reviewed interface, with power off, and then conduct supervised bench testing.

GPIO5 is a boot strapping pin on the classic ESP32. Confirm the attached circuit does not force an inappropriate reset level. If it interferes, choose a verified suitable output pin and update config.h. GPIO18 commonly serves SPI clock; reconsider the mapping in the future SD phase.

Do not attach the J5019, salvaged cells, or solar system in Phase 1. Approximately 3.6V cell voltage does not prove cell health or safety. Cell condition, protection, charging chemistry, topology and module compatibility require a separate review before designing or energizing that system.

## Hardware facts still to verify

- Exact board/module model, flash size, USB bridge, pinout and power-path schematic.
- Actual Y3.0 manufacturer documentation, supply range/current and interface mode.
- ECHO peak voltage, idle polarity, pulse timing and behavior when no target is present.
- TRIG threshold, pulse width, minimum trigger spacing and startup settling time.
- Minimum reliable distance / blind zone, maximum range, beam geometry and transducer orientation.
- Whether both transducers and the PCB are suitable for condensation and tank mounting; no waterproof rating is assumed.
- Resistor values/tolerances, divider pulse rise time, logic margins and supply variation.
- Temperature-dependent sound velocity, tank geometry, mounting offset and multipath effects.

References: [Espressif ESP32 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf), especially electrical limits and boot configurations. No verified Y3.0 datasheet has been supplied.
