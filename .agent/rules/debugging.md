---
trigger: always_on
---

If there is a bug with an existing test, do not revise the test nor come up with "simplified" testing mechanisms to pinpoint the crash. Instead, use "debug.py" (and alterations to debug.py) to run the debugger and find out what's going on. Avoid directly invoking LLDB, use "debug.py".

Work until a crash is fully fixed. Fixes should NOT alter existing runtime functionality (language features).
