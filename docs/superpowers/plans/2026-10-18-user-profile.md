# User Profile and Avatar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a QQ/WeChat-style personal profile page with editable nickname, signature and uploaded avatar, while keeping `username` immutable and login-only.

**Architecture:** Add a server-side `ProfileService` and profile/avatar commands, store profile fields in MySQL `users`, and render a Qt `ProfileDialog` reachable from friends, room members, search and chat header. Avatar images are client-cropped/compressed JPEG/PNG bytes cached by `user_id + avatar_version`.

**Tech Stack:** Linux C++17 server, epoll ET, MySQL 8.4 C API, existing binary frame protocol; Qt 5.15 Widgets client, native Winsock/OpenSSL, QImage/QPixmap, QtTest.

**Spec:** `docs/superpowers/specs/2026-10-18-user-profile-design.md`

## Global Constraints

- Server: Linux, C++17, GCC 9+, `-Wall -Wextra -O2`; no platform macros in business logic.
- Client: Qt 5.15 Widgets, C++17, native Winsock; UI work only on the main thread.
- Never put database access in the client; all profile data goes through the server.
- Protocol integers are big-endian; never send C++ structs directly.
- Nickname: 1–32 Unicode code points after trimming; not unique.
- Signature: 0–128 Unicode code points after trimming.
- Username: immutable, login-only; never editable through profile update.
- Avatar: JPEG or PNG, maximum 48 KiB; empty means fallback to colored initial avatar.
- `avatar_version` is u32 and monotonically increases on upload and clear.
- Any authenticated user may view another user's public profile.
- The workspace is **not a Git repository**. Replace "commit" steps with a checkpoint: build, run the listed tests, and record the result. Do not initialize Git unless the user asks.
- Keep existing room data and history; profile migration is additive.
- Do not add IP fields in this iteration.

## Review Focus

- A 32-Chinese-character nickname must not be truncated in login, search, friend list, room members, conversations or history; byte-based limits and 33-byte buffers are the likely failure.
- Clearing an avatar must not make clients loop forever between `avatar_version=N` and `AVATAR_GET` returning version 0; the response must return the stored version with empty bytes.
- Online friend profile updates must refresh the friend's nickname/avatar without re-login, while existing remarks still win as display names.
- A non-friend private chat opened from a room member list must still be able to fetch the real avatar for its header.
- Oversized/invalid avatar bytes and unauthorized profile edits must return a clear error without corrupting the stored profile.

---

## File Structure

**Server**

- `common/protocol.h` — profile/avatar command constants.
- `common/text_utils.h/.cpp` — UTF-8 code point count and trimming.
- `server/profile/profile_service.h/.cpp` — profile GET/UPDATE/AVATAR handling and push targets.
- `server/db/mysql_connection.h/.cpp` — profile/avatar SQL, relation lookup, friend IDs, widened nickname buffers.
- `server/net/connection.h/.cpp` — `setNickname`.
- `server/room/room_manager.h/.cpp` — `updateNickname`.
- `server/business/worker_pool.h/.cpp` — `ProfileService` in `WorkerContext`.
- `server/main.cpp` — profile command routing, connection nickname update, push dispatch.
- `deploy/migrate_profiles.sql`, `deploy/schema.sql`.
- `tests/test_text_utils.cpp`.

**Client**

- `core/user_profile.h` — client profile model and display-name helper.
- `core/avatar_utils.h/.cpp` — square crop, JPEG compression, fallback initials.
- `core/avatar_cache.h/.cpp` — versioned `QImage` cache.
- `ui/profile_dialog.h/.cpp` — view/self-edit profile page.
- `ui/friend_list_widget.h/.cpp` — avatar-area click behavior.
- `ui/friend_item_delegate.h/.cpp` — real avatar painting and avatar role.
- `ui/main_window.h/.cpp` — commands, entries, own strip, cache wiring, profile actions.
- `ui/user_search_dialog.h/.cpp` — "查看资料" action.
- `chatroom_client.pro`, `tests/tests.pro`, `tests/test_client_core.cpp`.

**Tools**

- `tools/profile_client.py` — profile GET/UPDATE/AVATAR/push integration.
- `tools/friend_client.py` — parse avatar version and regress friend behavior.

---

### Task 1: UTF-8 Text Utilities and Nickname Limits

**Files:**
- Create: `common/text_utils.h`
- Create: `common/text_utils.cpp`
- Test: `tests/test_text_utils.cpp`
- Modify: `CMakeLists.txt`
- Modify: `server/auth/auth_service.cpp`
- Modify: `server/db/mysql_connection.cpp`
- Modify: `client/ui/login_page.cpp`

