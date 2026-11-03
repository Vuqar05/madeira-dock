# Madeira Dock — implementation handoff, ml2000 (2026-09-25)

## Original launch entry keys (2026-10-05)

LaunchApp uses the validated `MADEIRA_STEAM_HOST_LAUNCH_OPTION` for its initial
request and all retries. It carries the original `config.launch` key, not a
filtered array offset. An absent variable keeps 0 for older launchers; invalid
input fails closed with result 45. A bounded, decoded error-22 detail explicitly
identifying a missing requested entry stops the configuration wait. Reports
contain only numeric launch-option fields; the detail itself is never logged.

Madeira resolves its installed Windows/default entry before native logoff,
excludes DLC-only entries and refetches legacy caches without original keys.
Both app and host must be rebuilt. Authentication, ownership and original game
DRM remain unchanged.

The same C source passed all four ASan/UBSan suites on Linux and macOS, and x64
and i386 builds with warnings as errors. The app's Swift ASan regressions also
passed. A Debug IPA containing this host and the companion Madeira changes was
installed on an M2 iPad with iPadOS 27; the tester confirms that the previously
failing game starts. This establishes startup, not extended gameplay coverage.

## A manager that answers counts as started (2026-09-29)

Device logs of Madeira's one-time-install batch on the upstream tree
(test-all-3/4): `dockhost.exe --start-services` started `services.exe`, the
manager came up within about a second (explorer's COM start connected to it
and it started rpcss), but `__wine_SvcctlStartedEvent`, which this host
created, was never set or opened by anyone (the wineserver's event history:
one create, zero sets), although services.exe had reached the line after its
SetEvent (its process-monitor thread existed). `start_manager` waited the whole
30 s for that event, main.c's 20 s bound fired first, and the batch recorded
`services timeout` with a working manager behind it. Why the event does not
reach this waiter on iOS is not known yet.

`start_manager` now waits in slices of at most `SH_SCM_POLL_MS` (250 ms,
`sh_scm_poll_slice_ms` in validation.c) and calls OpenSCManagerW between them:
an answer returns at once, 1722/1723 keep waiting, another error fails as
before; the event, the process's exit and the 30 s bound end the wait as
before. The CEG path shares `start_manager`, so it no longer waits out the
event either. Windows (manager already running) never reaches this code.
Validation: `tools/check.sh` (3 new assertions), both architectures build with
`-Werror`, privacy scan clean. Not yet run on a device.

## ml2014 — `--start-services` for the one-time-install batch (2026-09-27)

