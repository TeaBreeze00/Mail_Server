#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>

#define MAX_BUFFER_SIZE 1024

// Define current session state codes
#define INITIAL_STATE 0
#define GREETING_STATE 1
#define MAIL_STATE 2
#define RECIPIENT_STATE 3
#define DATA_STATE 4

#define RESPONSE_OK "250 OK\r\n"
#define RESPONSE_BAD_SEQUENCE "503 Bad sequence of commands\r\n"
#define RESPONSE_NOT_IMPLEMENTED "502 Command not implemented\r\n"
#define RESPONSE_SYNTAX_ERROR "500 Syntax error, command unrecognized or too long\r\n"
#define RESPONSE_SYNTAX_ERROR_PARAM "501 Syntax error in parameters or arguments\r\n"
#define RESPONSE_SEND_ERROR "Cannot send string to client.\n"
#define RESPONSE_UNSUPPORTED_PARAM "555 parameters not recognized or not implemented\r\n"
#define RESPONSE_MAILBOX_NOT_FOUND "550 mail box not found\r\n"
#define RESPONSE_START_MAIL "354 OK Start mail input\r\n"
#define RESPONSE_SERVICE_UNAVAILABLE "421 Service not available, closing channel\r\n"
#define RESPONSE_LOCAL_ERROR "451 Requested action aborted due to local error\r\n"

static void process_client(int client_fd);

int main(int argc, char *argv[]) {
  
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    return 1;
  }
  
  run_server(argv[1], process_client);
  
  return 0;
}

/**
 * Validates and replies to commands based on their correctness and support.
 *
 * @param client_fd socket file descriptor
 * @param command command string to validate
 *
 * @return non-negative value if command is valid, -1 otherwise  
 */
int validateCommandAndRespond(int client_fd, char *command) {
  if (!strncasecmp(command, "HELO ", 5) || !strncasecmp(command, "MAIL FROM:", 10) ||
      !strncasecmp(command, "RCPT TO:", 8) || !strncasecmp(command, "DATA\r\n", 6)) {
    return send_string(client_fd, RESPONSE_BAD_SEQUENCE);
  }

  if (!strncasecmp(command, "EHLO ", 5) || !strncasecmp(command, "RSET\r\n", 6) ||
      !strncasecmp(command, "VRFY ", 5) || !strncasecmp(command, "EXPN ", 5) ||
      !strncasecmp(command, "HELP ", 5) || !strncasecmp(command, "HELP\r\n", 6)) {
    return send_string(client_fd, RESPONSE_NOT_IMPLEMENTED);
  }

  return send_string(client_fd, RESPONSE_SYNTAX_ERROR); 
}

// Releases resources created in process_client
void cleanup_resources(net_buffer_t *buffer, user_list_t *users, int temp_fd) {
  destroy_user_list(*users);
  nb_destroy(*buffer);
  if (temp_fd > 0) {
    close(temp_fd);
  }
}

/**
 * Checks if a domain name is valid.
 *
 * @param domain input domain string
 * @param length optional length of the domain
 *
 * @return 1 if valid, 0 otherwise
 */
int is_valid_domain(char *domain) {
  int length = strlen(domain);
  if(length == 0) return 0;

  if (domain[0] == '.' || domain[0] == '-') return 0;

  for(int i = 1; i < length; i++) {
    if (domain[i]<'0' || domain[i]>'9' // Not digit
       && domain[i]<'a' || domain[i]>'z' 
       && domain[i]<'A' || domain[i]>'Z' // Not letter
       && domain[i] != '.' && domain[i] != '-')
      return 0;
   
    if (domain[i-1] == '.' && (domain[i] == '.' || domain[i] == '-'))
      return 0; 
  }

  return 1;
}

/**
 * Extracts the mailbox portion from a path.
 *
 * @param path reverse path or forward path
 *
 * @return pointer to the mailbox portion
 */
char* extract_mailbox(char *path) {
  char *p;
  p = strchr(path, '>');
  *p = 0;
  if ((p = strchr(path, ':')) != NULL)
    return p + 1;

  return path + 1;
}

/**
 * Checks if a path is valid.
 *
 * @param path input string to check
 *
 * @return 1 if valid, 0 otherwise
 */
