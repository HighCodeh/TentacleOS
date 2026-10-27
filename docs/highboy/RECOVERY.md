# Private paired recovery preparation

The integrated build produces applications, not a factory/full-restoration image.
`tools/highboy/recovery.py` adds an offline preparation and verification path for
reviewing restoration of a previously backed-up matching installation. It does
not flash, reset a target, generate keys or change the existing updater.

## Inputs and output

Supply the complete P4 and C5 `flash.bin` backups captured by the guarded updater,
each with its adjacent `manifest.json`, plus an app-only candidate that passes
`verify_package.py`. Both backups must cover the reviewed 8 MiB chip profile.
Use an IDF Python containing esptool; no tool/dependency is downloaded implicitly.

```sh
python3 tools/highboy/recovery.py prepare \
  --candidate integration/release/<candidate-directory> \
  --p4-backup /private/backups/p4/flash.bin \
  --c5-backup /private/backups/c5/flash.bin \
  --out integration/recovery_candidates/<new-review-directory>
python3 tools/highboy/recovery.py verify \
  integration/recovery_candidates/<new-review-directory>
```

The tool verifies backup capture hashes/lengths/targets, exact pinned partition
maps, chip/image checksums and appended SHA-256 digests for boot and selected
apps, valid/nonpending OTA metadata, candidate slot limits and nonblank assets.
It never treats a backup's self-declared "checked" status as sufficient evidence.
Image integrity checks do not authenticate signatures or verify device identity.

The private output includes both complete original snapshots, their capture
metadata, the copied verified candidate and `recovery.json`. That file records
snapshot/candidate/partition/assets hashes, selected slots and per-chip app-update
offsets. A private staging directory is verified before atomic publication;
errors leave no visible partial bundle and existing output is not overwritten.
Directories use mode 0700 and files 0600. No merged image or executable flashing
script is generated, so review cannot accidentally turn into a device operation.

Full snapshots may contain Wi-Fi credentials, companion tokens and private device
identity material. Keep them local, never publish them as release artifacts and
never move one device's snapshot onto another. Repository output is restricted to
ignored `integration/recovery_candidates/`; another explicit private directory
outside the repository is also supported. This does not prohibit someone from
manually copying the files elsewhere; their custody remains part of review.

## Remaining deployment review

`deployment_approved`, `device_identity_verified`, `security_state_verified`,
`assets_filesystem_validated` and `hardware_validated` remain false. Nonblank
assets are a presence check; the filesystem and runtime asset compatibility still
need inspection. The recovered pair's actual identities/security state, boot and
rollback behavior, per-device data handling and the full-restoration procedure
must be reviewed before any write. Secured-device provisioning and a production
release remain separate from these development snapshots. This tool does not
supply a universal image for erased devices, migrate partition layouts or enable
automatic main-branch releases.

## Verification evidence

`python3 tools/highboy/test_recovery.py` uses explicit synthetic flash/image
fixtures. It exercises both layouts, private publication, capture corruption,
wrong partitions, blank assets, pending/invalid OTA state, slot-1 selection,
missing peers, symlinks, tampered plans, fabricated approval, candidate corruption
and image-validator failures. The test's image validator is a fixture.

With a built candidate and the matching IDF Python, also run:

```sh
python3 tools/highboy/test_recovery_images.py integration/release/<candidate-directory>
```

This invokes the real esptool parser on both built apps and synthetic image
containers/flash snapshots, checks corruption and rejects crossed chip IDs. The
containers are intentionally non-bootable and assets are intentionally not a
filesystem. No healthy device backup, physical boot or restoration result is
inferred from either test suite.
