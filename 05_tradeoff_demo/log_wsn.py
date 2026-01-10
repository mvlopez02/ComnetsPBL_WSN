import serial
import threading
import queue
import time
import csv
import argparse
import sys
import matplotlib.pyplot as plt

# ---------------------- Argument parsing ----------------------
parser = argparse.ArgumentParser()
parser.add_argument("--sensor-port", required=True)
parser.add_argument("--root-port", required=True)
parser.add_argument("--baudrate", type=int, default=115200)
parser.add_argument("--out", default="run")
parser.add_argument("--window-sec", type=float, default=20.0) #Live plot shows last 20 seconds
args = parser.parse_args()

# ---------------------- Serial setup ----------------------
sensor_ser = serial.Serial(args.sensor_port, args.baudrate, timeout=0.1)
root_ser   = serial.Serial(args.root_port, args.baudrate, timeout=0.1)

# ---------------------- Queues ----------------------
sensor_q = queue.Queue()
root_q   = queue.Queue()

# ---------------------- CSV files ----------------------
sensor_csv = open(f"{args.out}_sensor_samples.csv", "w", newline="")
root_csv   = open(f"{args.out}_root_received.csv", "w", newline="")

sensor_writer = csv.writer(sensor_csv)
root_writer   = csv.writer(root_csv)

sensor_writer.writerow(["pc_time", "seq", "sensor_ms", "temp_centi", "temp_C"])
root_writer.writerow(["pc_time", "seq", "sensor_ms", "temp_centi", "temp_C"])

sensor_csv.flush()
root_csv.flush()

# ---------------------- Reader threads ----------------------
def serial_reader(ser, prefix, q):
    while True:
        try:
            line = ser.readline().decode(errors="ignore").strip()
            if line.startswith(prefix):
                q.put((time.time(), line))
        except Exception as e:
            print("Serial error:", e)
            break

threading.Thread(target=serial_reader, args=(sensor_ser, "S,", sensor_q), daemon=True).start()
threading.Thread(target=serial_reader, args=(root_ser,   "R,", root_q),   daemon=True).start()

# ---------------------- Plot setup ----------------------
plt.ion()
fig, ax = plt.subplots()
ax.set_xlabel("Sensor time [s]")
ax.set_ylabel("Temperature [°C]")
samples_line, = ax.plot([], [], ".", label="Sensor samples")
recv_line,  = ax.plot([], [], "o-", label="Received")
ax.legend()

samples_t, samples_v = [], []
recv_t, recv_v = [], []

t0 = None

# ---------------------- Main loop ----------------------
try:
    while True:
        # -------- Sensor data --------
        while not sensor_q.empty():
            pc_t, line = sensor_q.get()
            _, seq, ms, temp = line.split(",")
            ms = int(ms)
            temp = int(temp)
            temp_C = temp / 100.0

            if t0 is None:
                t0 = ms

            t = (ms - t0) / 1000.0

            samples_t.append(t)
            samples_v.append(temp_C)

            sensor_writer.writerow([pc_t, seq, ms, temp, temp_C])
            sensor_csv.flush()

        # -------- Root data --------
        while not root_q.empty():
            pc_t, line = root_q.get()
            _, seq, ms, temp = line.split(",")
            ms = int(ms)
            temp = int(temp)
            temp_C = temp / 100.0

            if t0 is None:
                t0 = ms

            t = (ms - t0) / 1000.0

            recv_t.append(t)
            recv_v.append(temp_C)

            root_writer.writerow([pc_t, seq, ms, temp, temp_C])
            root_csv.flush()

        # -------- Plot update --------
        if samples_t:
            samples_line.set_data(samples_t, samples_v)
        if recv_t:
            recv_line.set_data(recv_t, recv_v)

        if samples_t:
            tmax = samples_t[-1]
            ax.set_xlim(max(0, tmax - args.window_sec), tmax + 0.5)
            ax.set_ylim(min(samples_v) - 0.5, max(samples_v) + 0.5)

        fig.canvas.draw()
        fig.canvas.flush_events()
        time.sleep(0.05)

#Ctrl + C
except KeyboardInterrupt:
    print("\nStopping...")

finally:
    sensor_csv.close()
    root_csv.close()
    sensor_ser.close()
    root_ser.close()