int is_valid_path(char *path) {
  int length = strlen(path);
  if (length < 3 || strchr(path, '<') != path
  || strchr(path, '>') != path + length - 1)
    return 0;

  return 1;
}

void process_client(int client_fd) {
  
  int status;
  int session_state = INITIAL_STATE;
  int temp_file_fd = -1;
  int end_with_crlf = 1;

  char buffer[MAX_BUFFER_SIZE];
  char reverse_path[MAX_BUFFER_SIZE];
  char temp_file_template[] = "template-XXXXXX";

  net_buffer_t net_buffer = nb_create(client_fd, MAX_BUFFER_SIZE);
  user_list_t user_list = create_user_list();
  
  struct utsname sys_info;
  status = uname(&sys_info);
  if (status != 0) {
    status = send_string(client_fd, "220\r\n");
    if (status < 0) {
      fprintf(stderr, RESPONSE_SEND_ERROR);
      cleanup_resources(&net_buffer, &user_list, temp_file_fd);
      return;
    }
  } else {
    status = send_string(client_fd, "220 %s Simple Mail Transfer Service Ready\r\n", 
                sys_info.__domainname);
    if (status < 0) {
      fprintf(stderr, RESPONSE_SEND_ERROR);
      cleanup_resources(&net_buffer, &user_list, temp_file_fd);
      return;
    }
  }

  session_state = GREETING_STATE;
  while ((status = nb_read_line(net_buffer, buffer)) > 0) {

    if (session_state != DATA_STATE) {
      int length = strlen(buffer);
      if (length < 2 || buffer[length-1] != '\n' || buffer[length-2] != '\r'){
        status = send_string(client_fd, RESPONSE_SYNTAX_ERROR);
        if (status < 0) {
          fprintf(stderr, RESPONSE_SEND_ERROR);
          cleanup_resources(&net_buffer, &user_list, temp_file_fd);
          return;
        }
        continue;
      }

      int tail = length - 2;
      while (tail > 0) {
        if (buffer[tail - 1] != ' ') break;
        tail--;
      }
      buffer[tail] = '\r';
      buffer[tail + 1] = '\n';
      buffer[tail + 2] = 0;
      length = tail + 2;
    }

    if ((!strncasecmp(buffer, "NOOP ", 5) || !strncasecmp(buffer, "NOOP\r\n", 6))
       && session_state != DATA_STATE) {

      status = send_string(client_fd, "250 OK\r\n");
      if (status < 0) {
        fprintf(stderr, "Cannot send string to client.\n");
        cleanup_resources(&net_buffer, &user_list, temp_file_fd);
        return;
      }
      continue;
    }

    if (!strncasecmp(buffer, "QUIT\r\n", 6) && session_state != DATA_STATE) {
      status = send_string(client_fd, "221 OK\r\n");
      if (status < 0) {
        fprintf(stderr, RESPONSE_SEND_ERROR); 
      }
      cleanup_resources(&net_buffer, &user_list, temp_file_fd);
      return;
    }

    switch (session_state) {

      case GREETING_STATE:
        if (!strncasecmp(buffer, "HELO ", 5)) {
          char domain[MAX_BUFFER_SIZE];
          memset(domain, 0, sizeof(domain));
          sscanf(buffer + 5, "%s\r\n", domain);

          if (is_valid_domain(domain)) {
            status = send_string(client_fd, "250 OK %s greets %s\r\n", 
                              sys_info.__domainname, 
                              domain);
            session_state = MAIL_STATE;
          } else {
            status = send_string(client_fd, RESPONSE_SYNTAX_ERROR_PARAM);
          }
        } else {
          status = validateCommandAndRespond(client_fd, buffer);
        }

        if (status < 0) {
          fprintf(stderr, RESPONSE_SEND_ERROR);
          cleanup_resources(&net_buffer, &user_list, temp_file_fd);
          return;
        }
        break;

      case MAIL_STATE:
        if (!strncasecmp(buffer, "MAIL FROM:", 10)) {
          memset(reverse_path, 0, sizeof(reverse_path));
          sscanf(buffer + 10, "%[<@:-,.A-Za-z0-9>]%*s", reverse_path);
          if (!is_valid_path(reverse_path) && strcmp(reverse_path, "<>")) {
            status = send_string(client_fd, RESPONSE_SYNTAX_ERROR_PARAM);
          } else {
            char *token = strchr(buffer, '>');
            if (token[1] != '\r' || token[2] != '\n') {
              status = send_string(client_fd, RESPONSE_UNSUPPORTED_PARAM);
            } else {
              status = send_string(client_fd, RESPONSE_OK);
              session_state = RECIPIENT_STATE;
            } 
          }
        } else {
          status = validateCommandAndRespond(client_fd, buffer);
        }

        if (status < 0) {
          fprintf(stderr, RESPONSE_SEND_ERROR);
          cleanup_resources(&net_buffer, &user_list, temp_file_fd);
          return;
        }
        break;

      case RECIPIENT_STATE:
      case DATA_STATE:
        if (!strncasecmp(buffer, "RCPT TO:", 8)) {
          char recipient_path[MAX_BUFFER_SIZE];
          memset(recipient_path, 0, sizeof(recipient_path));
          sscanf(buffer + 8, "%[<@:-,.A-Za-z0-9>]%*s", recipient_path);
          if (is_valid_path(recipient_path)) {
            char *token = strchr(buffer, '>');
            if (token[1] != '\r' || token[2] != '\n') {
              status = send_string(client_fd, RESPONSE_UNSUPPORTED_PARAM);
            } else {
              char *mailbox = extract_mailbox(recipient_path);
              if (is_valid_user(mailbox, NULL)) {
                add_user_to_list(&user_list, mailbox);
                status = send_string(client_fd, RESPONSE_OK);
                session_state = DATA_STATE;
              } else {
                status = send_string(client_fd, RESPONSE_MAILBOX_NOT_FOUND);
              }
            }
          } else {
            status = send_string(client_fd, RESPONSE_SYNTAX_ERROR_PARAM);
          }
        } else if (!strncasecmp(buffer, "DATA\r\n", 6) && session_state == DATA_STATE) {
          strcpy(temp_file_template, "template-XXXXXX");
          temp_file_fd = mkstemp(temp_file_template);
          if (temp_file_fd < 0) {
            perror("mkstemp");
            send_string(client_fd, RESPONSE_LOCAL_ERROR); 
            return;
          }

          status = send_string(client_fd, RESPONSE_START_MAIL);
          session_state = DATA_STATE;
          end_with_crlf = 1;
        } else {
          status = validateCommandAndRespond(client_fd, buffer);
        }
        
        if (status < 0) {
          fprintf(stderr, RESPONSE_SEND_ERROR);
          cleanup_resources(&net_buffer, &user_list, temp_file_fd);
          return;
        }
        break;

      case DATA_STATE:
        status = 0;
        if (end_with_crlf && !strncasecmp(buffer, ".\r\n", 3)) {
          save_user_mail(temp_file_template, user_list);
          destroy_user_list(user_list);
          user_list = create_user_list();
          unlink(temp_file_template);
          close(temp_file_fd);

          session_state = MAIL_STATE;
          status = send_string(client_fd, RESPONSE_OK); 
        } else {
          char *data_to_write = buffer[0] == '.' ? buffer + 1 : buffer;
          if (write(temp_file_fd, data_to_write, strlen(data_to_write)) < 0) {
            perror("write");
            send_string(client_fd, RESPONSE_LOCAL_ERROR); 
            cleanup_resources(&net_buffer, &user_list, temp_file_fd);
            return;
          } else {
            int length = strlen(buffer);
            end_with_crlf = buffer[length - 2] == '\r' && buffer[length - 1] == '\n' ? 1 : 0;
          }
        }
        
        if (status < 0) {
          fprintf(stderr, RESPONSE_SEND_ERROR);
          cleanup_resources(&net_buffer, &user_list, temp_file_fd);
          return;
        }
        break;

      default:
        fprintf(stderr, "Unexpected state\n");
        cleanup_resources(&net_buffer, &user_list, temp_file_fd);
        return;
    }
  }

  fprintf(stderr, "Connection terminated unexpectedly\n");
  cleanup_resources(&net_buffer, &user_list, temp_file_fd);
  return;
}

