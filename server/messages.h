#ifndef MESSAGES_H
#define MESSAGES_H

#include <stddef.h>

/*
 * server/messages.*
 * - Private messaging (PM) module
 * - File storage: data/pm/{min_id}_{max_id}.txt
 * - Content encoded in Base64 to support spaces and special chars
 * - Real-time delivery via server push when recipient is in chat mode
 */

#define PM_OK              0
#define PM_ERR_SELF        1   // Cannot send to self
#define PM_ERR_NOT_FOUND   2   // User not found
#define PM_ERR_NOT_FRIEND  3   // Not friends (optional restriction)
#define PM_ERR_INTERNAL    4   // IO/memory error

// Initialize PM module (create data/pm directory if needed)
int pm_init(void);

// Send a private message (content should be Base64 encoded)
// Returns PM_OK on success, stores msg_id in out_msg_id
int pm_send(int from_user_id, const char* to_username, 
            const char* content_base64, int* out_msg_id);

// Get chat history with another user
// Returns messages in format: "msg_id:from_id:content_base64:timestamp,..."
// Latest messages first, limited by 'limit'
int pm_get_history(int user_id, const char* other_username,
                   char* out, size_t out_cap, int limit);

// Get list of conversations (users you've chatted with)
// Returns: "username:unread_count,..."
int pm_get_conversations(int user_id, char* out, size_t out_cap);

// Mark messages as read (when entering chat with someone)
int pm_mark_read(int user_id, const char* other_username);

// ============ Base64 Utilities ============

// Encode binary data to Base64 string
// Returns length of encoded string, or -1 on error
int base64_encode(const unsigned char* src, size_t src_len, 
                  char* out, size_t out_cap);

// Decode Base64 string to binary data
// Returns length of decoded data, or -1 on error
int base64_decode(const char* src, unsigned char* out, size_t out_cap);

// Helper: encode plain text to Base64 (convenience wrapper)
int base64_encode_str(const char* text, char* out, size_t out_cap);

// Helper: decode Base64 to plain text (convenience wrapper)
int base64_decode_str(const char* b64, char* out, size_t out_cap);

#endif
