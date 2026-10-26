<!--
SPDX-License-Identifier: GPL-3.0-or-later

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

# Muse SDK provenance

See [Muse integration documentation](../../../../docs/muse/README.md) for runtime behavior.

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