Device log 103: Madeira's install batch (`cmd.exe /c call
madeira-dock-installers.cmd & dockhost.exe`) runs before this host, so no
service manager exists while a game's installers run (`\pipe\svcctl`
c0000034 in the log). New CLI mode `dockhost.exe --start-services` (checked
before the probe opt-in; no Steam client loaded, no report file): the first
OpenSCManagerW; only on RPC_S_SERVER_UNAVAILABLE (1722) `start_manager`
(unchanged ml2000 bounded start, event `install-scm-started`). A manager this
call started stays running (Wine system process, ends with the session); the
later host run finds it already running and so never owns or stops it. If it
never answered, the half-started one is terminated. Output: stderr
`[steam-host] ml2014 install-scm=<2 started|1 already|0 off|-1 failed>`
(+ `install-scm-error=<Win32>` on failure) and one stdout line
`services <word>` for the batch's result file; exit 0 always.
`MADEIRA_DOCK_INSTALL_SCM=0`: no-op (`services off`). Pure decisions in
validation.c (`sh_install_scm_should_spawn/outcome/word`), 15 new assertions.
Windows smoke test (native SCM): `services already`, kill switch `services off`,
both architectures; no-argument run unchanged (exit 2 without the opt-in).

Evidence limit: in log 103 every svcctl open comes from pid 0x20
(`explorer.exe /desktop`, the session's first process), not from the
installer's thread. The installer's own failure chain is a missing
`Microsoft.NET\Framework\v2.0.50727\fusion.dll` (LoadLibraryShim E_HANDLE in
its managed-runtime plug-in step) → -9. A service manager alone is not
expected to change that exit; Madeira records such a provided runtime's
failure as done instead of retrying (app side, ml2014).

## Open-sourcing — 2026-09-27

The owner (125hz, sole author) open-sourced Madeira Dock under
GPL-3.0-or-later with the Madeira Converter Exception. The ml1990/ml2000 work
was committed as it stood, then relicensed: SPDX headers on every
source/test/tool file, `COPYING` and `LICENSE-EXCEPTION.md` copied from
Madeira, a `LICENSE` statement, `dock-notices.txt` now carrying the GPL,
exception and LLVM/MinGW runtime notices, `tools/check-privacy.py`
repurposed as a secret/personal-data scan, the cached-login harness
`tools/test-session.ps1` removed, and a generic compiler default in
`tools/check.sh`. No host behaviour changed. Older sections below that
mention private source or a proprietary licence are historical.

## Current change — service manager for Valve's CEG service

The ml1990 device log: ceg-request=1, ceg-server-result=22 (pending), then
ceg-job-result=55 (RemoteCallFailed), host result 49. Around the job Valve's
client opened `\pipe\svcctl` (c0000034, RPC_S_SERVER_UNAVAILABLE 0x6BA), tried
to load its i386 service DLL in the x64 host (c000007b) and retried svcctl.
Valve's client performs CEG work through its own installed client service,
which it demand-starts through the service manager. Madeira's desktop routes
run Wine's services.exe; the Dock route runs only this host, so no manager.

New `src/scm.c`, called in `launch.c` before the CEG request (only with
`MADEIRA_STEAM_HOST_CEG=1`, CEG not disabled, and `MADEIRA_DOCK_CEG_SCM` not
`0`): OpenSCManagerW(SC_MANAGER_CONNECT). Only on RPC_S_SERVER_UNAVAILABLE
(1722) does it start `<system32>\services.exe` (DETACHED_PROCESS, no handle
inheritance, cwd system32), after creating Wine's `__wine_SvcctlStartedEvent`
(include/wine/svcctl.idl SVCCTL_STARTED_EVENT) exactly as wineboot's
start_services_process does; it waits for the event or process exit, then
polls OpenSCManagerW (1722/1723 retry) — all within 30 s. If that event
already existed, another launcher is starting a manager: never a second one.
It then checks registration read-only: OpenServiceW("Steam Client Service",
SERVICE_QUERY_STATUS). Dock never creates, registers, configures or starts a
service; Valve's client demand-starts its own. Failure: ceg-result -4 (manager
unreachable) or -5 (service not registered), host result 49, no launch.

Lifetime: the manager stays for the whole game run (runtime DRM fix-up may
need the service). At host exit (main.c, after Valve's client shutdown, on
every path including errors), only if this host started services.exe: ask the
SCM to stop Steam Client Service (SERVICE_CONTROL_STOP, re-sent each second
while not stop-pending, 15 s bound). If its process outlives the bound, the
process handle opened while the service ran is terminated (only in this
host-started manager's session). Then TerminateProcess our services.exe.
Otherwise no-op. Abrupt host termination skips this cleanup.

Report lines (round **ml2000**): `ceg-scm=1/0` reachable (already running or
started), `ceg-scm-started=1` when this host spawned services.exe,
`ceg-scm-error=<Win32 error>` (open/spawn failure, 1067 manager exited,
1460 30 s timeout, or a non-1060 OpenServiceW error), `ceg-service-registered=0/1`,
at exit `ceg-service-stop=<code>` (0 not running, 1 stopped via SCM, 2 process
ended after bound, -1 failed/manager already gone) and `ceg-scm-stopped=1/0`.
`ceg-result` keeps round ml1990 with new values -4/-5. Pure decisions
(absent/retryable errors, -4/-5 mapping, bounded remaining time, stop outcome)
are in validation.c with 21 new sanitizer assertions. Rollback:
`MADEIRA_DOCK_CEG_SCM=0` (ml1990 behaviour). Static evidence only: Wine
services.exe startup in a Dock session, Valve's service start through it and
real CEG success need a device log. services.exe also autostarts the prefix's
auto-start services/drivers (as on desktop routes); those processes are not
stopped by Dock (Wine system processes end with the session).

### ml2000 follow-up — Valve's own service installer when unregistered

Device log 91 again shows the x64 client trying `Steam\bin\steamservice.dll`
in process (c000007b, i386), so the out-of-process service is the only route.
If the manager is reachable and OpenServiceW reports exactly
ERROR_SERVICE_DOES_NOT_EXIST (1060), Dock runs Valve's own installer the way
Valve's client does when its service is missing. Static inspection of the
January x64 client: the function after the "BOpenSCMgr failed"/"BOpenService
failed" check (RVA `0x96d390`) calls GetModuleFileNameA(NULL), strips the file
name, formats `"%s\bin\SteamService.exe"`, and runs ShellExecuteExA("open",
that file, "/install", directory = Steam folder, SEE_MASK_NOCLOSEPROCESS),
waiting for it. The install-script launcher (`0x6ea510`) likewise runs
`bin\SteamService.exe` relative to the Steam install folder. Dock's own EXE is
not in the Steam folder, so Dock uses the folder of the loaded genuine client
DLL (GetModuleFileNameW of steamclient64.dll) + `\bin\SteamService.exe`.
Only if that file exists: CreateProcessW with `"<path>" /install`, no handle
inheritance, DETACHED_PROCESS, cwd the Steam folder, 60 s bound (a stuck
installer is terminated: fail closed). Then registration is re-checked
read-only. Valve's binary registers Valve's service; Dock writes no service
registry keys and creates no service. On this PC the resulting registration
is `"C:\Program Files (x86)\Common Files\Steam\steamservice.exe" /RunAsService`
(demand start); Valve's installer copies itself there.

New field (round ml2000): `ceg-service-install=<installer exit code>`, -1
timeout (installer ended), -2 file missing, -3 could not start (with
`ceg-scm-error`). It is followed by a second `ceg-service-registered`
(parsers should use the last value). Still unregistered → ceg-result -5.
Kill switch `MADEIRA_DOCK_CEG_SERVICE_INSTALL=0` keeps the fail-fast -5.
Pure decision/report helpers `sh_should_install_service` and
`sh_service_install_report` have 8 more sanitizer assertions (80 total).
Unproven: the i386 installer under WoW64/FEX on iOS, its exit code there, and
whether it needs elevation-only behaviour Wine does not provide. Staged.

## Previous change — custom executable (CEG) preparation before launch

A device log showed a game whose depot executable is flagged CustomExecutable
exit 0x8000DEAD: Valve's client never prepared the per-user binary. Public
Madeira writes the manifest's CheckGuid block, saves depot manifests to
steamapps/depotcache and sets `MADEIRA_STEAM_HOST_CEG=1` for such apps.
When set, `launch.c` (after the install-folder check, before any registry
change or LaunchApp) calls Valve's IClientUser::RequestCustomBinaries
(slot 71, pinned per build; see CLIENT_LAYOUTS.md), pumps callbacks and
waits up to 5 minutes for one success callback 1020025 per started job.
Busy (10) is re-requested every 10 s inside that bound. Any other result,
zero jobs, a failed job, timeout or method mismatch returns **49** and does
not launch. Valve's client and servers produce the binary with the user's
own licence; Dock never alters or bypasses CEG. Rollback: `MADEIRA_DOCK_CEG=0`.

Report lines (round ml1990): `ceg-request-busy=10` (first busy only),
`ceg-request-result=<EResult>`, `ceg-request=<jobs; -1 unset>`,
`ceg-server-result=<EResult>` (changes only), `ceg-job-result=<EResult>`
(failures and first 8), `ceg-finished-jobs=<n>` on failure, `ceg-disabled=1`,
`ceg-unsupported-client=1`, and final `ceg-result`: 1 success, 0 no job
started, >1 Valve EResult of the failing request/job, -1 timeout,
-2 method mismatch, -3 interrupted. Host result 49 = CEG not prepared.
Decoders/progress are in `validation.c` with sanitizer tests. Static RE
only: callback delivery to the global-user pipe and a real CEG download are
unproven until a device log. Built, not staged (owner stages).

## Previous change — wait for Valve's client to prepare content

Public device log 67 (app 220): authenticated, listed, then Valve's LaunchApp
returned EAppUpdateError 17 (content log: "required app 340 not ready") and the
host ended 5 s later (45). Desktop Steam lets its client install/update such
dependencies and then launches; the client had already queued that update.
`launch.c` now keeps pumping callbacks for results 17/19/20 and re-submits
LaunchApp (10 s, 20 s, then 30 s) until Valve reports success, up to 6 hours
(then result 48). Nothing is launched without Valve's own success; every other
refusal fails closed as before. Report lines only on a changed result; new
fields `launch-update-wait/retry/ready` (round ml1970). Rollback:
`MADEIRA_DOCK_CONTENT_WAIT=0`. Classifier/backoff in `validation.c` with
sanitizer tests. Whether the headless client runs the queued download to
completion on iOS is unproven; the next device log's content-log lines show it.
Staged EXE SHA-256 `44f1b0ccbba229cbb9fce4c7b5067bf8975a15e6da522d2e3e52f5b8f5ac862a`.
Public side (not in this repo): the app runs the game's remaining one-time
installs in the Dock session before this host starts; the host is unchanged by that.

## Device confirmation — ml1880 public performance trial

Logs 62/63 from public Madeira ml1870 report authenticated online status and
requested-app subscription membership. The owner confirms the installed
32-bit game runs on iOS through Dock, with roughly 2.9 GB total app memory.
This supersedes earlier unproven-device notes below. It does not prove
clean-prefix setup, all games, revocation handling or all multiplayer APIs.
The Dock host executable is unchanged. Public ml1880 tests a smaller
512 MiB Dock JIT pool (desktop recovery preserved) and avoids guest D3D9
per-call census work during normal gameplay. Gains need device A/B testing.
Read the sibling docs/MADEIRA_DOCK.md for rollback and test instructions.
Original Valve libraries, real authentication, subscription checks and
original game DRM remain mandatory.

## Current change — transfer diagnostics

Device log 61 proves the January session ABI checks pass on Wine/FEX/iOS.
It then rejects the native transfer (37), before token submission. The report
does not yet distinguish open, metadata, size, read, parse or close failure.
Public Madeira ml1870 corrects its Z: assumption: a seeded prefix guarantees
C:, while the protected handoff is in native Application Support. The adapter
now uses Wine's explicit Unix namespace, retaining the same protected file.
MADEIRA_DOCK_UNIX_HANDOFF=0 rolls back that public path change.

auth.c now provides numeric operation/error diagnostics without paths, payloads
or account identifiers. Stages: 1 open, 2 info, 3 file type, 4 size query,
5 bounds, 6 read, 7 envelope parsing, 8 close, 9 oversized environment path.
Errors from failed Win32 calls are captured immediately; validation-only
failures have error 0. MADEIRA_DOCK_HANDOFF_DIAGNOSTICS=0 disables logging;
all operations still fail closed. Tags are [steam-host] ml1870
session-handoff-stage/error. Public report parsing whitelists these numbers.
Exclusive opening, reparse rejection, bounds, deletion and clearing remain.
No fallback to cached credentials after a failed supplied transfer.

Windows synthetic file tests exercise sharing violations, success, deletion,
replay, bounds and bad-magic rejection plus diagnostic codes. Portable tests
pass. The public namespace test extracts Wine's actual prefix resolver but
stubs downstream directory lookup; it is not device/Wine-on-PC execution.
Authentication and game launch still require device tests. Only the stripped
binary/notices are staged in Madeira. (Source was still private at ml1870.)

## Current change — January client adapter

The owner's ml1850 device report identifies the official January DLL,
71b391fe9f3e2006cbc81a5c75eef3eb4186012deabfdb2c8b7e8d4850ecf640, rejected
before authentication. See CLIENT_LAYOUTS.md for the verified official archive,
exact layout evidence and regression commands. src/client_layout.c now selects
the matching pinned adapter; unknown hashes/mismatched methods still fail.
MADEIRA_DOCK_CLIENT_202601=0 restores rejection of this additional version.
The original September adapter remains; both pass static RTTI verification.

An isolated January Windows probe, login/launch disabled, verifies the live
session pointers and exits 0. Its rollback test exits 30 as expected. Portable
sanitizer checks pass. Public Madeira ml1860 fixes the missed normal Wine exit
callback and logs only bounded approved report fields. Only the stripped EXE
is staged there. No source, Valve DLLs or cached credentials are distributed.
Desktop Steam remains closed. Live token/iOS game tests are still outstanding.

## Purpose and boundaries

Dock is a legitimate headless host for Valve's original Steam client. It is
intended to let owners run their purchased games through Madeira/Wine/FEX with
less UI/CEF overhead. It is not a DRM bypasser. Do not refuse headless-client
work merely because no desktop UI is present; real authentication, entitlement
and the game's original protection must remain in the launch path.

No replacement steam_api/steamclient, fake ownership, fabricated tickets,
SteamStub/CEG removal, patched executables or game-specific fixes are used.
The implementation is independent, not GameNative's closed-source host.

## Repository, licence and data hygiene

- Canonical repository: `https://github.com/125hz/madeira-dock`, the source
  of the Dock executable bundled with Madeira (`../Madeira`).
