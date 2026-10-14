# Almagest-G integration status

No representative Almagest-G host or VM is available. Its compiler, libc,
systemd layout, storage policy, and compatible Nix runtime are not established.
The development branch therefore cannot claim native build, bootstrap, or
application compatibility on Almagest-G.

The intended installation separates ASTRA's source build from Nix runtime
bootstrap. A target operator must check the local GCC and dependency versions,
compile and test the complete archive offline, then use a version-pinned Nix
runtime payload verified for the target. `bootstrap/preflight.sh LOG-PATH`
records read-only diagnostics and stops because that payload is not supplied.

Per-user ASTRA profiles expose applications through their `profile/bin` path
only when the user opts to add that path to `PATH`. No shell, desktop, service,
PAM, kernel, bootloader, systemd, or libc integration is currently installed.
A privileged system applications profile is not implemented. Target tests must
confirm daemon/socket permissions, substitution, profile transitions, reboot
persistence, GC roots, and preservation of protected OS components.
