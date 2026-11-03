# Exact client adapters — ml1860

"Private" below means Valve's undocumented client interfaces. These are
compatibility adapters for genuine Valve libraries, not replacements for the
libraries or their authentication/DRM.
An unrecognized SHA-256 or mismatched method address fails closed. Interface
names and PE timestamps alone never authorize private calls.

The previously supported September build has SHA-256
`caba4826aa3501039d095aee1843a6bfb270fb43a3ab4455b2d6733223579fee`.
ml1860 also supports the owner's device's exact January build:
`71b391fe9f3e2006cbc81a5c75eef3eb4186012deabfdb2c8b7e8d4850ecf640`,
PE timestamp 1769726215. `MADEIRA_DOCK_CLIENT_202601=0` disables this adapter.
`[steam-host] ml1860 session-client-adapter=202601` identifies selection;
it is not authentication or entitlement evidence.

## Provenance and independent inspection

Fetched from Valve on 2026-09-24 via HTTPS:
`https://client-update.akamai.steamstatic.com/steam_client_win32`.
The returned manifest reports version 1769731672 and names
`bins_win32.zip.23e34a6d4b10596a44561a5100dac5585d2517da`, downloaded
from the same host. Archive SHA-256 matches its manifest:
`8b712b2a3412a9066b7725f4e1c5cef9a7ca5b187b6585a5b92d25d09df0ba62`.
Its steamclient64.dll matches the device report byte fingerprint exactly.
Windows Authenticode verification reports Valid, signer Valve Corp.
Downloaded files and analysis output stay ignored in `.build/client-jan2026`.
No Valve DLLs or archives are included in the app/repository distribution.

RTTI gives offset-zero vtables CSteamClient at RVA 0x12ec718,
IClientUserMap at 0x12b8938, IClientAppManagerMap at 0x12c0278.
All 14 called private methods are pinned in `src/client_layout.c`. User and
manager method names were independently checked via their RIP-relative IPC
strings, including LogOn, SetLoginToken, BIsSubscribedApp, GetSubscribedApps,
LaunchApp and GetAppInstallDir. Slots are unchanged between these two builds.

Disassembly comparison of the called wrappers (through their first return)
retains identical argument registers/stack widths and return widths. Engine
user/manager wrappers have identical normalized instructions. Differences in
the others are equivalent signed-negative cleanup tests and reordered loads
before the same underlying call; manager IPC type constants vary by build.
Those constants are implemented by Valve's own matching library, not Dock.
GetAPICallResult still forwards the same buffer/size/callback/failure args.
This inspection is bounded ABI evidence, not full behavioral compatibility.
The launch callback remains strictly checked for ID 1270027, 524 bytes, the
requested GameID and success. Do not loosen it if a future device test fails.

Reproduce the independent RTTI/table checks with Python (no DLL execution):

```
python tools/check-client-layout.py PATH_TO_OFFICIAL_STEAMCLIENT64_DLL
```

Portable sanitizer tests reject unknown/partial/changed fingerprints and
mismatched method pointers. A native Windows probe using the isolated official
January DLL verified all session pointers, with login/launch disabled, and
exited 0. Disabling the January adapter reproduces rejection 30. This does not prove native-token authentication or iOS game launch.
The original supported September table is also checked for regression.

## Device result motivating the change

Log 60 / ml1850 got past the earlier native stale-image crash, loaded both
interfaces, then returned session-unsupported-client=1 / probe-result=30.
It never reached authentication. The apparent infinite wait was separately a
public runtime notification gap on normal Wine exit. ml1860 fixes the common
exit wrapper and exports a bounded whitelist of Dock report fields, including
this adapter revision and fingerprint, in the normal Madeira log.

## ml1990 — custom executable (CEG) preparation

Both pinned builds add one called method, independently identified in each:
IClientUserMap slot 71 wrapper, September RVA `0x84e8b0`, January RVA
`0x82a8c0`. Slot 70 is GetCustomBinariesState (`0x769b60` / `0x74cc60`;
not called). Each wrapper names "RequestCustomBinaries" through a
RIP-relative LEA and serializes IPC function ID `0x08711e2c` and fencepost
`0x08b15ab0`; `tools/check-client-layout.py` now verifies slot, RVA, name
and function ID. Slot 70's wrapper fails that name check (January negative control).

