USE chatroom;

DROP PROCEDURE IF EXISTS migrate_profiles;

DELIMITER //

CREATE PROCEDURE migrate_profiles()
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'users'
          AND COLUMN_NAME = 'signature'
    ) THEN
        ALTER TABLE users
            ADD COLUMN signature VARCHAR(128) NOT NULL DEFAULT '' AFTER nickname;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'users'
          AND COLUMN_NAME = 'avatar'
    ) THEN
        ALTER TABLE users
            ADD COLUMN avatar MEDIUMBLOB NULL AFTER signature;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'users'
          AND COLUMN_NAME = 'avatar_version'
    ) THEN
        ALTER TABLE users
            ADD COLUMN avatar_version INT UNSIGNED NOT NULL DEFAULT 0 AFTER avatar;
    END IF;
END//

DELIMITER ;

CALL migrate_profiles();
DROP PROCEDURE migrate_profiles;
