# Flux Queries for ESP32-S3 Weather Station

This document contains the Flux queries used for visualizing sensor data and host telemetry in Grafana.

## ESP32 Sensor Data

The ESP32 publishes a single JSON message to `sensors/esp32/data` at QoS 1. Telegraf flattens each JSON key into its own field under measurement `weather`:

| `_field`      | Source             | Notes                                      |
|---------------|--------------------|--------------------------------------------|
| `temperature` | `temperature` key  | °C, float                                  |
| `humidity`    | `humidity` key     | %RH, float                                 |
| `status`      | `status` key       | 0=OK, 1=timeout, 2=CRC err, 3=exception    |
| `poll_count`  | `poll_count` key   | RTU cycles since boot                      |

SDM120 power-meter values are not published to MQTT/InfluxDB yet. They are available from the ESP32 Modbus TCP slave holding registers documented in the README.

### Temperature (°C)
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "temperature")
  |> yield(name: "temperature")
```

### Humidity (%RH)
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "humidity")
  |> yield(name: "humidity")
```

### ESP Status
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "status")
  |> yield(name: "status")
```

### Poll Count
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "poll_count")
  |> yield(name: "poll_count")
```

### All fields in one query
(Grafana splits series by `_field`):
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
```

## Query Variants

- **Smoothed for time-series panels** — insert before `yield`:
  ```flux
    |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
  ```
- **Latest value for Stat / Gauge panels** — append instead of or after `yield`:
  ```flux
    |> last()
  ```

## Pi Host Telemetry

Telegraf is configured to collect host metrics from the Raspberry Pi. This data is stored in the same `sensors` bucket.

**Measurements available:**
- `cpu`: `usage_user`, `usage_system`, `usage_idle`, etc.
- `mem`: `used_percent`, `available`, etc.
- `system`: `load1`, `uptime`, etc.
- `disk`: `free`, `used_percent`, etc.
- `pi_cpu_temp`: raw value in milli-Celsius (divide by 1000 for °C).

### Example: Pi CPU Temp (°C)
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "pi_cpu_temp")
  |> map(fn: (r) => ({ r with _value: float(v: r._value) / 1000.0 }))
  |> yield(name: "pi_cpu_temp")
```
