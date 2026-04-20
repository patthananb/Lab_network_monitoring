# Flux Querying Guide for Grafana

This guide covers advanced Flux query patterns, Grafana variables, and optimization techniques for the Weather Station dashboard.

## Grafana `v` Variables

Every Flux query in Grafana has access to a built-in `v` object injected automatically by the InfluxDB datasource:

| Variable | Description | Example value |
|---|---|---|
| `v.timeRangeStart` | Start of the dashboard time picker | `now-24h` |
| `v.timeRangeStop` | End of the dashboard time picker | `now` |
| `v.windowPeriod` | Auto-calculated interval that fits the panel width | `5m` |

You never define `v` — Grafana updates it whenever the user changes the time range and reruns all queries automatically.

## Fixing "Too Many Datapoints"

Grafana will warn when a query returns more points than the panel can draw. Fix it by adding `aggregateWindow()` before `yield()`:

```flux
|> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
```

`v.windowPeriod` auto-sizes the bucket so the number of returned points always matches the panel's pixel width.

## `aggregateWindow()` Reference

```flux
|> aggregateWindow(every: <duration>, fn: <function>, createEmpty: <bool>)
```

| Parameter | Purpose |
|---|---|
| `every` | Size of each time bucket (`1m`, `5m`, `v.windowPeriod`) |
| `fn` | Aggregation applied per bucket |
| `createEmpty` | `false` = skip empty buckets; `true` = insert nulls for gaps |

### Choosing `fn`:

| `fn` | When to use |
|---|---|
| `mean` | Continuous sensors — temperature, humidity, CPU % |
| `last` | Status values — ping result, online/offline |
| `max` / `min` | Peaks and troughs — anomaly detection, alerts |
| `sum` | Totals — event counts, rainfall |

## Query Patterns by Panel Type

### Time-series panel
Use `aggregateWindow` to reduce points:
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "temperature")
  |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
  |> yield(name: "temperature")
```

### Stat / Gauge panel
Use `last()` to show the most recent value only:
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "humidity")
  |> last()
  |> yield(name: "humidity")
```

### State timeline / status panel
Use `aggregateWindow` with `fn: last` to preserve discrete state changes:
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "ping")
  |> filter(fn: (r) => r["_field"] == "result_code")
  |> aggregateWindow(every: 5m, fn: last, createEmpty: false)
```

## Advanced Measurement Examples

### Pi CPU Load % (total)
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "cpu")
  |> filter(fn: (r) => r["cpu"] == "cpu-total")
  |> filter(fn: (r) => r["_field"] == "usage_idle")
  |> map(fn: (r) => ({r with _value: 100.0 - r._value}))
  |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
  |> yield(name: "cpu_usage")
```

### Networking Outage Log (Table panel)
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "ping")
  |> filter(fn: (r) => r["_field"] == "result_code")
  |> filter(fn: (r) => r._value > 0)
  |> map(fn: (r) => ({_time: r._time, status: "Offline", result_code: r._value}))
  |> sort(columns: ["_time"], desc: true)
```
