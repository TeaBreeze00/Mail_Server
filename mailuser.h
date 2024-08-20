/* mailuser.h
 * Handles users, authentication and mail data
 * Author  : Jonatan Schroeder
 * Modified: Nov 5, 2017
 */

#ifndef _MAILUSER_H_
#define _MAILUSER_H_

#include <stdio.h>

#define MAX_USERNAME_SIZE 255
#define MAX_PASSWORD_SIZE 255

typedef struct user_list *user_list_t;
typedef struct mail_item *mail_item_t;
typedef struct mail_list *mail_list_t;

int is_valid_user(const char *username, const char *password);

user_list_t create_user_list(void);
void add_user_to_list(user_list_t *list, const char *username);
void destroy_user_list(user_list_t list);

void save_user_mail(const char *basefile, user_list_t users);
mail_list_t load_user_mail(const char *username);

void destroy_mail_list(mail_list_t list);
unsigned int get_mail_count(mail_list_t list);
mail_item_t get_mail_item(mail_list_t list, unsigned int pos);
size_t get_mail_list_size(mail_list_t list);
unsigned int reset_mail_list_deleted_flag(mail_list_t list);

size_t get_mail_item_size(mail_item_t item);
const char *get_mail_item_filename(mail_item_t item);
void mark_mail_item_deleted(mail_item_t item);

#endif
