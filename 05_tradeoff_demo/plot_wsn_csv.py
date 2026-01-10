import csv
import matplotlib.pyplot as plt

SENSOR_FILE = "run1_sensor_samples.csv"
ROOT_FILE   = "run1_root_received.csv"

sensor_t = []
sensor_v = []

recv_t = []
recv_v = []

# -------------------- Load sensor samples --------------------
with open(SENSOR_FILE, newline="") as f:
    reader = csv.DictReader(f)
    t0 = None
    for row in reader:
        ms = int(row["sensor_ms"])
        temp = float(row["temp_C"])

        if t0 is None:
            t0 = ms

        t = (ms - t0) / 1000.0
        sensor_t.append(t)
        sensor_v.append(temp)

# -------------------- Load received samples --------------------
with open(ROOT_FILE, newline="") as f:
    reader = csv.DictReader(f)
    t0 = None
    for row in reader:
        ms = int(row["sensor_ms"])
        temp = float(row["temp_C"])

        if t0 is None:
            t0 = ms

        t = (ms - t0) / 1000.0
        recv_t.append(t)
        recv_v.append(temp)

# -------------------- Plot --------------------
plt.figure(figsize=(10, 5))

plt.plot(sensor_t, sensor_v, ".", label="Sensor samples (local)", alpha=0.6)
plt.plot(recv_t, recv_v, "o-", label="Received samples (root)")

plt.xlabel("Time [s]")
plt.ylabel("Temperature [°C]")
plt.title("WSN sampling vs transmission rate")
plt.legend()
plt.grid(True)

plt.tight_layout()
plt.show()
