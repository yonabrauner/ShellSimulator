#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/limits.h>
#include "LineParser.h"

typedef struct process{
        cmdLine* cmd;                         /* the parsed command line*/
        pid_t pid; 		                  /* the process id that is running the command*/
        int status;                           /* status of the process: RUNNING/SUSPENDED/TERMINATED */
        struct process *next;	                  /* next process in chain */
    } process;

#define TERMINATED  -1
#define RUNNING 1
#define SUSPENDED 0
#define HISTLEN 20

int debugMode = 0;
process* proc_list = NULL;
char* history[HISTLEN];
char* input;
int historyIndex = 0;

void initHistoryBuffer(){
    for (int i=0; i < HISTLEN; i++)
        history[i] = NULL;
}

void addToHistory(char* line){
    if (historyIndex == HISTLEN - 1 && history[historyIndex]){
        free(history[0]);
        for (int i=0; i < historyIndex; i++)
            strcpy(history[i], history[i + 1]);
        free(history[historyIndex]);
    }
    history[historyIndex] = malloc(strlen(line));
    strcpy(history[historyIndex], line);
    if (historyIndex < HISTLEN - 1)
        historyIndex++;
}

void freeHistory(){
    for (int i = 0; i <= historyIndex; i++)
        if (history[i])
            free(history[i]);
}

void printHistory(){
    for (int i = 0; i <= historyIndex; i++)
        fprintf(stdout, "  %d\t%s", i, history[i]);
}

int redoHistory(){
    if (input && strncmp(input, "!!", 2) == 0){
        free(input);
        input = strdup(history[historyIndex - 1]);
        return 1;
    }
    else {
        input[strlen(input) - 1] = '\0';
        int num = atoi(input + 1);
        if (num < HISTLEN && num >=0 && history[num]){
            free(input);
            input = strdup(history[num]);
            return 1;
        }
    }
    return 0;
}

char* statusConvert(int status){
    if (status == TERMINATED) return "Terminated";
    else if (status == SUSPENDED) return "Suspended";
    else if (status == RUNNING) return "Running";
    else return "";
}

void updateProcessStatus(process* process_list, int pid, int status){
    while (process_list && process_list->pid != pid)
        process_list = process_list->next;
    process_list->status = status; 
}

void updateProcessList(process **process_list){
    if (!process_list)
        return;
    process* currProcess = *process_list;
    while(currProcess){
        int newStatus = RUNNING, res, status;
        res = waitpid(currProcess->pid, &status, WNOHANG | WUNTRACED | WCONTINUED);
        if(WIFEXITED(status) || WIFSIGNALED(status))
            newStatus = TERMINATED;
        else if(WIFSTOPPED(status))
            newStatus = SUSPENDED;
        else if(WIFCONTINUED(status))
            newStatus = RUNNING;
        if(res)
            updateProcessStatus(currProcess,currProcess->pid,newStatus);
        currProcess = currProcess->next;
    }
}

void addProcess(process** process_list, cmdLine* cmd, pid_t pid){
    process* newProcess = (process*)malloc(sizeof(process));
    newProcess->cmd = cmd;
    newProcess->pid = pid;
    newProcess->status = RUNNING;
    if (*process_list)
        newProcess->next = *process_list;
    else
        newProcess->next = NULL;
    *process_list = newProcess;
}

void freeProcess(process* proc){
    freeCmdLines(proc->cmd);
    proc->cmd = NULL;
    proc->next = NULL;                              
    free(proc);

}

void freeProcessList(process* process_list){
    if (!process_list)
        return;
    process* nextProcess = process_list->next;
    while(nextProcess){
        freeProcess(process_list);
        process_list = nextProcess;
        nextProcess = process_list->next;
    }
    freeProcess(process_list);
}

void printProcess(int ind, process* proc){
    fprintf(stdout, "%d\t%d\t%s  \t", ind, proc->pid, statusConvert(proc->status));
    for (int i=0; i<proc->cmd->argCount; i++)
        fprintf(stdout, "%s ", proc->cmd->arguments[i]);
    fprintf(stdout, "\n");
}

