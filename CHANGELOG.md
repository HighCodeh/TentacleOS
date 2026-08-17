## [1.3.1](https://github.com/HighCodeh/TentacleOS/compare/v1.3.0...v1.3.1) (2026-06-04)

### Bug Fixes

* **sx1262:** make HAL re-creatable and tolerate shared SPI3 bus (fccb5271790931a47055a7e62e1cc9f02f3b7b67) - [@yajatnarayan](https://github.com/yajatnarayan)

## [1.3.0](https://github.com/HighCodeh/TentacleOS/compare/v1.2.4...v1.3.0) (2026-05-29)

### Features

* **build:** added sdkconfig.defaults and remove de sdkconfig & sdkconfig.old (502872ee5c8b774c6d7052e72fb6ef7c26940d03) - [@anarchyysm](https://github.com/anarchyysm)
* **buttons:** log button presses with name and GPIO (2a95e367ca1bfefcef9079587a3834ca8e642e78) - [@anarchyysm](https://github.com/anarchyysm)
* **c5_flasher:** SYNC handshake, 921600 baud, and ACK flow control (8eb239a61a3746f7d7f8e5f816c737a87787439f) - [@anarchyysm](https://github.com/anarchyysm)
* **license:** New license for tentacleOS (e0100702b1731f7b2a068013ee0da8f76fc238b8) - [@anarchyysm](https://github.com/anarchyysm)
* **lora/meshcore:** add BLE companion + public API (934f0460c7f70d44b379624e3ea1cf2b22a1611e) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshcore:** add contact db + flood router (eff596be627eb350d28cf8971ab4430e3b295918) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshcore:** add crypto + NVS foundation (3bc5bf7373de56c3c2b26b73533eb1a29b68b943) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshcore:** split radio (P4) and phone transport (C5) over SPI (c221420c40f47663581d81629d779d2e36d0f039) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshtastic:** add E2E PKI with X25519 + AES-CCM (7c32ea3ed055ba259b9bf352593247b65e7dc01d) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshtastic:** add ReliableRouter with ACK/NAK and CSMA jitter (491dda692bedba04cf069077241e5f7b7c9d7323) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshtastic:** add roles, NodeDB and channel settings (1197ff5ee8e2b20a5a48677dc128f6009acd9a9a) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshtastic:** bootstrap stack and enable companion path (cd0c888e817b2affaf39e71357546cde2a0031cb) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshtastic:** expand admin module (SecurityConfig, GET_CONFIG, owner) (a54c7a47dd209ea59b88a406d8ea6d9b9276b44f) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshtastic:** implement 12-state PhoneAPI handshake with QueueStatus (43f6ca580b4ab3c9a595fa78c7c8474a78522d95) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshtastic:** MQTT bridge skeleton (disabled, AP-only safe) (9bece7407f747d872adf82b949d0087808a9ff91) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshtastic:** NeighborInfo module (9218ae318233db7595dcb02e78d3f363f4e10774) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshtastic:** reliable peer discovery (boot burst, fast phase, retry) (7acf26e6f35e25e32a0f05666cff95fc7adafc0e) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshtastic:** split radio (P4) and phone transports (C5) over SPI (f0e55747efdddaa8d9131caaea7fba60c387d7e6) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshtastic:** Unishox2 compression for text port=7 (5652030de8350d9b3c2775f84995a11d2b40e1a6) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/rnode:** add command dispatcher and Reticulum command handlers (bb8451d1fbe75c569407bbe65fcc0befef0b022d) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/rnode:** add config persistence and airtime tracker (827b7b8e7af35b924c97b1e9b4276faa8d295894) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/rnode:** add KISS framing and serial transport (418ca46e6b4f8088d6460bfbf9dddebef017b3d3) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/rnode:** wire core lifecycle and CSMA listen-before-talk (5edfef2ce03bfbe1ebee47738d0bfe859b8e3482) - [@lthiagovs](https://github.com/lthiagovs)
* **lora:** add BLE NimBLE and WiFi AP/TCP transports (ecf00b1e0e74a6422329c29fece8e5f3a7e94433) - [@lthiagovs](https://github.com/lthiagovs)
* **lora:** add core - wire format, AES-CTR, TX queue, CSMA/CA (d693518551ac19e8224dc8e59c373f7ec96b584d) - [@lthiagovs](https://github.com/lthiagovs)
* **lora:** add core modules, text, routing, nodeinfo, position (8df5a8a77a6c9e3ece7ce7ed41e4f18741ad0724) - [@lthiagovs](https://github.com/lthiagovs)
* **lora:** add module dispatcher and Data protobuf parser (6731efa2ad244a091b18dd45af06c2fe00e43fe3) - [@lthiagovs](https://github.com/lthiagovs)
* **lora:** add multi-PSK channels and PacketHistory dedup (7f653ce6aa17351c4d6c21a27acd7c1c002987fc) - [@lthiagovs](https://github.com/lthiagovs)
* **lora:** add PhoneAPI FSM for config handshake (f48d26ab6a33da1394ad60e20dcd56fb9f8af2ff) - [@lthiagovs](https://github.com/lthiagovs)
* **lora:** add region tables, modem presets and device roles (09651ae21902e4af6dc722321d7df45f0734b602) - [@lthiagovs](https://github.com/lthiagovs)
* **lora:** add telemetry, traceroute and admin modules (5ec92bbd41968a3ff590cad9cc99e37190685c1e) - [@lthiagovs](https://github.com/lthiagovs)
* **lora:** scaffold component with NVS persistence (f0a472efd9bccb3c10c5669db99a57c7f2691cc4) - [@lthiagovs](https://github.com/lthiagovs)
* **rfid:** add AWID 26-bit protocol decoder (a1e1cb56fa56530573740d90339676c8c4d3ea3f) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add EM4100 protocol decoder (ce9a6184a02064c43d3357aff373d46ac702240e) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add FDX-B ISO 11784 animal tag protocol decoder (548472bff088b20657f3e2f6bd98fff4a9323e65) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add Gallagher enterprise protocol decoder (21a9993bd59bf4ee2e5b002868ae8c7b555b372f) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add HID Corporate 1000 35-bit protocol decoder (a7beed34f0731d9a8d50c659c61d4b112f830c73) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add HID Prox H10301 26-bit protocol decoder (ec8176f6d1f4dde6d24fc873bb5382e7eb172781) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add Indala 26-bit protocol decoder (5e311012baa593c5ff0b2cc0af552aa509027e3b) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add Jablotron 26-bit protocol decoder (410a9586a9d0ef961b3b6a2f258b4b946fcd1879) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add Kantech ioProx XSF protocol decoder (605e8fb9bc1f997c6a077f6ebed372c637c39ed4) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add Keri 26-bit protocol decoder (7ce19aa59c951f8f55df0d29953e44890c982712) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add Nexwatch/Honeywell QuadraKey protocol decoder (e9a90b4544c0472b3918d03b92e3d41fc56e1096) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add Noralsy protocol decoder (d5ad6c68f77235291dbc680138962591b2783274) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add PAC/Stanley 26-bit protocol decoder (37a0cf92c8640dfa005526e09d225d186c21b581) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add Paradox proprietary protocol decoder (3e17b020245764e67b790cc438427c664de616c8) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add protocol decoder interface and registry (19bc2998bc53b3a73dab7344af3a5ff8c1d17b28) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add Pyramid/Farpointe 26-bit protocol decoder (546fcbfcbbd1cf6c1e47b9421c166d2e176bd4dd) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add RFID card storage with NVS backend (d756263a2db9d795c3f38192ffcea8e080d457f7) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add RFID manager bridging driver to protocol layer (9e066e97efc3757af32f9ffb281f642087c148db) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add Securakey protocol decoder (65500e01a324bbf6768fc71491dd9c823ff37992) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add Viking 26-bit protocol decoder (e96631442451c5da13e628f901d6a21150abf59f) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add YS_RFID2 HAL UART abstraction layer (54dbef7ea7c8de567042da6236370cf58af90ff8) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add YS-RFID2 core driver with reader task (47d0f37ea7e295899b96882330da2c10da89ad9a) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add YS-RFID2 types and public API header (b1d18fddc28002d3fc051b1c181bee28635ca9f8) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** add YS-RFID2 UART stream parser (934c9ad717cad39ae454b66aac93331bd1a1e8e2) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** integrate YS-RFID2 driver into build and boot sequence (eeed33ee16e532648af383921fe385c42ed639ce) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** register all 17 protocols in registry (be4adc89d289ba5fcba9a2c48be94cb9a89b0eb2) - [@anarchyysm](https://github.com/anarchyysm)
* **rfid:** register RFID application in build system (54e46c506535dedcb22c9899d38c597ae9f02ad8) - [@anarchyysm](https://github.com/anarchyysm)
* **spi_bridge:** session lifecycle for long-running ops (b161728d70ede025836c59cbebcf55443f501f6f) - [@anarchyysm](https://github.com/anarchyysm)
* **spi_bridge:** session lifecycle, alive flag, and centralized timeouts (95c163889defd706f2ba50e3d516b051c2bf72ba) - [@anarchyysm](https://github.com/anarchyysm)
* **sx1262:** add SX1262 LoRa driver (b7b6d7874df988d9760d203f11846231772d108b) - [@lthiagovs](https://github.com/lthiagovs)

### Bug Fixes

* **about:** show ESP32-P4 on P4 firmware build (c37785ad03c21946f01b9b24dc608b8b7bbb5c41) - [@Jestriker](https://github.com/Jestriker)
* **build:** added meshcore gatt into C5 Cmake (a8431beb41f0eb1cc05dff987b4c15cfc93df41f) - [@anarchyysm](https://github.com/anarchyysm)
* **build:** hardcode IDF_TARGET in CMakeLists to prevent wrong toolchain selection (1f5cbffdd6cde68cee2a014ab0b06380b21b480b) - [@anarchyysm](https://github.com/anarchyysm)
* **ci:** specify IDF target per firmware and update container to v5.5.3 (ef5bbcb25415b0a107f9b7e238d7a790ee8c7d39) - [@anarchyysm](https://github.com/anarchyysm)
* **led_control:** no-op blink when strip not initialized (86a02be6c5ce978f4b91b4fd45b750e777767fb7) - [@anarchyysm](https://github.com/anarchyysm)
* **lora/meshcore:** companion protocol interop fixes (e1142790b06e851c2ca0f7903787923df6e0f90e) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshcore:** companion protocol interop fixes (a954704203be5bfeac125cfa0ed1ecf2ce6fa428) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshtastic:** correct protocol encoding and admin variants (7b6e56cec73515af80921a5da35d67f54434cb29) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshtastic:** correct Routing ACK encoding (error_reason field 3) (151a62b4929b71be97a17aa4c6e5cc0bfa25f167) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshtastic:** emit NodeInfo update to phone + parse want_response from Data (22c8a079dad3440b641427fc006b4bc30ab3027c) - [@lthiagovs](https://github.com/lthiagovs)
* **lora/meshtastic:** silence NimBLE GATT notify log spam (a144327d7da48f2374a4358fd91fe71518653609) - [@lthiagovs](https://github.com/lthiagovs)
* **sdkconfig:** enable FreeRTOS trace facility for P4 sys_monitor (b0a8c0833c320d228ff7a6a7c297713f086e3ff2) - [@anarchyysm](https://github.com/anarchyysm)
* **sdkconfig:** enable LVGL Montserrat fonts 12 and 14 for P4 (5670e2f90412c3f26a28d57d33a701a2278d358c) - [@anarchyysm](https://github.com/anarchyysm)
* **sys_monitor:** pin task to core 0 (C5 is unicore) (9a3eee8a7b9934ceedfb65ecba16257f744d58e1) - [@anarchyysm](https://github.com/anarchyysm)
* **tools:** correct format.sh path in error message (6bccc373b42667de18df70b75e2910eb9c73961d) - [@Jestriker](https://github.com/Jestriker)
* **tools:** use arrays instead of string concatenation in format.sh (e77db17d278457a4c5758fddcce33b6478118ba1) - [@anarchyysm](https://github.com/anarchyysm)

## [1.2.4](https://github.com/HighCodeh/TentacleOS/compare/v1.2.3...v1.2.4) (2026-04-11)

### Bug Fixes

* **subghz:** proposital typo to test github releases (1bd6d2f80f29bbb83a25f4f0ac2df0c5ade94ad1) - [@anarchyysm](https://github.com/anarchyysm)

## [1.2.3](https://github.com/HighCodeh/TentacleOS/compare/v1.2.2...v1.2.3) (2026-04-11)

## [1.2.2](https://github.com/HighCodeh/TentacleOS/compare/v1.2.1...v1.2.2) (2026-04-11)

### Bug Fixes

* **ci:** trying do puy author name to releases (68100103ee7882d2a6b91ba1c22420f5f6d70174) - @

## [1.2.1](https://github.com/HighCodeh/TentacleOS/compare/v1.2.0...v1.2.1) (2026-04-11)

### Bug Fixes

* **ci:** author names to release changelogs (06756513618de9f33abc1fc6453cf2b78da9a578) —

## [1.2.0](https://github.com/HighCodeh/TentacleOS/compare/v1.1.0...v1.2.0) (2026-04-11)

### Features

* **assets:** add infrared menu icons (e07a3e618c9061a98c1252932842a72ea16bc727) — @
* **assets:** add inter font (9860659ae175183b16b3fff0f3dd72e3061c7d56) — @
* **assets:** add new frames for coverflow menu (5064762356d7528d9a52b246d2d8814ad3d3d2f6) — @
* **assets:** add new menu icons for redesigned UI (8786bc3aea9201d03030ac75280092b065c809f3) — @
* **assets:** add octobit profile portrait (168ba34dd47d9f851c2a0dc8841420eee333af7f) — @
* **build:** register new UI components and screen include paths (a9625a015f356c226ba097631410e7a73f3aa08b) — @
* **core:** plug first boot and config loading into boot sequence (dcde322dc1b60c88a76dff8dd9cd95421abbd234) — @
* **evil-twin:** add template upload P4->C5 via SPI and password save to SD (49e474f0adf6f34c96712f4bef9c3078d1f6e7f5) — @
* **ir:** new multi-protocol ir library (02d835aed8363a9ce5afa2ece963881868d060ea) — @
* **kernel:** integrate tos_theme and tos_log into boot sequence (8b9825465fa231a5cf505efcf2e460eceb414932) — @
* **log:** add tos_log system with SD persistence and 2MB rotation (6f761670c840bc19e87fa311c9243f05b53cd6e4) — @
* **sdkconfig:** enable FAT32 LFN, increase LVGL heap to 128KB, set PSRAM (67699985f3ebfe998268072c5b51da1782199d78) — @
* **storage:** add centralized flash path headers for P4 and C5 (ec35d8bff2184bbb0c6515ea1320626efc76b99b) — @
* **storage:** add centralized loot filename generator with date prefix (c9562363071cc13c1115a90d71864a90ab0398b6) — @
* **storage:** add first boot setup with folder structure and default configs (8d8c637dc6624a83ca795f9a67b6121789979789) — @
* **storage:** add modular config system with SD/flash fallback (a500554e89bfdfeb6f675fa605288b56c22283c5) — @
* **storage:** add stream I/O API for high-thoughput continuous writes (27d8ad91c82819c8888542315aa2231fbeff3467) — @
* **storage:** add tos_theme (4364cc900c9c284f65f32ded1c9ef31aadbf8025) — @
* **storage:** add unified path registry tos_storage_paths.h (cc7f062f9590d14a45c6923d7924fee6800b2073) — @
* **storage:** add unified path registry tos_storage_paths.h (727d554e7c2f91f5f2fe86174fa938653942654d) — @
* **storage:** cross-filesystem copy, smart theme migration, sd folder structure setup (a0330e7e3f50b51e85fd5d16a5d59265ecb52011) — @
* **tools:** add automatic 3D frame rotation script (c21cc57efc1c739c80f606fc79d1e281ba394d23) — @
* **tools:** integrate frame rotation into ps1 script (2d93ac12b5d8c211173b2ff131fabc5e8477c9d6) — @
* **tools:** integrate frame rotation into sh script (6a885bad06c61ff042daee017f5e0328f9e1a8ec) — @
* **ui:** add infrared menu to coverflow (21735cc6e28e5eeb1789a0ecacc3fb679f5feb3e) — @
* **ui:** add infrared screens to ui_manager (d86d16648f16478d8d658f9d9d771d6c9aeb23b4) — @
* **ui:** add msgbox_is_open() to check visibility state (3960ac4dd36266251c8ace0a2832f2c6222a309a) — @
* **ui:** add new ir screens to ui_manager.h (89c084eb09a3e093666d1355f6ad07d13c4d99a9) — @
* **ui:** add protocol colors, .conf section parser and sd lazyload support (1306c91209f3da891cfbb499cd63e8112cbdb832) — @
* **ui:** add SCREEN_NFC_MENU and SCREEN_FILES to screen_id_t (040c14cd690a826a4ea28e75c280ee5906f825ce) — @
* **ui:** add SD lazyload with flash override and unload support (53b3d7de123e9b166ca8608b44ca9db464e9a3bf) — @
* **ui:** added nfc and files screen and increase ui task stack (fdad7bafac1cd90a0c4a0a38137a27beedc410c1) — @
* **ui:** added theme colors and input lock to interface settings screen (9f0037ee289445c570cad96bed13a612389f48cd) — @
* **ui:** files screen (683c43def38683908f9a9c363c0bbbf77debf8a8) — @
* **ui:** include theme selector screen & input lock function (33381a106952356efbdffb5486f346a18825137b) — @
* **ui:** include theme selector screen in ui_manager (c5f2e0a31054f15007c91cc568b0cfdad18a386d) — @
* **ui:** new area chart component (079074eb34c551f42479bf6a1f5226837426b18e) — @
* **ui:** new chart component (018d8744b7acab267dd9b0e348149e05981b132a) — @
* **ui:** new dropdown component (c634eec0e7c24d373ef09d83fc5222f4024c8c8d) — @
* **ui:** new infrared burst screen (6aa4f567e963b989f9bbc29647a3ad5caa03bc1e) — @
* **ui:** new infrared controller screen (6b311e3fca8d8d1c4b214be044775e2e2bddeb99) — @
* **ui:** new infrared menu screen (31ea0273ed53c21a997e25d0d5f9755fee194d50) — @
* **ui:** new infrared receive screen (9d3a7f36e1c84fc011c91e1fe8040ce39f6a419c) — @
* **ui:** new intensity bar component (da6a7e8fa2d682b4aefc8fe42d61d431bcb3c215) — @
* **ui:** new nfc menu screen (1e44ea4ee4cc465ba56e3586f9bf9407d5a95608) — @
* **ui:** new page dots component (817a3facbac8606af35df37eccf992828c2e4199) — @
* **ui:** new saved infrared signals screen (33a3fbd19e0a58e9e65e22ca02456191f1e1d4ff) — @
* **ui:** new send infrared screen (70e10828fd1305705c87dfa0f2432638f2dd25ee) — @
* **ui:** new spinner component (b85ec85699b364643b718eac7ebf2594f8557a29) — @
* **ui:** new text viewer component (eb717d877fd43f6409be15a2c7ac2b8c074d482d) — @
* **ui:** new theme selector screen (495bfd5a845216e0213e10d455b7ff84a9f0d4b6) — @
* **ui:** new toggle button component (8ebf8c3ceb8007816ab58e78f4198ba537241332) — @
* **ui:** redesign button component (b856851d1eed87873916743cecd81928c833840a) — @
* **ui:** redesign coverflow (ca4f59161a2b142749e71a5921f278b8536bef61) — @
* **ui:** redesign header component (953168a46aac2530d1aa590d29b853e4ab5b1fe2) — @
* **ui:** redesign keyboard component (ac4455e21ce01183e5a88e7527194be6c438cc49) — @
* **ui:** redesign menu component (7eb58f05645ebca00f93c3cc4d246b9b40e690de) — @
* **ui:** redesign messagebox component (cad146531974895a219c821a885c0e84bdd4f9ef) — @
* **ui:** update battery settings screen appearance (29397b41bb11b7909bae8ad15cd8bd34b3363dc7) — @
* **ui:** update connection settings screen appearance (297f616da1eace48d5774032968f0e361e46d4f6) — @
* **ui:** update display settings screen appearance (bbb10cb96a86557459784264a9d78fad1592fa1c) — @
* **ui:** update home screen appearance (db31162864445f2bfc774523123982c495e3d38c) — @
* **ui:** update interface settings screen appearance (2d7707ae5cad2d78dddacd84ebe5ffb00ad5d154) — @
* **ui:** update settings screen appearance (a0b7f0b9c80607f7d8f2d839464c9bee648b59a6) — @
* **ui:** update sound settings screen appearance (7b2713a5ba5df8d795855ece014d9cd29df4c761) — @
* **ui:** update wifi screen appearance (f4b397251c3667bb7dba13e3bfa2b559fcb8b18e) — @

### Bug Fixes

* **build:** insert storage_api, _vsf and _assets into CMAKE GLOB_RECURSIVE (1c45ee31ae6304ec9b02689b32e28412c159cab0) — @
* **cc1101:** the GDO0 and GDO2 was inverted, fixed (be0e1a1eeab9744974bfcf44016e24c3269ad67a) — @
* **p4/nfc:** add missing includes (601cf5c52fd6a9c6ff717548e2691f99c98b01c3) — @
* **ui:** load saved theme on boot (1991343bec3bb8f0b61806912d970c61ba064e2b) — @
* **ui:** missing stdio.h include (ee9ffde4e47b9b40d87f273af67cca6b19663abb) — @
* **ui:** missing stdio.h include in wifi_probe_ui (111df6f1382f0b25745755e02db582fa0dadf490) — @
* **ui:** prevent scroll animation when switching themes (6b782ac3f56f240cfdb17a7bd4f442a5b8219a7b) — @

### Reverts

* Revert "chore(clang-format): format ALL source files, firmware p4 & c5" (6ac12ea9f41a95e14c9b4a2164f5c2a05c5b2189) — @

# [1.1.0](https://github.com/HighCodeh/TentacleOS/compare/v1.0.1...v1.1.0) (2026-03-19)


### Bug Fixes

* **bridge_manager:** uses FIRMWARE_VERSION, removed cJSON, more cleaner ([1442dbb](https://github.com/HighCodeh/TentacleOS/commit/1442dbbfe55e919bba70a8274a19ec1cc2c27a8e))
* **git_actions:** limit header increased to 200 char ([6d41131](https://github.com/HighCodeh/TentacleOS/commit/6d4113183f33ddbbdc28a282316bceb589a9c018))
* **ota:** removed old load_version_from_assets(), i forget to removed, now we use sync_version_to_assets ([85753ca](https://github.com/HighCodeh/TentacleOS/commit/85753cab5dbc8713a848ef35a295aa7d9ccde1fb))


### Features

* **nfc:** add APDU command/response handling ([774e306](https://github.com/HighCodeh/TentacleOS/commit/774e3064d04a03dd863e5d4f9076ca746266665b))
* **nfc:** add card info display utility ([f78acfd](https://github.com/HighCodeh/TentacleOS/commit/f78acfdb75ea8fcc0a04eca5339243e83f2383ad))
* **nfc:** add card reader application ([bc1678c](https://github.com/HighCodeh/TentacleOS/commit/bc1678c7eaea1609533edb357a53869890ad7101))
* **nfc:** add card scanner application ([f8b853b](https://github.com/HighCodeh/TentacleOS/commit/f8b853bc97facad6e654444f1ec7d4501d946f28))
* **nfc:** add card storage ([54ecb61](https://github.com/HighCodeh/TentacleOS/commit/54ecb61da8532b5a0094f1b5bdb65c1610ea5018))
* **nfc:** add Crypto1 cipher and key utilities ([d26743f](https://github.com/HighCodeh/TentacleOS/commit/d26743fa750f4aa069a1554eef5dbea11a9f767e))
* **nfc:** add DESFire protocol and emulation ([6cdcc9a](https://github.com/HighCodeh/TentacleOS/commit/6cdcc9af2ca887daf5754e0de9718b0e29dd1bed))
* **nfc:** add emulation diagnostic tool ([2a39461](https://github.com/HighCodeh/TentacleOS/commit/2a394614f164469f0da98e47c81317b040d8648d))
* **nfc:** add emulation listener with passive target mode ([c98cfa7](https://github.com/HighCodeh/TentacleOS/commit/c98cfa7a08c0f082a1b13a076843899be4ca91a7))
* **nfc:** add EMV contactless payment protocol ([b0f6093](https://github.com/HighCodeh/TentacleOS/commit/b0f60937603975e2e976d8eac988fa3255865836))
* **nfc:** add FeliCa (NFC-F) protocol and card emulation ([35f1a1b](https://github.com/HighCodeh/TentacleOS/commit/35f1a1b61d562befc33ae22cee77e4fe6821b418))
* **nfc:** add GPIO IRQ pin abstraction ([216d1e2](https://github.com/HighCodeh/TentacleOS/commit/216d1e2b83273edea6d739d2a6f6549ca6cfdfe0))
* **nfc:** add HAL implementation ([10a66fc](https://github.com/HighCodeh/TentacleOS/commit/10a66fc53614e0890558004975becca25e6fd64a))
* **nfc:** add ISO 14443-A framing and CRC ([8f4a9a1](https://github.com/HighCodeh/TentacleOS/commit/8f4a9a1cb84ebb2ce05871d00807b5f69a78652b))
* **nfc:** add ISO 14443-B protocol and card emulation ([f4760bc](https://github.com/HighCodeh/TentacleOS/commit/f4760bcead6e4b377c3bc5aa9aaaf484ac4797ae))
* **nfc:** add ISO 15693 (NFC-V) protocol and card emulation ([5cafbbe](https://github.com/HighCodeh/TentacleOS/commit/5cafbbeb855ea7d80ff08bb28dc2d929ac9b1bec))
* **nfc:** add ISO-DEP T=CL protocol layer ([ef06dd3](https://github.com/HighCodeh/TentacleOS/commit/ef06dd380289db9264e45e4908e37b8d516567c4))
* **nfc:** add known card detection ([7cd20c6](https://github.com/HighCodeh/TentacleOS/commit/7cd20c6126d47c668439d304e33a669c985336df))
* **nfc:** add LLCP and SNEP for NFC peer-to-peer ([9209c83](https://github.com/HighCodeh/TentacleOS/commit/9209c83bc9a1d9d33b45c993510b0d9fd34a7e14))
* **nfc:** add microsecond busy-wait timer ([c4ae7cf](https://github.com/HighCodeh/TentacleOS/commit/c4ae7cf313a3eb6295947f6850332c0fce11c379))
* **nfc:** add MIFARE Classic emulation ([58450e2](https://github.com/HighCodeh/TentacleOS/commit/58450e2e9c74352139daa5ba3c36f3983c25532c))
* **nfc:** add MIFARE Classic protocol header ([0ec24a0](https://github.com/HighCodeh/TentacleOS/commit/0ec24a04d5ebb004f8428d43288f13a6b91b7b34))
* **nfc:** add MIFARE Classic read/write protocol ([2d0baea](https://github.com/HighCodeh/TentacleOS/commit/2d0baeaf29e4f61416f9ab28fee1cdc3dd9a6ab3))
* **nfc:** add MIFARE Plus support ([24b3e38](https://github.com/HighCodeh/TentacleOS/commit/24b3e38221e1c007306a728b6c151b4482ae58a3))
* **nfc:** add NDEF read/write support ([126e88e](https://github.com/HighCodeh/TentacleOS/commit/126e88ed02870616745563f1c296bb336e05affd))
* **nfc:** add nested attack for unknown keys ([5c7c538](https://github.com/HighCodeh/TentacleOS/commit/5c7c538de9b696c3f2e6db18ec5e960f5c48e252))
* **nfc:** add NFC crypto utilities ([d2d0862](https://github.com/HighCodeh/TentacleOS/commit/d2d0862675b6c564847fa1d089822f812c1ac054))
* **nfc:** add NFC debug logging utility ([0a0c829](https://github.com/HighCodeh/TentacleOS/commit/0a0c8299cd16b75d73c716177f9973d75a4ad067))
* **nfc:** add NFC device abstraction ([c5ba2ea](https://github.com/HighCodeh/TentacleOS/commit/c5ba2ea046875b79c4c86eb7328202f1044f5038))
* **nfc:** add NFC tag detection service ([8b7f090](https://github.com/HighCodeh/TentacleOS/commit/8b7f0906a3b86db0e8d220da9df1ca92194975e1))
* **nfc:** add nfc_common shared definitions ([d836cb1](https://github.com/HighCodeh/TentacleOS/commit/d836cb1ec0eef1b7f530339f0be880a42f5d3ed0))
* **nfc:** add poller and ISO-DEP headers ([2e86e21](https://github.com/HighCodeh/TentacleOS/commit/2e86e211fc9900b231be0dda54c8db6194d14218))
* **nfc:** add public API header and default config ([7b0ea29](https://github.com/HighCodeh/TentacleOS/commit/7b0ea293f8665762da18e3d416ff54fec3e84d19))
* **nfc:** add RF configuration service ([cb7faee](https://github.com/HighCodeh/TentacleOS/commit/cb7faee5d2148502bf677d40bb18536491564913))
* **nfc:** add scan manager with FreeRTOS task ([2e54dde](https://github.com/HighCodeh/TentacleOS/commit/2e54ddef402d5c8a6f1069191887c60ca076b324))
* **nfc:** add shared error codes for NFC driver ([2e2b13b](https://github.com/HighCodeh/TentacleOS/commit/2e2b13be69f8fc2b0a7fec6415a80deae02c7c32))
* **nfc:** add shared NFC data types ([f7e861c](https://github.com/HighCodeh/TentacleOS/commit/f7e861c4687a6a8d4c0ea08347a0ce24c3a8c159))
* **nfc:** add SPI communication interface ([4883084](https://github.com/HighCodeh/TentacleOS/commit/48830849df52f3657e3d27b2208cdd627e0888b5))
* **nfc:** add SPI/GPIO/timer HAL implementation for ESP32-P4 ([1dff605](https://github.com/HighCodeh/TentacleOS/commit/1dff6050e4e333ada26c8b021660c5683fb72324))
* **nfc:** add T4T card emulation ([3d5d0ff](https://github.com/HighCodeh/TentacleOS/commit/3d5d0ff2d2313fc46005dd222f71033ed65b9752))
* **nfc:** add T4T tag operations ([82da055](https://github.com/HighCodeh/TentacleOS/commit/82da055a113aab5b50deabad4a4fca637f26ff99))
* **nfc:** add Type 1 Tag protocol ([9fd0987](https://github.com/HighCodeh/TentacleOS/commit/9fd09878ec9b6f4d6a1ab7833d884e469590ce9a))
* **nfc:** add Type 2 Tag card emulation ([c239cfd](https://github.com/HighCodeh/TentacleOS/commit/c239cfd3355cb400b11d3011159c2f2310a60f9a))
* **ota:** header file responsible by identify actual firmware version ([e229b59](https://github.com/HighCodeh/TentacleOS/commit/e229b5970b63b99815e96b9a489e0d173ea39774))
* **st25r3916:** add AAT calibration with NVS cache ([157dd17](https://github.com/HighCodeh/TentacleOS/commit/157dd17dfbc76c7ce4e98e66e93ee36bc5fe419f))
* **st25r3916:** add antenna auto-tuning header ([62d627b](https://github.com/HighCodeh/TentacleOS/commit/62d627b0cad0d850e896d9dd0c9ac34b55261375))
* **st25r3916:** add core driver header with init and field control API ([cc0a025](https://github.com/HighCodeh/TentacleOS/commit/cc0a0255b92f4997c4f9d6193822aaf4e1c9d06b))
* **st25r3916:** add core driver implementation ([fa13bb0](https://github.com/HighCodeh/TentacleOS/commit/fa13bb0bfb382d6c329688f3e7a7f78fb02da7e7))
* **st25r3916:** add direct command table ([0486c3a](https://github.com/HighCodeh/TentacleOS/commit/0486c3af9a5ea4a17501a0648cb0a10a3af69ea6))
* **st25r3916:** add FIFO control header ([1665da1](https://github.com/HighCodeh/TentacleOS/commit/1665da16b9342aadd2696dfaad709e3fcc8f8f89))
* **st25r3916:** add FIFO control implementation ([cd64976](https://github.com/HighCodeh/TentacleOS/commit/cd64976df7789be0041ad556f94ae8928234e33f))
* **st25r3916:** add IRQ handling header ([8524afd](https://github.com/HighCodeh/TentacleOS/commit/8524afd751d1dc7ae05406f16e3edd01ad954a91))
* **st25r3916:** add IRQ handling with 10µs TXE polling ([b650221](https://github.com/HighCodeh/TentacleOS/commit/b650221ff6e0cfbe198d080fae983154283a4ab1))

## [1.0.1](https://github.com/HighCodeh/TentacleOS/compare/v1.0.0...v1.0.1) (2026-03-17)


### Bug Fixes

* **release:** testing release .bin generatos ([299b3fd](https://github.com/HighCodeh/TentacleOS/commit/299b3fd9da00bbce1bc69fc148684a771a705ba6))

# 1.0.0 (2026-03-17)


### Bug Fixes

* add missing ui headers ([1954f4f](https://github.com/HighCodeh/TentacleOS/commit/1954f4f16e6b5667a45fa54d1ad470c530430284))
* **assets:** bluetooth & storage icon size ([3165f77](https://github.com/HighCodeh/TentacleOS/commit/3165f773519c36744593712416b711fa0774e2e4))
* **assets:** boot logo name ([3728ed7](https://github.com/HighCodeh/TentacleOS/commit/3728ed7fde4816ae97ca290e8d1abf659bf49bd4))
* **assets:** moved file manager web interface to storage folder ([22d979f](https://github.com/HighCodeh/TentacleOS/commit/22d979f308e94eddb97eb09a69419e1513706905))
* **bad_usb/screen:** removed BT HID functions dued screens errors (idk how) ([0f19e14](https://github.com/HighCodeh/TentacleOS/commit/0f19e14323374a89089b3f8fad14c35237a596f4))
* **build:** register missing spi_bridge component ([3e313b9](https://github.com/HighCodeh/TentacleOS/commit/3e313b9a0f15b8efdf19540265677acde0f43858))
* **build:** update littlefs partition image to use temp directory ([30dbc99](https://github.com/HighCodeh/TentacleOS/commit/30dbc99e3f8d453ba70cb680989d1fd348f0104c))
* **buzzer:** gpio back to 46 ([6fb673c](https://github.com/HighCodeh/TentacleOS/commit/6fb673c96bfa4b47509ee51f6cf0de6cfa41c1c3))
* **buzzer:** migrate buzzer to LEDC timer 1 ([b1d77ec](https://github.com/HighCodeh/TentacleOS/commit/b1d77eca1523bb71e0581ab0fbcdfffaf288449a))
* **connect_wifi_ui:** align wifi list colors with current theme ([fa1005c](https://github.com/HighCodeh/TentacleOS/commit/fa1005c5a9318b37b40d3bdd9b614b6ec3f2f3bd))
* corrigido driver do chip BQ25896 e registro de status ([14ea26e](https://github.com/HighCodeh/TentacleOS/commit/14ea26e533463bc298111c0ed863210855e30156))
* **display:** prevent unnecessary BLE screen streaming when inactive ([20d4870](https://github.com/HighCodeh/TentacleOS/commit/20d487044335ecc9a7938ee3bdaaf08c025a7c88))
* **dns & evil_twin:** fixing typo and unused variables ([9bcac1e](https://github.com/HighCodeh/TentacleOS/commit/9bcac1e63a955cb997a37c1db35213dd020ce4b8))
* **docs:** update documentation to latest storage api ([aff9671](https://github.com/HighCodeh/TentacleOS/commit/aff967151f070dabcc1824733726d6b30e52841c))
* **drivers:** correct SPI signature mismatches in P4 ([afee8c0](https://github.com/HighCodeh/TentacleOS/commit/afee8c0e3acfb515cc626b82606c04854754d090))
* **evil_twin_html:** removed quotes ([1178c01](https://github.com/HighCodeh/TentacleOS/commit/1178c012d41aef9c0d0c8d25b484aa489ba3e521))
* fix battery UI rendering bug ([9a3e8b4](https://github.com/HighCodeh/TentacleOS/commit/9a3e8b4b2beeb6d65b938ec009ea121bb8a33bd2))
* **kernel:** remove wifi_stop from startup ([d7f7deb](https://github.com/HighCodeh/TentacleOS/commit/d7f7deb253661c98a91b782651b218266d0d79c2))
* **pins:** correct extra pins ([f88a85a](https://github.com/HighCodeh/TentacleOS/commit/f88a85aaeba285d5ac1d9e384f34ca61a5fe03e6))
* README ([28485f2](https://github.com/HighCodeh/TentacleOS/commit/28485f23f356ff1e3eed371ab178e046bbd340c6))
* remove flickering ([927cc00](https://github.com/HighCodeh/TentacleOS/commit/927cc00a049720f93232751b069f1ae4c572c3c5))
* remove static from get_html_buffer ([2e01abb](https://github.com/HighCodeh/TentacleOS/commit/2e01abb0605a02ab26b269024da6c2a188d45158))
* round cornners ([6098ab4](https://github.com/HighCodeh/TentacleOS/commit/6098ab4a6d3d77ccb3a7c44250c57caa0ae0da21))
* **sd_card:** correct MISO pin definition and add missing header ([26474f8](https://github.com/HighCodeh/TentacleOS/commit/26474f8ded9e213e3a1d8c70bca75033dc0de107))
* **spi:** add missing spi_bridge header ([89dfbc5](https://github.com/HighCodeh/TentacleOS/commit/89dfbc5456af8f696113cd659991530865ac9312))
* **spi:** use SPI2_HOST and removed SPI3_HOST ([8b92f59](https://github.com/HighCodeh/TentacleOS/commit/8b92f59618b278c726e5748d4ee7def44d01b5b0))
* **storage:** auto-create parent directories before write operations ([7d88e6a](https://github.com/HighCodeh/TentacleOS/commit/7d88e6af7274f50bd77b3d497de52f952396b681))
* **subghz:** remove unused variables in analyzer ([49cf02a](https://github.com/HighCodeh/TentacleOS/commit/49cf02a2e17a37ed41af22d24c39e71675f95ffb))
* **tools:** fixed python binary inside lvgl-env ([2bbbcc1](https://github.com/HighCodeh/TentacleOS/commit/2bbbcc1f3cd070b643d27377867dbec6a11adc89))
* **ui_settings:** remove extra closing braces ([451bada](https://github.com/HighCodeh/TentacleOS/commit/451badaf3e14187cfa3dc5b72dbb732d6ae4c66b))
* **ui:** added about settings to screen list ([85eb1ff](https://github.com/HighCodeh/TentacleOS/commit/85eb1ffb966cd112c5ccce7c17b2d607634d5a40))
* **ui:** boot logo bin size ([31fcdc2](https://github.com/HighCodeh/TentacleOS/commit/31fcdc22f7942949b8ce1a4e0b743e7a5da18e29))
* **ui:** fix keyboard_open call ([6c9c486](https://github.com/HighCodeh/TentacleOS/commit/6c9c486a23d86979db9f9cd41b282725f82389af))
* **ui:** implement asynchronous wifi scanning and progressive list population ([4a625b2](https://github.com/HighCodeh/TentacleOS/commit/4a625b29fbad76dd001b3c098af8e72928d8ffac))
* **ui:** interface settings item event ([939ea7a](https://github.com/HighCodeh/TentacleOS/commit/939ea7af7f650cb74f3c2110ed1e9de89af1d1a6))
* **ui:** nimble mem alloc & font size ([aebf644](https://github.com/HighCodeh/TentacleOS/commit/aebf644d784149560e4bbfa16a9463da878d5502))
* **ui:** switch footer button ([0bb4430](https://github.com/HighCodeh/TentacleOS/commit/0bb443027b8764c2058ff15ff1aeba80ee04d255))
* **vfs:** add detailed error logging to write operations ([d87745f](https://github.com/HighCodeh/TentacleOS/commit/d87745f9af863125f3bce73b71e41fd319d952b1))
* **wifi): Usado para as correções críticas dfix(wifi:** Used for critical crash fixes (allocation of the TCB to internal memory instead of PSRAM) and memory leak fixes (task cleanup) ([e2512f9](https://github.com/HighCodeh/TentacleOS/commit/e2512f9afd4cb811a8fec5a163235b5a06720e0b))
* **wifi:** missing headers on wifi sniffer ([a42b89f](https://github.com/HighCodeh/TentacleOS/commit/a42b89fc2413be1a9c6e601e769818472f3f95db))
* **wifi:** Used for critical crash fixes (allocation of the TCB to internal memory instead of PSRAM) and memory leak fixes (task cleanup) ([bb64feb](https://github.com/HighCodeh/TentacleOS/commit/bb64febaef32b3c97fe6a5896a31fa462d86cb16))


### Features

* **802.11:** LLC/SNAP header for data frames ([8976fbc](https://github.com/HighCodeh/TentacleOS/commit/8976fbcbe29ff018eb7cbe657b55edc547e33890))
* add backlight driver and update related modules ([c831732](https://github.com/HighCodeh/TentacleOS/commit/c831732d71a70e5f4b6d2c2fa879860c45b74a42))
* add GPIO management module ([3451606](https://github.com/HighCodeh/TentacleOS/commit/34516063b26babc28ea21eb764f51b825f3b77e9))
* add new folders and improve sub_menu structure and configuration ([b55b742](https://github.com/HighCodeh/TentacleOS/commit/b55b742fd12ff98e46d233fa18c202001148849c))
* add new icons ([7f167bc](https://github.com/HighCodeh/TentacleOS/commit/7f167bc4740f4e86e0c34271cdf2a9d6bdd4d0c0))
* add new icons ([ac13ba1](https://github.com/HighCodeh/TentacleOS/commit/ac13ba12e3b8c14152a0fa071c1009db084badc3))
* add new icons to the icons directory ([3d8ab62](https://github.com/HighCodeh/TentacleOS/commit/3d8ab62b94e42dad63d52df03b4157756bcfcfba))
* add new menu_generic folder in Applications ([9ec99e4](https://github.com/HighCodeh/TentacleOS/commit/9ec99e4d388846af2ac63b51c72850da64d2bdac))
* add UART module for terminal communication ([77fcc7b](https://github.com/HighCodeh/TentacleOS/commit/77fcc7bf6ade6cb2d4cce5f3bf2f0c7c356eb695))
* added backward compatibility ([8325fd5](https://github.com/HighCodeh/TentacleOS/commit/8325fd59903ca8908e68274417423a9971b3a890))
* added i2c_init and spi_init folders for bus initialization ([77ec609](https://github.com/HighCodeh/TentacleOS/commit/77ec609c765223a191388b95b6ea2577d794f934))
* added initial keyboard structure ([dd73782](https://github.com/HighCodeh/TentacleOS/commit/dd737824602b6c4ee7d32692c0aa2f3ecfce6c57))
* added new button to pin mapping ([75bb2cd](https://github.com/HighCodeh/TentacleOS/commit/75bb2cd04e24721fa81f922f2c1c9cd6dacf8c20))
* added tiny usb PRIV src to Drivers ([fdb03b2](https://github.com/HighCodeh/TentacleOS/commit/fdb03b2a4d1ceb958638f7a39a96ddd06e56ea89))
* added tinyusb descriptor HID and Configs ([e272842](https://github.com/HighCodeh/TentacleOS/commit/e2728425ffac4bd7a2806cbeca660c665bea5fe8))
* added TODO list and new config to root Cmake ([a617456](https://github.com/HighCodeh/TentacleOS/commit/a6174569e326b9725c1f8a345fd71ef294f4fcae))
* adding more data functions to http_service ([587f07f](https://github.com/HighCodeh/TentacleOS/commit/587f07f6a5695376f406af71c277a22005f28748))
* **ap_scanner:** get auth mode and if wps is enabled ([6bfcc8d](https://github.com/HighCodeh/TentacleOS/commit/6bfcc8d37e03ad4b582eecac00129c3d0484caa0))
* **ap_scanner:** results is now saved to internal_flash or micro-sd ([4294fd9](https://github.com/HighCodeh/TentacleOS/commit/4294fd9c5b120827d20672b97cf844ad5f296e93))
* **ap_scanner:** Scans all channels searching for routers. Saves SSID, BSSID (MAC), RSSI (signal), Channel, and Encryption Type, and saves to some place. ([7a3ddde](https://github.com/HighCodeh/TentacleOS/commit/7a3dddef33f89a9a1620c4a20d40ba7054a9ebe5))
* **assets_manager:** lazy load to all .bin images inside psram ([6c4551b](https://github.com/HighCodeh/TentacleOS/commit/6c4551b1969bf581533ca727df85f1fc208ab904))
* **assets:** add bluetooth menu icon ([9990a47](https://github.com/HighCodeh/TentacleOS/commit/9990a47b7920d64d54aecab697cd67e305859d2e))
* **assets:** add boot logo image to assets ([3c8736d](https://github.com/HighCodeh/TentacleOS/commit/3c8736dbad4fd3c068983813c991d252baa2843a))
* **assets:** add file manager html interface ([1254262](https://github.com/HighCodeh/TentacleOS/commit/1254262b56134570465df00d6db34164e1ad45d5))
* **assets:** add UI assets and selection indicators ([6eebd1b](https://github.com/HighCodeh/TentacleOS/commit/6eebd1b5091bb4f5da319e00dbac1affdde68956))
* **assets:** browse files menu coverflow frames ([89f7e10](https://github.com/HighCodeh/TentacleOS/commit/89f7e10c9ee0a9deeb8b8d14c4092599954b797b))
* **assets:** buzzer config file ([593873c](https://github.com/HighCodeh/TentacleOS/commit/593873cf8c52c1faa58cbecf3136f32a6305b5fc))
* **assets:** buzzer notes configuration file ([c6db83c](https://github.com/HighCodeh/TentacleOS/commit/c6db83cf817b977e6ee2407ce41b996c039f310e))
* **assets:** buzzer sounds ([8ca74a5](https://github.com/HighCodeh/TentacleOS/commit/8ca74a52d7d1a8c25497ee542e8b6c8e9a8f22e6))
* **assets:** coverflow menu assets ([c15a0cb](https://github.com/HighCodeh/TentacleOS/commit/c15a0cb325d892d10eb659fd4a953fedd2db9af5))
* **assets:** implement assets storage system using LittleFS ([d8adb20](https://github.com/HighCodeh/TentacleOS/commit/d8adb20c0578de0d9c82cefe848f873f5f19081b))
* **assets:** interface config file ([b478670](https://github.com/HighCodeh/TentacleOS/commit/b47867040952c693d670bb5124c8dc89bf737b94))
* **assets:** new boot logo ([f2cc9eb](https://github.com/HighCodeh/TentacleOS/commit/f2cc9ebdff3fc18ab39fe00c302a4b2013263c7c))
* **assets:** new octobit boot images ([41eaac4](https://github.com/HighCodeh/TentacleOS/commit/41eaac4aa4c6e83b3039e1726e5f6d67b28f14cf))
* **assets:** nfc coverflow frames ([e7a0715](https://github.com/HighCodeh/TentacleOS/commit/e7a07157ae6a76b09f633013ce7040dd5e85dca8))
* **assets:** octobit images ([838604c](https://github.com/HighCodeh/TentacleOS/commit/838604c9eea67420031e606cb1586ad45f9bf163))
* **assets:** screen config file ([4aa385d](https://github.com/HighCodeh/TentacleOS/commit/4aa385d01316ae09777ead5ef6cbbe0ba7ba4f58))
* **assets:** themes config file ([fd372f8](https://github.com/HighCodeh/TentacleOS/commit/fd372f81251cffef74eb998a84c34b34a8168789))
* **bad_usb:** added hid report for mouse and register callback ([691ff9e](https://github.com/HighCodeh/TentacleOS/commit/691ff9eb6652d2bfe11634d6fd4b628e7ea55a31))
* **bad_usb:** added quackscript parser, to use scripts unless static payloads ([e39d19f](https://github.com/HighCodeh/TentacleOS/commit/e39d19fcd77dd6882fbef5f92baf4ee37f45e08f))
* **bad_usb:** basic payload to bad_usb RICK ROLL ([5fef5c2](https://github.com/HighCodeh/TentacleOS/commit/5fef5c2314e9b525a9f106f63c5f725bac670431))
* **bin_conversor:** added script and automation build to convert .png files to .bin ([29668bf](https://github.com/HighCodeh/TentacleOS/commit/29668bfefa8e0889554f6a67a6575f4d34768ebc))
* **ble_scanner:** add uuid to json file ([d3a5f83](https://github.com/HighCodeh/TentacleOS/commit/d3a5f83128874b9cad1ba379a5d6273538d47117))
* **ble_service:** added connect for direct target connections ([acc3594](https://github.com/HighCodeh/TentacleOS/commit/acc3594001ef917d954b9f160ad2d12c60b56c38))
* **ble_service:** added raw snnifer and rssi tracker ([7069a5b](https://github.com/HighCodeh/TentacleOS/commit/7069a5b152a47ebedbba273e9d80ca3e5517647e))
* **ble_service:** now scan bring uuids too ([f2fd5c9](https://github.com/HighCodeh/TentacleOS/commit/f2fd5c9488ca8df9f3601debb7a152103cb7477f))
* **ble_sniffer:** application to format raw hexa sniffer ([3f5c9d9](https://github.com/HighCodeh/TentacleOS/commit/3f5c9d9e767de66d3096a3e69934ee0b90b88465))
* **ble_tracker:** application to get_rssi and start it ([1d2731c](https://github.com/HighCodeh/TentacleOS/commit/1d2731c19ffba94b94c21033ea713d4fff0cc6cc))
* **ble:** added airtag/SmartTag scanner ([c8d5b5b](https://github.com/HighCodeh/TentacleOS/commit/c8d5b5b933a20503ba3b3d31445fc7a5612ad48c))
* **ble:** ble connect flood, send a buch of connection request to DoS the device ([16a4543](https://github.com/HighCodeh/TentacleOS/commit/16a45438254db115c8b5d79fadda3c80bcf62c91))
* **ble:** ble screen server to try transmite screen data via ble ([e049173](https://github.com/HighCodeh/TentacleOS/commit/e0491737548ad03baad82546248e946021f88d6c))
* **ble:** exposure notification, just count how many devices around to get density estimation ([49ea447](https://github.com/HighCodeh/TentacleOS/commit/49ea447cfe738621052d06bceffd9ac54623472e))
* **ble:** implement SPI streaming for ble sniffer ([2effe56](https://github.com/HighCodeh/TentacleOS/commit/2effe568b5ee11dac9b601dc51ac7dbf6af535ea))
* **ble:** l2cap flood, send invalid l2cap packets to device ([0fab881](https://github.com/HighCodeh/TentacleOS/commit/0fab88146889d9289d129bcc32dd9f0efe869e86))
* **ble:** register HID for ble connection ([4aff351](https://github.com/HighCodeh/TentacleOS/commit/4aff351c55846a85c398de390dd0888c874e0a31))
* **ble:** scanner of ble devices ([f493021](https://github.com/HighCodeh/TentacleOS/commit/f49302147cc77c5750e30b69b8bff01970decfa0))
* **ble:** skimmer detection ([5c2c14c](https://github.com/HighCodeh/TentacleOS/commit/5c2c14c62e322eeaef82510e4cba424dcffa9e3f))
* bluetooth ([325875e](https://github.com/HighCodeh/TentacleOS/commit/325875eeaa3fea0cc5715d0199e584fe26aa038e))
* bluetooth ([f39614a](https://github.com/HighCodeh/TentacleOS/commit/f39614afcb8c709bc6b13a7c18acab545918850f))
* bluetooth ([fa87907](https://github.com/HighCodeh/TentacleOS/commit/fa8790728148fbd28f615690d41127c023dcf699))
* **bluetooth_service:** added more functions to service like start/stop, change mac, scan list size, disconnect all devices... ([0fd94bf](https://github.com/HighCodeh/TentacleOS/commit/0fd94bffd90188384d98ba7902aca3aa384642e7))
* **bluetooth:** added android, AppleJuice, Samsung, SourApple, TuttiFrutti, spams ([0b5c322](https://github.com/HighCodeh/TentacleOS/commit/0b5c3226c77c975e0b0a21e1ce0a4ade543c6852))
* **bridge:** implement SPI communication protocol and P4-C5 bridge ([fb794ca](https://github.com/HighCodeh/TentacleOS/commit/fb794ca74572bf2a82c4dabd79e921489812e979))
* **bt:** implement bluetooth service bridge ([d10baef](https://github.com/HighCodeh/TentacleOS/commit/d10baef4e1f9e524213c28ccfeaf1e9906549855))
* **build:** add boot screen source and include paths to CMake ([f8af3fc](https://github.com/HighCodeh/TentacleOS/commit/f8af3fc338ef5ad68f4850dea3614dc3e8f57d44))
* **build:** add wifi scan ap screen ([3995542](https://github.com/HighCodeh/TentacleOS/commit/3995542a60a9734fa2de801079df7441cf11ef75))
* **build:** add wifi scan menu screen ([c050fc1](https://github.com/HighCodeh/TentacleOS/commit/c050fc1f026eb84c688650c235ab8946f145cdbe))
* **build:** add wifi scan monitor screen ([c631b9d](https://github.com/HighCodeh/TentacleOS/commit/c631b9dbf54e805991ee70cecc30ca099a7d1fa7))
* **build:** add wifi scan probe screen ([2d64263](https://github.com/HighCodeh/TentacleOS/commit/2d642631f68e7d6d8ee471f078f0afc88bff90a3))
* **build:** add wifi scan stations screen ([922ac7a](https://github.com/HighCodeh/TentacleOS/commit/922ac7a53cabf15dc7e18c0b80de1026c05abfd7))
* **build:** add wifi scan target screen ([aa9a168](https://github.com/HighCodeh/TentacleOS/commit/aa9a1683fdf2b31e4f47e6a1a1234f02d255c1a9))
* **build:** allow font montserrat 10 ([20ce818](https://github.com/HighCodeh/TentacleOS/commit/20ce818682fce65e67cda6fa442a9fb1d084ab36))
* **build:** dir to put common files like txt to cmake know witch version is it to rename the final .bin ([1d381b2](https://github.com/HighCodeh/TentacleOS/commit/1d381b27f7c5d2990e7c18fdd6e023285f2f4b87))
* **build:** dir to put tools to general use ([fda0adb](https://github.com/HighCodeh/TentacleOS/commit/fda0adb5be7d9361a0636e630bb053d13e2b6a59))
* **build:** implement cross-platform asset conversion ([0bcba78](https://github.com/HighCodeh/TentacleOS/commit/0bcba7866a96051643d4142cc8313ecc20af5e04))
* **build:** register about settings screen and include paths ([f073975](https://github.com/HighCodeh/TentacleOS/commit/f0739759f43a82dadc2c1bd02bfc639ed1b3dcf5))
* **build:** register battery settings screen and include paths ([b135ec4](https://github.com/HighCodeh/TentacleOS/commit/b135ec450a25ceeb50e2c4542486f307d39dab05))
* **build:** register connection settings screen and include paths ([566fda7](https://github.com/HighCodeh/TentacleOS/commit/566fda762da5233d4ec8d2e26005144135fc1966))
* **build:** register interface settings and include paths ([ddf8815](https://github.com/HighCodeh/TentacleOS/commit/ddf88151cd33bfaf96706809ed2cf866bfbfe514))
* **build:** register new screens in CMake ([a15a9ac](https://github.com/HighCodeh/TentacleOS/commit/a15a9ac40b4ed6554c45e8fec618dafc8005f861))
* **build:** send the sdkconfig to the both firmwares ([8906f3e](https://github.com/HighCodeh/TentacleOS/commit/8906f3eb6b0b369c90fcb9757ad0b961d299ce41))
* **build:** sound settings screen include path ([012271d](https://github.com/HighCodeh/TentacleOS/commit/012271d3edd0ac99a7a6362edd93fc06dc3cb287))
* **buttons_gpio:** added esp_lcd to REQUIRES ([794b433](https://github.com/HighCodeh/TentacleOS/commit/794b43392381fe74a0d8f4f137a1feaaeeb229d1))
* **buttons_gpio:** read logic buttons from gpio ([62ba9c9](https://github.com/HighCodeh/TentacleOS/commit/62ba9c9c29fa44fe8a12e2e5e61fa807dfcf6661))
* **buzzer:** add persistent configuration and volume control ([db7354e](https://github.com/HighCodeh/TentacleOS/commit/db7354e37f5fac7f83fe66de35a39de79ea1dbc3))
* **c5/core:** implement unified radio engine and high-speed SPI bridge ([06a0353](https://github.com/HighCodeh/TentacleOS/commit/06a03532af00d35bbeae9cc605874d4188a66894)), closes [hi#speed](https://github.com/hi/issues/speed)
* **captive_portal:** storage to save users, password, 2fa or tokens ([a205a9c](https://github.com/HighCodeh/TentacleOS/commit/a205a9c3d07f9fcdbcd4362f480183c85f97f738))
* **cc1101_spectrum:** read -dBm and send to spectrum ([883c60c](https://github.com/HighCodeh/TentacleOS/commit/883c60c85b3a857da6b8de3e1ceff39ff1599bb2))
* **cc1101:** implement radio preset management(0-6) ([718f39c](https://github.com/HighCodeh/TentacleOS/commit/718f39c3a58853ac346cb25d9e0ef6e42304cd13))
* **cc1101:** new cc1101 driver ([7515086](https://github.com/HighCodeh/TentacleOS/commit/7515086bcb7d730d9d95edfb41cb6f4a3ed2df14))
* **config:** config file for OTA ([f82c397](https://github.com/HighCodeh/TentacleOS/commit/f82c39779dcf7341b3220f8e456e5e237e0b4479))
* **conn_ui:** handle wifi enable state and networks access ([a38e8fc](https://github.com/HighCodeh/TentacleOS/commit/a38e8fc77a8b533db508c812dd7a87e03063af46))
* **connect_wifi:** handle saved passwords and status polling ([30e9db4](https://github.com/HighCodeh/TentacleOS/commit/30e9db41e7a1f96f9c8ee062aeb4fbd3d3ddaa94))
* **console:** added cd command to change workind dir ([1db1961](https://github.com/HighCodeh/TentacleOS/commit/1db1961b8b034d3bbd0cbbf9a93b1dcc1573a20f))
* **console:** added ip command to see AP and STA ips ([291548d](https://github.com/HighCodeh/TentacleOS/commit/291548dfb4da1faa679bcdd3682fd24da6a7c054))
* **console:** added, sniffer, scan client, evil twin, etc to commands ([3e6673a](https://github.com/HighCodeh/TentacleOS/commit/3e6673a8a87008874c8be0a51992ebb8b2218b79))
* **console:** command tasks to see running tasks in moment ([0905b1b](https://github.com/HighCodeh/TentacleOS/commit/0905b1b358cdf1f9e2dc0593a1bb00507db1f2fb))
* **console:** console commands for filesystem and system ([18a992d](https://github.com/HighCodeh/TentacleOS/commit/18a992d1a57f36566f427761548281c2ed8f6a2c))
* **console:** console service to sendo commands via terminal ([c45e5fa](https://github.com/HighCodeh/TentacleOS/commit/c45e5fa835814a4c8b2f10f200821c36c2352a7b))
* **console:** wifi commands to save configs ([84e7196](https://github.com/HighCodeh/TentacleOS/commit/84e719606eb36a046ec8ed4e83cb7e79629d36ee))
* **console:** wifi commands to use wifi service and applications functions ([c256939](https://github.com/HighCodeh/TentacleOS/commit/c256939f0c8de7329308807b9ab3aaacff841d3b))
* **contributing:** MD file containg the contributing steps and tips for this project ([455d4fe](https://github.com/HighCodeh/TentacleOS/commit/455d4fe73ac922d12984b8426a439800b4f25d17))
* **deauther_detector:** deauther packet detector with promiscuous mode and channel hopping ([06096f1](https://github.com/HighCodeh/TentacleOS/commit/06096f17d4e106b46509b817422967e4149efc69))
* **dns:** added the stop function to dns server ([4435863](https://github.com/HighCodeh/TentacleOS/commit/4435863f4245dc57fc906e230ab77014915a69db))
* **ducky_parser:** ducky parser for BT ([987f74f](https://github.com/HighCodeh/TentacleOS/commit/987f74f7273c21e9076e3783daf3f83d8bc214b3))
* **ducky:** ducky parser for mouse commands ([1212585](https://github.com/HighCodeh/TentacleOS/commit/121258569d99b4af132677828a10835731900680))
* **espnow_chat:** chat between highboys using espnow ([fa0d3d3](https://github.com/HighCodeh/TentacleOS/commit/fa0d3d3b0c5c7f0dc2b73f2640f465caf340a151))
* **espnow_chat:** config files for chat using esp-now ([c8151a6](https://github.com/HighCodeh/TentacleOS/commit/c8151a62f47e0bbf07fae8f07082008bf424c9d2))
* **evil_twin:** now loads custom html from flash and save passwords on json ([3bc90fb](https://github.com/HighCodeh/TentacleOS/commit/3bc90fbe644c11f3d5406d7d59c55bae7d4bac37))
* **gatt:** added gatt explorer function ([238e203](https://github.com/HighCodeh/TentacleOS/commit/238e203a9f3740d2e1f0f4e052129487a5047df4))
* **github:** files to github workflow build realeases and version automaticly ([0fad330](https://github.com/HighCodeh/TentacleOS/commit/0fad330fbb7ddf8b044174a11cb44ec62e8e2187))
* **header_ui:** add dynamic wifi status icon ([b85b228](https://github.com/HighCodeh/TentacleOS/commit/b85b2284a474e12d1ed70d5cdba54c61ec9ccfa5))
* **html:** html for captive portal ([30c136f](https://github.com/HighCodeh/TentacleOS/commit/30c136faa407898694061f4e7eb00bbdd59ca29a))
* http server ([233b1f8](https://github.com/HighCodeh/TentacleOS/commit/233b1f8570588ad0b08b17ffb81a2ecfd2be57b2))
* **http:** send file from sd directory ([0b4ec24](https://github.com/HighCodeh/TentacleOS/commit/0b4ec24781a9f291bd126c919711402c4fd5a88f))
* **icons:** add new icon resources ([6285ebb](https://github.com/HighCodeh/TentacleOS/commit/6285ebb807a42f8fd52f2e67500a14622734852f))
* implement init routine for battery management IC ([96c30c3](https://github.com/HighCodeh/TentacleOS/commit/96c30c343706e44157cd8ea297159e2783630c32))
* infrared menu ([f299cfc](https://github.com/HighCodeh/TentacleOS/commit/f299cfccf8c50933a03b590ad294fe88ba46b03b))
* **input:** add keyboard mode support ([0931fab](https://github.com/HighCodeh/TentacleOS/commit/0931fab7edb775140700ef495890c60ac8b9185b))
* integrate GPIO menu option into main menu ([476bbe4](https://github.com/HighCodeh/TentacleOS/commit/476bbe4a53ba57265d6e2a1447aef9f6a847732c))
* ir service ([acf38ec](https://github.com/HighCodeh/TentacleOS/commit/acf38ecdd253b807f962a4f06846a8ff02af642e))
* JSON processor ([3ea1442](https://github.com/HighCodeh/TentacleOS/commit/3ea144277c4587bece4692d2117437c2fade928e))
* JSON processor ([cd9f3d8](https://github.com/HighCodeh/TentacleOS/commit/cd9f3d827512d22be75a18463df2d0be8665bd72))
* **kernel.c:** added safeguards and use the new sys_monitor ([99b110f](https://github.com/HighCodeh/TentacleOS/commit/99b110fa16f35517ca25af3005ed94279e58555a))
* **kernel:** added temporary ram monitor ([acd192e](https://github.com/HighCodeh/TentacleOS/commit/acd192e8b20f3da8def050a9f29b86c1c879cafe))
* **keyboard:** add submit callback and keyboard mode integration ([745e9ea](https://github.com/HighCodeh/TentacleOS/commit/745e9ea352e10fdc374f153f1d03b975c14b5096))
* **lvgl_port:** middlewares to lvgl indev and disp ([dde679c](https://github.com/HighCodeh/TentacleOS/commit/dde679cd814634be4132fd2c8e0781c2953d1ba4))
* **mac_vendors:** most commom mac vendors to scam clients ([d2043c9](https://github.com/HighCodeh/TentacleOS/commit/d2043c921aca04bf20b3191cbb302b1d432e6b53))
* **menu:** add sd_browser entry and integrate microSD browser ([71588a5](https://github.com/HighCodeh/TentacleOS/commit/71588a5d77e8ee534850cc6c4e0b81d9e09fc94c))
* **menu:** added back button ([84b1c91](https://github.com/HighCodeh/TentacleOS/commit/84b1c915a6f615c5b14ea3ae689f66ab9e8ab6a5))
* **menu:** back to home button ([2a31671](https://github.com/HighCodeh/TentacleOS/commit/2a3167113a81d4f91d9f17b6ce8d8205bb585881))
* new configs to HID and BLE ([50d0837](https://github.com/HighCodeh/TentacleOS/commit/50d083733789de52999160e2c77e0321d3b566cc))
* new menus logic ([bebb65d](https://github.com/HighCodeh/TentacleOS/commit/bebb65d468e7a7031e81ca7d2c1520486f2e9174))
* new sd card service ([385b505](https://github.com/HighCodeh/TentacleOS/commit/385b505b9c3e9feb4d7b43ceea9d38a34de0f4f3))
* **OTA:** Over-The-Air updating firmware ([5e6e384](https://github.com/HighCodeh/TentacleOS/commit/5e6e38406ab384e262c3084214b16ead019c7099))
* **oui_lookup:** added oui lookup to know devices vendor ([21da595](https://github.com/HighCodeh/TentacleOS/commit/21da595b6f45dd0cba4317ccebb6aa0d5f10d27f))
* **pcap:** pcap serializer header ([8cb10ce](https://github.com/HighCodeh/TentacleOS/commit/8cb10ceb267c4c3913816297f0e19887573675d3))
* **pins:** added gpio extra pins ([e518df4](https://github.com/HighCodeh/TentacleOS/commit/e518df49b2296f7cb125bd4895a4eb81a09438d2))
* **port_scan:** added single and multi range ip of port scan (ip range and CIDR) ([76e8752](https://github.com/HighCodeh/TentacleOS/commit/76e8752871d42dab34f5443d0fc414f34dd5f02c))
* **probe_monitor:** Captures "Probe Request" frames to see what SSIDs nearby devices are searching for ([691dcad](https://github.com/HighCodeh/TentacleOS/commit/691dcadcf11e14f0f0cf522cdd36633103b3d4f2))
* **protocol_spi:** implement binary SPI bridge between P4 and C5 ([866386c](https://github.com/HighCodeh/TentacleOS/commit/866386cfdb4c3450b5f1e07e68883a6f652b3c60))
* **protocol_spi:** implement binary SPI bridge between P4 and C5 ([da964bb](https://github.com/HighCodeh/TentacleOS/commit/da964bb021d6f75f26b5ab998df68933cbffae02))
* sd_manager service ([d3601ec](https://github.com/HighCodeh/TentacleOS/commit/d3601ec92ddc9209a783ee299b65addbb6adc3e5))
* **setup:** setup.sh file to verify for git conventions ([3625dc2](https://github.com/HighCodeh/TentacleOS/commit/3625dc2dcbb7e80ddb71b6298a789706e232fd85))
* **signal_monitor:** create a task for signal monitor and put it to PSRAM ([a8aca1c](https://github.com/HighCodeh/TentacleOS/commit/a8aca1c642ec38f44f516be7e4bec1d1442163fd))
* **signal_monitor:** Signal strength graph (RSSI) of a specific target for physical location. ([1463144](https://github.com/HighCodeh/TentacleOS/commit/146314435909b0c8befd75a325e94e90b312323c))
* simple bad-usb ([0fd9fad](https://github.com/HighCodeh/TentacleOS/commit/0fd9fad2f8237c5606d90ab0cfdb866c306250bb))
* **spectrum:** added new subghz spectrum screen ([8090094](https://github.com/HighCodeh/TentacleOS/commit/8090094243b953fe45c33666bba4ef2b75941547))
* **spi_protocol:** more protocols to improve more the spi_bridge ([613b198](https://github.com/HighCodeh/TentacleOS/commit/613b198c8120a131b2169935aaff4e1a1762e34c))
* **spi_slave:** configuration to C5 start do listen commands ([2cd101c](https://github.com/HighCodeh/TentacleOS/commit/2cd101c4502a4a3adfeded18bead1a4af82276ec))
* **spi/c5:** define unified SPI command set and packed data structures ([44b8ec6](https://github.com/HighCodeh/TentacleOS/commit/44b8ec6c0a1b0934582df0c2db66ae7ab67bbc56))
* **spi:** expand protocol definitions and add structured data types ([45db194](https://github.com/HighCodeh/TentacleOS/commit/45db194520e52f6c59da92d39fa1a26c3afef422))
* **spi:** implement asynchronous streaming headers ([16a7c83](https://github.com/HighCodeh/TentacleOS/commit/16a7c83dcdb358acd613d04a32706371327b7a63))
* **spi:** implement binary probe monitoring and incremental data fetching ([142b0fe](https://github.com/HighCodeh/TentacleOS/commit/142b0fe72962f50dd9d45217fbe0d85fcc963844))
* **spi:** implement binary sniffer control ([ac873cb](https://github.com/HighCodeh/TentacleOS/commit/ac873cb53f66ec583dd484b458563b00dedb6995))
* **spi:** implement stream task and improved command handling ([70bb987](https://github.com/HighCodeh/TentacleOS/commit/70bb987782372262183577c65e04eead4dab464a))
* **spi:** implement synchronized client scanner with binary result fetching ([02cf303](https://github.com/HighCodeh/TentacleOS/commit/02cf303813af9101ce0041ccdc83ef1c0ba1dd74))
* **spi:** offload deauth detection and packet analysis to c5 ([b3c8855](https://github.com/HighCodeh/TentacleOS/commit/b3c88559920bd5da07d26f14bfd6f1dd1d54dc0c))
* **spi:** spi bridge now verify firmware versions to know when update high boy ([1ce9c58](https://github.com/HighCodeh/TentacleOS/commit/1ce9c581b073b16ede8e86f75f451c968ba12048))
* **st25r3916:** add core driver module ([02a642d](https://github.com/HighCodeh/TentacleOS/commit/02a642d7f5f459d38840e45cd272e688a6af0f48))
* **st25r3916:** add FIFO control module ([0e5f103](https://github.com/HighCodeh/TentacleOS/commit/0e5f1031f5be7575ea20056a4059a775bce080cd))
* **st25r3916:** add IRQ polling implementation ([181bc88](https://github.com/HighCodeh/TentacleOS/commit/181bc886c59988e2ef8ef2e0f50f348e8880d4cc))
* **st25r3916:** add register map and bit definitions ([f9554d3](https://github.com/HighCodeh/TentacleOS/commit/f9554d3ca1e03a2d22bbca865bdead9fafa9891f))
* **st25r3916:** adddirect command definitions ([9225295](https://github.com/HighCodeh/TentacleOS/commit/922529527965705ac8c3c4feb140e67870e427a2))
* **st25r3916t:** add unimplemented AAT calibration stub ([d4241c2](https://github.com/HighCodeh/TentacleOS/commit/d4241c2484b0adf2bf808353bec14a4378bce4fd))
* **st7789:** add persistent display config and rotation support ([313bdd9](https://github.com/HighCodeh/TentacleOS/commit/313bdd9ecf33b208469fd468c35049b018d8a14f))
* **storage_assets:** add file write support ([f7c3d19](https://github.com/HighCodeh/TentacleOS/commit/f7c3d19926a5e3d6ac87838313fb951d8088c174))
* **storage:** add modules for microSD-based storage access and control ([e6abb05](https://github.com/HighCodeh/TentacleOS/commit/e6abb05e72e4bf6ee7bb44a8afa982df03032488))
* **storage:** added a place to save scanned aps ([472e476](https://github.com/HighCodeh/TentacleOS/commit/472e476cff77aaaa27887788ae1295f0d5d0cf34))
* **storage:** added new file to store know networks ([af8f9d0](https://github.com/HighCodeh/TentacleOS/commit/af8f9d00c5a4b7fea545100b9052704c220b940a))
* **storage:** ensure default directories on initialization ([fa616dc](https://github.com/HighCodeh/TentacleOS/commit/fa616dccef54aa8a0f84b0aa38c8f72b91ae1bd1))
* **storage:** file to store scanned clients ([67bbc8f](https://github.com/HighCodeh/TentacleOS/commit/67bbc8fdf98b1bea9a77f4715e3c864bff9ba217))
* **storage:** json to save ble scanned devices ([12acb0b](https://github.com/HighCodeh/TentacleOS/commit/12acb0b9f93358ad2e124ecb59e1c9ed421747d8))
* **subghz:** add protocol serializer and parser for High Boy format ([b70d097](https://github.com/HighCodeh/TentacleOS/commit/b70d0975cdfe5e9acb7194f6b87f51ba3a36e93f))
* **subghz:** add storage abstraction layer for signal saving ([4d03937](https://github.com/HighCodeh/TentacleOS/commit/4d03937c433963adf558da016440344bb97d4af1))
* **SubGhz:** application for spectrum subghz ([29cf1f7](https://github.com/HighCodeh/TentacleOS/commit/29cf1f7fc656c4ffc2ad85ee5678061406cff3c6))
* **subghz:** file loader ([4b34b2c](https://github.com/HighCodeh/TentacleOS/commit/4b34b2c385103fd3f1a6250cf6848cad5c2334e9))
* **subghz:** hoping frequencys ([6647ab3](https://github.com/HighCodeh/TentacleOS/commit/6647ab3a9b721fda05c5ebae7e8b7befb052598f))
* **subghz:** integrate presets and storage into receiver logic ([135d076](https://github.com/HighCodeh/TentacleOS/commit/135d076aca1a91602bbd7dbc0f606f54f80bbca6))
* **subghz:** now spectrum analyser includes waterfall visualization ([cebd683](https://github.com/HighCodeh/TentacleOS/commit/cebd683e1dd1d37b29fbf51192140473aaf9a631))
* **SubGhz:** receiver raw RF 433,92 symbols ([e675c13](https://github.com/HighCodeh/TentacleOS/commit/e675c132e9b55cd9fe7c9c350f927080110d1974))
* **subghz:** spectrum subghz screen ([2a499ec](https://github.com/HighCodeh/TentacleOS/commit/2a499ec8aa83835960d0d4829b9c2aad8b4a71c7))
* **subghz:** testing configs ([9a561ec](https://github.com/HighCodeh/TentacleOS/commit/9a561ec580bc4169ab55b45c3e892eea63e47945))
* **subghz:** trying to decode signals and identify protocols ([39ed227](https://github.com/HighCodeh/TentacleOS/commit/39ed227fdcd2927ff1cf99d5119f9e78df18aad2))
* **subghz:** type for structured data for subghz protocol ([bc5ee75](https://github.com/HighCodeh/TentacleOS/commit/bc5ee750e1c0b36e83e07d4b68f2aa62bd8a6b2e))
* switching hardcoded HTML to sd-card stored .html ([98a7c0b](https://github.com/HighCodeh/TentacleOS/commit/98a7c0bece71150d00e10c165fc58486911e5ab7))
* **sys_monitor:** added constant task monitor, delete task if heap < 256 bytes ([d2e7ca6](https://github.com/HighCodeh/TentacleOS/commit/d2e7ca69c140caaf106f7a6c6fed8a5b52f5ffc4))
* **sys_monitor:** espcial handling for UI TASK ([2569056](https://github.com/HighCodeh/TentacleOS/commit/256905679ffb62659436644397ccc6366814d5af))
* **sys:** implement automated C5 firmware update system ([d2c70b8](https://github.com/HighCodeh/TentacleOS/commit/d2c70b8cd22449d097d58afd53c12957b357fefb))
* **target_scanner:** Performs a focused scan on a specific BSSID to identify all connected clients ([5cc666b](https://github.com/HighCodeh/TentacleOS/commit/5cc666b68e1edd24ea4a579e3f06c1141a59101a))
* **TEMPLATE:** Template for Firmware Issues ([b8402fd](https://github.com/HighCodeh/TentacleOS/commit/b8402fdaff6b9307d3716167a9aef796dc6c4e23))
* **TEMPLATE:** Template for Firmware pull requests ([f0d5373](https://github.com/HighCodeh/TentacleOS/commit/f0d537349c9de5e8db1bcc90df5831716fb43d11))
* testing i2c ([2c5dba5](https://github.com/HighCodeh/TentacleOS/commit/2c5dba5a1e14b450e2fddecc3b3ae6fdf8773ed6))
* **tools:** automate build & flash for esp32-p4 and esp32-c5 ([8d4e131](https://github.com/HighCodeh/TentacleOS/commit/8d4e1312bf190a7b812c4b73b551336fd4b59ee1))
* **tools:** automate building for esp32-p4 and esp32-c5 ([b2b712e](https://github.com/HighCodeh/TentacleOS/commit/b2b712e45b450390d75d8cc62d13d4e8008a03d9))
* **tools:** automate flash for esp32-p4 and esp32-c5 ([d44a82a](https://github.com/HighCodeh/TentacleOS/commit/d44a82a32360bc2ada348af5d20760e5562922fd))
* **tusb_desc:** added descriptor to mouse HID, now is Keyboard + mouse ([5dbc1e4](https://github.com/HighCodeh/TentacleOS/commit/5dbc1e4fe500eaae044c7de19bada1ea072671ee))
* **ui_manager:** added bluetooth service start ([c1c7793](https://github.com/HighCodeh/TentacleOS/commit/c1c7793ff4a1b1134d012dd68fa95f7455fa359d))
* **ui_manager:** ui hard_reset task if initial task become deleted ([a3ceea4](https://github.com/HighCodeh/TentacleOS/commit/a3ceea48a96971d9d39d4c70b4c44e3203eb8288))
* **ui:** add about settings screen ([4da6914](https://github.com/HighCodeh/TentacleOS/commit/4da6914944acf4be946c384c3e256b3bf10454f5))
* **ui:** add battery settings screen ([f77b93e](https://github.com/HighCodeh/TentacleOS/commit/f77b93e2454e4334b2d512cb99e5d80ad85c9f9d))
* **ui:** add boot screen with delay before home screen ([5219552](https://github.com/HighCodeh/TentacleOS/commit/5219552a02b745000abaeab8b18b452f0b1bcf37))
* **ui:** add connection settings screen ([7e4132d](https://github.com/HighCodeh/TentacleOS/commit/7e4132dba1a7213a2ead96458c06a4a9f8de1178))
* **ui:** add display settings placeholder screen ([69d1e3b](https://github.com/HighCodeh/TentacleOS/commit/69d1e3ba65217fa585c7e4a7d59532bc4e779bfa))
* **ui:** add footer visibility control and theme integration ([3baec16](https://github.com/HighCodeh/TentacleOS/commit/3baec16c58610b233abe7bef46febe6d2caafbbc))
* **UI:** add header and footer components ([d01a34f](https://github.com/HighCodeh/TentacleOS/commit/d01a34f65c3ae82cca4555a348b917be41914de0))
* **UI:** add initial menu with dots and indicator ([c354b29](https://github.com/HighCodeh/TentacleOS/commit/c354b298c71645124299d58d2d9bf95d4a0f92c6))
* **ui:** add interface settings screen ([99ee3e5](https://github.com/HighCodeh/TentacleOS/commit/99ee3e5c9e77e925b34781201959f92f661bbc25))
* **ui:** add keyboard overlay with buzzer feedback ([b93fd44](https://github.com/HighCodeh/TentacleOS/commit/b93fd449d19e1309f1576f6466438ddf43080344))
* **ui:** add message box ([873b120](https://github.com/HighCodeh/TentacleOS/commit/873b120e504c18e0bcdcf42c1dae394549731a8c))
* **ui:** add persistent theme management and JSON loading ([ef5b25c](https://github.com/HighCodeh/TentacleOS/commit/ef5b25cb0cd717aa364b82954490d68bac14fc11))
* **ui:** add settings screen ([b614487](https://github.com/HighCodeh/TentacleOS/commit/b614487b7c67e87380862f3560dc03acf132f959))
* **ui:** add sound settings screen ([7b8b2e6](https://github.com/HighCodeh/TentacleOS/commit/7b8b2e6f80e5c595db7f5efc4c02fb1a96a2a4fb))
* **ui:** add time display to header ([4ad8d46](https://github.com/HighCodeh/TentacleOS/commit/4ad8d469eb0905e63fc044e2689b242343a3e86f))
* **ui:** add wifi ap screen to ui manager ([55946eb](https://github.com/HighCodeh/TentacleOS/commit/55946eb16afd419b2e9390d8eb8f2a2b41413b96))
* **ui:** add wifi scan menu to ui manager ([abd8b24](https://github.com/HighCodeh/TentacleOS/commit/abd8b2494bce60adc6ac26253831fe657975e755))
* **ui:** add wifi scan monitor to ui manager ([98c4ae5](https://github.com/HighCodeh/TentacleOS/commit/98c4ae5157cf7f047d5514f51e4641419243b658))
* **ui:** add wifi scan probe to ui manager ([299f7d8](https://github.com/HighCodeh/TentacleOS/commit/299f7d87cfb2688af754adfb0f425adc5b5f74a3))
* **ui:** add wifi scan stations to ui manager ([f9f5d3e](https://github.com/HighCodeh/TentacleOS/commit/f9f5d3ee08b46cc5a254db075197a12dbf9a64b5))
* **ui:** add wifi scan target to ui manager ([58cfe94](https://github.com/HighCodeh/TentacleOS/commit/58cfe94d4574fbfcdbf1d52c5dfa50cb91cc099c))
* **ui:** added buzzer sound effects to coverflow menu ([22fdaec](https://github.com/HighCodeh/TentacleOS/commit/22fdaeccee044f0391e4c286d209d682ea4e6e1d))
* **ui:** added menu and submenu for bluetooth spam ([5caccda](https://github.com/HighCodeh/TentacleOS/commit/5caccdaa59c3177e6e9be03f6e78ea072e26131b))
* **ui:** bluetooth connect screen ([12a976e](https://github.com/HighCodeh/TentacleOS/commit/12a976e3192767354ce5bf810f5545b160e0e49e))
* **UI:** browse files menu option ([217b540](https://github.com/HighCodeh/TentacleOS/commit/217b5403e43fa6adb3e5ef10dbbf2b3fd34d1bca))
* **ui:** display navigation and buzzer feedback in settings screen ([f665e79](https://github.com/HighCodeh/TentacleOS/commit/f665e791069f0d7fd1d8c44cd11c34970d58ded7))
* **ui:** dynamic theme in keyboard component ([77daddc](https://github.com/HighCodeh/TentacleOS/commit/77daddc7470e78f2cd7e36aab5dbb41c2d166c8f))
* **UI:** i forget to send header to use ui_manager ([89fef5a](https://github.com/HighCodeh/TentacleOS/commit/89fef5a74973091be6c57adbae15e1e4e9692788))
* **ui:** implement boot screen ([9712fef](https://github.com/HighCodeh/TentacleOS/commit/9712fef6cb28f7cc59ae00e4aee35b3327e7e8b0))
* **ui:** implement dynamic theme switching and persistence ([932ea41](https://github.com/HighCodeh/TentacleOS/commit/932ea41d70c36486cadb01d31fbdc379c182872b))
* **ui:** implement menu with dynamic theme ([40a861c](https://github.com/HighCodeh/TentacleOS/commit/40a861cbe5f52f9d31bf175c28ee5914d60dd1a2))
* **UI:** implement screen switching for wifi and subghz ([913f3e2](https://github.com/HighCodeh/TentacleOS/commit/913f3e236d184ac6b221a1ba34142dfe2922f2b2))
* **ui:** improve message box input handling ([ffcc3c1](https://github.com/HighCodeh/TentacleOS/commit/ffcc3c1d66a745868b5fdb793c722a3ef4f0d4f3))
* **ui:** improve settings screen layout ([9bd56cd](https://github.com/HighCodeh/TentacleOS/commit/9bd56cdaeab8ca53984ffc279e57b6ebc6849549))
* **ui:** integrate about settings into screen navigation ([63e2185](https://github.com/HighCodeh/TentacleOS/commit/63e2185d07139a7ee8e9bf082c60379cfcd038c8))
* **ui:** integrate battery settings into screen navigation ([7e449d5](https://github.com/HighCodeh/TentacleOS/commit/7e449d52ca09ba739b4b77d3c2b6b0e505d192bb))
* **ui:** integrate connection settings into screen navigation ([079a734](https://github.com/HighCodeh/TentacleOS/commit/079a7340c4eac037c0f1489391846fa9f2b38ff5))
* **ui:** integrate display settings into UI manager ([18a5843](https://github.com/HighCodeh/TentacleOS/commit/18a58436aa7275eb80ac3a888ad53c40deecacf6))
* **ui:** integrate interface settings into screen navigation ([fe413e7](https://github.com/HighCodeh/TentacleOS/commit/fe413e7a3031556144928ce215b6e3c8c9bc5464))
* **ui:** integrate settings screen into UI manager and screen enum ([e60fe72](https://github.com/HighCodeh/TentacleOS/commit/e60fe7272420bf3903bad0736124230533dfc267))
* **ui:** integrate sound settings into screen navigation ([8768c0b](https://github.com/HighCodeh/TentacleOS/commit/8768c0be7285462457c00cd639f05344c07118d3))
* **ui:** integrate theme and file-based audio into sound settings ([c11cec1](https://github.com/HighCodeh/TentacleOS/commit/c11cec106f261510ce5425e2500b1a6c82f36869))
* **ui:** integrate theme and sound effects into about screen ([adf950b](https://github.com/HighCodeh/TentacleOS/commit/adf950b20937d6ae5274930e97d480c9ddedbd5f))
* **ui:** integrate theme and sound effects into battery settings ([c9ce63b](https://github.com/HighCodeh/TentacleOS/commit/c9ce63b2d5017fda690e43f155cceb427df97415))
* **ui:** integrate theme and styles into header ([ed95ea8](https://github.com/HighCodeh/TentacleOS/commit/ed95ea80fd12dcb592d23d57d08eb46d648b7e70))
* **ui:** integrate theme, brightness control, and display rotation ([99cdff5](https://github.com/HighCodeh/TentacleOS/commit/99cdff5945baed030057d1258ec530bf8a3c9d7d))
* **ui:** keyboard in connect wifi screen ([f9ab92d](https://github.com/HighCodeh/TentacleOS/commit/f9ab92d3ff71ab655676fabe9aa10044bb149449))
* **UI:** Load binary image to psram ([00a1d51](https://github.com/HighCodeh/TentacleOS/commit/00a1d51c5a7b8843c112c23da2db316b3f15e208))
* **ui:** new bad usb UI ([dae007e](https://github.com/HighCodeh/TentacleOS/commit/dae007e3c52721cdf2f0310bf7ef9c8848703f3b))
* **ui:** new boot screen ([d7a2e3a](https://github.com/HighCodeh/TentacleOS/commit/d7a2e3af851684be5f89f92eca4ef32be91ebb14))
* **ui:** new initial ui using lvgl (just home for now) ([efdf7d1](https://github.com/HighCodeh/TentacleOS/commit/efdf7d1c0d12f44b3584c994d45442e4ff414a8a))
* **ui:** new wifi and config screen ([67530db](https://github.com/HighCodeh/TentacleOS/commit/67530dbc2353f0abb5e7d83a9d79f091634d8e8a))
* **ui:** new wifi UIs ([a096801](https://github.com/HighCodeh/TentacleOS/commit/a096801f180ece0a7c0b1541ca12e97e47fc6ac3))
* **UI:** refactor home & header to use PSRAM for menu and improve alignment ([ecc6dab](https://github.com/HighCodeh/TentacleOS/commit/ecc6dab2b48370cf436f3194b98166c4ec7bcbfc))
* **ui:** refine boot screen animations ([48c4415](https://github.com/HighCodeh/TentacleOS/commit/48c44155ea56534506eb496f52a4ec1d7ba1b785))
* **ui:** refine display settings layout and animations ([caf3c97](https://github.com/HighCodeh/TentacleOS/commit/caf3c9712171ed9e522c927930066d636f7761ad))
* **ui:** register new screen in to ui manager ([c8b6723](https://github.com/HighCodeh/TentacleOS/commit/c8b6723e9295a5694e9898dd4d26a3496ebed3d3))
* **ui:** register new screens in ui manager switch ([702d3ea](https://github.com/HighCodeh/TentacleOS/commit/702d3ea09d0131cf4d6699c90697e50ab5890208))
* **ui:** unify settings menu theme and input timer ([4c705a1](https://github.com/HighCodeh/TentacleOS/commit/4c705a1ff6523df6c8552aee457a72128650cc36))
* **ui:** wifi connect screen ([f98b138](https://github.com/HighCodeh/TentacleOS/commit/f98b138d173c6559e42f9c7943217b5acaceb9d7))
* **ui:** wifi scan ap screen ([78b7a3d](https://github.com/HighCodeh/TentacleOS/commit/78b7a3dd171f0c588e386c0d5cbacd4f098fb70c))
* **ui:** wifi scan follow dynamic theme ([0bcb494](https://github.com/HighCodeh/TentacleOS/commit/0bcb49492f67bca48fdd8e1857336292365081fe))
* **ui:** wifi scan menu screen ([c2e860c](https://github.com/HighCodeh/TentacleOS/commit/c2e860c5b94ae74e25f41991d29e288489dec522))
* **ui:** wifi scan monitor ([efd2514](https://github.com/HighCodeh/TentacleOS/commit/efd2514d9e28868cf0263895f69462ff3c0f3592))
* **ui:** wifi scan probe screen ([99d1ea3](https://github.com/HighCodeh/TentacleOS/commit/99d1ea3b4ea8816ac8137c8ead2d6e160ed0876d))
* **ui:** wifi scan stations screen ([4e27fb0](https://github.com/HighCodeh/TentacleOS/commit/4e27fb00a3894c699dab77cd6f3b4f3b8c24db7b))
* **ui:** wifi scan target screen ([05c4b58](https://github.com/HighCodeh/TentacleOS/commit/05c4b5854824cd258dae30ae0f1ad880bb7cabb7))
* **ui:** wifi screen follow dynamic theme ([e043a7e](https://github.com/HighCodeh/TentacleOS/commit/e043a7e3d5e92a0b95dc7f9eb064dd6feba080f3))
* **UI:** wifi ui ([c9afce2](https://github.com/HighCodeh/TentacleOS/commit/c9afce275022dc4013642e5f5a0a967348f843b9))
* virtual stream display ([90a2fed](https://github.com/HighCodeh/TentacleOS/commit/90a2fed3bd274551653de0eff9026587b26b3aae))
* wifi deauther ([533d3ac](https://github.com/HighCodeh/TentacleOS/commit/533d3ac376f52724bbb61c534569aca0e655b56f))
* wifi deauther ([c8d444c](https://github.com/HighCodeh/TentacleOS/commit/c8d444ceccadb7dfbba0fb91b43bb68d7c07bd17))
* **wifi_beacon_spam:** wifi beacon spam, random and custom ([1542207](https://github.com/HighCodeh/TentacleOS/commit/154220717571f2a2d575513f07f5bd83a085beda))
* **wifi_deauther:** added broadcast deauth frame ([08aeed0](https://github.com/HighCodeh/TentacleOS/commit/08aeed0bb04b3a418b39bed7fe7a8b312348b5d6))
* **wifi_deauther:** send wifi association request ([4341f5c](https://github.com/HighCodeh/TentacleOS/commit/4341f5cc135aa8e1407b0297f2b05d69a8902e4e))
* **wifi_deauther:** send wifi association request ([fbe1129](https://github.com/HighCodeh/TentacleOS/commit/fbe1129ffcf7c9975f40c2be9c30001ea9627273))
* **wifi_flood:** added association, auth and probe request flood ([6ccec62](https://github.com/HighCodeh/TentacleOS/commit/6ccec625fff79d030942ca29a21b8df7639bd1d5))
* **wifi_menu:** new wifi ui ([1379778](https://github.com/HighCodeh/TentacleOS/commit/13797780af13fb813969b8b6750e299ab0985607))
* **wifi_service:** added channel hopping and wifi promiscuous start/stop ([001848d](https://github.com/HighCodeh/TentacleOS/commit/001848dcb5281ab1d3472baa4d045f7a4c69ad26))
* **wifi_service:** added wifi_stop e wifi_start (its not a init or deinit) ([ba45f12](https://github.com/HighCodeh/TentacleOS/commit/ba45f12b124c5e50d541acf98de11387aba7f418))
* **wifi_service:** get ssid to connected wifi ([0ec8756](https://github.com/HighCodeh/TentacleOS/commit/0ec87567d1acd4a20cf44318fc7fe1058ed0f392))
* **wifi_service:** get ssid to connected wifi ([8ce5f7f](https://github.com/HighCodeh/TentacleOS/commit/8ce5f7f38a6d3c461aca5fcdeb5c19f82d67f4cf))
* **wifi_service:** header for wifi 802.11 protocol headers, like 802.11 MAC handler and 802.11 Frame control ([ebdc290](https://github.com/HighCodeh/TentacleOS/commit/ebdc290d5d831dc350b660c3ddf3fc7081e6bded))
* **wifi_service:** load and save config files ([7957a4b](https://github.com/HighCodeh/TentacleOS/commit/7957a4bf9c4342000c3e7ca86c5c9edd83c04bbc))
* **wifi_service:** new functions to save config and know networks ([c622c2c](https://github.com/HighCodeh/TentacleOS/commit/c622c2c18e5d3fdf85526edf25b17c0403c05061))
* **wifi_service:** perform a full scan in all channels ([a9394b3](https://github.com/HighCodeh/TentacleOS/commit/a9394b3b5b3c06e7b3e6ed51c016b3745792c35f))
* **wifi_service:** perform full scan in all channels ([7d670e2](https://github.com/HighCodeh/TentacleOS/commit/7d670e2f3aec54dbcb88fe1baf303a7b8c02908d))
* **wifi_service:** verify if wifi is active and if wifi is connected to some AP ([3cf4718](https://github.com/HighCodeh/TentacleOS/commit/3cf47187fb60655dfdf3b94ee70ad1a5bac9c4ff))
* **wifi_sniffer:** ensure that sniffer caputured all EAPOL packets and capture PMKID AP SSID ([6e984cf](https://github.com/HighCodeh/TentacleOS/commit/6e984cffc1a334d83e178fe87e36b8bad897e9df))
* **wifi_sniffer:** wifi sniffer, BEACONS-PROBES-RAW-EAPOL-PMKID ([518de3c](https://github.com/HighCodeh/TentacleOS/commit/518de3c06e3c941ea8459a7c83cb1de02b7c2ee5))
* **wifi_ui:** redirect scan option to scan menu ([c13e557](https://github.com/HighCodeh/TentacleOS/commit/c13e557ef612cb284cbc9c831041347d2bf5e73f))
* **wifi/c5:** implement unified command dispatcher for complex operations ([c42892c](https://github.com/HighCodeh/TentacleOS/commit/c42892c8b381c994b40fe3ddab4fe51f1e8e53b5))
* **wifi:** add helpers for sniffer and target scanner UI ([61033b5](https://github.com/HighCodeh/TentacleOS/commit/61033b5695fbdf443eba38e3490a901a5e3d57ad))
* **wifi:** add traffic analyzer and related headers ([39f6748](https://github.com/HighCodeh/TentacleOS/commit/39f6748893f561741c43bc55a53ca64778f01d09))
* **wifi:** adding a TCP/UDP port scanner (port, protocol, banner). ([06862ec](https://github.com/HighCodeh/TentacleOS/commit/06862ecfb2ae008c657c827e1f5f4c9e0b87a552))
* **wifi:** adding default config files ([da537a9](https://github.com/HighCodeh/TentacleOS/commit/da537a94d864f1dcc9f7f08001b873d90cfff71d))
* **wifi:** deauther packet detector (testing discord webhook) ([e7b5b42](https://github.com/HighCodeh/TentacleOS/commit/e7b5b420eee1589be0792d73203a9c9f2dcaaa50))
* **wifi:** expose live result count for dynamic SPI tracking on target scanner ([ad9380e](https://github.com/HighCodeh/TentacleOS/commit/ad9380e407c347d220c2317c1c9aaf19bbae776d))
* **wifi:** implement binary deauther control ([87a728e](https://github.com/HighCodeh/TentacleOS/commit/87a728e45a02456636f0d6662b5fe29bf6aa478e))
* **wifi:** implement dynamic count point for probe monitor ([36a5003](https://github.com/HighCodeh/TentacleOS/commit/36a500301ed61c08e046dd4d4a6dcc91427187b3))
* **wifi:** implement full wifi service bridge via SPI commands ([ae89382](https://github.com/HighCodeh/TentacleOS/commit/ae89382f99f94f5252a71cc4d06f7f0007ead48d))
* **wifi:** implement SPI streaming on sniffer ([cab2962](https://github.com/HighCodeh/TentacleOS/commit/cab2962fea30299b89e8cbb96aa7e354b3111603))


### Performance Improvements

* **cc1101:** optimization ro remove dangling pointers and stack memory corruption ([16c50cc](https://github.com/HighCodeh/TentacleOS/commit/16c50cc14910d70202dd1597bf47b12b78611cad))
* **cc1101:** optimization to remove dangling pointers and stack memory corruption ([1230037](https://github.com/HighCodeh/TentacleOS/commit/1230037d8c5c0db5dfa15b5a44fca12330e84213))
