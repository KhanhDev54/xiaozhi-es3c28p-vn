# Otto Robot – Battery ADC calibration

## Current mapping (`power_manager.h`)

```cpp
BATTERY_LEVELS[] = {{2050, 0}, {2400, 100}};
```

- **0%:** ADC ≤ 2050 (divider 2×100k)
- **100%:** ADC ≥ 2400

## Why 2400 instead of 2450

At ~4.22 V (full cell), some boards (Md3-L, Md-x4l) read ADC ~2426–2430 and showed 94–95% with the 2450 threshold. Lowering the 100% point to 2400 aligns display with a full pack across board variants.

**Trade-off:** Mid-range % (40–85%) may read slightly higher than with 2450=100%.

## Does not affect charging

`battery_level_` is display/MCP only. Charge stop is handled by the charger IC; `IsCharging()` uses GPIO21, not the percentage.

## Related restores (firmware gốc)

- 0% threshold: 2050 (not vn2’s 2150)
- `PauseBatteryUpdate()` / `ResumeBatteryUpdate()` in `otto_controller.cc` during servo motion
- ADC `clk_src = ADC_RTC_CLK_SRC_DEFAULT`

## Rollback

- Full scale again: `{2050, 0}, {2450, 100}`
- Or keep 2450 and use snap-at-top (cách 2): after interpolation, `if (average_adc >= 2400) battery_level_ = 100;`

## Verify

1. ~4.22 V → 100% on all board variants  
2. ~4.0 V → not 100%  
3. Robot moving → % stable (battery pause active)
