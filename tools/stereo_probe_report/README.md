# Stereo probe report evaluator

The device probe records Camera2 facts in a versioned JSON report. This tool
applies the Milestone 16 gates on the host:

```bash
python3 -m unittest discover -s tools/stereo_probe_report -p 'test_*.py'
python3 tools/stereo_probe_report/evaluate.py stereo-probe-report.json \
  --physical-baseline-meters 0.064
```

With an authorized Quest connected, the repository wrapper pulls the app-private
report and invokes the same evaluator:

```bash
./scripts/pull_stereo_probe.sh --physical-baseline-meters 0.064
```

The optional physical baseline is the measured distance between the two RGB
camera optical centres. When omitted, the evaluator checks only that the
Camera2-derived baseline is plausible and reports the missing cross-check as a
warning.

`PASS_WITH_DEBT` means the non-surrogable capture path passed but calibration or
physical sync still needs an explicit validation artifact. The individual
P1A/P1B/P2/P3/P3O/P4 gates keep missing factory calibration separate from
failed concurrent capture and from timestamp-only synchronization evidence.
