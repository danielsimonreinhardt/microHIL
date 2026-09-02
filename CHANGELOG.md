# Changelog

## Unreleased

- Projektgerüst angelegt (firmware/, host/, docs/)
- STM32CubeMX-Projekt `firmware/microHIL_fw` für STM32F446RET (CMake-Toolchain) generiert
  - USB OTG FS als Device mit CDC-Klasse (Virtual COM Port)
  - Pinbelegung: 4x Relais, 8x digitaler Eingang, 8x digitaler Ausgang, 4x Analogeingang, 2x Analogausgang (DAC), 2x schaltbarer 12V-Ausgang mit Strommessung, CAN1/CAN2 (Pins reserviert, Ansteuerung folgt später)