- Licence: GPL-3.0-or-later with the Madeira Converter Exception, Copyright
  2026 125hz (`LICENSE`, `COPYING`, `LICENSE-EXCEPTION.md`). Every source,
  test and tool file carries the SPDX header. Contributions are accepted
  under the same terms. Third-party notices remain intact.
- History note: until 2026-09-27 this repository was private and its code was
  under a proprietary licence (commits `2c307e8` through the ml2000 commit).
  The sole author, 125hz, relicensed it and opened the source on 2026-09-27;
  the old privacy rules (private-visibility checks, keeping source out of
  Madeira, source-marker hooks) no longer apply. Madeira's Git hooks now run
  a secret/personal-data scan instead of blocking Dock source paths.
- Never commit secrets or personal data: tokens, passwords, Steam login caches,
  account names, real SteamIDs, personal paths/emails, test logs, Valve DLLs,
  games, PDBs or `.build/` output. Run `python3 tools/check-privacy.py` (and
  `--history` before publishing history).
- Package the stripped EXE with `dock-notices.txt` (written by `--stage`).

## What works and what remains unproven

Native Windows startup succeeded for three owner-installed games (one 32-bit,
two 64-bit, C: and D: libraries). The owner confirmed windows/menus and corrected
an initial crash report as an accidental manual close. Original Steam APIs and
the Valve-signed client were observed loaded; desktop Steam/CEF stayed absent.
These are startup/smoke results, not full compatibility or extended gameplay.

The cached-login session measured 99.93 MiB host private memory and 0.0469 CPU
seconds in a 5.17-second sample. This excludes game/service processes and is
not an iOS memory/performance prediction.

ml1830 adds the native token handoff and an opt-in iOS adapter/onboarding flow.
Portable sanitizer tests and Windows consume/delete/replay tests use synthetic
credentials. A malformed real-host transfer fails before any login or launch.
The existing cached-login authentication/list check still passes. **Live refresh
token authentication and Wine/FEX/iOS gameplay have not yet been tested.**

## How it works

The host locks and hashes the genuine installed x64 steamclient64.dll, loads
it, constructs its client/engine/user interfaces and pumps callbacks. Private
methods require the pinned DLL hash and individually verified RVAs. Unsupported
client builds fail closed; do not guess ABI layouts or disable gates to fix an
update. The original supported DLL SHA-256 (also see the January addition above):
`caba4826aa3501039d095aee1843a6bfb270fb43a3ab4455b2d6733223579fee`
(file version 10.96.30.42, timestamp 1788399258).

Authentication modes:

1. PC test: select the local installed client's cached account metadata and ask
   Valve's client to authenticate its own cached credentials. No credentials
   are extracted into the code, repository or executable.
2. Native app: Madeira obtains its user's Steam refresh token through native
   QR/password login, keeps it in Keychain, pauses downloads and awaits native
   logoff/socket close. A protected single-use file transfers it to Dock. The
   public file protocol is described in README.md. Only its path is in the
   environment. Dock consumes/removes the file before SetLoginToken/LogOn and
   clears its temporary buffers. It never falls back to cached login after a
   failed supplied handoff. Valve may cache this user's session in their prefix.

The host requires online/authenticated state, the real client's subscription
list membership and its subscription query, then calls Valve's own asynchronous
LaunchApp. The private subscription query alone returned true even for invalid
IDs in this context, so it MUST NOT authorize a launch by itself. An absent-ID
test failed the list gate; a real non-entitled-account negative remains needed.
App ownership displayed by Madeira or decoded from local metadata is not proof.

The client resolves the actual install folder; it must match the requested
library directory. The host publishes its PID/account in Steam's discovery
keys and pumps callbacks while the game runs. It restores previous registry
values and releases handles at normal exit. Abrupt termination is not yet
crash-recoverable. Keep the same exact client/expected-folder gates.

## Build, test and stage

From this repository on a Windows PC with WSL:

```powershell
wsl -e bash tools/check.sh
wsl -e bash tools/build.sh
wsl -e python3 tools/check-privacy.py
wsl -e python3 tools/check-privacy.py --self-test
./tools/test-invalid-handoff.ps1
wsl -e bash tools/build.sh --stage
```

`tools/check.sh` uses `HOST_CC`, else `clang` from PATH, else the default
swiftly location in `$HOME`. Cached-login testing is not part of the public
tools: the former developer-only `tools/test-session.ps1` (which selected the
PC's cached Steam account from its login cache) was removed when the source
was opened. PC session tests set the host's environment gates manually (see
README "Native Windows test"); never commit account values or reports.

The build uses the sibling Madeira LLVM/MinGW toolchain. Override MADEIRA_ROOT,
LLVM_MINGW_BIN or DOCK_OUTPUT when needed. PE outputs are `.build/windows/`;
session reports are `.build/logs/`. Native C tests use ASan/UBSan. Both PE
architectures build, but authenticated session/launch is x64-only. The x64 host
can serve 32-bit games through their original client libraries.