**Interfaces:**
- Produces:
  - `std::size_t chatroom::utf8CodePointCount(const std::string& value)`
  - `std::string chatroom::trimAsciiWhitespace(const std::string& value)`
  - `bool chatroom::containsControlCharacters(const std::string& value)`

- [ ] **Step 1: Write the failing text-utility test**

Add `tests/test_text_utils.cpp` using the existing `tests/test_framework.h` style:

```cpp
CHECK_EQ(chatroom::utf8CodePointCount("abc"), std::size_t(3));
CHECK_EQ(chatroom::utf8CodePointCount("你好"), std::size_t(2));
CHECK_EQ(chatroom::utf8CodePointCount("A😀"), std::size_t(2));
CHECK_EQ(chatroom::trimAsciiWhitespace("  hi\n"), std::string("hi"));
CHECK_TRUE(chatroom::containsControlCharacters(std::string("a\nb")));
CHECK_TRUE(!chatroom::containsControlCharacters("你好"));
```

Add a `main()` that calls the test function and returns its result, matching the existing server test pattern.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake -S . -B build && cmake --build build -j4 --target test_text_utils`
Expected: failure because the header/functions do not exist.

- [ ] **Step 3: Implement the utility functions**

Add `common/text_utils.cpp` to `chatroom_common` in `CMakeLists.txt`, add `test_text_utils` target and `add_test(NAME text_utils ...)`, then implement the three functions. Count non-continuation UTF-8 bytes (`(byte & 0xC0) != 0x80`). Trim ASCII spaces/tabs/CR/LF only.

- [ ] **Step 4: Use code-point validation for registration nickname**

In `server/auth/auth_service.cpp`:

- Replace `nickname.size() > 32` with `utf8CodePointCount(nickname) > 32`.
- Use `containsControlCharacters(nickname)` instead of the byte loop.
- Read nickname with a 128-byte maximum accepted payload, then apply the 32-code-point rule.

In `client/ui/login_page.cpp`, before emitting `registerRequested`, reject `nickname.toUcs4().size() > 32` and show "昵称不能超过 32 个字符".

- [ ] **Step 5: Widen nickname read buffers**

In `server/db/mysql_connection.cpp`, replace every `char nickname_buffer[33]`, `char peer_nickname[33]`, `char reply_sender_buffer[33]` and corresponding `sizeof(...) - 1` capacities with 129-byte buffers. This includes user lookup, friend list, friend requests, user search, conversations, history and reply snapshots. Do not change `VARCHAR(32)` in MySQL; it already counts characters.

- [ ] **Step 6: Run the tests and server build**

Run: `cmake --build build -j4 && (cd build && ctest --output-on-failure)`
Expected: all tests pass, including `text_utils`.

- [ ] **Step 7: Checkpoint**

Record the ctest output. Do not commit because the workspace is not a Git repository.

---

### Task 2: Profile Schema and MySQL DAO

**Files:**
- Create: `deploy/migrate_profiles.sql`
- Modify: `deploy/schema.sql`
- Modify: `server/db/mysql_connection.h`
- Modify: `server/db/mysql_connection.cpp`

**Interfaces:**
- Produces:
  - `struct UserProfile { uint64_t id; std::string username; std::string nickname; std::string signature; std::string avatar; uint32_t avatar_version = 0; };`
  - `bool MysqlConnection::getUserProfile(uint64_t target_id, UserProfile& profile, bool& found, std::string& error)`
  - `bool MysqlConnection::getAvatar(uint64_t target_id, uint32_t& version, std::string& avatar, bool& found, std::string& error)`
  - `bool MysqlConnection::updateUserProfile(uint64_t user_id, const std::string& nickname, const std::string& signature, uint8_t avatar_action, const std::string& avatar, uint32_t& version, std::string& error)`
  - `bool MysqlConnection::getProfileRelation(uint64_t viewer_id, uint64_t target_id, uint8_t& relation, std::string& remark, std::string& error)`
  - `bool MysqlConnection::listFriendIds(uint64_t user_id, std::vector<uint64_t>& ids, std::string& error)`
  - `UserSearchRow::avatar_version` (`uint32_t`).

**DAO semantics:**
- `avatar_action`: `0` unchanged, `1` set, `2` clear.
- Version increments exactly once on action 1 or 2; action 0 leaves it unchanged.
- `getAvatar` returns stored version even when avatar is NULL; avatar bytes are empty in that case.
- Relation values: `0` self, `1` friend, `2` none, `3` caller sent pending request, `4` target sent pending request.
- Remark is the viewer's `friend_remarks.remark`, or empty for non-friends.

- [ ] **Step 1: Write the migration and schema changes**

`deploy/migrate_profiles.sql` should add:

```sql
USE chatroom;

