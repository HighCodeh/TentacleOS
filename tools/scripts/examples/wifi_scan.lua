-- @caps 0x08
-- Copyright (c) 2025 HIGH CODE LLC
--
-- TentacleOS is free software: you can redistribute it and/or modify
-- it under the terms of the GNU General Public License as published by
-- the Free Software Foundation, either version 3 of the License, or
-- (at your option) any later version.
--
-- TentacleOS is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
-- GNU General Public License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

tos.call("wifi.scan")
local count = tos.call("wifi.ap_count")
for i = 0, count - 1 do
  local ap = tos.call("wifi.ap_get", i)
  print(ap.ssid, ap.rssi, ap.channel)
end
