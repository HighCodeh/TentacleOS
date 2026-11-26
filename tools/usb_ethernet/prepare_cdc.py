#!/usr/bin/env python3
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

"""Generate the pinned CDC-NCM notification compatibility source."""
import argparse
import hashlib
from pathlib import Path

SOURCE_SHA256 = "88bd7d7ffa42f6f5c76f7ec9880f7cbc63a4aabb6e480dc6af0a72a7277b6617"
SPEED_CASE = """        case USB_CDC_NOTIF_CONNECTION_SPEED_CHANGE:
            // NCM rate information does not change link state or require a reply.
            if (notif->wLength != HIGHBOY_NCM_SPEED_BYTES ||
                transfer->actual_num_bytes < sizeof(*notif) + HIGHBOY_NCM_SPEED_BYTES) {
                ESP_LOGW(TAG, "Malformed CDC connection speed notification");
            }
            break;
"""

def prepare(source, destination):
    raw = source.read_bytes()
    if hashlib.sha256(raw).hexdigest() != SOURCE_SHA256:
        raise SystemExit("CDC source changed; review the NCM notification compatibility patch")
    text = raw.decode()
    marker = "        case USB_CDC_NOTIF_RESPONSE_AVAILABLE:"
    if text.count(marker) != 1:
        raise SystemExit("CDC notification patch location is ambiguous")
    text = text.replace(marker, SPEED_CASE + marker)
    marker = "static void notif_xfer_cb(usb_transfer_t *transfer)\n{"
    text = text.replace(marker, "#define HIGHBOY_NCM_SPEED_BYTES 8\n\n" + marker)
    marker = "        cdc_notification_t *notif = (cdc_notification_t *)transfer->data_buffer;"
    text = text.replace(marker, """        if (transfer->actual_num_bytes < sizeof(cdc_notification_t)) {
            ESP_LOGW(TAG, "Truncated CDC notification");
            goto resubmit_notification;
        }
""" + marker)
    text = text.replace("        // Start polling for new data again", "resubmit_notification:\n        // Start polling for new data again")
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_text() != text:
        destination.write_text(text)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    prepare(args.source, args.destination)