ALTER TABLE users
    ADD COLUMN signature VARCHAR(128) NOT NULL DEFAULT '' AFTER nickname,
    ADD COLUMN avatar MEDIUMBLOB NULL AFTER signature,
    ADD COLUMN avatar_version INT UNSIGNED NOT NULL DEFAULT 0 AFTER avatar;
```

Add the same three columns to the `users` table in `deploy/schema.sql`. Use an information_schema-guarded procedure or equivalent so a repeated migration does not fail in production.

- [ ] **Step 2: Add DAO declarations and `avatar_version`**

Add the `UserProfile` struct and method declarations to `mysql_connection.h`; add `uint32_t avatar_version = 0` to `UserSearchRow`.

- [ ] **Step 3: Implement profile reads**

Use prepared statements. Explicitly select `signature`, `avatar_version`, and `avatar`; read the avatar into a 48 KiB + 1 buffer and return the stored version even when `is_null` is true. `getProfileRelation` checks self, then `friendships`, then the directional pending `friend_requests` rows, then queries `friend_remarks` for the viewer/target pair.

- [ ] **Step 4: Implement profile updates**

Validate inputs at the service layer, but DAO must also reject unknown action values. Use prepared statements with no SQL concatenation. For action 1 bind avatar bytes; for action 2 bind NULL; for action 0 bind no avatar. Then update nickname/signature and read back the new `avatar_version` in the same operation.

- [ ] **Step 5: Implement friend ID lookup**

`listFriendIds` returns the other user's ID for every friendship row containing `user_id`, ordered consistently for deterministic push ordering.

- [ ] **Step 6: Build the server**

Run: `cmake --build build -j4`
Expected: no warnings/errors.

- [ ] **Step 7: Apply the migration remotely**

Copy `deploy/migrate_profiles.sql` to the server with `scp`, then run:

```bash
ssh chatroom-dev 'sudo mysql chatroom < /tmp/migrate_profiles.sql'
```

Verify `signature`, `avatar`, `avatar_version` exist in `users`. Do not restart production yet.

- [ ] **Step 8: Checkpoint**

Record the build result and migration output.

---

### Task 3: Profile GET / AVATAR GET Protocol and Service

**Files:**
- Modify: `common/protocol.h`
- Create: `server/profile/profile_service.h`
- Create: `server/profile/profile_service.cpp`
- Modify: `CMakeLists.txt`
- Modify: `server/business/worker_pool.h`
- Modify: `server/business/worker_pool.cpp`
- Modify: `server/main.cpp`
- Create: `tools/profile_client.py`

**Interfaces:**
- Produces:
  - `kCmdProfileGet = 0x00A0`
  - `kCmdAvatarGet = 0x00A2`
  - `ProfileResult ProfileService::handle(const Frame& request, const RoomMember& caller)`
  - `struct ProfileResult { Frame response; };` in this task; extended in Task 4.
- Consumes: DAO methods from Task 2.

- [ ] **Step 1: Write the initial integration test**

Create `tools/profile_client.py` using the existing `room_client.Client` helpers. It should register/login two users and assert:

1. `PROFILE_GET` returns the target's username, nickname, UID, empty signature, avatar version 0, empty avatar, online flag and relation.
2. A non-friend can fetch the profile.
3. `AVATAR_GET` for a user with no avatar returns version 0 and empty bytes.
4. Fetching a non-existent ID returns error 1008.

Run it against a temporary plaintext 9102 server. Expected: fail because the command does not exist.

- [ ] **Step 2: Add protocol constants**

Add `kCmdProfileGet = 0x00A0`, `kCmdProfileUpdate = 0x00A1`, `kCmdAvatarGet = 0x00A2`, `kCmdProfileUpdatePush = 0x00A3` to `common/protocol.h`.

- [ ] **Step 3: Implement `ProfileService` read paths**

Write `handleProfileGet` and `handleAvatarGet`. Use `getUserProfile`, `getProfileRelation`, `getAvatar`, and `SessionManager::isOnline`. Encode:

```text
PROFILE_GET response: target_id u64, username, nickname, signature,
                       avatar_version u32, avatar bytes, online u8,
                       relationship u8, remark
