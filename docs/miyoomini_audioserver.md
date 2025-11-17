# Miyoo Mini audioserver preload shim

The Miyoo Mini firmware historically shipped two different `as_preload.so`
files ("old" and "new") to let audioserver intercept MI_AO traffic.  RetroArch
now carries a single, portable implementation under
`audio/drivers/mi_ao_sendframe_hook.c` that replaces both binaries.

## Building the shim

```
arm-linux-gnueabihf-gcc -shared -fPIC -O2 -o as_preload.so \
   audio/drivers/mi_ao_sendframe_hook.c -ldl
```

Copy the resulting `as_preload.so` next to RetroArch and start it with:

```
LD_PRELOAD=./as_preload.so ./retroarch
```

The shim auto-detects the correct `libmi_ao.so` location, but you can force a
specific one via `MI_AO_PRELOAD_SO=/path/to/libmi_ao.so` before launching
RetroArch.  This covers both the legacy and current audioserver builds without
needing to ship two separate preload binaries.
