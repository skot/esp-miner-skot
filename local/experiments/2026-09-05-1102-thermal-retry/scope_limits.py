"""User-authorized measurement allowance; not an ASIC voltage-rating change."""
import numpy as np

RAW_LIMIT_V = 1.25
MEAN_LIMIT_V = 1.22
MEAN_WINDOW_S = 20e-6

def assess(lower, upper, sample_interval_s):
    count = max(1, round(MEAN_WINDOW_S / sample_interval_s))
    result = dict(raw_limit_v=RAW_LIMIT_V, mean_limit_v=MEAN_LIMIT_V,
                  mean_window_samples=count, sample_interval_s=sample_interval_s,
                  requested_mean_window_s=MEAN_WINDOW_S,
                  effective_mean_window_s=count * sample_interval_s,
                  mean_window_resolved=bool(sample_interval_s <= MEAN_WINDOW_S))
    reasons = []
    for name, values in [('lower', lower), ('upper', upper)]:
        values = np.asarray(values, dtype=float)
        if len(values) < count or not np.all(np.isfinite(values)):
            reasons.append(name + ': invalid/short waveform')
            continue
        prefix = np.r_[0.0, np.cumsum(values)]
        peak = float(values.max())
        mean_peak = float(((prefix[count:] - prefix[:-count]) / count).max())
        result[name + '_max_v'] = peak
        result[name + '_max_moving_mean_v'] = mean_peak
        if peak >= RAW_LIMIT_V:
            reasons.append(name + ': raw voltage limit')
        if mean_peak >= MEAN_LIMIT_V:
            reasons.append(name + ': moving-mean voltage limit')
    result['reasons'] = reasons
    result['voltage_accepted'] = not reasons
    return result
