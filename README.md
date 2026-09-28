# OPESYMarqueeConsole
Marquee Console Submission for CSOPESY
# CSOPESY Semi-Major Output 1 - ASCII Marquee Console

A console-based ASCII marquee program developed for **CSOPESY Semi-Major Output 1**. The program supports configurable ASCII text animation, runtime commands, adjustable speed, and concurrent command input while the marquee is running.

## Group Members

1. Simon Gabriel M. Kabiling

**Section:** S01

---

## Entry File

```text
marquee.cpp
```

Contains the `main()` function and the complete implementation of the command interpreter, animation thread, and console display.

## Files

| File          | Description                                                                              |
| ------------- | ---------------------------------------------------------------------------------------- |
| `marquee.cpp` | Full source code containing the command interpreter, animation thread, and display logic |
| `config.txt`  | Startup configuration file; must be placed in the same folder as `marquee.exe`           |

---

## Requirements

* Windows 10 or Windows 11
* MinGW-w64 `g++`

  * MSYS2 UCRT64 is recommended
* C++17
* POSIX threads support

MSVC is also supported:

```text
cl /EHsc /std:c++17 marquee.cpp
```

---

## How to Build

Using MinGW-w64, compile with **`g++`**, not `gcc`:

```bash
g++ -std=c++17 -O2 -pthread marquee.cpp -o marquee.exe
```

Using `gcc` instead of `g++` may result in linker errors involving undefined C++ references.

---

## How to Run

1. Make sure the following files are in the same folder:

```text
marquee.exe
config.txt
```

2. Open a terminal in that folder.

3. Run:

```text
marquee.exe
```

4. The program will display the command prompt:

```text
Command>
```

5. Enter one of the supported commands.

For the best results, run the program in a real console window such as **Windows Terminal, PowerShell, or Command Prompt**. If running from an IDE, use an external console window.

---

## Commands

| Command           | Description                                            |
| ----------------- | ------------------------------------------------------ |
| `help`            | Displays all available commands and their descriptions |
| `start_marquee`   | Starts the marquee animation                           |
| `stop_marquee`    | Stops/pauses the marquee animation                     |
| `set_text <text>` | Sets the text displayed by the marquee                 |
| `set_speed <ms>`  | Sets the marquee refresh interval in milliseconds      |
| `exit`            | Stops the animation thread and exits the program       |

### Examples

Set the marquee text:

```text
set_text Hello world!
```

Set the animation speed:

```text
set_speed 300
```

Start the marquee:

```text
start_marquee
```

Stop the marquee:

```text
stop_marquee
```

Exit the program:

```text
exit
```

Commands are **not case-sensitive**.

Runtime commands override the corresponding values loaded from `config.txt`.

---

## Configuration

The program reads its initial settings from `config.txt`.

### Configuration Keys

| Key              | Description                                                     |
| ---------------- | --------------------------------------------------------------- |
| `console_width`  | Console width in characters. Valid range: 40-300                |
| `console_height` | Console height in rows. Valid range: 15-100                     |
| `ascii_height`   | Height of the marquee area in rows. Valid range: 5-20           |
| `default_speed`  | Starting refresh interval in milliseconds. Valid range: 1-60000 |
| `default_text`   | Starting marquee text                                           |
| `spacing`        | Blank columns between the end of the text and its wrapped copy  |
| `step`           | Number of columns moved per animation frame                     |
| `poll_ms`        | Keyboard polling interval in milliseconds. Valid range: 1-100   |

Missing or invalid configuration values automatically fall back to safe default values.

### Example `config.txt`

```text
console_width=120
console_height=30
ascii_height=10
default_speed=300
default_text=Hello world!
spacing=10
step=1
poll_ms=50
```

---

## Program Behavior

The marquee animation runs on a separate thread. This allows users to enter commands while the animation is running.

The program uses Windows console output functionality to update the display without repeatedly clearing the entire screen. This reduces flickering and keeps the command input area stable.

The main components of the program are:

* **Command Interpreter** - Reads and processes user commands.
* **Animation Thread** - Handles the continuous marquee movement.
* **Configuration Loader** - Reads startup settings from `config.txt`.
* **Console Display** - Renders and updates the ASCII marquee.

---

## Project Structure

```text
ASCII-Marquee-Console/
│
├── marquee.cpp
├── config.txt
└── marquee.exe
```

`marquee.exe` is generated when the source code is compiled and does not need to be included in the source repository if the project submission only requires the source code and configuration file.

---

## Sample Execution

```text
Command> set_text Hello world!
Text updated.

Command> start_marquee
Marquee started.

Command> stop_marquee
Marquee stopped.

Command> exit
Exiting...
```

---

## Notes

* `config.txt` must be located in the same directory as `marquee.exe`.
* The program is intended to run in a Windows console environment.
* The animation and command input operate concurrently.
* Runtime settings can override the initial configuration.
* Use `g++` rather than `gcc` when compiling with MinGW-w64.
