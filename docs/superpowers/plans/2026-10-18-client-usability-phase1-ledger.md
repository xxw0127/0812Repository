# Client usability phase 1 ledger

Plan: docs/superpowers/plans/2026-10-18-client-usability-phase1.md

- Task 1: complete (core types + UTC time + QtTest harness; tests 5 passed)
- Task 2: complete (MessageStore + Outbox; tests 10 passed total)
- Task 3: complete (renderer; tests 14 passed total)
- Task 4: complete (history/push/local echo use MessageStore + HTML renderer; app builds; screenshots `screenshot-chat-render.png`, `screenshot-own-bubble.png`)
- Task 5: complete (send status/seq ack/timeout/retry/outbox restore; verified online sending, offline failure, restart retry; screenshots `screenshot-send-status.png`, `screenshot-send-sent.png`, `screenshot-send-failed.png`, `screenshot-retry-sent.png`)
- Task 6: complete (connection banner, offline input disable, login-page errors; screenshots `screenshot-offline-banner2.png`, `screenshot-recovered-banner.png`, `screenshot-login-error2.png`)
- Task 7: complete (final tests 15/15, app build, README/audit/plan updated, screenshots saved)

## Final review fixes (fresh subagent review)

- C1 fixed: history reconciliation for lapsed ACK + `markSent` merge by `message_id`; test `messageStoreReconcilesLapsedAck` RED→GREEN; suite 15/15.
  Ruling: history payload has no `client_msg_id`, so reconciliation uses outgoing + identical content + <=10s; cost: two intentionally identical messages within 10s could collapse; exact fix needs protocol change.
- C2 fixed: clear draft on context change; preserve room/private context across RESUME; manual screenshot `screenshot-resume-private.png`.
  Ruling: drafts are cleared on switching conversations; cost: unsent text is lost on switch.
- I1 fixed: 8s auth/RESUME timeout, malformed response resets login busy, RESUME failure schedules reconnect.
- I2 fixed: nonblocking TLS handshake with 5s deadline, nonblocking plaintext send with wait, `requestStop` shuts down active socket.
- I3 fixed: member reconnect `QTimer`, connection generation guard, `logging_out_` retained until disconnect.
- Minor fixed: UTC parse via QDate/QTime UTC (DST safe), invalid ACK timestamp no longer overwrites local time, retry only when failed, `running_` false on worker exit, logout status not overwritten.
- Deferred minors: preserved failed-message chronological ordering, history-error load-more re-enable, yellow connecting banner, auth-timeout message overwritten by disconnect, no Qt GUI automation.
