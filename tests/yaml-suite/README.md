# yaml-test-suite

Conformance cases from [yaml-test-suite](https://github.com/yaml/yaml-test-suite)
(`data` branch, MIT licensed, copyright its authors), run against `stdlib/yaml.cxy`.

- `cases/<ID>/in.yaml`: the input.
- `cases/<ID>/in.json`: the expected value, or `cases/<ID>/error`: parsing must fail.
- `cases/<ID>/name`: the case title.
- `runner.cxy`: parses every case and prints `PASS <id>` / `FAIL <id>`.
- `__snapshots/runner.cxy.out`: the expected output. Cases that are not implemented
  yet show up as `FAIL`; fixing one changes the snapshot (`tests/runner.sh -u`).

Run from the build directory:

    ../tests/runner.sh -f yaml-suite
    # or directly
    ./cxy dev --stdlib ../src/cxy -o /tmp/yaml-suite ../tests/yaml-suite/runner.cxy && /tmp/yaml-suite ../tests/yaml-suite/cases

Only cases with a single-document `in.json` or an `error` marker are imported
(multi-document and sub-test cases are skipped). To refresh the data:

    git clone --depth 1 -b data https://github.com/yaml/yaml-test-suite /tmp/yts
    python3 tests/yaml-suite/import-suite.py /tmp/yts
