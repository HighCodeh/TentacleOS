<p align="center">
  <img src="pics/Highboy_repo.png" alt="HighBoy Banner" width="1000"/>
</p>


# HighBoy Firmware (Beta)

[![License](https://img.shields.io/github/license/HighCodeh/TentacleOS)](LICENSE)
[![GitHub Stars](https://img.shields.io/github/stars/HighCodeh/TentacleOS)](https://github.com/HighCodeh/TentacleOS/stargazers)
[![GitHub Forks](https://img.shields.io/github/forks/HighCodeh/TentacleOS)](https://github.com/HighCodeh/TentacleOS/network/members)
[![Pull Requests](https://img.shields.io/github/issues-pr/HighCodeh/TentacleOS)](https://github.com/HighCodeh/TentacleOS/pulls)

> **Languages**:  [🇺🇸 English](README.md) | [🇧🇷 Português](README.pt.md) 

This repository contains a **firmware in development** for the **HighBoy** platform.
**Warning:** this firmware is in its **beta phase** and is **still incomplete**.

---

## Officially Supported Targets

We are expanding support for the latest Espressif chips:

| Target | Status |
| :--- | :--- |
| **ESP32-S3** | Main Development |
| **ESP32-P4** | Experimental (firmware_p4) |
| **ESP32-C5** | Experimental (firmware_c5) |

---

## Firmware Structure

Unlike basic examples with a single `main.c`, this project uses a modular structure organized into **components**, which are divided as follows:

- **Drivers** – Handles hardware drivers and interfaces.
- **Services** – Implements support functionalities and auxiliary logic.
- **Core** – Contains the system's central logic and main managers.
- **Applications** – Specific applications that use the previous modules.

This division facilitates scalability, code reuse, and firmware organization.

See the general project architecture:
<p align="center">
  <img src="pics/architecture.png" alt="Arquitetura do Firmware" width="40%"/>
</p>


## How to use this project

We recommend that this project serves as a basis for custom projects with ESP32-S3.
To start a new project with ESP-IDF, follow the official guide:
[ESP-IDF Documentation - Create a new project](https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#start-a-new-project)

### Initial project structure

Despite the modular structure, the project still maintains an organization compatible with the ESP-IDF build system (CMake).

Example layout:

```bash
├── CMakeLists.txt
├── components
│   ├── Drivers
│   ├── Services
│   ├── Core
│   └── Applications
├── main
│   ├── CMakeLists.txt
│   └── main.c
└── README.md
```

---

## How to Contribute

Contributions are what make the open-source community such an amazing place to learn, inspire, and create. Any contributions you make are **greatly appreciated**.

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feat/AmazingFeature`)
3. Commit your Changes using **Conventional Commits** (`git commit -m 'feat(scope): add some AmazingFeature'`)
4. Push to the Branch (`git push origin feat/AmazingFeature`)
5. Open a Pull Request

Please read our [**CONTRIBUTING.md**](CONTRIBUTING.md) for more details on the coding style and build process.

---

## Code of Conduct

We are committed to providing a friendly, safe, and welcoming environment for all. Please read our [**Code of Conduct**](CODE_OF_CONDUCT.md) to understand the expectations for participating in this project.

---

## Our Supporters

Special thanks to the partners supporting this project:

[![PCBWay](pics/PCBway.png)](https://www.pcbway.com)

---

## License
This project is licensed under the **Apache License, Version 2.0**. See the [LICENSE](LICENSE) file for details.
