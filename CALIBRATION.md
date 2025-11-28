# 🎛️ Radar Calibration Guide: FocusGuard

The **FocusGuard** system relies on specific "Range Gating" settings to function correctly. The objective is to create a configuration where the sensor is **blind** to you (the user) but highly **sensitive** to anyone walking behind you.

> **Prerequisite:** You will need the **HLKRadarTool** app (iOS/Android) for this one-time setup.

---

## 📱 Step 1: Connect to the Sensor

1.  **Power Up:** Turn on your ESP32 (which powers the attached `HLK-LD2410` sensor).
2.  **Open App:** Launch the **HLKRadarTool** app on your smartphone.
3.  **Connect:** Enable Bluetooth and select the device named `HLK-LD2410B_xxxx`.

> **Note:** If prompted for a password, the default is usually `HiLink`. If that fails, try leaving it blank.

---

## 📏 Step 2: Understand the Gates

The sensor divides the physical distance into "Gates." Each gate represents a distance of approximately **0.75 meters**.



* **Gate 0 (0.00m - 0.75m):** Your body/chest (The User).
* **Gate 1 (0.75m - 1.50m):** Your desk edge and monitor area.
* **Gate 2 (1.50m - 2.25m):** The **"Danger Zone"** immediately behind you.
* **Gate 3 (2.25m - 3.00m):** The **"Approach Zone"**.
* **Gate 4 (3.00m - 3.75m):** The far background.

---

## 🔧 Step 3: Engineering Mode

To adjust specific gates, you must enter the advanced mode in the app:

1.  Tap **"Engineering Mode"** (sometimes labeled "Advanced Settings").
2.  Locate the sliders for **Move Sensitivity** and **Static Sensitivity** corresponding to each Gate number.

---

## 🛑 Step 4: The Tuning Settings

Apply the following values to configure the logic zones.

### A. The "Ignore" Zone (You)
*We want the sensor to completely ignore the first 1.5 meters so your typing and shifting in your chair do not trigger the alarm.*

| Gate | Zone Description | Movement Sens | Static Sens |
| :--- | :--- | :--- | :--- |
| **Gate 0** | User / Chest | **0** | **0** |
| **Gate 1** | Desk / Monitor | **0** | **0** |

### B. The "Guard" Zone (Intruders)
*We want high sensitivity for the area directly behind your chair to detect approaching people immediately.*

| Gate | Zone Description | Movement Sens | Static Sens |
| :--- | :--- | :--- | :--- |
| **Gate 2** | Danger Zone | **80 - 90** | *Default* |
| **Gate 3** | Approach Zone | **80 - 90** | *Default* |

### C. The Background (Office Noise)
*If there is foot traffic further away (4m+), we want to filter it out.*

| Gate | Zone Description | Movement Sens | Static Sens |
| :--- | :--- | :--- | :--- |
| **Gate 4+** | Far Background | **0 - 10** | **0 - 10** |

---

## 💾 Step 5: Save & Test

1.  **Save:** Tap **"Write to Sensor"** or **"Save"** within the app. This writes the configuration to the radar's non-volatile memory.
2.  **Reboot:** Power cycle the ESP32 (unplug and replug) to ensure the new gates are active.

### Verification Test
* **Sit at your desk:** The LED should remain **Green** (Flow State).
* **Intruder Test:** Have a friend walk up behind you. The LED should transition:
    * **Blue** (Detection) $\rightarrow$ **Yellow** (Warning) $\rightarrow$ **Red** (Alert).