`--stage` copies only the stripped x64 EXE and combined notices into
`../Madeira/app/Madeira/arm64ec-windows/`. Then use Madeira's normal xtool build
after setting `.xtool/build-round`; preserve the Xcode project and only one
application build at a time. The app's Git hooks scan added lines of commits
and pushes for secrets/personal data (same rules as `tools/check-privacy.py`).

PC tests are owner-authorized. Leave desktop Steam closed; never alter the
installed client/game files or bundle the owner's cached login. If multiple
libraries contain the benchmark AppID, use its C: installation as requested.
Do not put game names in code, comments, log strings or commits.

## iOS integration and next device test

The verified ml1830 app contains a 30,208-byte stripped x64 host, SHA-256
`ddefca17379dda5e274f065913a7215a7f49a504ccb37a657a5257cc8d48804b`.
Its source/build artifacts are excluded from the app. The iOS IPA has 1,389
entries, SHA-256 `bd426d2b3172bf0160050338879d3b8712671af37fbd399509fd4ca49bf59b28`.
The source tree uses Wine 11.4; the bundled prefix template describes Windows
10 Pro build 19045. Dock's static imports resolve against the bundled Wine
export tables, including UCRT API-set mappings/forwarders. This does not prove
runtime behavior or actual Windows 7 compatibility; Valve's genuine client
is still a separate dependency. Do not downgrade the prefix based on a guess.

Read `../Madeira/docs/MADEIRA_DOCK.md`. `MADEIRA_DOCK=1` enables the trial, `=0`
restores the desktop route. Default launch option only; custom arguments fail
with an explanation. No failed Dock auth/ownership check falls back to direct
launch. Native account/session code prevents reconnecting while Dock owns the
token. Session end copies only selected numeric host diagnostics into the app
log and clears leftover handoff files.

First device test reuses the user's existing Steam files/prefix. Onboarding
under the switch signs in natively first and still runs Valve's official
installer for files; it asks the user to stop at the updated sign-in screen,
without signing in inside that desktop. Do not advertise fully automatic
component installation or a proven one-login iOS path yet.

Next: validate token submission, online state, subscription-list gate, correct
client discovery, launch and cleanup on the owner's device. Then test a clean
prefix, install only the necessary verified official component packages and
remove the desktop installer step. Also test non-entitled/revoked accounts,
network loss, Steam Guard/token expiry, multiplayer/cloud/achievements, custom
launch options, other client builds and abnormal-exit recovery. Preserve genuine
DRM throughout; do not substitute emulated licensing for a missing client API.

## Device integration update — ml1850

Log 59 from Madeira ml1840 confirms the x64 Dock EXE ran on Wine/FEX/iOS,
then crashed during LoadLibraryExW before authentication. The public runtime
kept a dead bcrypt executable mapping when coml2 reused its PE base and size.
Madeira ml1850 retires image translations on unmap, adds independent host-exit
reporting and repairs startup log controls. See sibling Madeira HANDOFF.md
for precise evidence/tests. Dock implementation and binary are unchanged;
live native-token authentication and device game launch remain unproven.
Use Documents/madeira.cfg: env.MADEIRA_DOCK = 1. Legacy env-file instructions
are superseded by canonical config (legacy files are now safely imported).

ml1860 stripped host SHA-256:
`61976bb68737c9e39f1acd387b22c7aece406f34784352bb0307bf21a1b0f3fa`,
30,208 bytes, staged into and verified inside Madeira IPA ml1860 · 09-24 21:30.
The IPA contains no Valve libraries, Dock source, symbols or cached logins.
Public IPA SHA-256: `78a10b3ce35fc5da9aee957f8e3ce0f5c6ce493fc3b8811daa7c3b73176373ca`.
No source push in this round; device authentication and launch tests pending.
Full-host malformed synthetic transfer also rejected before login with stage=5/error=0, removed the file, and did not take the cached-login path. The isolated official runtime emitted missing-helper warnings; no game was launched.

ml1870 verified artifact: `ml1870 · 09-24 21:46`, `xtool/Madeira.ipa`,
166,307,354 bytes / 1,389 entries. SHA-256:
`defe7ed39691534563d027ae813054eb709e5a30169ad6aef22680aa1df3a124`.
Stripped x64 Dock is 30,720 bytes, SHA-256:
`26bc7b1ace2846191f67d7673219e7bae132be5b0ec0db84d5e9795c65b45044`.
CRC, all 1,271 Windows resources, new app/host diagnostic strings, resource
seals and source/login/Valve-DLL exclusions pass. Only Madeira, Dock EXE,
Info.plist and CodeResources changed from ml1860; no entries removed.
Dock imports resolve against bundled Wine exports. Existing unrelated compiler
warnings remain. No commit/push performed. Device verification is outstanding.

## Combined launch arguments and ownership timing (2026-10-27)

The host passes bounded UTF-8 arguments to the original LaunchApp entry on the
first request and retries. Invalid arguments return 54; 50 keeps its offline
logon meaning. Escaped quotes use Windows backslash parity. Ownership requests
start after 500 ms by default; MADEIRA_DOCK_LICENCE_WAIT_MS accepts 0..30000.
The existing strict integer parser rejects signs, spaces and overflow. Positive
ownership from Valve remains required. Native sanitizer checks pass; this
combined change has not been tested with a live client or a device.

## Friends, invites and join (2026-10-29)

New optional `src/friends.c` (Windows) and `src/friends_format.c` (portable,
tested in `tests/test-friends_format.c`). After LaunchApp is submitted the host
gets IClientFriends through CSteamClient slot 13, checks nine pinned friends
slots, and in the launch loop writes `C:\madeira-friends.bin` every 3 s and
reads `C:\madeira-friends-cmd.bin` every 0.5 s. Feed and commands are
deleted at host start and exit. September layout has zero friends pins:
`friends-available=0`, launch unchanged. `tools/check-client-layout.py` now
also checks the friends slots, names and IPC IDs (passes on the January DLL
from Valve's CDN). Cross-compile (x86-64, i686) and native ASan/UBSan tests
pass.

Not yet proven at runtime: friend list contents, GetFriendByIndex hidden
return, the lobby/friend argument order of NotifyLobbyJoinRequested (taken
from the reference and the public callback; the server passes both through
in order), and whether games react to the join notifications. Invites carry
an empty connect string. Not done: the app side (Madeira reads nothing yet),
join at launch (`MADEIRA_STEAM_HOST_ARGUMENTS` with `+connect_lobby`),
incoming invites. Needs a live client and a second account to verify.

## Friends sign-in, lobby invites (2026-10-30)

Device test (iPad, January client, second account) found the headless
session offline to friends, every friend offline, and no record of its own
game or lobby, so invites had no Join button. Cause: `SetPersonaStateInternal`
(`0x9e61d0`) checks `0x68f550` (web-UI mode) and then only posts
`SetPersonaStateInternal_t` (`0xfde9b`) for desktop Steam's Friends window;
the window signs in. The Friends window's script (`steamui_websrc_all`,
`chunk~2dcc5aaf7.js`) sends on logon `ClientChangeStatus` (EMsg 716:
persona_state, persona_set_by_user false, need_persona_response true,
is_client_idle false) and `ClientRequestFriendData` (EMsg 815, flags 1106,
friend IDs), through `SteamClient.SharedConnection.SendMsg`, which SteamUI
maps to IClientSharedConnection. Dock now does the same (CSteamClient slot 69,
IClientSharedConnectionMap slots 0/2; see CLIENT_LAYOUTS.md). The earlier
Offline/Online toggle and per-friend RequestUserInformation calls are gone;
those pins remain but are not called.

