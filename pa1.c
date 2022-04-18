/**********************************************************************
 * Copyright (c) 2021
 *  Sang-Hoon Kim <sanghoonkim@ajou.ac.kr>
 *  Byeongsu Kang <raonsol@kakao.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTIABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>

#include <string.h>

#include "types.h"
#include "list_head.h"
#include "parser.h"


/***********************************************************************
 * struct list_head history
 *
 * DESCRIPTION
 *   Use this list_head to store unlimited command history.
 */
LIST_HEAD(history);
struct hist {
    char *cmd;
    struct list_head list;
};

#include <sys/wait.h>
static int __process_command(char * command);

static int exec_command(int nr_tokens, char *tokens[], int *pipe) {
    int result = -1, pid;

    // for (int i = 0; i < nr_tokens; i++) 
    //     fprintf(stdout, "tok %d: %s\n", i, tokens[i]);
    
    // child
    if ((pid = fork()) == 0) {
        if (pipe) {
            // TODO: connect STDERR to pipe
            // fprintf(stdout, "Connecting pipe...\n");
            // close(pipe[0]);
            dup2(pipe[1], STDOUT_FILENO);
            close(pipe[1]);
        }
        
        if (strcmp(tokens[0], "history") == 0) {
            struct hist *cur;
            int idx = 0;
            list_for_each_entry(cur, &history, list) {
                fprintf(stderr, "%2d: %s", idx++, cur->cmd);
                for(int i=0;i<10;i++)
                    if(cur->cmd[i]=='\0') printf("NULL on idx %d\n", i);
                // fprintf(stderr, "char: %c %c\n", cur->cmd[2], cur->cmd[3]);
                // printf("----------------------------------\n");
                // FIXME: ! 명령 중첩될 때 숫자가 사라짐 -> fork 후 NULL이 삽입되는 것으로 추정
                // break line if string does not have it
                if (cur->cmd[strlen(cur->cmd) - 1] != '\n')
                    fprintf(stderr, "\n");
            }

            exit(1);  // success

        } else if (strcmp(tokens[0], "!") == 0) {
            if (nr_tokens == 1)
                fprintf(stderr, "! command must have two arguments\n");
            else {
                int target_idx = atoi(tokens[1]);
                int cnt = 0, hist_size = 0;
                struct hist *cur;
                char *arg = NULL;

                // fprintf(stdout, "target_idx: %d\n", target_idx);
                list_for_each_entry(cur, &history, list)
                    hist_size++;
                
                list_for_each_entry(cur, &history, list) {
                    if (cnt == target_idx) {
                        arg = cur->cmd;
                        break;
                    }
                    cnt++;
                }
                // printf("Arg: %s", arg);

                // Create string to compare to prevent infinite loop
                char recur_cmd[MAX_COMMAND_LEN];
                sprintf(recur_cmd, "! %d\n", target_idx);

                if (cnt >= hist_size - 1)
                    fprintf(stderr, "Index is exceeded than history size!\n");
                else if (!strcmp(arg, recur_cmd))
                    fprintf(stderr, "Cannot call itself!\n");
                else
                    result = __process_command(arg);
            }
            exit(result);

        } else {  // normal command
            if (execvp(tokens[0], tokens) == -1) {
                fprintf(stderr, "Unable to execute %s\n", tokens[0]);
                // fprintf(stdout, "%s\n", strerror(errno));
                exit(-1);
            }
        }

    } else if (pid == -1) {
        fprintf(stderr, "Fork Failed\n");
        result = -1;

    } else {  // parent
        if (pipe) {
            close(pipe[1]);
            dup2(pipe[0], STDIN_FILENO);
            close(pipe[0]);
        }
        int status;
        result = wait(&status);
        // fprintf(stdout, "PID %d finished\n", pid);
        if (status == -1) result = status;  // execute fail
    }
    return result;
}

static int find_pipe(int nr_tokens, char *tokens[]){
    int pipe_idx = -1;
    for (int i = 0; i < nr_tokens; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            pipe_idx = i;
            break;
        }
    }
    return pipe_idx;
}

/***********************************************************************
 * run_command()
 *
 * DESCRIPTION
 *   Implement the specified shell features here using the parsed
 *   command tokens.
 *
 * RETURN VALUE
 *   Return 1 on successful command execution
 *   Return 0 when user inputs "exit"
 *   Return <0 on error
 */