Wrapper ABI (both builds, identical instructions except call targets):
rcx this, edx u32 AppID, r8b bool, r9b bool, one stack argument serialized
as 8 bytes; it reads one response byte (must be 11) and a 4-byte result,
returning 55 (k_EResultRemoteCallFailed) on IPC failure. The server
dispatcher (`cmp eax,0x8711e2c`) calls the user object's slot 71 with the
same five values. That implementation (`0x9b0860` Jan / `0x9e0d40` Sep)
immediately writes `0` through the fifth value, so it is a `uint32 *`
(the older 32-bit reference lists it as bytes4), then returns: 2 unknown
or uninstalled app, 10 app busy (updating/running flags), 1 with no
CustomExecutable entries, 9 no install folder, else 1 and the count of
per-file jobs started or already in progress. OpenSteamworks reference
lists the older build as `RequestCustomBinaries(AppId_t, bool, bool, uint)`.

First bool: skip DRM-identifier extraction and force a refresh. With zero
identifiers the job reports success without downloading, so Dock passes
false. Second bool: job flag 0x10, which ends the job with Busy (10) after a
server Busy reply instead of Valve's own retry loop; Dock passes false and
bounds the total wait itself (5 minutes).

Completion: CDRM::OnDRMDownloadJobFinish posts callback **1020025**
(`0xf9079`), 8 bytes: int32 EResult, uint32 AppID, once per job (source file
clientjobrequestcustombinary.cpp; strings "OnDRMDownloadJobFinish: %u (%s)").
Each server reply also posts 1020024 (`0xf9078`), 12 bytes: EResult, AppID,
bool. Identical IDs/sizes in both builds. The launch-time runtime DRM path
posts 1020025 with the same layout (Timeout 16) after a 0x8000DEAD report.

LaunchApp does not itself request custom binaries: the only callers of the
per-file request (`0x4f2a50` Jan) are this IClientUser method and the running
game's legacy DRM IPC fix-up. Hence the explicit request before LaunchApp.
Extraction reads PE resources (STEAM_SPLIT_GUID/STEAM_GUIDD/STEAM_MINSTANCE)
in process. The file list comes from the app manifest's CheckGuid entries.
Delivery of 1020025 to a global-user pipe and server success remain unproven
at runtime: this is static evidence only.

## Offline logon (see OFFLINE.md)

Both pinned builds add three called methods on IClientUserMap, at the same
slots in each: 5 `GetLogonState` (September RVA `0x77e460`, January
`0x7612c0`, IPC function `0xb3679023`), 214 `CanLogonOffline` (`0x73cbb0` /
`0x720330`, `0xe391b9f0`) and 215 `LogOnOffline` (`0x8415d0` / `0x81dad0`,
`0x706f013f`). `tools/check-client-layout.py` verifies slot, RVA, the method
name each wrapper references through a RIP-relative LEA, and the function ID.

Wrapper ABI, identical in both builds apart from call targets and the known
signed-negative cleanup test: slots 5 and 214 take only `this` and read back a
4-byte result; slot 215 takes `this` and one byte (dl, serialized as 1 byte)
and reads back a 4-byte result. Neighbouring slots 216
`ValidateOfflineLogonTicket` and 217 `BGetOfflineLogonTicket` are not called:
Dock never handles the ticket itself.

## Friends research (Phase 0, static only; nothing is called yet)

Inspected 2026-10-29 on the January build only. The CDN still serves manifest
version 1769731672 and the same `bins_win32` archive (SHA-256 matches the
manifest); its steamclient64.dll has the pinned January fingerprint, and
`tools/check-client-layout.py` passes on it. The September DLL is no longer
served, so **friends support is planned for the January adapter only**; on
September (or any other build) the friends feature reports unavailable and
the launch proceeds as before.

Accessor: CSteamClient slot **13** (`0x943510`). Its instructions are
identical to slot 8 (GetIClientUser, pinned) except the field it returns:
`ClientInterfaces_t+0x20` instead of `+0x18`. The constructor caller at
`0x9ed414` stores the object built at `0x728730`, which installs vtable
`IClientFriendsMap` (RVA `0x12bbed8`), into `+0x20`; the same caller stores
the IClientUserMap object into `+0x18` (`0x9ed5fc`), cross-checking slot 8.

IClientFriendsMap slots, each named by its wrapper's RIP-relative LEA:

| Slot | RVA | Method |
| ---: | --- | --- |
| 7 | 0x752d60 | GetFriendCount |
| 10 | 0x7526f0 | GetFriendByIndex |
| 12 | 0x754910 | GetFriendRelationship |
| 13 | 0x7542b0 | GetFriendPersonaState |
| 14 | 0x753c20 | GetFriendPersonaName |
| 23 | 0x753250 | GetFriendGamePlayed (IPC 0xe0ce182e) |
| 25 | 0x753580 | GetFriendGameServer |
| 93 | 0x754aa0 | GetFriendRichPresence |
| 96 | 0x82b830 | RequestFriendRichPresence |
| 194 | 0x81f0a0 | NotifyLobbyJoinRequested (IPC 0x0e74cb2a) |
| 195 | 0x81f4f0 | NotifyRichPresenceJoinRequested |
| 215 | 0x812f10 | InviteUserToGame (IPC 0x481f046d) |

Not to be called: 188/190 ActivateGameOverlayInviteDialog* (overlay UI).

Full wrapper review (Phase 1; each serializes the pipe, the function ID, its
arguments, then a fencepost, and checks response byte 11):

| Slot | Arguments after `this` | Returns |
| ---: | --- | --- |
| 7 | int32 flags | int32 |
| 10 | hidden `uint64 *` result (rdx), int32 index, int32 flags | CSteamID through rdx |
| 13 | uint64 SteamID | int32 |
| 14 | uint64 SteamID | string (copied at once) |
| 23 | uint64 SteamID, `FriendGameInfo_t *` | bool, then 24 bytes into the pointer |
| 93 | uint32 AppID, uint64 SteamID, string key | string or NULL |
| 215 | uint32 AppID, uint64 SteamID, string connect | bool |
| 194 | uint32 AppID, uint64, uint64 | bool |
| 195 | uint32 AppID, uint64 SteamID, string connect | bool |

Slot 194's two IDs are serialized and forwarded by the server dispatcher
(`0x798d29`) in the same order; Dock passes lobby then friend, following the
reference and `GameLobbyJoinRequested_t`. Confirm on a live client.

Earlier entry-register notes (first instructions only): GetFriendGamePlayed takes rdx (8-byte SteamID) and r8 (out pointer);
InviteUserToGame takes edx (4 bytes), r8 and r9; NotifyLobbyJoinRequested
takes edx (4 bytes, AppID), r8 and r9 (lobby and friend SteamIDs). Phase 1
must finish the ABI review for every called slot (argument widths, output
struct sizes, response bytes) the way ml1990 did, and extend the checker.

Consequences for the plan:
- Joining a running game: NotifyLobbyJoinRequested / NotifyRichPresenceJoin
  Requested are the client's own path for an accepted invite (they post the
  game's GameLobbyJoinRequested / GameRichPresenceJoinRequested), so no game
  restart is needed for games that listen to those callbacks.
- Joining at start: the host already passes MADEIRA_STEAM_HOST_ARGUMENTS to
  LaunchApp's pszUserArgs (bounded), so `+connect_lobby <id>` needs no new
  launch path. Untested live.
- Host lifetime: the launch loop polls callbacks every 50 ms until 3 s after
  the game exits, so the friends feed and commands can run in that loop.

## Friends sign-in (January build, 2026-10-30)

| Where | RVA | What |
| --- | --- | --- |
| CSteamClient slot 69 | 0x9447d0 | GetIClientSharedConnection(user, pipe): same shape as slot 13, returns `ClientInterfaces_t+0x118`, where the bundle constructor (`0x9ed519`) stores the object built at `0x729cc0` (vtable IClientSharedConnectionMap) |
| IClientSharedConnectionMap 0 | 0x70a0b0 | AllocateSharedConnection() -> uint32 handle (IPC 0x309e429d) |
| IClientSharedConnectionMap 2 | 0x832cb0 | SendMessage(uint32 handle, const void *data, uint32 size) -> bool (IPC 0x8ad70af2) |

Other slots, not called: 1 ReleaseSharedConnection, 3
SendMessageAndAwaitResponse, 4 RegisterEMsgHandler, 5
RegisterServiceMethodHandler, 6 BPopReceivedMessage, 7 InitiateConnection.

A message is `uint32 EMsg | 0x80000000`, `uint32` header length, a
CMsgProtoBufHeader (empty, or `routing_appid` field 3), then the body; the
client fills SteamID and session as for the web UI. Field numbers come from
the Friends window's own message definitions: CMsgClientChangeStatus 1
persona_state, 5 persona_set_by_user, 7 need_persona_response, 8
is_client_idle; CMsgClientRequestFriendData 1 persona_state_requested, 2
friends (fixed64); CMsgClientMMSInviteToLobby 2 steam_id_lobby, 3
steam_id_user_invited (fixed64). Builders: `src/friends_format.c`, byte-checked
in `tests/test-friends_format.c`.