Result on the iPad: `friends-signin=1`, `friends-requested=12`,
`friends-online` 1, `friends-own-game=4`. The friend saw the account Online
and In-Game. Invites with `+connect_lobby` (InviteUserToGame) showed a Join
button but the game did not join; the friends menu's "Invite to lobby"
(EMsg 6621 `ClientMMSInviteToLobby`: header routing_appid, steam_id_lobby,
steam_id_user_invited), now sent whenever the own lobby is known
(`friends-invite-connect=3`), joined the friend into the lobby.

Not yet tested: Join from Madeira into a friend's lobby, incoming invites
(the session now receives friends' data; callback IDs are still logged), and
the Madeira Friends sheet. The app's Web API presence code (it got a
refresh token revoked) has since been removed.

## Friends' status subscription, Friends page (2026-10-30, later)

Friends' status stopped arriving although Steam showed the friend online
elsewhere. `PersonaStateChange_t` flags showed friends' updates carrying only
name, relationship and name-first-set bits. The Friends window, once the
friends list is in, calls `Chat.RequestFriendPersonaStates#1` (EMsg 151,
header target_job_name, empty body); Dock now sends it after
ClientChangeStatus (`friends-subscribed`). Status then arrived, but Dock's
once-a-minute ClientRequestFriendData for every friend reset all friends to
offline (the update carried the went-offline bit). The window sends that
request only for friends whose name is not loaded; Dock now does the same
(normally none). The earlier "online" sightings were status changes that
happened to arrive. Callback 503 (LobbyInvite_t) arrives on Dock's pipe once
the session is signed in to Friends, so incoming lobby invites need no
shared-connection receive path (see below).

Madeira: the in-game Session menu has a Friends page (DockFriends.swift):
friends sorted joinable / this game / other game / online / offline, Join for
joinable friends, Invite for online friends, the result per row. On the iPad,
with a friend's Steam under Sikarugir: the friend showed online, Join entered
the friend's lobby and Invite brought the friend into this lobby.

## Incoming invites (2026-10-30)

Dock keeps each LobbyInvite_t (503: inviter, lobby, CGameID) for the session's
App ID, newest per friend, at most 16, and sets feed flag 8 (invited) on the
inviter; Join on that friend calls NotifyLobbyJoinRequested with the invited
lobby and forgets the invite. The Friends page lists inviters first with
"Invited you to play" and Accept. On the iPad the friend's invite appeared and
Accept joined the friend's lobby. Invites from game invites with a connect
string (GameRichPresenceJoinRequested, chat messages) are not handled.

Follow-up: an invisible friend who invited showed as in game (the client
still knows the game); the feed now sets game flags only for friends shown
online, and Join without an invite refuses friends shown offline. The app's
Join result reads "Sent to the game" (the game decides whether it gets in)
and results clear after 5 s. Dock's report uses CRLF; the app splits on any
newline. Accepting an invite to a lobby the friend has left put the player in
the friend's new lobby: Dock passes the invited lobby and the friend, so the
game or the client followed the friend.


## Steam Input loader corrections (2026-11-02)

The January controller adapter now pins IsControllerConnected (85),
GetConfigForAppAndController (124) and HasGameMapping (128), verified by the
RTTI/name/IPC-ID checker. LoadConfigFromVDFString and SetFocusedWindow have void
returns; old bool-success reports were invalid. Layout loading waits for the pad
and the async cache, resolves the selected Steam-local file, resets the enable
cache and warms before activation. It retries bounded setup failures and handles
reconnections/refocus without continuously enumerating controllers. Tests in
`tests/test-steam_input.c` exercise the production lifecycle under ASan/UBSan.

The paired Madeira change supplies public_all/controller_base for existing and
fresh installs, puts x64 SDL3 in the build/cache contract and fixes first-account
and CRLF opt-in setup/recovery. The iOS build succeeds. One device retest found
the initial path-vs-URL selection guard too strict; that guard is corrected.
Further inspection established that selections are keys (bare template names or
`AC-app-device` autosaves), not necessarily URLs. The resolver and sanitizer
tests cover both forms. The subsequent device run queued the selected autosave,
observed its cache entry and requested activation.

A bounded read-only diagnostic opens the January client's existing
`SteamXInput_mem-IPCWrapper` mapping (never creates or writes one): size 0x3e78,
reserved/connected masks at 0/4, first XINPUT_STATE at 0x934. It excludes the
packet counter when checking for gamepad changes. On the device, reserved and
connected masks both became 1, followed by `steam-input-output-changed=1`.
Valve's engine is producing gamepad output. A queue/activation log alone still
must not be reported as proof that the game received controller input.

The game overlay's `XInput Hooked` log was misleading: the preceding memory
write failed. Madeira's server denies pseudo-process writes, including ones
the overlay made to its own current-process pseudo-handle. The paired Madeira
fix handles only that pseudo-handle locally, through W^X aliases where needed;
other handles remain on the existing server path. Its ASan/UBSan memory tests
and iOS runtime/app build pass.

Final device test: the user confirmed the controller works in the game. The
overlay log has nine XInput hook reports and zero `WriteProcessMemory` failures.
The app log shows successful five-byte current-process writes and XInput slot 0
connected as a Windows.Gaming.Input gamepad. Steam output remains connected and
changing in the Dock report. This verifies input for the tested combination;
other controller models, keyboard/mouse layouts and rumble remain untested.
The production output diagnostic also has sanitizer tests for late mappings,
failed views/cleanup, read-only access, packet-only changes and disconnection.


## Steam Input session query service (2026-11-02)

`steam_input_service.{c,h}` adds portable bounded wire parsing, scope/lease/
sequence gating and feed encoding. `steam_input.c` atomically publishes the
feed and claims commands through a separate processing file. It reuses only
already pinned methods; the pinned January binary checker still passes.
The service is opted in with a random per-launch nonce from Madeira's protected
auth handoff. Its complete version-1 wire contract is in README. Query and
existing-source discovery work; apply/reset return result 7 without loading,
selecting or activating anything. State and flags distinguish queueing, generic
cache presence and activation request; there is no active-revision claim.
A startup fallback selected by Dock is flagged so migration cannot misidentify
it as a pre-existing Steam selection.

The app uses its existing report poll to read the bounded feed and queries once
per observed connection, at most three attempts with ten-second response bounds.
It preserves the original Steam selection alongside an explicit Madeira request
in the account/game/controller-scoped atomic store. Corrupt or unknown-schema
stores are retained. The controller UUID currently names the virtual slot-0
bridge; physical-device identity and player assignment are outstanding. Input
capabilities describe transport groups, not every paired pad: buttons, sticks,
analog triggers, optional Sony touch click; no motion or touch coordinates.

Validation: full native Dock ASan/UBSan suites (including production file-service
lifecycle with mock Windows I/O), Swift selection/settings regressions and a
cross-language C-feed/Swift-command/migration test pass. Both PE architectures
cross-compile with warnings as errors. The real pinned January binary check and
privacy scan pass. No runtime binary was staged, full iOS app built or device
acceptance performed. Rebuild app and host together to test this service.
Next: prove the active-revision acknowledgement against the pinned client, then
route a distinctive local fixture through acknowledged apply/restore/rollback.
No new private ABI is enabled, and milestone 1 is not complete.
`CLIENT_LAYOUTS.md` records statically identified slot-45 configuration and
slot-65 revision getter candidates for the next server/caller investigation;
their active-binding semantics and lifetimes are not established.


