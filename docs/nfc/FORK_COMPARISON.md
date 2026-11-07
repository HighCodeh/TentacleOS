<!--
Copyright (c) 2025 HIGH CODE LLC

TentacleOS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

TentacleOS is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.
-->

# NFC functional comparison with the Flipper fork

Audit date: 2026-11-06. Reference: local `flipperzero-firmware-wPlugins`, clean
checkout at `13ed5ca8d82cbe6c681e27f0a5e1200a2c0f03ae`. TentacleOS was reviewed
as the current working tree, including uncommitted NFC changes.

This is a source and call-site audit, not an RF interoperability result. A protocol
API, a screen workflow, a typed saved-card model and a listener are separate
capabilities. DESFire has since received the implementation described below; the
other families do **not** currently have functional parity. The
comparison is complete for the scope below; implementation and physical validation
are still outstanding.

## Summary

| Family | Fork user workflow | Highboy user workflow | Required parity work |
|---|---|---|---|
| DESFire | Read card, applications, file metadata and accessible contents; save/load; identity emulation | Structured read, typed file load/save, detail pages and saved identity listener implemented | Physical interoperability acceptance remains pending |
| FeliCa | Lite and Standard reads, structured systems/services, listener and saved profiles | Structured reader, known-key Lite authentication, typed v1–4 import/v4 export and saved-profile listener | Physical RF interoperability validation |
| NTAG4xx | Version/model identification, typed files and ISO-DEP identity replay | Dedicated 28/29-byte parser, typed profiles, detailed views and saved identity replay | Physical RF validation; secure NTAG operations are outside this fork module |
| Type 4 Tag | NDEF read/write, platform-dependent creation, saved-message emulation | Typed NDEF read/load/save, confirmed write with readback, DESFire creation, saved-message listener | Hardware validation; bounded Type 4A mapping 2.0 |
| EMV | PPSE, PDOL/GPO, records, logs, extra metadata; save/load | Read workflow and typed files integrated into bank-card screen | RF acceptance on physical cards pending |

## DESFire

Fork evidence: [poller states][df-poller], [poller operations][df-ops],
[typed persistence][df-data] and [UI capabilities][df-ui]. The poller reads version,
free memory, master key settings/versions, application IDs, then application key
settings/versions and file IDs/settings. Accessible data, value and record files
have distinct read paths. Access failures must remain explicit, rather than becoming
invented zero-filled contents.

Implementation update (2026-11-06): [structured reader][hb-df-reader] and
[typed codec][hb-df-file] now replace the version-only screen probe. The reader
obtains version/free memory, key settings and versions, application IDs, file
IDs/settings and freely readable data/value/record contents. Transaction MAC
metadata and additional access-right sets are retained. Authentication-required
contents remain explicitly unavailable. The exchange supports wrapped and native
frames, bounded chaining and cancellation between exchanges.

