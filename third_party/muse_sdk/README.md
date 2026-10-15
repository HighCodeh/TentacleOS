# Muse SDK provenance

Imported SDK source originates from `facebookincubator/muse-gadget-sdk`, commit
`b9008abba7dc4109c66212b9b82e459d08b98b85`. The original Apache-2.0 license is
provided alongside this file. Copyright notices remain in individual files.
The official P4 pixel renderer is unchanged from that pinned source.

The High Boy port is not the stock SDK firmware. Local adapters modify the C5
chat, account, pairing, allocation and transport integration, use physical
authorization and bounded voice-note capture, and omit the SDK application,
support uploader, tunneling and OTA entrypoints. Imported local file hashes and
origins are recorded in `integration/import.json`; compare those paths to the
pinned SDK for the pre-existing port delta.

The MP3 decoder has its own license in
`firmware_c5/components/Service/minimp3/LICENSE`. Other vendored dependencies
retain their notices. TentacleOS remains governed by the repository's GPL
license. These notices do not imply Meta endorsement or a completed release
licensing audit.
