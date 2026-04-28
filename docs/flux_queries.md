# Flux Queries for ESP32-S3 Weather Station

This document contains the Flux queries used for visualizing sensor data and host telemetry in Grafana.

## ESP32 Sensor Data

The ESP32 publishes a single JSON message to `sensors/esp32/data` at QoS 1. Telegraf flattens each JSON key into its own field under measurement `weather`:

| `_field`      | Source             | Notes                                      |
|---------------|--------------------|--------------------------------------------|
| `temperature` | `temperature` key  | °C, float                                  |
| `humidity`    | `humidity` key     | %RH, float                                 |
| `status`      | `status` key       | Weather RTU status: 0=OK, 1=timeout, 2=CRC err, 3=exception, 4=bad response |
| `poll_count`  | `poll_count` key   | Successful weather RTU cycles since boot   |
| `power_status` | `power_status` key | SDM120 RTU status: 0=OK, 1=timeout, 2=CRC err, 3=exception, 4=bad response |
| `power_voltage` | `power_voltage` key | V, float                                 |
| `power_current` | `power_current` key | A, float                                 |
| `power_watts` | `power_watts` key  | W, float                                  |
| `power_apparent_va` | `power_apparent_va` key | VA, float                         |
| `power_reactive_var` | `power_reactive_var` key | VAr, float                       |
| `power_factor` | `power_factor` key | ratio, float                              |
| `power_frequency` | `power_frequency` key | Hz, float                           |
| `power_energy_kwh` | `power_energy_kwh` key | kWh, float                         |

If one RTU device fails, the firmware still publishes that device's status code and omits only that device's value fields.

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

### Power Voltage (V)
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "power_voltage")
  |> yield(name: "power_voltage")
```

### Power Current (A)
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "power_current")
  |> yield(name: "power_current")
```

### Active Power (W)
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "power_watts")
  |> yield(name: "power_watts")
```

### Apparent Power (VA)
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "power_apparent_va")
  |> yield(name: "power_apparent_va")
```

### Reactive Power (VAr)
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "power_reactive_var")
  |> yield(name: "power_reactive_var")
```

### Power Factor
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "power_factor")
  |> yield(name: "power_factor")
```

### Frequency (Hz)
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "power_frequency")
  |> yield(name: "power_frequency")
```

### Total Active Energy (kWh)
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "power_energy_kwh")
  |> yield(name: "power_energy_kwh")
```

### SDM120 Status
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "power_status")
  |> yield(name: "power_status")
```

### All SDM120 power fields
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "power_voltage" or
                       r["_field"] == "power_current" or
                       r["_field"] == "power_watts" or
                       r["_field"] == "power_apparent_va" or
                       r["_field"] == "power_reactive_var" or
                       r["_field"] == "power_factor" or
                       r["_field"] == "power_frequency" or
                       r["_field"] == "power_energy_kwh")
  |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
  |> yield(name: "sdm120_power")
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
