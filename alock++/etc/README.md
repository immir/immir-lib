# Fallback Linux-PAM headers

`security/pam_appl.h`, `security/_pam_types.h`, and
`security/_pam_compat.h` are unmodified copies from
`pam-devel-1.5.1-28.el9_8.1.x86_64`. Their original notices are preserved.
The two companion headers are required by `pam_appl.h`.

For a Linux system with the PAM runtime installed but no PAM development
headers, build with:

```sh
make clean
make CFLAGS='-Wall -O2 -I. -I./local/include -idirafter ./etc'      LIBS='-lX11 -lXext -lXrender -l:libpam.so.0 -lm -lXrandr'
```

`-idirafter` prefers installed system headers and uses these copies as a
fallback. `-l:libpam.so.0` uses GNU ld's exact-filename syntax to link the
runtime library without requiring an unversioned `libpam.so` symlink.
This command is for the default configuration (Xrandr enabled, WebKit disabled).
The runtime PAM library, service configuration, and authentication modules
must still be installed on the target system. The normal build is unchanged.