Why it is needed: `SetPersonaStateInternal` (`0x9e61d0`) returns early in
web-UI mode (`0x68f550` true) after posting `SetPersonaStateInternal_t`; the
status sender `0x590850` needs logon state 4 and drops an unchanged status
(`0x1d`). Logon setup (`0x9dab1d`) reads `PersonaStateDesired` and
`AutoSignIntoFriends`.

Service calls use EMsg 151 with header field 12 target_job_name; Dock sends
`Chat.RequestFriendPersonaStates#1` this way (no response is read).

## Steam Input research

January build only (September not inspected, so unpinned: the probe reports
`steam-input-available=0`). Static PE/RTTI and disassembly only.

- `IClientControllerSerialized` is pipe-level: the pipe constructor (RVA
  `0x942a0b`) creates `IClientControllerSerializedMap` (vtable RVA `0x12c9fd0`)
  with user 0 and stores it at pipe entry `+0x60`. The neighbouring entries
  check the layout: `+0x38` to `+0x50` are the System, SystemDock, SystemAudio
  and SystemDisplay manager maps (CSteamClient slots 38 to 41), `+0x58` is
  `IClientVRMap` (slot 61).
- CSteamClient slot 66, RVA `0x943380`: `GetIClientControllerSerialized`
  (rcx this, edx HSteamPipe), returning `movq 0x60(%rcx,%rax)` of the
  pipe's entry.
- IClientControllerSerializedMap slot 22, RVA `0x765790`:
  `GetNumConnectedControllers` (rcx this only, int32 result). The wrapper
  names itself through a RIP-relative LEA and serializes IPC function ID
  `0x9b1eac33`, fencepost `0xa02721fd`.

All 149 slots name their methods the same way, so later steps can pin
`GetControllerEnableSupport` (132), `SetControllerSetting` (113),
`GetConfigForAppAndController` (124) and others. `tools/check-client-layout.py`
verifies slot 66's `+0x60` load and slot 22's name and function ID.
`src/steam_input.c` runs only with `MADEIRA_DOCK_STEAM_INPUT=1`.


### January Steam Input loader additions (2026-11-02)

| Serialized slot | Method | RVA | IPC ID |
|---|---|---|---|
| 85 | IsControllerConnected(index, bool) | `0x814730` | `0x9a5a4987` |
| 124 | GetConfigForAppAndController(index, const char *app, uint64 *workshop, CUtlString *selection) | `0x7487a0` | `0x143b8c3f` |
| 128 | HasGameMapping(uint32 app) | `0x77bd00` | `0x35c300c2` |

The checker validates RTTI entries, wrapper name references and IPC IDs. The
server's HasGameMapping forwards to CSteamController slot 62 (`0x5d7e70`),
which queries the parsed cache by controller/app through `0x5d4f30` and
`0x5d23f0` and releases its locks. This is cache presence, not proof of XInput
output. GetConfigForAppAndController returns a selection key, not a resolved
URL. January SteamUI.dll's resolver (`0x544440`) adds `template://` to a bare
template name; `AC-app-device` resolves through the controller-config app's
local storage as `account/config/app/device.vdf`. Workshop IDs are returned
separately. Dock reads bare templates, legacy template URLs and account-local
autosaves, and keeps an unavailable personal/Workshop selection unchanged.
The output CUtlString storage is reused for the session.

LoadConfigFromVDFString and SetFocusedWindow have void returns. Their wrappers
read only a response fencepost; interpreting a returned AL as success is invalid.

### Active-revision investigation candidates (not runtime pins)

A read-only RTTI/wrapper scan of the same pinned January binary identifies
serialized slot 45 `GetControllerConfiguration` at RVA `0x749c10` (IPC ID
`0xe724b7ee`) and slot 65 `GetBindingRevision` at `0x741200` (IPC ID
`0x0b7f0f1d`). Slot 61 names `GetBindingVDFString` at `0x741730`; slot 105
names `GetControllerIdentity` at `0x74a110`. These are investigation leads,
not supported APIs, and none is added to the runtime layout or invoked.

