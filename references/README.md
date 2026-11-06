# Original hardware references

`original/` preserves the initial gfx-test project, including the register-level
ILI9488 implementation, XPT2046 calibration and its original build configuration.
These are reference sources; the active ESP-IDF project is in `firmware/`.

Every original file is recorded with its byte count and SHA-256 in
`original/manifest.json`. Git stores the archived sources without line-ending
conversion so checkout does not invalidate those hashes. Do not run automatic
formatters over these archived sources.

The old root `src/` and ESP-IDF configurations were consolidated here in Git
after verifying that their archived copies were identical. Local copies are
retained and ignored so cleanup does not discard the original files. Debug logs,
device flash backups and Bluetooth scans are not part of the source archive.
