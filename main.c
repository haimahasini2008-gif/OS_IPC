#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static void show_menu(void)
{
    printf("\n====================================\n");
    printf("       IPC CHAT APPLICATION\n");
    printf("====================================\n");
    printf("1. Start Chat as User A\n");
    printf("2. Start Chat as User B\n");
    printf("3. Exit\n");
    printf("====================================\n");
    printf("Enter your choice: ");
    fflush(stdout);
}

int main(void)
{
    char choice[32];

    while (1)
    {
        show_menu();

        if (fgets(choice, sizeof(choice), stdin) == NULL)
        {
            printf("\nExiting application.\n");
            break;
        }

        choice[strcspn(choice, "\n")] = '\0';

        if (strcmp(choice, "3") == 0)
        {
            printf("Thank you for using IPC Chat!\n");
            break;
        }

        const char *user;

        if (strcmp(choice, "1") == 0)
        {
            user = "A";
        }
        else if (strcmp(choice, "2") == 0)
        {
            user = "B";
        }
        else
        {
            printf("Invalid choice. Please enter 1, 2, or 3.\n");
            continue;
        }

        pid_t pid = fork();

        if (pid < 0)
        {
            perror("fork");
            continue;
        }

        if (pid == 0)
        {
            execl("./chat", "chat", user, (char *)NULL);
            perror("execl");
            _exit(EXIT_FAILURE);
        }

        int status;

        while (waitpid(pid, &status, 0) == -1)
        {
            perror("waitpid");
            break;
        }
    }

    return 0;
}
