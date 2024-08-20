#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 1024

// Define current session state code
#define SESSION_INIT_STATE 0
#define CLIENT_INIT_STATE 1
#define TRX_FIRST_STATE 2
#define TRX_SECOND_STATE 3
#define TRX_THIRD_STATE 4
#define TRX_FOURTH_STATE 5

#define OK "250 OK\r\n"
#define BAD_SEQUENCE "503 Bad sequence of commands\r\n"
#define NOT_IMPLEMENTED "502 Command not implemented\r\n"
#define SYNTAX_ERROR "500 Syntax error, command unrecognized or too long\r\n"
#define SYNTAX_ERROR_PARAM "501 Syntax error in parameters or arguments\r\n"
#define SEND_STRING_ERROR "Cannot send string to client.\n"
#define UNSUP_PARAM "555 parameters not recognized or not implemented\r\n"
#define MAIL_BOX_NOT_FOUND "550 mail box not found\r\n"
#define OK_354 "354 OK Start mail input\r\n"
#define SERVICE_NOT_AVAILABLE "421 Service not available, closing channel\r\n"
#define LOCAL_ERROR "451 Requested action aborted due to local error\r\n"

static void handle_client(int fd);

int main(int argc, char *argv[]) {
  
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
    return 1;
  }
  
  run_server(argv[1], handle_client);
  
  return 0;
}

/**
 * Check if the command is invalid, unsupported or out of order and do
 * corresponding reply.
 *
 * @param fd socket file descriptor
 * @param cmd command to check
 *
 * @return non-negative value if OK, -1 otherwise  
 */
int checkCmdAndReply(int fd, char *cmd) {
  // Note that we always check if it's a in-order and valid command before
  // calling the function, so we just need to check if it's a valid supported
  // command to determine if it's out of order. We don't need to check if it's
  // NOOP and QUIT either.
  if (!strncasecmp(cmd, "HELO ", 5) || !strncasecmp(cmd, "MAIL FROM:", 10) ||
      !strncasecmp(cmd, "RCPT TO:", 8) || !strncasecmp(cmd, "DATA\r\n", 6)) {
    return send_string(fd, BAD_SEQUENCE);
  }

  // Check if it's unsupported command
  if (!strncasecmp(cmd, "EHLO ", 5) || !strncasecmp(cmd, "RSET\r\n", 6) ||
      !strncasecmp(cmd, "VRFY ", 5) || !strncasecmp(cmd, "EXPN ", 5) ||
      !strncasecmp(cmd, "HELP ", 5) || !strncasecmp(cmd, "HELP\r\n", 6)) {
    return send_string(fd, NOT_IMPLEMENTED);
  }

  // If run to here, it's an invalid command.
  return send_string(fd, SYNTAX_ERROR); 
}

// relese data object created in handle_client
void release(net_buffer_t *nb, user_list_t *userList, int tmpFD) {
  destroy_user_list(*userList);
  nb_destroy(*nb);
  if (tmpFD > 0)
    close(tmpFD);
}


/**
 * check if domain is a valid domain.
 *
 * @param domain input domain character string
 * @param len    length of domain, optional
 *
 * @return 1 if it is, 0 if it isn't
 */
int isValidDomain(char *domain) {
  int len = strlen(domain);
  if(len == 0) return 0;

  if (domain[0] == '.' || domain[0] == '-') return 0;

  for(int i = 1; i < len; i++) {
    if (domain[i]<'0' && domain[i] > '9' // Not digit
       && domain[i] < 'a' && domain[i] > 'z' 
       && domain[i] < 'A' && domain[i] > 'Z' // Not letter
       && domain[i] != '.' && domain[i] != '-')
       return 0;
   
    if (domain[i-1] == '.' && (domain[i]=='.' || domain[i] == '-'))
      return 0; 
  }

  return 1;
}

/**
 * find where mailBox starts
 *
 * @param path reverse-path or forward path, it has to be a valid path.
 *
 * @return the pointer to where mail box starts
 */
char* getMailBox(char *path) {
  
  char *p;
  p = strchr(path, '>');
  *p = 0;
  // path is in form <@domain,@domain,...,@domain:mailbox>
  if ((p = strchr(path, ':')) != NULL)
    return p + 1;

  // path is in form <mailbox>
  return path + 1;
}

/**
 * check if path is a valid path, i.e. <...>, there must be at least one
 * character between <>.
 *
 * @param path input character string
 *
 * @return 1 if it is, 0 if it isn't
 */
