# Box Sorter (Arduino Conveyor Sorting System)

Firmware for a conveyor machine that sorts boxes by height. A DC motor drives the belt, two ultrasonic distance sensors detect and measure each box, a servo moves the sorting gate, and a 16x2 I2C LCD shows the machine status.

## How it works

1. **Startup calibration.** With the belt empty, the side sensor takes 9 samples, keeps the valid ones, and stores the median as the empty-belt baseline. If fewer than 5 readings are valid, the system halts with `Side Sensor Err` instead of running uncalibrated.
2. **Two-sensor detection.** A box only counts when the top sensor reads at least 0.8 cm closer than the empty-belt distance (7.0 cm) AND the side sensor reads at least 0.8 cm closer than its own baseline, on 2 consecutive checks. This rejects single noisy echoes.
3. **Stop and measure.** The belt stops, waits 250 ms for vibration to settle, then takes 5 readings from the top sensor, 100 ms apart. It computes the median, discards any reading more than 0.35 cm away from it, and averages the rest. If fewer than 4 readings survive, the measurement is rejected as unstable and the box passes through with the gate unchanged.
4. **Classification.** Sensor-to-box distance <= 3.4 cm means LARGE: the gate goes to 160 deg. Distance >= 3.6 cm (and still below the 7.0 cm empty distance) means SMALL: the gate goes to 0 deg. Anything in between is uncertain: the gate keeps its previous position and the LCD shows `Unstable Read / Check Box`. The LCD `Height` line shows the sensor-to-box distance, not the box height itself; with the sensor mounted 7 cm above the belt, box height = 7.0 - reading.
5. **One box at a time.** After a decision the belt restarts immediately, and further detection is ignored until both sensors see the belt clear again.

## Hardware

### What the firmware expects (confirmed from the code)

| Part | Connection |
|---|---|
| Arduino-compatible board (Uno-class pinout; Servo + Wire libraries) | - |
| Ultrasonic distance sensor 1 (top, above the belt) | TRIG D2, ECHO D3 |
| Ultrasonic distance sensor 2 (side) | TRIG D4, ECHO D6 |
| DC motor + H-bridge driver with IN1/IN2/enable inputs | IN1 D7, IN2 D8, ENA D5 (PWM, fixed speed 80/255) |
| Servo (sorting gate) | Signal D9 (0 deg closed, 160 deg open) |
| 16x2 LCD with I2C backpack, address 0x27 | SDA/SCL (A4/A5 on Uno-class boards) |
| Conveyor belt mechanism | Driven by the DC motor |

### Compatible example parts

Typical parts for this design. These models are common choices that match the firmware interfaces, not yet confirmed against the physical build.

- Arduino Uno R3 (or Nano)
- 2 x HC-SR04 ultrasonic sensors
- L298N dual H-bridge motor driver
- SG90 or MG996R servo
- 16x2 LCD with PCF8574 I2C backpack
- DC gear motor + conveyor rollers

### Wiring guide (for the compatible parts)

- **Ultrasonic sensors:** VCC to 5V, GND to GND, TRIG/ECHO as in the table. Mount the top sensor about 7 cm above the belt to match `EMPTY_TOP_CM`; if you mount it at another height, measure the empty-belt distance and update the constant. The sensor needs a clear view of the box top, and soft or angled box surfaces give weaker echoes.
- **Motor driver (L298N-style):** motor to OUT1/OUT2; IN1 to D7, IN2 to D8, ENA to D5 with the ENA jumper removed so it takes PWM; motor supply to the driver's power input; driver GND shared with the Arduino GND.
- **Servo:** signal to D9; power from its own 5-6 V supply with the ground shared with the Arduino (see the power problem below).
- **LCD:** VCC 5V, GND, SDA to A4, SCL to A5 on Uno-class boards. If the screen stays blank, scan the I2C bus: these backpacks ship as 0x27 or 0x3F depending on the batch.

## Problems this firmware was built to solve

The sketch was written by trial and error: most of what goes beyond the happy path exists because of problems that came up while building the machine. They are the classic problems of ultrasonic sorting projects, and the sources at the end document the same issues and fixes.

1. **Noisy ultrasonic readings.** Single readings from a trigger/echo sensor bounce by centimeters when the target surface is angled or soft, and a leftover echo from a previous ping can produce a believable but wrong value if measurements are fired too fast. Fixes in the sketch: every reading passes a 2-200 cm validity window; classification uses the median of 5 samples taken 100 ms apart and rejects outliers more than 0.35 cm from the median; the two sensors are always read one after the other with a 35 ms gap, so they never hear each other's pulses.
2. **False box detections.** One bad echo from a passing hand or belt vibration looks exactly like a box. Fix: a box only counts when BOTH sensors agree, on 2 consecutive checks (`DETECT_CONFIRMATIONS`).
3. **Measuring a moving box.** Readings taken while the belt moves are inconsistent. Fix: the belt stops, waits 250 ms for the vibration to settle, and only then measures.
4. **Timeouts freezing the machine.** `pulseIn()` with its default 1-second timeout stalls the whole loop when no echo comes back. Fix: a 25 ms timeout (about 4 m of range), and zero readings are discarded everywhere.
5. **Uncalibrated start-up.** The side sensor's empty-belt distance depends on how it is mounted. Fix: a 9-sample median baseline at startup, and the machine refuses to run (`Side Sensor Err`) if the baseline cannot be measured.
6. **Servo jitter and brown-outs.** A servo and a DC motor sharing the Arduino 5V rail leads to jitter, random resets, and erratic behavior when the motor starts. The standard fix is hardware: power the servo from a separate 5-6 V supply with common ground, and add a 470 uF or larger capacitor across 5V/GND near the servo. Adafruit's servo guide and Arduino forum threads document exactly this failure and fix.
7. **Sorting the same box twice.** After a decision, the box is still in front of the sensors, and a naive loop classifies it again. Fix: the `waitingForClear` state; the next cycle starts only after both sensors see the belt empty.

## Calibration checklist

- Mount the top sensor, measure the empty-belt distance, and set `EMPTY_TOP_CM` to it.
- Adjust `LARGE_MAX_CM` and `SMALL_MIN_CM` to your box sizes, keeping a dead band between them.
- Keep the belt empty at power-on so calibration measures the belt, not a box.

## Sources

Background reading for the common problems and fixes above:

- HC-SR04 accuracy: noisy readings, ghost echoes, sensor-to-sensor interference, spacing measurements, temperature drift: https://zbotic.in/hc-sr04-accuracy-improving-ultrasonic-distance-readings/
- Servo misbehaving: reservoir capacitor across 5V/GND (Adafruit): https://learn.adafruit.com/adafruit-arduino-lesson-14-servo-motors/if-the-servo-misbehaves.md
- Servos browning out Arduino power supplies (Arduino Stack Exchange): https://arduino.stackexchange.com/questions/94556/how-can-i-get-servos-to-not-brownout-my-arduinos-and-power-supply
- Interfacing multiple ultrasonic sensors without crosstalk (The Engineering Projects): https://www.theengineeringprojects.com/2015/02/interfacing-multiple-ultrasonic-sensor-arduino.html

## Status

The component models listed under "compatible example parts" are common parts that match the firmware, not yet confirmed against the physical build. Wiring photos of the actual machine will be added.