The DESFire screen now saves each read, loads a named `.nfc` from the dumps folder,
saves a new typed copy, pages through applications/keys/files/data and starts an
identity-only ISO14443-4A listener using the saved UID/ATQA/SAK/ATS. See
[DESFire workflow and limits](README.md#desfire-structured-workflow).

The reference UI advertises `EmulateUid`, not full DESFire application emulation.
Highboy's identity mode bypasses both the legacy native responder and default
NDEF application. Full authenticated DESFire application emulation and protected
writes remain outside this fork-parity scope. Existing authentication helper APIs
are not a claim that those extensions are integrated.

The codec supports the fork's v4 field names, missing optional free-memory/flags,
absent file contents, key metadata and typed file geometry. Highboy presence
extensions distinguish inaccessible metadata from actual zeros on reload. A file
whose settings could not be read remains usable in Highboy; the fork may reject
that extended partial profile because its loader requires file settings. Resource
limits and this compatibility boundary are explicit in the subsystem README.

Host tests exercise the production reader/codec with all six file types, native
fallback, denied access, cancellation, malformed input and round trips. Separate
tests cover native/wrapped chaining, saved identity setup, RF-resource cleanup and
UI dispatch. Firmware compilation and host tests do not establish physical
interoperability: repeated reads, save/reload identity emulation and recovery on
the board remain acceptance checks.

## FeliCa

Fork evidence: [poller][fel-poller], [listener][fel-listener] and
[profile serialization][fel-data]. The reader has Lite authentication states and
Standard system/service traversal. The listener handles read/write without
encryption, RequestResponse, RequestService, SearchServiceCode and RequestSystemCode,
with separate Lite and Standard behavior. Its saved model represents Lite block
state and Standard systems/services, rather than just a flat byte array.

Highboy now uses the typed `nfc_felica` model through the protocol service.
Standard reads enumerate systems, areas, services, key versions and accessible
public blocks. Lite reads preserve the status of each of the 28 mapped blocks,
with optional known-key internal/external authentication and MAC validation.
Unknown keys and inaccessible blocks are not replaced by usable zero values.

The saved-profile listener implements polling, RequestResponse, RequestService,
SearchServiceCode, RequestSystemCode and read/write without encryption. Lite
includes session challenge/MAC state, memory permissions, write counters and
field-loss reset. Standard writes only modify existing writable public blocks.
Writes are validated before mutation and session changes are saved to a new file.
This does not implement Standard encrypted services or recover unknown keys;
those are outside the reference workflow compared here.

Typed import accepts FeliCa data-format versions 1–4 and export writes version 4.
Repeated per-system field names remain scoped to their system. Highboy adds an
explicit `Highboy key known` field to distinguish missing keys from zero keys;
legacy complete Lite profiles retain the fork's key-availability convention.
The listener additionally rejects replayed write counters. Lite discovery exposes
its system and public-service metadata even without a Standard system array.

Validation: `test_felica_profile.py` exercises the production reader, codec,
listener and crypto under ASan/UBSan, including hand-authored legacy-format
fixtures, multiple systems, denied blocks, wrong keys, invalid descriptors,
field reset and independent TripleDES MAC vectors. `test_capture_workflow.py`
and `test_st25tb_write_ui.py` cover service cleanup, storage failures and UI entry
points. P4 compilation and host tests do not establish physical RF timing:
repeated reads, field removal/re-entry and saved-profile emulation still need
validation on the V2.03 board with a physical reader.

## NTAG4xx and Type 4 are separate comparisons

### NTAG4xx

Fork evidence: [version poller][ntag-poller], [model and persistence][ntag-data]
and [UI capabilities][ntag-ui]. This module probes GetVersion, recognizes models
including 413 DNA, 424 DNA and 424 DNA TagTamper, saves the version structure and
ISO14443-4A identity, and offers identity emulation. It does not establish a
baseline of complete NTAG424 secure messaging, SDM or key administration.

Highboy now has a dedicated `nfc_ntag4xx` model, reader and typed codec. It
accepts exactly 28-byte and 29-byte GetVersion replies, preserving the optional
fabrication-key ID and checking the product-type low nibble as the fork does.
The previous shared DESFire parser accepted both lengths but discarded the
29th byte; the earlier audit's claim that the 28-byte reply was rejected was
incorrect. The dedicated parser also rejects truncated or oversized versions.

Models 413 DNA, 424 DNA and 424 DNA TagTamper are named using the same version
fields. Unknown hardware versions retain their raw data and an unknown label;
426Q is not guessed, because the fork does not implement its version mapping.
Details include identity/ATS, hardware/software fields, batch, production date,
fabrication fields and the reference model's nominal memory layout.

The NTAG4xx screen reads/saves, loads/copies typed Flipper NFC v4 profiles and
replays saved ISO14443-4A identity through the existing Type 4 target. The codec
preserves `PICC Version` and ATS interface/historical bytes. Optional Highboy
CC/NDEF capture fields preserve the earlier public NDEF reading feature without
claiming inaccessible data. Missing NDEF does not invalidate version identity;
its failure is reported separately. Identity replay does not emulate NDEF,
GetVersion, authentication, SDM or key administration, matching the fork's
`EmulateUid` feature rather than its separate generic Type 4 implementation.

Host tests `test_ntag4xx.py` and `test_ntag4xx_workflow.py` cover real codecs,
hand-authored fork-layout files, 28/29-byte replies, unknown models, invalid ATS,
duplicate fields, empty/full NDEF extensions, storage failures, optional read
failures and cancellation/RF cleanup. The shared UI test covers load/view/replay
controls. Physical reader interoperability remains pending.

### Generic Type 4 Tag

Fork evidence: [read/write states][t4-poller], [creation operations][t4-ops],
[listener][t4-listener], [persistence][t4-data] and [UI features][t4-ui]. The UI offers
full NDEF emulation and writing. Application/CC/NDEF creation is specifically
platform-dependent (DESFire paths), not an arbitrary-card formatting guarantee.
The module uses ISO14443-4A; it is not evidence of identical Type 4B coverage.

Highboy now wires [Type 4 primitives][hb-t4] into the NDEF screen with typed
Flipper v4 persistence, saved-message emulation through the [target engine][hb-t4-emu],
confirmed physical writing with readback and confirmed DESFire NDEF application
creation. `nfc_t4t.c`, `nfc_t4t_file.c` and `nfc_t4t_create.c` contain the profile,
APDU listener, codec and creation operations. Emulation preserves accepted writes
in a new numbered SD profile. Archive validation permits repeated `NDEF Data`
only for Type 4 and validates the aggregate declared length.

Bounds: Type 4A mapping 2.0, 1024-byte CC and 32766-byte NDEF payload. Physical
creation uses the reference DESFire AID/ISO identifiers and access settings;
it never deletes an existing application. Missing files can be created in the
reference application; existing files require matching geometry/access settings.
A matching CC is preserved and a blank CC can be initialized; incompatible
nonblank CC contents fail. Existing NDEF payload is preserved during completion.
RF validation remains pending; host tests exercise APDU/file semantics, creation
commands and error paths, NLEN write ordering, CC TLVs and UI confirmation.

## EMV

Fork evidence: [poller states][emv-poller], [APDU/parser operations][emv-ops],
[typed profile][emv-data] and [UI capabilities][emv-ui]. Its normal read workflow
includes PPSE, application selection, populated PDOL/GPO, AFL records, transaction
log records, last-online ATC, PIN tries and a fallback search for cardholder name.
There is no EMV listener or write operation in this reference.

Highboy's [bank-card screen][hb-emv-ui] now calls the complete
[application reader][hb-emv] for each discovered AID (up to eight). It validates
status words, nested BER lengths, AFL ranges and PDOL sizes before use. Required
read failures preserve partial metadata and report failure; optional GET DATA,
log and name queries may be unsupported without invalidating the primary read.
Missing values remain distinct from zero. The reader bounds AFL work to 128
records and logs to 16, and checks cancellation between APDUs. It reuses the
common ISO-DEP/APDU handling for chained responses and corrected Le.

The model contains AID, labels/names, PAN, holder, AIP, dates, country/currency,
priority, PIN tries, ATC/last-online ATC and decoded log entries. Track 2 can
supply PAN and expiry when separate tags are absent; discretionary track bytes
are not retained. PDOL defaults follow the fork's terminal profile, including its synthetic
amount/date values. No VERIFY or GENERATE AC command is issued.

`nfc_emv_file.c` imports/exports the fork's `Device type: EMV` version-4 text
format, including ATS and its little-endian country/currency fields. Highboy
extensions preserve availability, partial-read status, counters and logs that
the reference file format does not retain. A fork-only file cannot distinguish
an absent PIN counter from zero; Highboy-generated files retain that distinction.
The screen automatically saves individual application profiles under
`/sdcard/nfc/dumps/emv_NNNN.nfc`, and opens saved files without RF activity.

`tools/nfc_api/test_emv.py` exercises production parsing, required PDOL, long
BER GPO encoding, AFL validation, required/optional read failures, name fallback,
logs, cancellation, fork-only import, malformed documents and full/partial file
round trips under ASan/UBSan. Hardware RF acceptance remains a separate check.

## Persistence and acceptance boundaries

[NFC FILES][hb-archive] validates a document envelope and preserves accepted file
bytes. It does not deserialize these families into listener-ready protocol models.
Native Highboy captures preserve the fields actually obtained by the current
reader; they are not complete Flipper profiles. Existing archive tests therefore
do not establish typed interoperability or complete fork-file acceptance.

For each family, close implementation only after testing the complete path:
read or import -> typed model -> display -> save -> reload -> supported action.
Add fork-format fixtures with partial/optional data and synthetic APDU traces,
then test stop/restart and repeated physical interactions. A passing host suite or
firmware build alone cannot certify RF parity.

[df-poller]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/mf_desfire/mf_desfire_poller.c
[df-ops]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/mf_desfire/mf_desfire_poller_i.c
[df-data]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/mf_desfire/mf_desfire.c
[fel-poller]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/felica/felica_poller.c
[fel-listener]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/felica/felica_listener.c
[fel-data]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/felica/felica.c
[ntag-poller]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/ntag4xx/ntag4xx_poller.c
[ntag-data]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/ntag4xx/ntag4xx.c
[ntag-version]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/ntag4xx/ntag4xx_i.c
[t4-poller]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/type_4_tag/type_4_tag_poller.c
[t4-ops]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/type_4_tag/type_4_tag_poller_i.c
[t4-listener]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/type_4_tag/type_4_tag_listener_i.c
[t4-data]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/type_4_tag/type_4_tag.c
[t4-serialization]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/type_4_tag/type_4_tag_i.c
[emv-poller]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/emv/emv_poller.c
[emv-ops]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/emv/emv_poller_i.c
[emv-data]: ../../flipperzero-firmware-wPlugins/lib/nfc/protocols/emv/emv.c
[df-ui]: ../../flipperzero-firmware-wPlugins/applications/main/nfc/helpers/protocol_support/mf_desfire/mf_desfire.c
[ntag-ui]: ../../flipperzero-firmware-wPlugins/applications/main/nfc/helpers/protocol_support/ntag4xx/ntag4xx.c
[t4-ui]: ../../flipperzero-firmware-wPlugins/applications/main/nfc/helpers/protocol_support/type_4_tag/type_4_tag.c
[emv-ui]: ../../flipperzero-firmware-wPlugins/applications/main/nfc/helpers/protocol_support/emv/emv.c
[hb-service]: ../../firmware_p4/components/Applications/nfc/nfc_protocol_service.c
[hb-df]: ../../firmware_p4/components/Applications/nfc/protocols/mifare/mf_desfire.c
[hb-df-emu]: ../../firmware_p4/components/Applications/nfc/protocols/mifare/mf_desfire_emu.c
[hb-fel]: ../../firmware_p4/components/Applications/nfc/protocols/felica/felica.c
[hb-fel-emu]: ../../firmware_p4/components/Applications/nfc/protocols/felica/felica_emu.c
[hb-t4]: ../../firmware_p4/components/Applications/nfc/protocols/iso14443a/t4t.c
[hb-t4-emu]: ../../firmware_p4/components/Applications/nfc/protocols/iso14443a/t4t_emu.c
[hb-listener]: ../../firmware_p4/components/Applications/nfc/nfc_listener.c
[hb-archive]: ../../firmware_p4/components/Applications/nfc/nfc_archive.c
[hb-emv]: ../../firmware_p4/components/Applications/nfc/protocols/emv/emv.c
[hb-emv-ui]: ../../firmware_p4/components/Applications/ui/screens/nfc/nfc_bankcard_ui.c

[hb-df-reader]: ../../firmware_p4/components/Applications/nfc/nfc_desfire.c
[hb-df-file]: ../../firmware_p4/components/Applications/nfc/nfc_desfire_file.c
