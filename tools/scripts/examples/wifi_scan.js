// @caps 0x08
// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
tos.call("wifi.scan");
var count = tos.call("wifi.ap_count");
for (var i = 0; i < count; i++) {
  var ap = tos.call("wifi.ap_get", i);
  print(ap.ssid, ap.rssi, ap.channel);
}
