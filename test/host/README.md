MC3 diagnostic cache tests run without ESP-IDF or hardware:

```sh
cc -std=c11 -Wall -Wextra -Werror -pthread -Icomponents/asic/include \
  test/host/test_mc3_diagnostics.c components/asic/mc3_diagnostics.c \
  -o /tmp/test_mc3_diagnostics
/tmp/test_mc3_diagnostics
```

They cover failed-startup retention, missing versus zero counters, isolation of
counter epochs, invalid measurement durations, and clearing on reinitialization.
Dashboard rate conversion and domain/core mapping are covered by
`asic-diagnostics.component.spec.ts` in the AxeOS test suite.
