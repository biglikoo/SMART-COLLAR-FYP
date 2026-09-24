# Smart Collar FYP: Embedded Firmware & Data Analytics

This repository contains the core embedded C++ firmware, data collection scripts, and field validation dashboards for my undergraduate thesis project: **Development of a TinyML-Powered Smart Collar with NB-IoT Connectivity for Livestock Health Prediction**.

## Repository Architecture

The repository is structured to separate the low-level embedded ESP32 firmware iterations from the Python data analysis and validation tools:

SMART-COLLAR-FYP/
├── firmware/
│   ├── data_collection_esp32/       <-- Phase 1: Raw sensor logging & Wi-Fi UDP streaming
│   └── production_wrf_esp32/        <-- Phase 2: Final on-board TinyML WRF inference
├── dashboards/                      <-- Python tools & field validation scripts
├── data_raw/                        <-- Official multi-sensor empirical collection CSVs
├── .gitignore                       <-- Excludes compiler build artifacts & virtual envs
└── README.md                        <-- Project documentation

## Firmware Iterations

The hardware development followed a structured two-phase firmware approach on the ESP32 dual-core microcontroller:

1. **Phase 1 (data_collection_esp32/):** 
   * Dedicated to polling the multimodal sensor array (MPU-6050 IMU, DS18B20 dermal thermometer, DHT22 ambient sensor, and piezoelectric strain transducer) at 16 Hz.
   * Formats raw streams into 4-second sliding windows and transmits packets over a local Wi-Fi hotspot using UDP sockets for supervised labelling.

2. **Phase 2 (production_wrf_esp32/):** 
   * The production deployment firmware. 
   * Integrates the INT8-quantized Weighted Random Forest header file (Ecycle_WRF_Model.h) generated from the training pipeline.
   * Executes real-time on-device inference via FreeRTOS task partitioning (sub-millisecond execution latency) to classify health states and trigger event-driven telemetry flags.

## Dashboards & Validation
The dashboards/ directory hosts the custom Python scripts used during the field testing and data acquisition phases to capture telemetry feeds, monitor sensor orthogonality, and evaluate real-world classifier responses.

## Getting Started
To compile the ESP32 firmware, ensure you have the Espressif IoT Development Framework (ESP-IDF) or the VS Code ESP-IDF extension installed. Open either firmware subfolder as an independent workspace project to build and flash to your ESP32 hardware node.
