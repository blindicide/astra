# Command and JSON interface (0.3.0-dev)

The command line is `astra [switches] COMMAND [arguments]`. `--help` prints
usage; `--version` prints the development version without opening state.
`--profile NAME` selects a private user profile for `list`, `why`, `history`,
`install`, `remove`, `upgrade`, `rollback`, `clean`, or `repair`. Profile names
are validated before use. `--yes` confirms a reviewed change plan; it never
authorizes untrusted cache settings, signature failures, or source builds.
`--allow-build` is a separate, explicit source-build consent for profile
changes. `--dry-run` reports the change plan without applying it.

`--offline` prevents repository refresh and cache network access. Cached
`search` and `info` use the selected SQLite index. Profile operations use
local store contents only and fail when an output is absent. Offline `doctor`
skips the remote cache probe, while still checking the local daemon.

`--quiet` suppresses successful operation output on profile changes, clean,
repair, and repository update. Errors still go to standard error. `--verbose`
reports the state directory to standard error. `--json`, `--quiet`, and
`--verbose` are mutually exclusive. Switches rejected as meaningless for a
command return status 2. Diagnostic failures return status 1; successful
commands return 0.

The following commands support `--json`. Each response is one JSON object per
line with integer `schema_version: 1`:

| Command | Stable top-level fields |
| --- | --- |
| `--version` | `version` |
| `doctor` | `command`, `database`, `profiles_verified`, `backend`, `runtime`, `cache_policy`, `daemon`, `cache` |
| `verify` | `command`, `database`, `profiles_verified`, `backend` |
| `repo list` | `command`, `repositories` |
| `repo update`, `update` | `command`, `revision`, `nar_hash`, `indexed_packages`, `profiles_changed` |
| `search QUERY` | `command`, `packages` |
| `info PACKAGE` | `command`, `package` |
| `history show ID` | `command`, `hash`, `id`, `status`, `sequence`, `previous_generation`, `target_generation`, `intent`, `events` |

`database` contains `integrity` and `schema_version`; `backend` contains
`found`, `candidate_version`, `version`, and `detail`. Doctor's `runtime`,
`cache_policy`, `daemon`, and `cache` are individual check objects. Offline
cache detail is `skipped offline`; its `reachable` value is false because no
probe was made. Package availability is `unverified` until a supported Nix
operation confirms it. The selected repository revision is a 40-character
immutable Git revision. Repository list entries and update results include
the locked `nar_hash`; a migrated historical revision can have an empty hash
until it is fetched again. `profiles_changed` is false on metadata updates.

Other profile commands currently reject `--json` explicitly. Human-facing
output and errors are not a machine-readable interface. This development
schema may change before a stable release; consumers should check its version.
