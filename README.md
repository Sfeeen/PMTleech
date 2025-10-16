# 🚀 PMT Leech
#### A Parallel Memory Transactions Monitor

<p align="center">
  <img src="/PMTLeech_concept.drawio.png?raw=true" alt="PMT Leech Concept" title="PMT Leech PCB" width="600"/>
</p>

**PMT Leech** is a hardware + software tool that lets you monitor live memory accesses from a CPU to an external parallel memory chip.  
It streams transactions in real-time, while giving you full control over the system clock — allowing **pausing, stepping, and instruction-level debugging**.

---

## 🔧 When Should You Use PMT Leech?

PMT Leech is especially useful when working with **older electronic systems** that meet these two conditions:
- ✅ The system uses a **parallel memory chip** (like SRAM or EPROM)
- ✅ The CPU operates with an **external clock input**

---

## ⚙️ How It Works

By attaching PMT Leech to your memory chip (via clipper) and supplying your own clock signal to the CPU:
- You gain **live streaming** of memory transactions
- You can **pause or step** through firmware execution  
- You can **reverse engineer or debug** embedded firmware in a safe, controlled way

---

## 🧰 Hardware Overview

<p float="left">
  <img src="3D_view_PMTLeech.png?raw=true" alt="PMT Leech PCB" title="PMT Leech PCB" width="300"/>
  <img src="image_PMT_leech_hardware_in_use.png?raw=true" alt="PMT Leech In Use" title="In Use" width="300"/>
</p>

---

## 🖥️ Software Features

<p align="center">
  <img src="/PMTLeech_software.png?raw=true" alt="PMT Leech Software" title="Software UI" width="1000"/>
</p>

- 🧩 Configurable pinout: Supports 28, 30, or 32-pin chips
- 📡 Live recording of transactions (up to 70kHz)
- 🎨 Interactive memory viewer with color-coded highlights
- ⏱ Step by **clock** or **instruction**
- 🧠 Detect and label **foreign packets** (from other devices on the memory bus)
- 💾 Off-circuit memory read/write with chip socket

---

## 🎞️ Demo

<p align="center">
  <img src="Animation.gif" alt="Demo Animation" title="PMT Leech Demo" width="800"/>
</p>

---

## 📋 Requirements

### ✅ Memory Chip
- Parallel bus
- ≤ 8-bit data
- ≤ 18 address lines
- 5V compatible (or use 330Ω series resistors)

### ✅ CPU
- Must allow external clock input
- 5V compatible (or 330Ω resistors)

### ⚠️ Note on Timing
The CPU will be **underclocked** (e.g., 8 MHz → 50 kHz). This may affect peripherals like UART:
- Ex: 9600 baud becomes ~60 baud — comms may break.

---

## 🧲 In-Circuit Clip Options

- [📄 3M Test Clip Brochure (PDF)](research/in%20circuit%20connection%20to%20chips/Test%20Clip%20Brochure%203M.pdf)

<p float="left">
  <img src="research/in%20circuit%20connection%20to%20chips/test_clips.png?raw=true" alt="3M Clips" width="400"/>
  <img src="clipper.jpg?raw=true" alt="Clipper Photo" width="400"/>
</p>

---

## 🧠 Supported Memory Types

- EEPROM
- EPROM
- SRAM — **especially valuable** for volatile firmware systems

💡 Many older systems used **SRAM for firmware** due to speed advantages. PMT Leech lets you read these **volatile** setups **without desoldering**.

---

## 🔍 Example Use Cases

- **Bypass password protection**: Record firmware transactions during password validation and discover comparison logic or values.
- **Firmware reverse engineering**: Step through opcode streams and infer instruction flow.
- **Brownout glitching**: Control timing to inject faults and analyze behavior.
  - Related project: [Siemens Advanced Operator Panel](https://github.com/Sfeeen/Siemens-Advanced-Operator-Panel)

---

## 🛠️ Hardware Revisions

### PMT Leech v2 (Current)
<p align="center">
  <img src="3D_view_PMTLeech.png?raw=true" alt="PMT Leech v2" width="1000"/>
</p>

### PMT Leech v1 (Development Stage)
<p align="center">
  <img src="/hardware_development/3Dview_PMT_leech.png?raw=true" alt="PMT Leech v1" width="1000"/>
</p>

---

##

📧 **Contact**: `svenonderbeke [at] hotmail [dot] com`  
More info to be added soon.

---

### ☕ Support

If you’d like to support the development of PMT Leech, you can donate via Ko-fi:
[![Buy Me a Coffee](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/sventronics)

## 🧪 Version

**Current Version:** `PMT Leech v2`