The slot-45 wrapper serializes one four-byte input from EDX, reads a
four-byte return value and deserializes an additional output through the
R8 argument. Slot 65 serializes one four-byte input, reads a boolean return
and copies two four-byte outputs through the R8/R9 arguments. These observations
do not establish the server-side meaning, object ownership or lifetime. In
particular, a binding's revision might describe an editable/cache object rather
than the layout currently executing on a controller. Next inspect the matching
server handlers and SteamUI callers, establish whether slot 45 exposes an active
binding or a copy, verify output storage/lifetime and asynchronous activation
ordering, then pin/test an acknowledgement using a distinctive mapping. The
version-1 query service intentionally does not use these candidates as proof.


### Configuration-loaded notification (2026-11-02)

Valve documents `SteamInputConfigurationLoaded_t` as a configuration-load
notification, with App ID, device handle, creator, action-manifest major/minor
revisions and native-action/gamepad flags:
https://partner.steamgames.com/doc/api/ISteamInput#SteamInputConfigurationLoaded_t
The pinned January client posts callback 2803 with 40 bytes at `0x5ccd37`
and `0x5ccd71`. The x64 payload layout is app at 0, device at 8, creator at 16,
major/minor at 24/28 and bool flags at 32/33 (tail padding to 40).
The observer accepts only this exact size and valid booleans, filters the
launched App ID and bounds numeric reporting to 16 observations. Creator is
neither decoded nor retained; device handles never enter logs. There are no
new private method calls. Notification delivery to Dock still requires device
verification. These revisions identify the action manifest, not the exact VDF
content; this notification alone cannot acknowledge a selected content hash.

Further server tracing rules out using slot 45 with controller index 0 as an
active-revision getter: its serialized server thunk `0x953540` delegates to
CSteamController slot 115 (`0x5d31f0`), which looks up the supplied binding
handle in the binding-object tree and serializes that object. Slot 65 delegates
through `0x9532c0` to CSteamController slot 135 (`0x5d1890`), looking up the
same tree and reading that object's revision fields. Neither establishes which
object is executing for the controller. Slot 103 GetControllerStatusEvent is
a fixed 12-byte controller-status snapshot; it is not an event queue or an
active-layout acknowledgement. None of these candidate methods is enabled.


### Next load-completion lead (2026-11-02; not enabled)

January's `CInternalCallback<CRemoteClientManager, LoadControllerConfigResult_t>`
RTTI resolves to vtable RVA `0x12db500`. Its constructor at `0x8d6033` stores
member handler `0x8e9990` and registers callback ID 1060003 (`0x102ca3`) at
`0x8d6065`. The controller load completion at `0x5cce40` posts that callback
with 32 bytes starting at its job object +0x50 (sender call at `0x5cce62`).
The R2 Dock report observes callback ID 1060003 on its pipe, independently of
the public 2803 notification. No payload is decoded, saved or logged.

This is a lead for correlating asynchronous load completion to the requested
binding, not an active-revision acknowledgement. Next trace the complete payload
construction and handler, establish binding/controller/app identity and ownership,
and distinguish cache-load completion from engine activation before adding a
bounded decoder. Never infer a content-hash acknowledgement from its name.


### Native document revision in the private completion (2026-11-02)

Further construction tracing establishes these fields in January callback
1060003 (32 bytes): completed bool at 0, controller index at 4, App ID at 8,
creator at 16 (deliberately not decoded), document revision at 24. Remaining
metadata/padding is skipped. `CLoadControllerConfigWorkItem` constructor
`0x5b7810` stores the index at +0x54 and app at +0x58, then copies mapping
+0x170 (creator) to +0x60 and mapping +0x19c to +0x68. The completion sender
posts the 32 bytes at +0x50. The parsed field at mapping +0x19c comes directly
from the root `revision` VDF key (`0x62dedd`..`0x62df09`). The writer increments
it at `0x633cfe` before writing `revision`; serializing a binding is therefore
not necessarily a passive read. This is separate from the public callback's
action-manifest major/minor values at mapping +0x224/+0x228.

The real-binary checker now asserts the work-item vtable target, sender size,
constructor field origins, VDF key and revision store/increment instructions.
The runtime decoder is gated by the existing exact January adapter and filters
the launched app/controller 0, validates exact size and bool/index bounds, skips
creator and caps reporting. A separate metadata reader compares the queued
VDF's document revision without changing it. No callback changes the production
service to active. A compile-time opt-in, one-shot synthetic revision-731 probe
now exercises live queueing and original-byte restoration. Its distinct A/B
mapping, callback ordering and original-file preservation require device proof
before promoting this correlation into an exact-revision acknowledgement.
