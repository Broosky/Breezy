# 💡 Breezy

A temperature-triggered fan controller that automatically powers one or more box fans when the AC system is running. Designed to
improve airflow through long corridors or distant rooms without requiring manual intervention.

Breezy supports multiple configurable automatic modes, along with a manual override that forces the fans on and bypasses automatic
control. The goal is a "set it once and forget it" solution. The output stage supports loads up to 600W, allowing multiple fans or 
other devices to be controlled simultaneously.

The system is intended for use with standard non-smart thermostats where the fan control hardware must remain electrically isolated
and physically distant from the AC unit. The simplest detection method uses a thermistor placed near an air vent to sense cool
airflow when the AC engages.

Originally built for a family member, the project also serves as an introduction to embedded systems development.

## 🔹 Alternative Detection Methods

- Compressor current sensing using a custom clamp ammeter:
  - Toroid current transformer → Hall effect sensor → comparator.
  - Advantages:
    - Detects compressor engagement directly.
    - More tightly synchronized with AC operation.
  - Disadvantages:
    - Less practical when controlled fans are located far from the HVAC unit.
 
## 🔹 Future Ideas

- Upgrade to a Wi-Fi or Bluetooth-enabled MCU to broadcast AC activity to remote receiver nodes throughout the home.
- Add a dedicated configuration mode with a small HMI for runtime setting changes.
- RTC support for scheduled behaviours and time-based logic.

> If you found this project useful, interesting, or worth keeping an eye on, consider giving it a ⭐️.
> It helps others discover the project and motivates me to keep building and sharing more.

## 🔹 Construction

![Breezy 1](<Construction/Breezy 1.jpg>)

![Breezy 2](<Construction/Breezy 2.jpg>)

## 🔹 Rev 1 Schematic

![Rev 1](<Schematics/Rev 1.png>)

## 🔹 V1.1.0 Firmware For Rev 1 Schematic

- Additional modes of operation templates.
- Quiet times rough-in.
- Gamma corrected LED pulsing.
- General improvements, refactoring.

## 🔹 V1.0.0 Firmware For Rev 1 Schematic

- Initial release.

##

> Educational Use Notice: This project is provided for educational and learning purposes only. You are welcome to read, study, and experiment
> with this software and/or hardware. It is not intended for commercial use. This software and/or hardware is provided "as is", without warranty
> of any kind. The author assumes no responsibility for any damages or issues resulting from its use.