AVATAR_GET response: target_id u64, avatar_version u32, avatar bytes
```

Use `ByteWriter::putString` for avatar bytes (u16 length; 48 KiB fits).

- [ ] **Step 4: Wire the service**

Add `server/profile/profile_service.cpp` to `CMakeLists.txt`; add `ProfileService& profile` to `WorkerContext`; construct it in `workerLoop` with the worker's database and sessions. Add `profile_command = command == kCmdProfileGet || command == kCmdAvatarGet || command == kCmdProfileUpdate` and route `context.profile.handle` through the same response/push loop pattern as `FriendService`.

- [ ] **Step 5: Build and run the test**

Build the server, start a 9102 test instance, run:

```bash
python3 tools/profile_client.py --host 127.0.0.1 --port 9102
```

Expected: all initial GET/AVATAR checks pass.

- [ ] **Step 6: Checkpoint**

Stop the 9102 instance and record the output.

---

### Task 4: Profile Update, Nickname Propagation and Push

**Files:**
- Modify: `server/profile/profile_service.h`
- Modify: `server/profile/profile_service.cpp`
- Modify: `server/main.cpp`
- Modify: `server/net/connection.h`
- Modify: `server/net/connection.cpp`
- Modify: `server/room/room_manager.h`
- Modify: `server/room/room_manager.cpp`
- Modify: `tools/profile_client.py`

**Interfaces:**
- Produces:
  - `void Connection::setNickname(std::string nickname)`
  - `void RoomManager::updateNickname(uint64_t user_id, const std::string& nickname)`
  - `struct ProfilePushTarget { int io_thread_id; uint64_t connection_id; };`
  - `struct ProfileResult { Frame response; bool nickname_changed = false; std::string new_nickname; std::string push_frame; std::vector<ProfilePushTarget> push_targets; };`

**Update rules:**
- Only `caller.user_id` can be changed.
- Nickname 1–32 code points, no control chars.
- Signature 0–128 code points, no control chars.
- Avatar action 0/1/2; action 1 accepts only JPEG/PNG magic and ≤48 KiB; action 2 clears.
- Empty nickname is error 1001.
- Avatar errors return 1017.
- Only nickname/avatar-version changes produce a push; signature-only updates do not need a push.

- [ ] **Step 1: Extend the integration test before implementation**

Add tests to `tools/profile_client.py`:

- Update nickname/signature, then `PROFILE_GET` returns the new values.
- Username remains unchanged and cannot be sent as an editable field.
- A 32-Chinese-character nickname survives `PROFILE_GET`, friend list, room members and history after a new message.
- Upload a small JPEG; version increments; `AVATAR_GET` bytes match.
- Clear the avatar; version increments again; `AVATAR_GET` returns empty bytes with the new version.
- Invalid magic and >48 KiB payloads return 1017.
- A user cannot update another user's profile.
- An online friend receives `0x00A3` with the new nickname/version; an offline friend does not.
- After update, a new room message from that user carries the new nickname.

Run the extended test; expected fail at the first update assertion.

- [ ] **Step 2: Add `Connection::setNickname`**

Add the method and have the existing `setAuth` call it. Do not change auth semantics.

- [ ] **Step 3: Add `RoomManager::updateNickname`**

Lock the manager mutex and update every room member with the matching `user_id`, preserving connection/IO thread fields.

- [ ] **Step 4: Implement the update path**

Validate body fields, call `updateUserProfile`, build the success response with the new nickname/signature/version. If nickname or avatar version changed:

- Query friend IDs.
- For each online friend session, append a target and build one shared `PROFILE_UPDATE_PUSH` frame containing `user_id`, `nickname`, `avatar_version`. A single push frame can be shared by all targets.
- Set `nickname_changed` when the nickname differs.

- [ ] **Step 5: Wire propagation in `main.cpp`**

After `context.profile.handle`:

- If `nickname_changed`, call `room_manager.updateNickname(user_id, new_nickname)` and post a task to the caller's IO thread that calls `connection->setNickname(new_nickname)`.
- Group `push_targets` by IO thread and call `postMulticast` with the shared push frame.
- Post the profile response as usual.

- [ ] **Step 6: Build and run integration**

Build, start 9102, run `profile_client.py`. Expected: all profile/update/avatar/push/nickname tests pass.

- [ ] **Step 7: Checkpoint**

Stop 9102 and record the output.

---

### Task 5: Friend List Avatar Version Compatibility

**Files:**
- Modify: `server/friend/friend_service.cpp`
- Modify: `client/core/friend_view.h`
- Modify: `client/ui/main_window.cpp`
- Modify: `tools/friend_client.py`

**Interfaces:**
- `FriendView` gains `quint32 avatar_version = 0`.
- `FRIEND_LIST` item becomes `user_id, username, nickname, online, remark, avatar_version`.
- Client profile/avatar rendering consumes this field in later tasks.

- [ ] **Step 1: Update the friend integration test**

Modify `friend_list()` in `tools/friend_client.py` to read the extra `u32` and assert it is 0 for a new user. Run against 9102; expected fail because the server still sends the old layout.

- [ ] **Step 2: Implement the server field**

Write `avatar_version` after `remark` in `FriendService::handleList`. The value comes from `UserSearchRow::avatar_version` populated by `listFriends`.

- [ ] **Step 3: Parse the client field**

Add `avatar_version` to `FriendView`, parse it in `kCmdFriendList`, and store it in the friend view. Do not use it for requests yet.

- [ ] **Step 4: Build and run tests**

Build server/client, run `friend_client.py`; expected all friend checks pass. Run client tests; expected pass.

- [ ] **Step 5: Checkpoint**

Record output.

---

### Task 6: Client Avatar Utilities and Cache

**Files:**
- Create: `client/core/avatar_utils.h`
- Create: `client/core/avatar_utils.cpp`
- Create: `client/core/avatar_cache.h`
- Create: `client/core/avatar_cache.cpp`
- Modify: `client/chatroom_client.pro`
- Modify: `client/tests/tests.pro`
- Modify: `client/tests/test_client_core.cpp`

**Interfaces:**
- Produces:
  - `QImage chatroom::client::centerCropSquare(const QImage& source)`
  - `QByteArray chatroom::client::encodeAvatarJpeg(const QImage& source, int max_bytes = 48 * 1024)`
  - `QImage chatroom::client::fallbackAvatar(const QString& name, quint64 seed, int size)`
  - `QImage chatroom::client::circularAvatar(const QImage& source, int size)`
  - `class AvatarCache { bool needsFetch(quint64, quint32) const; void store(quint64, quint32, const QImage&); bool lookup(quint64, QImage&) const; void clear(); }`

- [ ] **Step 1: Write failing Qt tests**

Add test slots to `test_client_core.cpp`:

```cpp
void avatarCenterCrop();
void avatarCompressionLimit();
void avatarFallback();
void avatarCacheVersion();
```

Use a 2×1 `QImage` and verify the cropped dimensions are 1×1; encode a noisy 256×256 image and verify `size() <= 48*1024`; verify fallback image is non-null; store/retrieve a cache entry and verify `needsFetch(id, version+1)` is true.

- [ ] **Step 2: Add QtGui/Widgets to the test project**

Change `tests/tests.pro` to `QT += core gui widgets testlib`, remove `QT -= gui`, and change the test main macro to `QTEST_MAIN` (the tests now render QImage fonts). Add the new sources/headers and `../core/avatar_utils.cpp`, `../core/avatar_cache.cpp`.

- [ ] **Step 3: Implement avatar utilities**

Use `QImageReader` at the UI call site, but keep `avatar_utils` pure functions over `QImage`. `centerCropSquare` covers the center, `encodeAvatarJpeg` tries sizes/qualities in the order from the spec until ≤48 KiB, and `fallbackAvatar` draws the first character on a colored circle. Use `QPainter` with antialiasing.

- [ ] **Step 4: Implement the cache**

Store `version`, `QImage`, and `loaded`. `needsFetch(id, version)` returns false for version 0, false for a loaded same-version entry, and true otherwise. `store` with an empty `QImage` marks a loaded version with no avatar. `lookup` returns false for empty images.

- [ ] **Step 5: Run the client tests**

Run: `cd client/tests && ./run_client_tests.ps1`
Expected: all tests pass, including new avatar tests.

- [ ] **Step 6: Build the client**

Run: `cd client && ./build_client.ps1`
Expected: success.

- [ ] **Step 7: Checkpoint**

Record test count and build result.

---

### Task 7: Client Profile Model and Dialog

**Files:**
- Create: `client/core/user_profile.h`
- Create: `client/ui/profile_dialog.h`
- Create: `client/ui/profile_dialog.cpp`
- Modify: `client/chatroom_client.pro`
- Modify: `client/tests/test_client_core.cpp`

**Interfaces:**
- `struct UserProfile { quint64 user_id; QString username; QString nickname; QString signature; QString remark; quint32 avatar_version; QImage avatar; bool online; quint8 relationship; };`
- `QString chatroom::client::profileDisplayName(const UserProfile& profile)`
- `class ProfileDialog : public QDialog` with:
  - `void setProfile(const UserProfile& profile, bool self)`
  - `void setAvatar(const QImage& image)`
  - `void setBusy(bool busy)`
  - `void setStatus(const QString& text, bool error = false)`
  - signals `saveRequested(const QString&, const QString&, int avatar_action, const QByteArray&)`, `messageRequested(quint64)`, `addFriendRequested(quint64)`, `removeFriendRequested(quint64)`, `remarkRequested(quint64)`, `refreshRequested(quint64)`

**Dialog behavior:**
- Self mode: editable nickname/signature, change/remove avatar, save; username/UID read-only.
- View mode: read-only fields; action buttons based on relationship.
- `avatar_action`: 0 unchanged, 1 set, 2 clear; when 1 emit JPEG bytes from `encodeAvatarJpeg`.
- Remark wins as the primary name; nickname shown as secondary when remark exists.
- Empty signature shows the fallback text.

- [ ] **Step 1: Write the display-name test**

Add `profileDisplayNameFallsBackToNickname` to the Qt test:

```cpp
UserProfile p;
p.nickname = "昵称";
QCOMPARE(profileDisplayName(p), QString("昵称"));
p.remark = "备注";
QCOMPARE(profileDisplayName(p), QString("备注"));
```

Run; expected fail.

- [ ] **Step 2: Implement the model helper**

Create `core/user_profile.h` with the inline helper using remark-first behavior.

- [ ] **Step 3: Implement `ProfileDialog`**

Build the layout with QLabel avatar, name, username/UID, signature, action row, and edit widgets. Use `QFileDialog` and `QImageReader` for choose-image, `encodeAvatarJpeg` for compression, and preview the pending image. Wire buttons to the signals listed above; hide self-edit fields in view mode. Set object names and QSS-compatible styles consistent with existing dialogs.

- [ ] **Step 4: Add to the client project and build**

Add sources/headers to `chatroom_client.pro`; build. Manually instantiate the dialog in a temporary debug call or use the test harness to confirm it opens.

- [ ] **Step 5: Run tests**

Run: `cd client/tests && ./run_client_tests.ps1`
Expected: display-name test and all previous tests pass.

- [ ] **Step 6: Checkpoint**

Record result.

---

### Task 8: Friend Avatar Rendering, Own Profile Strip and Avatar Click

**Files:**
- Create: `client/ui/friend_list_widget.h`
- Create: `client/ui/friend_list_widget.cpp`
- Modify: `client/ui/friend_item_delegate.h`
- Modify: `client/ui/friend_item_delegate.cpp`
- Modify: `client/ui/main_window.h`
- Modify: `client/ui/main_window.cpp`
- Modify: `client/chatroom_client.pro`

**Interfaces:**
- `class FriendListWidget : public QListWidget`; signal `void avatarClicked(QListWidgetItem* item)`; override `mouseReleaseEvent`.
- `FriendItemDelegate` gains `kAvatarRole = Qt::UserRole + 7`, holding a `QPixmap`.
- `MainWindow` gains:
  - `void updateOwnProfileStrip();`
  - `void openOwnProfile();` (temporary slot; opens an empty ProfileDialog in this task)
  - `void openProfile(quint64 user_id, const QString& fallback_name = QString());` (temporary stub in this task; replaced in Task 9)
  - `QPushButton* my_profile_button_ = nullptr;`
  - `FriendListWidget* conversation_list_ = nullptr;`
  - `AvatarCache avatar_cache_;`

- [ ] **Step 1: Add the custom friend list widget**

Implement `mouseReleaseEvent`: for left release, get `itemAt(pos)`; if the item has a non-zero user ID and `pos.x() <= visualItemRect(item).left() + 58`, emit `avatarClicked(item)` and return without calling the base. Otherwise call `QListWidget::mouseReleaseEvent`.

- [ ] **Step 2: Switch the list connection**

Create `conversation_list_` as `FriendListWidget*`; connect `itemClicked` (not `itemPressed`) to `onConversationClicked`, and connect `avatarClicked` to a new `onFriendAvatarClicked` that calls `openProfile(id)`. Keep `itemActivated` for keyboard activation. For this task `openProfile` may show the empty `ProfileDialog` as a smoke test; Task 9 replaces it with `PROFILE_GET`.

- [ ] **Step 3: Render real avatars**

Add the avatar role to `FriendItemDelegate::paint`: if a non-null `QPixmap` is present, draw it in the avatar rect using a circular clip; otherwise keep the existing initial/color drawing. In `refreshFriendListView`, call `avatar_cache_.lookup(view.user_id, image)` and set the role to `QPixmap::fromImage(circularAvatar(image, 36))` when available.

- [ ] **Step 4: Add the own profile strip**

Add a bottom button in the left panel with a 32px icon, nickname and `U...` text. Use `updateOwnProfileStrip()` after login/resume, profile updates and avatar cache updates. Clicking it calls `openOwnProfile()`; for this task it opens the empty `ProfileDialog` in self mode as a smoke test.

- [ ] **Step 5: Build and manually verify**

Build and launch the client. Verify the strip appears, friend rows still click into private chats, avatar-area clicks open the temporary profile dialog, and the existing friend list layout is unchanged without avatars.

- [ ] **Step 6: Checkpoint**

Record build and UI check.

---

### Task 9: Profile Requests, Avatar Fetching and Entry Points

**Files:**
- Modify: `client/ui/main_window.h`
- Modify: `client/ui/main_window.cpp`
- Modify: `client/ui/user_search_dialog.h`
- Modify: `client/ui/user_search_dialog.cpp`
- Modify: `client/ui/friend_request_dialog.h`
- Modify: `client/ui/friend_request_dialog.cpp`
- Modify: `client/chatroom_client.pro`

**Interfaces:**
- `MainWindow` gains:
  - `void requestProfile(quint64 user_id);`
  - `void requestAvatar(quint64 user_id, quint32 version);`
  - `void openProfile(quint64 user_id, const QString& fallback_name = QString());`
  - `void onProfileAvatarReceived(quint64 user_id, quint32 version, const QImage& image);`
  - `void onProfileDataReceived(const UserProfile& profile);`
  - `ProfileDialog* profile_dialog_ = nullptr;`
  - `QSet<quint64> pending_avatar_requests_;`
  - `QSet<quint64> pending_profile_requests_;`
- `UserSearchDialog` gains `profileRequested(quint64)` and a "查看资料" button.
- `FriendRequestDialog` gains `profileRequested(quint64)` and opens it on double-click.

**Behavior:**
- Send `PROFILE_GET` on open; show busy state.
- Parse `PROFILE_GET`, `AVATAR_GET`.
- On friend list parse, request avatars when `avatar_version != 0` and `avatar_cache_.needsFetch`.
- On private chat open, request avatar for the peer if not cached; for non-friends use version 0 request and store whatever version comes back.
- Add "查看资料" to friend context menu and group member context menu.
- Add a header "资料" button visible only in private context.
- Add profile action wiring: message, add friend, remove friend, set remark, refresh.
- On `PROFILE_UPDATE_PUSH`, update the friend view nickname/version and request avatar if version changed.

- [ ] **Step 1: Add protocol send helpers**

Implement request methods with `next_seq_++` and `ByteWriter`. Keep pending sets in sync; remove IDs on response or error. Use `kCmdProfileGet`/`kCmdAvatarGet`.

- [ ] **Step 2: Parse profile frames**

Add cases in `onFrameReceived` for `kCmdProfileGet`, `kCmdAvatarGet` and `kCmdProfileUpdatePush`. Decode strings/bytes exactly in the order from the spec. Store images in `avatar_cache_`; for empty images mark the version as loaded.

- [ ] **Step 3: Request own profile after authentication**

After `kCmdLogin` and `kCmdResume` set `user_id_`, call `requestProfile(user_id_)` so the own strip/profile dialog receives nickname, signature, avatar and version. Do not call it before the socket is authenticated.

- [ ] **Step 4: Wire avatar requests**

After parsing `FRIEND_LIST`, for each friend with a non-zero version not fresh in cache, send `AVATAR_GET`. Avoid duplicates with `pending_avatar_requests_`. On response, refresh the friend list and current header. When `selectPrivate` opens a peer, request the avatar if it is missing: use the friend's known version for friends, and version 0 for non-friends.

- [ ] **Step 5: Wire profile dialog opening**

Replace the temporary `openOwnProfile` with a `PROFILE_GET` for `user_id_`. Implement `openProfile` for other IDs. Keep a fallback name so the dialog can show initials while loading. On close, delete or hide the dialog safely.

- [ ] **Step 6: Wire profile dialog actions**

Connect `ProfileDialog` signals:
- `messageRequested` → `selectPrivate(profile.user_id, profileDisplayName(profile))`, then close.
- `addFriendRequested` → existing `sendFriendRequest`.
- `removeFriendRequested` → existing `sendFriendRemove`.
- `remarkRequested` → existing `setFriendRemark`.
- `refreshRequested` → `requestProfile(user_id)`.
- `saveRequested` is connected in Task 10.

- [ ] **Step 7: Wire all entry points**

- Friend context menu "查看资料".
- Member list context menu policy + "查看资料".
- Private header "资料" button.
- Search dialog "查看资料" for the selected row.
- Friend request dialog double-click/button.
- Own strip.

- [ ] **Step 8: Build and manual test**

Build. Verify profile pages open for self, friend and stranger; avatars load for friends; friend list and header update; no duplicate `AVATAR_GET`; missing users show an error.

- [ ] **Step 9: Checkpoint**

Record build and observed behavior.

---

### Task 10: Profile Editing, Update Push Handling and Final UI

**Files:**
- Modify: `client/ui/main_window.h`
- Modify: `client/ui/main_window.cpp`
- Modify: `client/ui/profile_dialog.cpp`
- Modify: `client/ui/profile_dialog.h`

**Interfaces:**
- `MainWindow::onProfileSaveRequested(const QString& nickname, const QString& signature, int avatar_action, const QByteArray& avatar_jpeg)` sends `PROFILE_UPDATE`.
- On `PROFILE_UPDATE` response, update `nickname_`, `QSettings session/nickname`, own strip, open profile dialog and subsequent own messages.
- On `PROFILE_UPDATE_PUSH`, update `friend_views_`, clear stale avatars, update list/header.
- Do not overwrite private-chat peer titles.

- [ ] **Step 1: Send profile updates**

Validate nickname/signature client-side using `toUcs4().size()`; send `avatar_action` and JPEG bytes. Set the dialog busy and disable save while waiting. Handle error 1017/1001 visibly.

- [ ] **Step 2: Apply update responses**

Parse the response nickname/signature/version, store `nickname_`, save it in QSettings, call `updateOwnProfileStrip()`, and refresh the profile dialog's read-only fields. Request the new avatar only if the version changed.

- [ ] **Step 3: Handle update pushes**

On `kCmdProfileUpdatePush`, update `friend_views_` nickname and avatar version. If version changed, erase the cache entry and request the new avatar. Refresh the friend list; update an open profile dialog and private header only when the user matches.

- [ ] **Step 4: Verify nickname display boundaries**

Confirm:
- Own strip/profile dialog update.
- Private peer title does not change to own nickname.
- Room member snapshot uses the new nickname after update.
- New outgoing messages use the new nickname.
- Friend with remark still shows remark.

- [ ] **Step 5: Build and manual test**

Build and test with two accounts (demo/demo2): update nickname, signature, avatar; verify push, fallback, clear avatar, remark priority, and non-friend header avatar.

- [ ] **Step 6: Checkpoint**

Record result.

---

### Task 11: Deployment, Documentation and Full Regression

**Files:**
- Modify: `README.md`
- Modify: `client/README.md`
- Modify: `docs/superpowers/specs/2026-10-18-user-profile-design.md` (implementation result only)
- Optional: screenshot files under `client/` (do not claim a screenshot exists unless captured)

- [ ] **Step 1: Run all tests**

Server:

```bash
ssh chatroom-dev 'cd /home/xxw/桌面/0917/chatroom/build && cmake --build . -j4 && ctest --output-on-failure'
```

Client:

```powershell
cd D:\OpenCode_WorkSpace\chatroom\client\tests
.\run_client_tests.ps1
cd D:\OpenCode_WorkSpace\chatroom\client
.\build_client.ps1
```

Expected: server 4/4 or updated count; client all tests pass; build succeeds.

- [ ] **Step 2: Run integration tests**

Start a temporary plaintext 9102 server, then run:

```bash
python3 tools/profile_client.py --host 127.0.0.1 --port 9102
python3 tools/friend_client.py --host 127.0.0.1 --port 9102
```

Expected: all profile, avatar, push, friend and remark checks pass. Stop 9102 afterward.

- [ ] **Step 3: Deploy production**

Copy changed server/tool/schema files over SSH; apply `migrate_profiles.sql` once; rebuild; restart `chatroom`; verify:

```bash
systemctl is-active chatroom nginx mysql
python tools/tls_smoke.py --host 192.168.2.113 --port 9443 --ca client/certs/ca.crt
```

Expected: services active, TLS smoke passes.

- [ ] **Step 4: UI screenshots**

Capture and visually inspect:

- Own profile strip
- Friend profile with avatar
- Self edit page
- Friend list with uploaded avatars
- Chat header with real avatar
- Fallback avatar after removal

Only reference screenshots that actually exist.

- [ ] **Step 5: Update docs**

Update root/client README:

- Commands `0x00A0–0x00A3`.
- `FRIEND_LIST` new `avatar_version`.
- Profile migration.
- Avatar size/compression rules.
- Nickname vs username distinction.
- Tests and deployment result.
- IP remains future work.

- [ ] **Step 6: Final checkpoint**

Record ctest, QtTest, integration, TLS and active-service results. No Git commit is possible in this workspace.

---

## Self-Review

**Spec coverage:** The tasks cover profile fields, avatar upload/clear/fallback, username immutability, nickname/signature editing, all specified entry points, server-side relation/remark, online friend push, friend-list avatar versions, non-friend chat avatars, Unicode limits, migration and deployment. IP and expanded fields remain explicit non-goals.

**Step scan:** Each task produces a buildable/testable deliverable. UI-only steps have manual checks where a headless automated test would be low value. Avatar version and Unicode edge cases have integration/unit tests.

**Type consistency:** `UserProfile`, `FriendView::avatar_version`, `AvatarCache`, `ProfileResult`, `ProfilePushTarget`, `ProfileDialog` signals and protocol field order are named consistently across server, client and tests.

**Review Focus mapping:** The five review-focus failures are owned by Task 1 (UTF-8), Task 4 (version/update/push/unauthorized), Task 6 (avatar limits), Task 9 (non-friend avatar) and Task 10 (display boundaries).

**Proportion:** The plan names interfaces and tests rather than transcribing every UI body; implementation details for layout and defensive checks remain with the builder.
