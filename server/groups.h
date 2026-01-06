#ifndef GROUPS_H
#define GROUPS_H

#include <stddef.h>

#define GROUP_OK 0
#define GROUP_ERR_NOT_FOUND 1
#define GROUP_ERR_EXISTS 2
#define GROUP_ERR_PERMISSION 3
#define GROUP_ERR_INTERNAL 4
#define GROUP_ERR_SELF 5

int groups_create(int owner_user_id, const char *group_name, int *out_group_id);
int groups_list(int user_id, char *out, size_t cap);
int groups_add_member(int owner_user_id, int group_id, const char *username);
int groups_list_members(int user_id, int group_id, char *out, size_t cap);
int groups_remove_member(int owner_user_id, int group_id, const char *username);
int groups_leave(int user_id, int group_id);


#endif
