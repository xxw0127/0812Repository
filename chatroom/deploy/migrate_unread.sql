USE chatroom;

INSERT INTO private_read_state (user_id, peer_id, last_read_message_id)
SELECT receiver_id, sender_id, MAX(id)
FROM private_messages
GROUP BY receiver_id, sender_id
ON DUPLICATE KEY UPDATE
    last_read_message_id = GREATEST(last_read_message_id, VALUES(last_read_message_id));