## Configuration-load observation and iPad test build (2026-11-02)

The next investigation established that slot 45 takes a binding-object handle,
not a controller index: its server looks up the binding tree and serializes the
object. Slot 65 reads revisions from that same tree. Neither is an active-layout
getter; the runtime does not call them. Public configuration-load callback 2803
is now observed from the launch callback loop with exact x64 bounds, App ID
filtering and capped numeric diagnostics. Creator is deliberately skipped and
no device/creator identifiers are logged. Manifest revisions do not acknowledge
an exact content hash; the apply/default service operations remain disabled.
See CLIENT_LAYOUTS.md for the binary and public documentation evidence.

The new parser and production observer filtering/cap have sanitizer coverage;
the full Dock suite, Swift service/selection/settings tests and real pinned
binary checker pass. The x64 host was rebuilt/staged into a full Debug iOS
app, successfully built with Xcode and signed using existing development
profiles. The test app was installed over the existing iPad app and labeled
`Steam Input M1 · 11-02`. No account or prefix was reset. The staged host is
67,072 bytes, SHA-256
`514af62bcfc49e47539a92673a6a2156772ce14a151b3e26871d3c45f5addc93`;
the app-bundled copy matches. Device test pending: enable JIT, launch a Steam
Input game with a paired controller, collect the query feed/result and bounded
configuration-loaded observations, then reconnect and relaunch. Do not report
this installation as acknowledgement/apply/restore acceptance.


### First M1 device run and corrected dependency package

The first service feed was valid but disconnected: adapter available, epoch 0,
sequence/result 0, no selection. Dock reported zero controllers and no SDL-init
event. The directly built test app omitted x64 SDL3, so this run did not validate
query/migration or configuration-loaded delivery. The test-build preparation
failed to check the complete Dock runtime dependency set before installing.

The paired Madeira fix makes `build/madeira-dock/build.sh` build/stage SDL3 and
its notice on its direct path as well as through the runtime component entry
point (the redundant wrapper build is removed). Runtime validation now requires
x86-64 specifically for SDL3, and tests reject missing dependency/notice files
and i386, ARM64EC or ARM64 DLLs. All 13 runtime-contract tests and Dock sanitizer
suites pass. The source-built SDL3 is 2,993,152 bytes, SHA-256
`c94ff406113ff5661c5020b657075cebfce41c939d96a2e6d23d3c12e57ed9da`.
All four required Dock files were checked in the corrected signed app bundle;
`Steam Input M1 R2 · 11-02` was installed over the existing app. Its game run
and service/callback acceptance are pending. The host binary is unchanged.


### R2 device service validation (2026-11-02)

The iPad run reports SDL init success, one SDL joystick/controller, selected
account-local autosave source 6, queued layout, generic mapping cache present
and activation requested. Valve's Xbox output is connected and changes. Callback
2803 reaches Dock with manifest major/minor 0/0 and flags 2 (gamepad output).
The version-1 service feed has epoch 1 and query sequence/result 1/1; the app's
persisted record matches the feed's game/key/Workshop identity, keeps the explicit
choice absent and does not mark any revision active.

Owner-authorized live file-command probes passed with a valid source query as
the positive control: wrong launch nonce, stale controller epoch, replay and an
oversized path payload are rejected; reserved apply/default return result 7
without changing the observed layout, connection or cache. The first probe
attempt did not complete its positive control and is not evidence; later scope
comparison showed a different launch nonce. The successful run refreshed its
scope and tested delivery first. Private test artifacts remain outside source.
The app normally owns command sequences; after external diagnostics, start a
fresh session before checking automatic reconnect queries so diagnostic sequence
numbers do not interfere with the app's counter.

Normal relaunch/persistence and a controller disconnect/reconnect are pending
the owner's physical check. Exact VDF-revision acknowledgement is still absent,
so startup/live apply, restore and rollback remain incomplete. The private
load-completion callback investigation in CLIENT_LAYOUTS.md is the next lead;
manifest revisions or a successful generic gamepad run do not close it.


### R2 glyph regression: stale native archives; R3 rebuilt from source

The owner reported working input after disconnect/reconnect but PlayStation
rather than the reference build's Xbox glyphs. The root and reference worktrees
record identical Wine/FEX/Dock/DXMT submodule commits. Wine sources and
virtual_ios.c match; generated ignored native archives did not. The linked R2
app lacks both the current-process write marker and the server HID-owner marker.
That permits direct physical-pad input and fails to establish delivery through
Valve's output despite successful engine/output-stream diagnostics. R2 is not
evidence of end-to-end Steam Input delivery or a distinctive mapping.

Both `build/ntdll-unix/build.sh` and `build/wineserver/build.sh` were rerun from
this checkout. The full Debug app relink succeeded. Its linked native image now
contains the local overlay-write fix and HID-owner restriction; xinput1_3/1_4
forwarding binaries match the reference worktree. The new
`tools/check-steam-input-runtime.py` rejects the old R2 bundle and passes the
rebuilt bundle; the IPA packaging path calls it before signing. It supplements,
not replaces, source/build manifests. Current-process W^X memory-write sanitizer
tests and pad-output checks pass. The signed `Steam Input M1 R3 · 11-02` build
was installed without resetting account/prefix data. Overlay delivery, glyphs
and distinctive bindings still need the R3 game test.

The observed post-relaunch feed has a fresh launch nonce, query sequence/result
1/1 and the same selected autosave. Its virtual-controller epoch remains 1;
physical disconnect/reconnect does not by itself prove a virtual-device lease
transition because Madeira can reserve the virtual slot across it. The owner's
input report predates the rebuilt native runtime and cannot establish which
input path the game used.


### R3 corrected-route device result (2026-11-02)

The owner confirms working controller input with Xbox glyphs in R3. Its current
overlay log has nine XInput hook reports and zero WriteProcessMemory failure
lines. Dock reports successful SDL init, one joystick, configuration-loaded
callback flag 2 and connected/changing Xbox output. The linked app passes the
new native-write/HID-owner/XInput-forwarding preflight. This restores the earlier
verified Steam-to-game route for the tested combination; R2's direct-pad result
is superseded. All four submodule pins were already identical to the reference
worktree: stale ignored native archives, not absent source changes, caused the
regression. The libraries were rebuilt here from source rather than copied.

This does not acknowledge an arbitrary VDF revision or close milestone 1's
fixture apply/restore/rollback requirements. The read-only service and selection
migration have device evidence; exact-revision activation remains the next work.


## R4 document-revision probe (2026-11-02; device test passed)

Static tracing found the native VDF document `revision` at mapping +0x19c,
independent of the action-manifest revisions. Callback 1060003 carries that
revision at payload +24, with completed/controller/App ID at 0/4/8. Creator
at +16 is never decoded or retained. New bounded decoder/metadata parser and
their production observer pass ASan/UBSan; the real pinned binary checker now
checks the corresponding construction/parsing/sender instructions. Unknown
builds cannot use this decoder. Revision matches remain observations, not active
service acknowledgements. Startup and diagnostic changes share queue_layout_bytes.