static int run_command(int nr_tokens, char *tokens[]) {
    int result = -1;  // default result is failed
    if (strcmp(tokens[0], "exit") == 0) return 0;

    // check if pipe command exists
    int nr_tokens_before = find_pipe(nr_tokens, tokens);

    if (strcmp(tokens[0], "cd") == 0) {
        if (nr_tokens == 1 || strcmp(tokens[1], "~") == 0)  // $cd or $cd ~
            result = chdir(getenv("HOME"));
        else
            result = chdir(tokens[1]);

    } else if (nr_tokens_before >= 0) {     // if pipe    
        // fprintf(stdout, "Pipe command found\n");
        if (nr_tokens_before == 0) {
            fprintf(stdout, "Parse error near %s\n", tokens[nr_tokens_before]);
            return -1;
        }

        int fd[2];  // 0==read, 1==write
        if (pipe(fd) < 0) {
		    fprintf(stderr, "Error on creating pipe\n");
            return -1;
        }

        // split tokens
        int nr_tokens_after = nr_tokens - nr_tokens_before - 1;
        char *tok_before[nr_tokens_before + 1], *tok_after[nr_tokens_after + 1];
        memcpy(tok_before, tokens, sizeof(char *) * nr_tokens_before);
        memcpy(tok_after, tokens + nr_tokens_before + 1,
                sizeof(char *) * nr_tokens_after);
        // add NULL at the end to use execvp()
        tok_before[nr_tokens_before] = NULL;
        tok_after[nr_tokens_after] = NULL;

        // for (int i = 0; i < nr_tokens_before; i++) 
        //     fprintf(stdout, "Front tok %d: %s\n", i, tok_before[i]);
        // for (int i = 0; i < nr_tokens_after; i++) 
        //     fprintf(stdout, "Back tok %d: %s\n", i, tok_after[i]);

        result = exec_command(nr_tokens_before, tok_before, fd);
        if (result > 1) result = exec_command(nr_tokens_after, tok_after, fd);

    } else {  // if no pipe
        result = exec_command(nr_tokens, tokens, NULL);
    }
    // fprintf(stdout, "run result: %d\n", result);
    
    if (result >= 0)  // success
        return 1;
    else
        return result;
}

/***********************************************************************
 * append_history()
 *
 * DESCRIPTION
 *   Append @command into the history. The appended command can be later
 *   recalled with "!" built-in command
 */
static void append_history(char *const command) {
    struct hist *node = (struct hist *)malloc(sizeof(struct hist));
    node->cmd = (char *)malloc(sizeof(char) * (strlen(command)));
    strcpy(node->cmd, command);
    //  break line if string does not have it
    // if (command[strlen(command)] != '\n') strcat(node->cmd, "\n");
    // printf("%s len: %lu\n", command, strlen(command));

    if (list_empty(&history)) INIT_LIST_HEAD(&history);
    list_add_tail(&node->list, &history);
}


/***********************************************************************
 * initialize()
 *
 * DESCRIPTION
 *   Call-back function for your own initialization code. It is OK to
 *   leave blank if you don't need any initialization.
 *
 * RETURN VALUE
 *   Return 0 on successful initialization.
 *   Return other value on error, which leads the program to exit.
 */
static int initialize(int argc, char * const argv[])
{
	return 0;
}


/***********************************************************************
 * finalize()
 *
 * DESCRIPTION
 *   Callback function for finalizing your code. Like @initialize(),
 *   you may leave this function blank.
 */
static void finalize(int argc, char * const argv[])
{
    //dump history
    struct hist *out;
    while ((out = list_first_entry_or_null(&history, struct hist, list))) {
        if (list_is_last(&out->list, &history)) {
            list_del_init(&out->list);
        } else {
            list_del(&out->list);
        }
        free(out->cmd);
        free(out);
    }
    // fprintf(stdout, "History dumped\n");
}

/*====================================================================*/
/*          ****** DO NOT MODIFY ANYTHING BELOW THIS LINE ******      */
/*          ****** BUT YOU MAY CALL SOME IF YOU WANT TO.. ******      */
static int __process_command(char * command)
{
	char *tokens[MAX_NR_TOKENS] = { NULL };
	int nr_tokens = 0;

	if (parse_command(command, &nr_tokens, tokens) == 0)
		return 1;

	return run_command(nr_tokens, tokens);
}

static bool __verbose = true;
static const char *__color_start = "[0;31;40m";
static const char *__color_end = "[0m";

static void __print_prompt(void)
{
	char *prompt = "$";
	if (!__verbose) return;

	fprintf(stderr, "%s%s%s ", __color_start, prompt, __color_end);
}

/***********************************************************************
 * main() of this program.
 */
int main(int argc, char * const argv[])
{
	char command[MAX_COMMAND_LEN] = { '\0' };
	int ret = 0;
	int opt;

	while ((opt = getopt(argc, argv, "qm")) != -1) {
		switch (opt) {
		case 'q':
			__verbose = false;
			break;
		case 'm':
			__color_start = __color_end = "\0";
			break;
		}
	}

	if ((ret = initialize(argc, argv))) return EXIT_FAILURE;

	/**
	 * Make stdin unbuffered to prevent ghost (buffered) inputs during
	 * abnormal exit after fork()
	 */
	setvbuf(stdin, NULL, _IONBF, 0);

	while (true) {
		__print_prompt();

		if (!fgets(command, sizeof(command), stdin)) break;

		append_history(command);
		ret = __process_command(command);

		if (!ret) break;
	}

	finalize(argc, argv);

	return EXIT_SUCCESS;
}
