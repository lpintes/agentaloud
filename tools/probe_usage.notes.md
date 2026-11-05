# probe_usage.py -- limits without a prompt (claude-gui-lkk.5.8)

Measured 2026-11-05 on CLI 2.1.314, Pro subscription.

- `control_request` `{"subtype": "get_usage", "skip_behaviors": true}`,
  sent after `initialize`, answers in about 0.3 s with `subtype: success`.
  No prompt goes out, so it costs no credit. Without `skip_behaviors` the
  CLI scans every transcript from the last seven days.
- `response.rate_limits_available` is false for an API key, Bedrock or
  Vertex; `rate_limits` is then null.
- `response.rate_limits.five_hour` and `.seven_day`:
  `{"utilization": 26, "resets_at": "2026-11-05T15:00:00.242538+00:00", ...}`.
  Utilization is a PERCENT (0-100), not 0..1 as in `rate_limit_event`, and
  `resets_at` is an ISO 8601 string, not unix seconds.
- `response.rate_limits.limits[]` has `kind` (`session`, `weekly_all`),
  `percent`, `severity` (`normal` seen) and `resets_at`. Other severity
  values were not seen.
- Lots of other windows (`seven_day_opus`, codenames) are null.
- Dead end: `sdk_status` in the binary is only the compaction status
  (`status: "compacting"`), not the rate limits.