int isValidPath(char *path) {

  int len = strlen(path);
  if (len < 3 || strchr(path, '<') != path
  || strchr(path, '>') != path + len - 1)
    return 0;

  return 1;
  // Ignore all A-d-l  
//  char *mailBox = strchr(path, ':');
//  mailBox = mailBox == NULL ? path + 1 : mailBox + 1;
//  path[len - 1] = 0;
  
//  int ret = isValidMailBox(mailBox);
//  path[len - 1] = '>';
//  return ret;
}

void handle_client(int fd) {
  
  // TODO To be implemented

  int err;
  int state = SESSION_INIT_STATE;
  int tempFileFD = -1;
  int lineEndWithCRLF = 1; // 1 if previous line is end with <CRLF>, 
                           // 0 otherwise. This is used to help determine
                           // the end of mail data.

  char buf[MAX_LINE_LENGTH];
  char reversePath[MAX_LINE_LENGTH];
  char template[] = "template-XXXXXX";


  net_buffer_t netBuf = nb_create(fd, MAX_LINE_LENGTH);
  user_list_t userList = create_user_list();
  

  // First, send an greeting message.
  struct utsname myInfo;
  err = uname(&myInfo);
  if (err != 0) {
    err = send_string(fd, "220\r\n");
    if (err < 0) {
      fprintf(stderr, SEND_STRING_ERROR);
      release(&netBuf, &userList, tempFileFD);
      return;
    }
  } 
  else {
    err = send_string(fd, "220 %s Simple Mail Transfer Service Ready\r\n", 
                myInfo.__domainname);
    if (err < 0) {
      fprintf(stderr, SEND_STRING_ERROR);
      release(&netBuf, &userList, tempFileFD);
      return;
    }
  }


  // Interactively receive and reply
  state = CLIENT_INIT_STATE;
  while ((err = nb_read_line(netBuf, buf)) > 0) {

    // If we are expecting commant, not data, we make sure it ends with <CRLF>
    // and remove trailing space before <CRLF>
    if (state != TRX_FOURTH_STATE) {
      // Makesure the last two character is \r\n
      int len = strlen(buf);
      if (len < 2 || buf[len-1] != '\n' || buf[len-2] != '\r'){
        err = send_string(fd, SYNTAX_ERROR);
        if (err < 0) {
          fprintf(stderr, SEND_STRING_ERROR);
          release(&netBuf, &userList, tempFileFD);
          return;
        }
        continue;
      }

    // Remove trailing space before CRLF
      int tail = len - 2;
      while (tail > 0) {
        if (buf[tail - 1] != ' ') break;
        tail--;
      }
      buf[tail] = '\r';
      buf[tail + 1] = '\n';
      buf[tail + 2] = 0;
      len = tail + 2;
    }

    // Is it a NOOP?
    if ((!strncasecmp(buf, "NOOP ", 5) || !strncasecmp(buf, "NOOP\r\n",6))
       && state != TRX_FOURTH_STATE) {

      err = send_string(fd, "250 OK\r\n");
      if (err < 0) {
        fprintf(stderr, "Cannot send string to client.\n");
        release(&netBuf, &userList, tempFileFD);
        return;
      }
      continue;
    }

    // Is it a QUIT?
    if (!strncasecmp(buf, "QUIT\r\n", 6) && state != TRX_FOURTH_STATE) {
      err = send_string(fd, "221 OK\r\n");
      if (err < 0) {
        fprintf(stderr, SEND_STRING_ERROR); 
      }
      release(&netBuf, &userList, tempFileFD);
      return;
    }


    // respond according to our state and received command
    switch (state) {

      case CLIENT_INIT_STATE:
        // Expect HELO
        if (!strncasecmp(buf, "HELO ", 5)) {
 
          // Check if domain part is valid
          char domain[MAX_LINE_LENGTH];
          memset(domain, 0, sizeof(domain));
//          sscanf(buf, "HELO %s\r\n", domain);
          sscanf(buf+5, "%s\r\n", domain);

          if (isValidDomain(domain)) {
            err = send_string(fd, "250 OK %s greets %s\r\n", 
                              myInfo.__domainname, 
                              domain);
            state = TRX_FIRST_STATE;
          }
          else // it isn't a valid domain
            err = send_string(fd, SYNTAX_ERROR_PARAM);
        }
        else  // it's not a HELO command
          err = checkCmdAndReply(fd, buf);

        // Check if send_string succeeded
        if (err < 0) {
            fprintf(stderr, SEND_STRING_ERROR);
            release(&netBuf, &userList, tempFileFD);
            return;
        }
        break;


      case TRX_FIRST_STATE:
        // Expect MAIL FROM:
        if (!strncasecmp(buf, "MAIL FROM:", 10)) {
          // Read reverse path
          memset(reversePath, 0, sizeof(reversePath));
          sscanf(buf+10, "%[<@:-,.A-Za-z0-9>]%*s", reversePath);
          //Check if MAIL BOX is valid.
          if (!isValidPath(reversePath) && strcmp(reversePath, "<>")) {
            err = send_string(fd, SYNTAX_ERROR_PARAM);
          }
          else { // It's a valid path
            char *token = strchr(buf, '>');
            // Check if it has parameters after reverse path i.e. not ">\r\n"
            if (token[1] != '\r' || token[2] != '\n') 
              err = send_string(fd, UNSUP_PARAM); 
            else{
              // The MAIL command is valid, respond and change state
              err = send_string(fd, OK);
              state = TRX_SECOND_STATE;
            } 
          }
        }
        else  // It's not a MAIL command
          err = checkCmdAndReply(fd, buf);
 
        // Check if send_string succeeded
        if (err < 0) {
          fprintf(stderr, SEND_STRING_ERROR);
          release(&netBuf, &userList, tempFileFD);
          return;
        }
        break;

     
      case TRX_SECOND_STATE:
        // Expect RCPT TO: 
        // Fall through
      case TRX_THIRD_STATE:
        // Expect RCPT TO: or DATA
        if (!strncasecmp(buf, "RCPT TO:", 8)) { // It;s RCPT command

          // Check if path is valid
          char forwardPath[MAX_LINE_LENGTH];
          memset(forwardPath, 0, sizeof(forwardPath));
          sscanf(buf+8, "%[<@:-,.A-Za-z0-9>]%*s", forwardPath);
          if (isValidPath(forwardPath)) {

            // Check if it has parameters after forward path, i.e. not">\r\n"
            char *token = strchr(buf, '>');
            if (token[1] != '\r' || token[2] != '\n'){
              err = send_string(fd, UNSUP_PARAM);
            }
            else { // no parameters

              char *mailBox = getMailBox(forwardPath);
              if (is_valid_user(mailBox, NULL)) {
                // user is valid, add it into list, respond and change state
                add_user_to_list(&userList, mailBox);
                err = send_string(fd, OK);
                state = TRX_THIRD_STATE;
              }
              else { 
                // user is not valid
                err = send_string(fd, MAIL_BOX_NOT_FOUND); 
              }
            }
          }
          else {// it's not a valid path
            err = send_string(fd, SYNTAX_ERROR_PARAM);
          }
        }
  	else if (!strncasecmp(buf, "DATA\r\n", 6) && state == TRX_THIRD_STATE){ 
               // It's a DATA command and We are expecting it
               strcpy(template, "template-XXXXXX");
               tempFileFD = mkstemp(template);
               if (tempFileFD < 0){
                   perror("mkstemp");
                   send_string(fd, LOCAL_ERROR); 
                   return;
               }

               err = send_string(fd, OK_354);
               state = TRX_FOURTH_STATE;
               lineEndWithCRLF = 1;
             }
             else { // it's not a command we expect
               err = checkCmdAndReply(fd, buf);
             } 
             
        // Check if send_string succeeded.
        if (err < 0) {
          fprintf(stderr, SEND_STRING_ERROR);
          release(&netBuf, &userList, tempFileFD);
          return;
        }
        break;


 
      case TRX_FOURTH_STATE:
        // Expect mail data
        err = 0;
        // is it the end ?
        if (lineEndWithCRLF && !strncasecmp(buf, ".\r\n", 3)) {
          save_user_mail(template, userList);
          destroy_user_list(userList);
          userList = create_user_list();

          // clear temp file
          unlink(template);
          close(tempFileFD);

          state = TRX_FIRST_STATE;
          err = send_string(fd, OK); 
        }
        else {
          // remove first character if it's a '.'
          char *whatToWrite = buf[0] == '.'? buf+1: buf;
          if (write(tempFileFD, (void *)whatToWrite, strlen(whatToWrite)) < 0){
            perror("write");
            send_string(fd, LOCAL_ERROR); 
            release(&netBuf, &userList, tempFileFD);
            return;
          }
          else {
            int len = strlen(buf);
            lineEndWithCRLF = buf[len-2]=='\r' && buf[len-1]=='\n' ? 1 : 0;
          }
        }
       
        // Check if send_string succeeded
        if (err < 0) {
          fprintf(stderr, SEND_STRING_ERROR);
          release(&netBuf, &userList, tempFileFD);
          return;
        }

        break;

      default:
        fprintf(stderr, "This is not possible\n");
        release(&netBuf, &userList, tempFileFD);
        return;
    }
  }

  // If goes here means nb_read_line has error
  fprintf(stderr, "Connection terminate abruptly\n");
  release(&netBuf, &userList, tempFileFD);
  return;
}
