-- @caps 0x08
-- Copyright (c) 2026 HIGH CODE LLC
-- SPDX-License-Identifier: GPL-3.0-or-later
tos.call("wifi.scan")
local count = tos.call("wifi.ap_count")
for i = 0, count - 1 do
  local ap = tos.call("wifi.ap_get", i)
  print(ap.ssid, ap.rssi, ap.channel)
end
