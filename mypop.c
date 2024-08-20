#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 1024

static void handle_client(int fd);
int valid_commands(char line[]);
void update(char count[], char size[], mail_list_t mailList);

int main(int argc, char *argv[]) {

    if (argc != 2) {
        fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
        return 1;
    }

    run_server(argv[1], handle_client);

    return 0;
}
/**
 *
 * Based on RFC1939
 */
void handle_client(int fd) {
    // Variables For The Function Block
    char user[MAX_USERNAME_SIZE];
    char pass[MAX_PASSWORD_SIZE];
    mail_list_t mailList = NULL;
    char count[10], size[20];
    int state = 0; // Authorization = 1, Transaction = 2
    int res; // For res = send_string error checking

    // Greetings
    struct utsname uts;
    uname(&uts);
    res = send_string(fd, "+OK %s POP3 server ready\r\n", uts.nodename);
    if (res == -1)
        return;
    net_buffer_t buffer = nb_create(fd, MAX_LINE_LENGTH); // read buffer
    // Command Reading and Processing
    while (1) {
        // Reading
        int c = 0;
        unsigned int cnt;
        char line[MAX_LINE_LENGTH]; // line
        int response = nb_read_line(buffer, line);
        //printf("%s", line);
        if (response == -1 || response == 0) {
            if (response == -1){
                send_string(fd, "-ERR connection was terminated abruptly\r\n");
            }
            if (response == 0)
                send_string(fd, "+OK connection was terminated successfully\r\n");
            if (mailList != NULL) {
                reset_mail_list_deleted_flag(mailList);
                destroy_mail_list(mailList);
            }
            line[0] = '\0'; // clearing line
            break;
        }
        else if (response > MAX_LINE_LENGTH || strlen(line) < 6 || (line[strlen(line)-2] != '\r'
                                                                    || line[strlen(line)-1] != '\n')) {
            res = send_string(fd, "-ERR invalid request\r\n");
            if (res == -1) {
                line[0] = '\0';
                break;
            }
            continue;
        }
        char cmd[MAX_LINE_LENGTH];
        char para[MAX_LINE_LENGTH];
        strcpy(cmd, line);
        if(strchr(cmd, ' ') == NULL) {
            cmd[strlen(cmd)-2] = '\0';
            para[0] = '\0';
        }
        else {
            // Arguments||Parameters Processing
            if(strlen(cmd) >= 8) {
                int idx = 0;
                for (int i = 0; i < strlen(cmd); i++) {
                    if(cmd[i] == ' ') {
                        idx = i+1;
                        break;
                    }
                }
                cmd[idx - 1] = '\0';
                strncpy(para, line+idx, MAX_LINE_LENGTH);
                para[strlen(para) - 2] = '\0';
            }
        }
        c = valid_commands(cmd); // gathering info about the command
        if (c == 0) {
            res = send_string(fd, "-ERR invalid command\r\n");
            if (res == -1)
                break;
            continue;
        }
        cmd[0] = '\0'; //clearing command holder
        line[0] = '\0'; // clearing line
        // Processing
        if (c <= 2 && (state == 0 || state == 1)) {

            // Only in Authorization State

            if (c == 1) { // USER
                if (strlen(para) < 1) {
                    res = send_string(fd, "-ERR user requires a username parameter\r\n");
                }
                else if (is_valid_user(para, NULL) == 0) {
                    res = send_string(fd, "-ERR user doesn't exist, try again\r\n");
                }
                else {
                    res = send_string(fd, "+OK enter your password\r\n");
                    strcpy(user, para);
                    para[0] = '\0'; // flushing
                    state++; // state = 1
                }
                if (res == -1)
                    break;
                continue;
            }
            if (c == 2) { // PASS
                if (state == 0) {
                    res = send_string(fd, "-ERR no valid username provided\r\n");
                }
                else if (strlen(para) < 1) {
                    res = send_string(fd, "-ERR pass requires a password parameter\r\n");
                }
                else if (is_valid_user(user, para) == 0){
                    res = send_string(fd, "-ERR password invalid, start with username again\r\n");
                    user[0] = '\0';// flushing username
                }
                else {
                    mailList = load_user_mail(user);
                    update(count, size, mailList);
                    cnt = get_mail_count(mailList);
                    res = send_string(fd, "+OK\r\n");
                    if (res == -1) {
                        destroy_mail_list(mailList);
                        break;
                    }
                    strcpy(pass, para);
                    state++; // state = 2
                }
                if (res == -1) {
                    break;
                }
                continue;
            }
        }
        if (c == 3) { // QUIT
            if (state == 2) {
                destroy_mail_list(mailList);
            }
            send_string(fd, "+OK %s POP3 server signing off \r\n", uts.nodename);
            break;
        }
        if (state == 2 && c >= 4) {

            // Only in Transaction State

            if (c == 4) { // STAT
                update(count, size, mailList);
                res = send_string(fd, "+OK %s %s\r\n", count, size);
                if (res == -1) {
                    destroy_mail_list(mailList);
                    break;
                }
                continue;
            }
            if (c == 5) { // LIST
                update(count, size, mailList);
                if (cnt == 0)
                    res = send_string(fd, "+OK no mail in the mailbox\r\n");
                else {
                    if (strlen(para) >= 1) {
                        unsigned int num = (unsigned int)strtol(para, (char **) NULL, 10);
                        if (num == 0)
                            res = send_string(fd, "-ERR invalid argument\r\n");
                        else {
                            mail_item_t mail = get_mail_item(mailList, num - 1);
                            if (mail == NULL)
                                res = send_string(fd, "-ERR no such message\r\n");
                            else {
                                char mailSize[10] = "";
                                sprintf(mailSize, "%zu", get_mail_item_size(mail));
                                res = send_string(fd, "+OK %s %s\r\n", para, mailSize);
                            }
                        }
                    }
                    else {
                        res = send_string(fd, "+OK %s messages (%s octets)\r\n", count, size);
                        if (res == -1) {
                            destroy_mail_list(mailList);
                            break;
                        }
                        for (int i = 0; i < cnt; i++) {
                            mail_item_t mail = get_mail_item(mailList,(unsigned int) i);
                            if (mail != NULL) {
                                char item[10] = "";
                                char mailSize[10] = "";
                                sprintf(item, "%u", i+1);
                                sprintf(mailSize, "%zu", get_mail_item_size(mail));
                                res = send_string(fd, "%s %s\r\n", item, mailSize);
                                if (res == -1) {
                                    destroy_mail_list(mailList);
                                    nb_destroy(buffer);
                                    return;
                                }
                            }
                        }
                        res = send_string(fd, ".\r\n");
                    }
                }
                if (res == -1) {
                    destroy_mail_list(mailList);
                    break;
                }
                continue;
            }
            if (c == 6) { // RETR
                update(count, size, mailList);
                if (strlen(para) >= 1) {
                    unsigned int num = (unsigned int)strtol(para, (char **) NULL, 10);
                    if (num == 0)
                        res = send_string(fd, "-ERR invalid argument\r\n");
                    else {
                        mail_item_t mail = get_mail_item(mailList, num - 1);
                        if (mail == NULL)
                            res = send_string(fd, "-ERR no such message\r\n");
                        else {
                            char mailSize[10] = "";
                            sprintf(mailSize, "%zu", get_mail_item_size(mail));
                            res = send_string(fd, "+OK %s octets\r\n", mailSize);
                            if (res == -1) {
                                destroy_mail_list(mailList);
                                break;
                            }
                            char fileName[FILENAME_MAX];
                            strcpy(fileName, get_mail_item_filename(mail));
                            char data[MAX_LINE_LENGTH];
                            FILE* email = fopen(fileName, "r"); // Read-Only
                            if (email == NULL) {
                                res =send_string(fd, "-ERR message doesn't exist\r\n");
                                if (res == -1)
                                    break;
                                continue;
                            }
                            int err; // for error checking

                            err = fseek(email, 0, SEEK_SET);
                            if (err != 0) {
                                res = send_string(fd, "-ERR message is corrupted\r\n");
                                if (res == -1)
                                    break;
                                continue;
                            }

                            while (fgets(data, MAX_LINE_LENGTH, email) != NULL) {
                                res = send_string(fd, "%s", data);
                                if (res == -1) {
                                    nb_destroy(buffer);
                                    return;
                                }
                            }
                            if (ferror(email)) {
                                clearerr(email);
                                res = send_string(fd, "-ERR message is corrupted\r\n");
                                if (res == -1){
                                    break;
                                }
                            }
                            res = send_string(fd,".\r\n");
                            err = fclose(email);
                            if (err == EOF) {
                                res = send_string(fd, "-ERR message can't be closed\r\n");
                                if (res == -1)
                                    break;
                                continue;
                            }
                        }
                    }
                } else
                    res = send_string(fd,"-ERR no argument provided\r\n");
                if (res == -1) {
                    destroy_mail_list(mailList);
                    break;
                }
                continue;
            }
            if (c == 7) { // DELE
                if (strlen (para) >= 1) {
                    unsigned int num = (unsigned int)strtol(para, (char **) NULL, 10);
                    if (num == 0)
                        res = send_string(fd, "-ERR invalid argument\r\n");
                    else if (num > cnt)
                        res = send_string(fd,"-ERR no such message, only %s messages in the mailbox\r\n", count);
                    else {
                        mail_item_t mail = get_mail_item(mailList, num - 1);
                        if(mail == NULL) {
                            res = send_string(fd, "-ERR %s already deleted\r\n", para);
                        }
                        else {
                            mark_mail_item_deleted(mail);
                            res = send_string(fd,"+OK message %s deleted\r\n", para);
                        }
                    }
                } else
                    res = send_string(fd, "-ERR no argument provided\r\n");
                if (res == -1) {
                    destroy_mail_list(mailList);
                    break;
                }
                continue;
            }
            if (c == 8) { // NOOP
                res = send_string(fd, "+OK\r\n");
                if (res == -1) {
                    destroy_mail_list(mailList);
                    break;
                }
                continue;
            }
            if (c == 9) { // RSET
                reset_mail_list_deleted_flag(mailList);
                update(count, size, mailList);
                res = send_string(fd, "+OK %s's mailbox has %s messages (%s octets)\r\n", user, count, size);
                if (res == -1) {
                    destroy_mail_list(mailList);
                    break;
                }
                continue;
            }
        } else {
            res = send_string(fd, "-ERR you must be authenticated\r\n");
            if (res == -1) {
                break;
            }
            continue;
        }
    }
    nb_destroy(buffer); // flushing the buffer
}
/** valid_commands compares the command in the command line and return an
 *  an integer result according to the spec mentioned below.
 *
 * @param line: the string from the buffer
 * @return 1 if command is USER, 2 if PASS, 3 if QUIT, 4 if STAT, 5 if LIST,
 *         6 if RETR, 7 if DELE, 8 if NOOP, 9 if RSET and 0 otherwise marking
 *         the command invalid.
 */
int valid_commands(char line[]) {
    if (strcasecmp(line, "USER") == 0)
        return 1;
    else if (strcasecmp(line, "PASS") == 0)
        return 2;
    else if (strcasecmp(line, "QUIT") == 0)
        return 3;
    else if (strcasecmp(line, "STAT") == 0)
        return 4;
    else if (strcasecmp(line, "LIST") == 0)
        return 5;
    else if (strcasecmp(line, "RETR") == 0)
        return 6;
    else if (strcasecmp(line, "DELE") == 0)
        return 7;
    else if (strcasecmp(line, "NOOP") == 0)
        return 8;
    else if (strcasecmp(line, "RSET") == 0)
        return 9;
    else {
        return 0;
    }
}
/** update updates the count and size for the complete list of the mails for
 *  the user which have not been marked for deletion
 *
 * @param count; updates the mail count for the function
 * @param size ; updates the total mail size for the function
 * @param mailList ; uses the list of mail acquired for the authenticated user
 */
void update(char count[], char size[], mail_list_t mailList) {
    sprintf(count, "%u", get_mail_count(mailList));
    sprintf(size, "%zu", get_mail_list_size(mailList));
}
