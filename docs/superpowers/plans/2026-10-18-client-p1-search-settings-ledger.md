# Client P1 ledger

Plan: docs/superpowers/plans/2026-10-18-client-p1-search-settings.md

- Task 1: complete (server USER_SEARCH; ctest 4/4; user_search_client PASS including self-exclusion and LIKE escaping)
- Task 2: complete (ContactStore scoped by server/account; client tests 16/16)
- Task 3: complete (UserSearchDialog + contacts; manual search/add/persist)
- Task 4: complete (client_log; log verified, no secrets)
- Task 5: complete (settings dialog + QSettings groups; notifications/server persist; logout preserves contacts)
- Task 6: complete (icon/version 0.2.0 + portable ZIP verified; VC runtime bundled)
- Task 7: complete (final tests/build/manual acceptance/docs)

## Final review fixes

- C1 fixed: changing server/TLS while logged in ends the session, removes `session`, updates login page, does not reconnect with the old endpoint.
- C2 fixed: `build_client.ps1` copies `vcruntime140.dll` into the package; ZIP contains it.
- I1 fixed: server excludes the caller from USER_SEARCH results; Python test covers it.
- I2 fixed: search has 8s timeout, sequence correlation, UTF-8 32-byte validation, busy state, and dialog error text.
- I3 fixed: LoginPage uses QSignalBlocker for programmatic settings; SettingsDialog normalizes TLS/port 9100/9443.
- I4 fixed: contacts are scoped by `host:port:user_id`; scoped ContactStore test added.
- I5 fixed: corrupt contact JSON is not overwritten; QSettings save status is checked.
- I6 fixed: debug builds copy `certs` to `$$OUT_PWD/debug`.
- I7 fixed: login page exposes a Cancel button while auto-login/RESUME is pending.
- I8 fixed: logout handles the LOGOUT response, waits up to 800 ms, and invalidates queued responses via connection generation.
- Minor fixed: search dialog closes on logout; duplicate search activation signal removed.

## Rulings

- Contacts are scoped by `server:port:user_id`. Cost: contacts saved by the pre-P1 global key are not shown; they remain in QSettings but are not migrated.
- Changing server/TLS while logged in ends the current session instead of reconnecting with the old endpoint. Cost: the user must log in again after changing the server.
- The portable ZIP bundles `vcruntime140.dll` from the build machine. Cost: older Windows versions may still require the VC++ 2015-2022 Redistributable/UCRT.


## Deferred minors

- Protocol/parse errors are not all logged (some still only update status).
- Log rotation ignores rename/open failures and can grow unbounded if `.1` is locked.
- USER_SEARCH has no rate limit and uses leading-wildcard scans.
- App icon contains one 64x64 image; 16/32/48/256 variants not generated.
- Inno Setup template points at build output rather than the clean dist tree; not compiled.
- `package_client.ps1 -Version` does not rewrite exe/installer version resources.
- Contact test does not restore global QSettings format/path after completion.
