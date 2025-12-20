#ifndef FRIENDS_H
#define FRIENDS_H

#define FRIEND_OK 0
#define FRIEND_ERR_SELF 1
#define FRIEND_ERR_NOT_FOUND 2
#define FRIEND_ERR_EXISTS 3
#define FRIEND_ERR_INTERNAL 4

int friends_send_invite(int from_user_id, const char *to_username);

#endif