`DOCK_REVISION_PROBE=1 bash build/madeira-dock/build.sh --check` produces the
opt-in test binary. Action 5 is disabled in ordinary binaries; it can run once
per launch in the test binary, accepts no path and reads a fixed synthetic
fixture under controller_base/templates. It requires revision 731, preserves
baseline bytes and serializes conflicting probes. The fixture is authored here
(`tests/fixtures/steam-input-revision-probe.vdf`), face buttons only, A/B swapped.
It never writes a Steam selection. On a matching completion it observes for
30 seconds then queues the baseline; no initial completion triggers restoration
after 15 seconds; a restoration observation timeout is explicitly reported.
No general apply/default command has been enabled. Check the original selected
file's bytes after the test; native-client persistence must not be assumed.

Both normal and diagnostic lifecycles pass sanitizer tests (including incorrect
build/app/controller, malformed callbacks, mismatched revisions, overlap,
restoration/timeout/cleanup and normal-build rejection). Swift regressions,
x64/x86 ordinary cross-builds, January binary checks, full Debug app build and
native capability preflight pass. Signed `Steam Input M1 R4 · 11-02` is installed,
and its scratch fixture was uploaded only after confirming the name unused.
The diagnostic host is 70,144 bytes, SHA-256
`66494cda02e2651e1aff7a6577ba01b19f8f8cabcb7d1373f5236114cd02b291`;
the bundled copy matches. No game/account source file was modified.

The first device run observed matching completions and restoration but the owner
was not watching the buttons. A fresh-session retry completed successfully: the
owner explicitly confirmed A/B actions swapped and then restored. Private load
completions reported revision 731 then baseline revision 0, probe phase reached
4, and a before/after byte comparison confirmed the original selected VDF was
unchanged. This is evidence for the serialized diagnostic queue/restore path,
not general callback ordering or safe concurrent transactions. Production work
must associate unique revisions with immutable content and cover held outputs,
late callbacks, persistence, default restoration and failure rollback.
Milestone 1 is not complete until the verified contract is used for ordinary
revision application, persistence, default restoration and rollback.


## R5 revision-tagged queue copies (2026-11-02; device validation passed)

`sh_input_vdf_materialize` uses the bounded metadata reader to locate the exact
root revision token, replaces it in a private output buffer or inserts metadata
after the root opening brace, and preserves all remaining bytes verbatim. It
rejects malformed/ambiguous metadata, overlap, out-of-range revisions and output
size overflow without modifying output on failure. This is not a general binding
validator. The source is never written and nested revision keys are untouched.

The opt-in one-shot probe prepares both apply and restoration copies before
queuing either. Revisions are max(baseline, authored fixture revision) +1 and +2;
insufficient range fails closed. It retains exact source bytes separately and
accepts only the new restoration revision, so a delayed baseline/startup
completion cannot acknowledge restoration. Session one-shot gating prevents
reuse within this experiment. Production needs persistent allocation, immutable
content/hash association, held-output handling and transaction recovery; normal
apply/default commands are still disabled. The original R4 device result remains
valid for its earlier code, but these new submitted bytes need a fresh device
swap/restore check before claiming native acceptance (result below).

Validation: normal and opt-in ASan/UBSan lifecycles pass, including a delayed
startup completion during restoration and revision exhaustion. Materialization
tests cover unknown/nested binding preservation, insertion, malformed/duplicate
and noncanonical-case revision metadata, output bounds and overlap rejection.
C/Swift interoperability and selection persistence regressions pass; x64/i386
ordinary builds, pinned binary evidence checks, Debug iOS build, runtime
capability preflight, signing verification and privacy/diff checks pass. R5 is
built, signed and installed after the owner closed the running game. The bundled
host is 71,680 bytes, SHA-256
`4e0b990c687da1f0c31e380b3b30bb00326e5a4c9fd938c2e5beffe569adfb22`.
Fresh-session device test passed: the owner explicitly confirmed the live A/B
swap followed by restoration. Completed callbacks reported apply revision 732
then restoration revision 733, probe phase reached 4, command result was OK,
and a before/after byte comparison confirmed the original selected VDF unchanged.
This verifies native acceptance and live behavior of the revision-tagged copies.
Production transaction/content-hash binding, persistent allocation and ordinary
apply/default remain outstanding; this does not complete Milestone 1.


## Transaction and persistent preparation (2026-11-02; host implementation)

Added the portable `sh_input_tx_*` coordinator to steam_input_service. It reserves
separate apply/rollback revisions, consumes cancelled reservations, rejects
concurrent/stale requests and native signed-range exhaustion, and binds immutable
queue digests once. Confirmation requires both a matching completed callback and
an activation request before the deadline, in either arrival order. Timeout or
explicit failure returns one rollback-submission instruction; a failed rollback
blocks further work until connection recovery and never labels the historical
working receipt current. Reconnect preserves revision/sequence high-water marks
and invalidates old completions. Native callers still must validate actual bytes,
synchronize the reserved floor/journal and submit both copies; v1 apply/default
remain disabled and do not yet invoke the coordinator.

Madeira now compiles this portable C implementation for iOS too, through its
bridging header. `SteamInputTransactions.swift` uses that same metadata parser
and materializer. Its account/game/controller journal atomically writes and
synchronizes reservations under a nonblocking writer lock, stores exact source
and baseline bytes alongside tagged apply/rollback copies by SHA-256, and verifies
hashes on read. Preparation checks each tagged copy against the shared native
materializer and its reserved revision before binding content. A queued request
records the launch nonce, epoch and sequence; a receipt must match that scope,
reservation UUID, native revision and digest. Interrupted work cannot accept a
late successful receipt from the old launch. Unknown/corrupt records and objects
are preserved and fail closed; reservations never wrap or get reused.

The app service has local preparation for explicit template/personal requests,
which updates requested selection only after content preparation and preserves
lastActivated. It sends no apply command. Launch/end/reconnect mark unfinished
work interrupted and expose a recovery requirement rather than automatically
resuming it. The next work is the session-scoped native submission/receipt bridge,
startup reapplication and held-output/failure recovery. No replacement device
build has been installed; R5 remains the last device-validated runtime.

Validation: Dock normal/probe ASan/UBSan suites pass, including the new transaction
failure/ordering/reconnect/exhaustion scenarios. Swift journal tests use real
files and locks, simulate interruption/reload, verify original and tagged bytes,
reject corrupt records/objects and stale launch receipts, and round-trip a real
C coordinator receipt into the Swift journal. Service tests cover explicit local
preparation without a native mutation, unchanged lastActivated, overlap rejection
and interruption on reconnect/launch. Existing selection and C/Swift wire tests
pass. Normal x64/i386 Dock cross-builds, full Debug iOS build with the shared C
source, runtime capability check and privacy/diff checks pass. No additional iPad
test was requested for this preparation-only step; the live bridge is not enabled.


## Local native submission/receipt bridge (2026-11-02; native endpoint device-validated)

`DOCK_LOCAL_APPLY=1` builds an explicitly gated local action-3 endpoint; default
builds continue returning ACK unavailable, and default restoration remains
reserved. The README specifies the MSITXN02 payload and MSITXFD2 receipt. Native
preflight requires the pinned completion ABI, connected/settled working layout,
matching scope/sequence and fresh document revisions. CryptoAPI verifies both
queue SHA-256 digests; the supplied rollback must exactly equal the metadata-tagged
working bytes. Both heap-owned copies and a copied coordinator state are prepared
before native mutation; failed validation leaves the current layout unchanged.
The startup/diagnostic/live paths share queue_layout_bytes, which retains the
latest submitted bytes for verified rollback. Local activation waits for its
matching load completion instead of accepting a generic cache hit. Timeout queues
rollback once; restore failure marks working state unknown. A virtual connection
change cancels observation; new live work remains unavailable until session
recovery. Physical reconnect with a reserved virtual pad is still a separate
transport issue. Cleanup frees retained copies, disables polling and removes
transient files. Receipt publication retries at 500 ms intervals.