void printProcessList(process** process_list){
    process* prevProcess = *process_list;
    updateProcessList(process_list);
    fprintf(stdout, "Index\tPId\tStatus\t\tCommand\n");
    int ind = 0;
    while (prevProcess && prevProcess->status == TERMINATED){       // getting rid of all TERMINATED processes at start of list
        printProcess(ind, prevProcess);
        prevProcess = prevProcess->next;
        ind++;
    }
    proc_list = prevProcess;
    if (!prevProcess)                                               // if all processes were TERMINATED - done
        return;
    printProcess(ind, prevProcess);
    ind++;
    process* currProcess = prevProcess->next;
    while (currProcess){
        printProcess(ind, currProcess);
        if (currProcess->status == TERMINATED){
            prevProcess->next = currProcess->next;
            freeProcess(currProcess);
            currProcess = prevProcess->next;
        }
        else{
            prevProcess = currProcess;
            currProcess = currProcess->next;
        }
        ind++;
    }
}

void handle_quit(cmdLine* pCmdLine){
    freeProcessList(proc_list);
    freeHistory();
    if (pCmdLine)
        freeCmdLines(pCmdLine);
    if (input)
        free(input);
}

void handle_cd(cmdLine* pCmdLine){
    if (chdir(pCmdLine->arguments[1]) < 0){
        perror("error in changing directory");
        handle_quit(pCmdLine);
        exit(EXIT_FAILURE);
    }
}

void handle_suspend(cmdLine* pCmdLine){
    if (kill(atoi(pCmdLine->arguments[1]), SIGTSTP) < 0){
        perror("error in suspend");
        handle_quit(pCmdLine);
        exit(EXIT_FAILURE);
    }
    updateProcessStatus(proc_list, atoi(pCmdLine->arguments[1]), SUSPENDED);
}

void handle_wake(cmdLine* pCmdLine){
    if (kill(atoi(pCmdLine->arguments[1]), SIGCONT) < 0){
        perror("error in wake");
        handle_quit(pCmdLine);
        exit(EXIT_FAILURE);
    }
    updateProcessStatus(proc_list, atoi(pCmdLine->arguments[1]), RUNNING);
}

void handle_kill(cmdLine* pCmdLine){
    if (kill(atoi(pCmdLine->arguments[1]), SIGINT) < 0){
        perror("error in kill");
        handle_quit(pCmdLine);
        exit(EXIT_FAILURE);
    }
    updateProcessStatus(proc_list, atoi(pCmdLine->arguments[1]), TERMINATED);
}

void handle_input_redirect(cmdLine* pCmdLine){
    int input = open(pCmdLine->inputRedirect, O_RDONLY);
    if (input < 0){
        perror("error in inputRedirect");
        handle_quit(pCmdLine);
        exit(EXIT_FAILURE);
    }
    if (dup2(input, STDIN_FILENO) < 0){
        perror("error in inputRedirect");
        handle_quit(pCmdLine);
        exit(EXIT_FAILURE);
    }
    close(input);
}

void handle_output_redirect(cmdLine* pCmdLine){
    int output = open(pCmdLine->outputRedirect, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output < 0){
        perror("error in outputRedirect");
        handle_quit(pCmdLine);
        exit(EXIT_FAILURE);
    }
    if (dup2(output, STDOUT_FILENO) < 0){
        perror("error in outputRedirect");
        handle_quit(pCmdLine);
        exit(EXIT_FAILURE);
    }
    close(output);
}

