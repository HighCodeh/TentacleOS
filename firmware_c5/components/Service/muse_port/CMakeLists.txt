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

# Minimal SDK plus guarded pairing; no app_run, board, tunnel, OTA, support uploader.
idf_component_register(SRCS
  "diagnostic/hb_muse_alloc_trace.c"
  "core/hb_muse_core.c"
  "sdk_adapter/hb_muse_pcm_adapter.c"
  "sdk_adapter/hb_muse_sdk_binding.c"
  "transport/hb_muse_wire.c"
  "transport/hb_muse_endpoint.c"
  "platform/hb_muse_platform.c"
  "platform/hb_muse_account_fetch.c"
  "platform/hb_muse_account_worker.c"
  "platform/hb_muse_store.c"
  "runtime/hb_muse_runtime.c"
  "bridge/hb_muse_rpc.c"
  "pairing/hb_muse_setup.c"
  "pairing/hb_muse_pairing.c"
  "pairing/hb_muse_pairing_gatt.c"
  "pairing/hb_muse_pairing_fragments.c"
  "token_uart/hb_muse_token_uart.c"
  "token_uart/hb_muse_token_frame.c"
  "sdk/muse_chat.c"
  "sdk/muse_account_api.c"
  "sdk/muse_chat_session.cpp"
  "sdk/muse_chat_text.c"
  "sdk/muse_text.c"
  "sdk/vm_api.c"
  "sdk/link_pairing.c"
  "sdk/pairing_signer_policy.c"
  "sdk/pairing_transcript.c"
  INCLUDE_DIRS "core" "sdk_adapter" "transport" "platform" "runtime" "bridge" "pairing" "token_uart" "sdk"
  PRIV_INCLUDE_DIRS "../../Drivers/sys_prio/include"
  EMBED_FILES "sdk/test_reply.mp3"
  REQUIRES firmware_crypto esp_timer esp_http_client esp-tls mbedtls nvs_flash esp_psram cjson noise_core minimp3 bt esp_driver_uart efuse
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE HB_MUSE_PLATFORM_CHECK_SDK_HEADERS=1)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=heap_caps_malloc" "-Wl,--wrap=heap_caps_calloc" "-Wl,--wrap=bt_osi_mem_malloc_internal" "-Wl,--wrap=bt_osi_mem_calloc_internal")

# Keep the IDF 6.1 application inside the existing OTA slots.
target_compile_options(${COMPONENT_LIB} PRIVATE -Os)
