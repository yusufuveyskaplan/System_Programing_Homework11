#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* See notes on why "volatile sig_atomic_t" is better*/
volatile sig_atomic_t canread = 0;

void sigio_handler(int signo) {
    canread = 1;
}

void spin_for_io() {
    while (!canread) {
        /* perform other work or busy-wait */
    }
}

int main() {
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        // Child process
        close(pipe_fd[0]);  // Close read end
        sleep(1);           // Delay before writing
        const char *msg = "Score: 20";
        write(pipe_fd[1], msg, strlen(msg));
        close(pipe_fd[1]);  // Close write end
        exit(EXIT_SUCCESS);
    } else {
        // Parent process
        close(pipe_fd[1]);  // Close write end

        // Set owner for pipe read-end to get SIGIO
        if (fcntl(pipe_fd[0], F_SETOWN, getpid()) == -1) {
            perror("fcntl F_SETOWN");
            exit(EXIT_FAILURE);
        }
        // Get current flags and add O_ASYNC, O_NONBLOCK
        int flags = fcntl(pipe_fd[0], F_GETFL);
        if (flags == -1) {
            perror("fcntl F_GETFL");
            exit(EXIT_FAILURE);
        }
        if (fcntl(pipe_fd[0], F_SETFL, flags | O_ASYNC | O_NONBLOCK) == -1) {
            perror("fcntl F_SETFL");
            exit(EXIT_FAILURE);
        }

        // Set up SIGIO handler
        struct sigaction action = {0};
        action.sa_handler = sigio_handler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        if (sigaction(SIGIO, &action, NULL) == -1) {
            perror("sigaction");
            exit(EXIT_FAILURE);
        }

        char buf[256];
        for (;;) {
            spin_for_io();    // Wait until SIGIO sets canread
            canread = 0;      // Reset flag
            ssize_t n;
            // Read all available data
            while ((n = read(pipe_fd[0], buf, sizeof(buf) - 1)) > 0) {
                buf[n] = '\0';
                printf("Parent received: %s\n", buf);
                fflush(stdout);
            }
        }
    }
    return 0;
}
