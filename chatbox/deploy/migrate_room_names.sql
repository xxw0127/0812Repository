-- 房间名从「大厅 N」改为「群聊 X」。
--
-- 只改名，不动 id：历史消息、成员关系、客户端本地会话索引都按 room_id
-- 关联，改名不会影响任何已有数据。
--
-- 每条的 WHERE 都带旧名，所以：
--   * 重复执行不会报错，也不会把已经改过的名字再覆盖一遍；
--   * 手动改过名的房间不会被误伤。
--
-- 执行：mysql -u chatbox -p chatbox < deploy/migrate_room_names.sql

UPDATE rooms SET name = '群聊A' WHERE id = 1  AND name = '大厅';
UPDATE rooms SET name = '群聊B' WHERE id = 2  AND name = '大厅 2';
UPDATE rooms SET name = '群聊C' WHERE id = 3  AND name = '大厅 3';
UPDATE rooms SET name = '群聊D' WHERE id = 4  AND name = '大厅 4';
UPDATE rooms SET name = '群聊E' WHERE id = 5  AND name = '大厅 5';
UPDATE rooms SET name = '群聊F' WHERE id = 6  AND name = '大厅 6';
UPDATE rooms SET name = '群聊G' WHERE id = 7  AND name = '大厅 7';
UPDATE rooms SET name = '群聊H' WHERE id = 8  AND name = '大厅 8';
UPDATE rooms SET name = '群聊I' WHERE id = 9  AND name = '大厅 9';
UPDATE rooms SET name = '群聊J' WHERE id = 10 AND name = '大厅 10';

-- 11（技术交流）与 12（闲聊）本来就不是「大厅」，保持原样。

SELECT id, name, max_members FROM rooms ORDER BY id;