void handle_pipe(cmdLine* pCmdLine){
    int pipefd[2];                                                                  
	if (pipe(pipefd) == -1){                                                        
        perror("pipe error");
        handle_quit(pCmdLine);
        exit(EXIT_FAILURE);
    }
    int firstChildId;
    if ((firstChildId = fork()) < 0){ 					                            
        perror("error in fork\n");
        handle_quit(pCmdLine);
        exit(EXIT_FAILURE);
    }
    else if (firstChildId == 0){
        fclose(stdout);                                                             
        dup(pipefd[1]);                                                             
        close(pipefd[1]);                       
        if (pCmdLine->inputRedirect)
            handle_input_redirect(pCmdLine);
        else if (execvp(pCmdLine->arguments[0], pCmdLine->arguments) < 0){                     
            perror("error in executing in first child\n");
            freeCmdLines(pCmdLine);
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    }
    else{
        if (debugMode == 1 && firstChildId > 0)                                                         // back in parent process - debug flag check
            fprintf(stderr, "PID: %d\nExecuting command: %s\n", firstChildId, pCmdLine->arguments[0]);
        addProcess(&proc_list, pCmdLine, firstChildId);
        close(pipefd[1]);
        int secondChildId;
        if ((secondChildId = fork()) < 0){
            perror("error in fork\n");
            handle_quit(pCmdLine);
            exit(EXIT_FAILURE);
        }
        else if (secondChildId == 0){
            fclose(stdin);                
            dup(pipefd[0]);               
            close(pipefd[0]);             
            if (pCmdLine->next->outputRedirect)
                handle_output_redirect(pCmdLine->next);
            else if (execvp(pCmdLine->next->arguments[0], pCmdLine->next->arguments) < 0){                    
                perror("error in executing in second child\n");
                freeCmdLines(pCmdLine);
                exit(EXIT_FAILURE);
            }
            exit(EXIT_SUCCESS);
        }
        else{
            addProcess(&proc_list, pCmdLine->next, secondChildId);
            if (debugMode == 1)                                            
                fprintf(stderr, "PID: %d\nExecuting command: %s\n", secondChildId, pCmdLine->next->arguments[0]);
            if (pCmdLine->blocking || pCmdLine->next->blocking){                                        
                waitpid(firstChildId, NULL, 0);        
                waitpid(secondChildId, NULL, 0);
            }
        }                      
    }
}

void handle_command(cmdLine* pCmdLine){
    int pid = fork();                                               
    if (pid < 0){
        perror("error in fork");
        handle_quit(pCmdLine);
        exit(EXIT_FAILURE);
    }
    else if (pid == 0){                                                 // in child process
        if (pCmdLine->inputRedirect)
            handle_input_redirect(pCmdLine);
        if (pCmdLine->outputRedirect)
            handle_output_redirect(pCmdLine);
        else if (execvp(pCmdLine->arguments[0], pCmdLine->arguments) < 0){
            perror("error in execute");
            freeCmdLines(pCmdLine);
            _exit(EXIT_FAILURE);
        }
        _exit(EXIT_SUCCESS);
    }
    else{
        addProcess(&proc_list, pCmdLine, pid);
        if (debugMode == 1){                                     // back in parent process - debug flag check
            fprintf(stderr, "PID: %d\nExecuting command: %s\n", pid, pCmdLine->arguments[0]);
        }
        if (pCmdLine->blocking){                                 // blocking check
            waitpid(pid, NULL, 0);
        }
    }
    
}

void execute(cmdLine *pCmdLine)
{
    if (strncmp(pCmdLine->arguments[0], "quit", 4) == 0){               // quit command
        handle_quit(pCmdLine);
        exit(EXIT_SUCCESS);
    } 
    else if (strncmp(pCmdLine->arguments[0], "cd", 2) == 0){            // cd command
        handle_cd(pCmdLine);
        return;
    }
    else if (strncmp(pCmdLine->arguments[0], "suspend", 7) == 0){       // suspend command
        handle_suspend(pCmdLine);
        return;
    }
    else if (strncmp(pCmdLine->arguments[0], "wake", 4) == 0){          // wake command
        handle_wake(pCmdLine);
        return;
    }
    else if (strncmp(pCmdLine->arguments[0], "kill", 4) == 0){          // kill command
        handle_kill(pCmdLine);
        return;
    }
    else if (strncmp(pCmdLine->arguments[0], "history", 7) == 0){          // history command
        printHistory();
        return;
    }
    else if (strncmp(pCmdLine->arguments[0], "procs", 5) == 0){          // procs command
        if (proc_list)
            printProcessList(&proc_list);
        else
            perror("no process available");        
        return;
    }
    if (pCmdLine->next){                                                // piped command
        if (pCmdLine->outputRedirect){                                  // illegal output redirection check
            perror("unable to redirect output of piped command\n");
            return;
        }
        if (pCmdLine->next->inputRedirect){                             // illegal input redirection check
            perror("unable to redirect input of piped command\n");
            return;
        }
        handle_pipe(pCmdLine);
    }
    else                                                                // regular command
        handle_command(pCmdLine);
}

int main(int argc, char **argv){
    char directory[PATH_MAX];
    input = (char*)malloc(2048);
    initHistoryBuffer();
    for (int i = 0; i < argc; i++)
        if (strncmp(argv[i], "-d", 2) == 0)
            debugMode = 1;
    cmdLine *pCmdLine = NULL;
    int shouldExecute = 1;
    while (1){
        getcwd(directory, PATH_MAX);
        printf("%s ", directory);
        fgets(input, 2048, stdin);
        if (strncmp(input, "!", 1) == 0)
            shouldExecute = redoHistory();
        if (shouldExecute){
            addToHistory(input);
            pCmdLine = parseCmdLines(input);
            execute(pCmdLine);
        } else
            fprintf(stdout, "history index not found.\n");
    }
    return 0;
}