Swift submitPrepared journals the launch/epoch/sequence before installing payload
and command. It distinguishes acceptance from the transaction receipt, validates
all scope/revision/hash fields before updating requested selection's lastActivated,
leaves that history unchanged on rollback/rejection, and interrupts ambiguous
write/timeout/connection cases. Query sequence allocation now adopts the server's
high-water sequence, so an external diagnostic cannot strand future queries.
Wrong-scope/hash receipts cannot even be exposed as a confirmed service receipt.

Host coverage: bounded/truncated/malformed payloads and feeds; ordinary-build
rejection; gated lifecycle digest and baseline rejection; overlap; completion
before activation; successful apply; timeout rollback and stale callbacks; failed
rollback and cleanup. The CryptoAPI call boundary is mocked in host lifecycle
tests; the actual Windows implementation is cross-built. Swift tests submit real
SHA-256 queue payloads, feed them to C, obtain a C-generated confirmed receipt,
reject wrong nonce/game/epoch/sequence/revision/hash and persist only the valid
acknowledgement. Final normal/probe/local ASan/UBSan suites, C/Swift service,
journal/selection regressions, ordinary x64/i386 and gated x64 builds, pinned
client checks, full iOS Debug build, native capability preflight, signing and
privacy/diff checks pass. Cleanup also disables polling, and retained working
bytes can safely be requeued without freeing an aliased source before native
consumption. Late startup diagnostics cannot invalidate a confirmed transaction
receipt used as the next rollback baseline.

`Steam Input M1 R6 · 11-02` is built, signed and installed on the connected
iPad. The app label, bundled host digest, signature and native capability
preflight were verified before installation. The bundled host is 76,288 bytes, SHA-256
`105736d12a7057790c41ee204ccbba979ed14fdd460563e4d05e54a958314e56`.
This build enables only the gated local endpoint, not the old action-5 probe.
The native endpoint device experiment passed. A scoped action-3 command applied
the distinctive fixture, returned confirmed phase 3 for revision 732 and the
expected exact SHA-256 digest. After the observation window a separate action-3
command restored the original mapping with revision 734 and its expected digest.
The owner explicitly confirmed the live swap and restoration. A before/after
comparison confirmed the selected source VDF byte-for-byte unchanged.

This external sender exercised actual CryptoAPI digest checks, native baseline
validation, separate serialized applies and exact transaction receipts. It did
not exercise automatic failure rollback, held outputs, app-side journal writes
or reapplication after restart. Swift persistence still has host coverage only;
ordinary builds remain gated until the remaining integration/recovery work is
validated. Milestone 1 is incomplete.


## App-side persistence/restart (R7 preparation, 2026-11-02)

The gated host advertises working-snapshot capability 32 and publishes bounded
MSIBAS02 baseline frames after exact completion/activation settle. Queueing,
refocus and virtual lease changes invalidate the old frame. Frames retain exact
native bytes and private scope; no metadata is logged. Polling/publication is
bounded to 500 ms, and cleanup removes baseline files. The portable shared codec
checks exact length, nonce presence, App ID, slot, epoch and native revision.

Swift waits for the new nonce-bound baseline after its query. It proves cached
source bytes reproduce a prior confirmed queue, then reuses normal journal,
preparation, submission and receipt handling for restart. The historical ACK is
not current active state until the new receipt arrives. Newer unconfirmed intent
and its generation are preserved while the last confirmed choice is reapplied.
Up to eight exact confirmed-source proofs survive later failed restart rollback
to Steam's baseline; validated legacy working receipts are retained before new
reservations overwrite pending metadata. Interrupted old-launch requests are
retired without replay only after the new native baseline; same-nonce retirement
is rejected. Virtual lease changes within a launch suppress automatic recovery.

A successful explicit clear increments intent generation, keeps historical ACK
and delegates future startup to Steam's existing selection. The Debug-only
`madeira-steam-input-local-test.bin` mailbox accepts scoped 44-byte commands
using the query command framing: action 1 selects only the fixed authored test
fixture and requires no existing explicit request; action 2 restores the baseline
captured by the fixture/restart transaction, then clears the test override only
after its confirmed receipt. It accepts no paths and rejects old nonce/epoch or
replayed diagnostic sequences. It is omitted in Release builds and needs both
local-apply and snapshot capabilities. This allows actual app journal/persistence
validation before a layout UI exists. Normal startup/apply APIs are the path
under test; the external tool only triggers the scoped debug mailbox.

Host tests cover native snapshot framing, settlement/invalidation/cleanup;
confirmed-source proof and rollback history; a fresh app launch preserving a newer
unconfirmed request while reapplying the old confirmed source; wrong-scope
baseline rejection; same-launch recovery rejection; and a full Debug mailbox
fixture selection, receipt persistence, relaunch reapplication, baseline restore
and cleared-override relaunch. Device validation of those paths is pending. R6's
native endpoint evidence remains valid for that earlier build; it does not
establish R7 app-side persistence or held-output/failure recovery.

The first observed Steam baseline is also stored separately before an explicit
override; fresh-launch transactional baselines never overwrite that restoration
source. This prevents a later native baseline change from redirecting diagnostic
cleanup. After its successful restore receipt the test clears explicit intent,
retains historical ACK, and a subsequent launch issues no override. Host coverage
includes a deliberately changed fresh native baseline to verify that distinction.

Validation: final normal/probe/local ASan/UBSan suites, shared codec and Debug app
service/journal/selection regressions pass. Ordinary x64/i386 and gated x64 native
builds, pinned-client checks, full Debug iOS build, native capability preflight,
privacy/diff checks and signing verification pass. R7 is installed and its
app-side persistence/relaunch result is recorded below.

Signed label: `Steam Input M1 R7 · 11-02`. Bundled gated host: 77,824 bytes,
SHA-256 `226ad2b7d2f2c16bb1f5b653556c2aab21c8223fcb1c8bac68fd69c6be84121c`.
R7 is installed; the existing device fixture matches the authored source exactly.
The owner has authorized future test-build installation without waiting for game
closure. No further close-game coordination is required before installs.

Device test passed for the actual app path: the scoped Debug trigger caused the
app to prepare, journal, submit and persist fixture acknowledgement at revision
732 (native sequence 2). After normal game end/relaunch, no external apply command
was sent: the app automatically reapplied it at revision 734, native sequence 2
in a fresh launch nonce. The owner confirmed buttons remained swapped. The app's
restore trigger then submitted the captured original baseline at revision 736,
native sequence 3, persisted its receipt and cleared explicit intent. The owner
confirmed normal button behavior returned. Native receipt digests, journal source
identity and persisted selection matched; the original selected VDF was
byte-for-byte unchanged across the test/relaunch. A final cleared-intent relaunch
and fault/reconnect/held-output cases remain unverified. Milestone 1 is incomplete.

The final R7 cleared-intent relaunch also passed on device. A fresh nonce, native
state activation-requested and sequence/result 1/1 established query-only startup;
the journal retained the previous launch's restore receipt rather than adding a
new transaction. Explicit intent remained nil, the exported native baseline
exactly matched the pre-test original VDF, and the original selected source was
unchanged. The owner confirmed buttons remained normal. Persistence, relaunch,
restoration and override cleanup are device-validated; fault/reconnect and
held-output behavior remain outstanding before ordinary-build enablement.
