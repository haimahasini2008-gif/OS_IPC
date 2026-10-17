#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>

#define FIFO_A_TO_B "/tmp/chat_a_to_b"
#define FIFO_B_TO_A "/tmp/chat_b_to_a"

#define BUFFER_SIZE 512

static int read_fd = -1;
static int write_fd = -1;

static volatile sig_atomic_t running = 1;
static pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

static int write_all(int fd, const char *data, size_t length)
{
    size_t sent = 0;

    while (sent < length)
    {
        ssize_t n = write(fd, data + sent, length - sent);

        if (n > 0)
        {
            sent += (size_t)n;
        }
        else if (n == -1 && errno == EINTR)
        {
            continue;
        }
        else
        {
            return -1;
        }
    }

    return 0;
}

/*
 * Read one newline-terminated message.
 * poll() lets the receiver periodically check whether
 * the application is shutting down.
 */
static int read_message(char *buffer, size_t capacity)
{
    size_t used = 0;

    while (running && used < capacity - 1)
    {
        struct pollfd pfd;
        pfd.fd = read_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int result = poll(&pfd, 1, 200);

        if (result == -1)
        {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (result == 0)
            continue;

        if (pfd.revents & POLLNVAL)
            return -1;

        if (pfd.revents & (POLLIN | POLLHUP | POLLERR))
        {
            char ch;
            ssize_t n = read(read_fd, &ch, 1);

            if (n == 1)
            {
                buffer[used++] = ch;

                if (ch == '\n')
                    break;
            }
            else if (n == -1 && errno == EINTR)
            {
                continue;
            }
            else if (n == 0)
            {
                continue;
            }
            else
            {
                return -1;
            }
        }
    }

    buffer[used] = '\0';

    return used > 0 ? (int)used : 0;
}

static void *receive_messages(void *arg)
{
    char buffer[BUFFER_SIZE];
    (void)arg;

    while (running)
    {
        int n = read_message(buffer, sizeof(buffer));

        if (!running)
            break;

        if (n < 0)
        {
            pthread_mutex_lock(&output_mutex);
            perror("Receiving message");
            pthread_mutex_unlock(&output_mutex);

            running = 0;
            break;
        }

        if (n == 0)
            continue;

        if (strcmp(buffer, "/quit\n") == 0)
        {
            pthread_mutex_lock(&output_mutex);
            printf("\nPartner disconnected.\n");
            fflush(stdout);
            pthread_mutex_unlock(&output_mutex);

            running = 0;
            break;
        }

        pthread_mutex_lock(&output_mutex);

        printf("\nPartner: %s", buffer);
        if (buffer[n - 1] != '\n')
            printf("\n");

        if (running)
            printf("You: ");

        fflush(stdout);

        pthread_mutex_unlock(&output_mutex);
    }

    return NULL;
}

static void cleanup(void)
{
    running = 0;

    if (read_fd != -1)
    {
        close(read_fd);
        read_fd = -1;
    }

    if (write_fd != -1)
    {
        close(write_fd);
        write_fd = -1;
    }

    pthread_mutex_destroy(&output_mutex);
}

int main(int argc, char *argv[])
{
    pthread_t receiver;
    char message[BUFFER_SIZE];

    const char *my_fifo;
    const char *partner_fifo;

    int receiver_started = 0;

    if (argc != 2 ||
        (strcmp(argv[1], "A") != 0 &&
         strcmp(argv[1], "B") != 0))
    {
        fprintf(stderr, "Usage: %s A or %s B\n", argv[0], argv[0]);
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1 ||
        sigaction(SIGTERM, &sa, NULL) == -1)
    {
        perror("sigaction");
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    if (strcmp(argv[1], "A") == 0)
    {
        my_fifo = FIFO_B_TO_A;
        partner_fifo = FIFO_A_TO_B;
    }
    else
    {
        my_fifo = FIFO_A_TO_B;
        partner_fifo = FIFO_B_TO_A;
    }

    if (mkfifo(FIFO_A_TO_B, 0600) == -1 && errno != EEXIST)
    {
        perror("Creating FIFO A_TO_B");
        return 1;
    }

    if (mkfifo(FIFO_B_TO_A, 0600) == -1 && errno != EEXIST)
    {
        perror("Creating FIFO B_TO_A");
        return 1;
    }

    /*
     * O_RDWR prevents the FIFO open operation from blocking
     * while waiting for another process.
     *
     * This is a simple local-chat design. A separate handshake
     * is still needed to prove that the partner is connected.
     */
    read_fd = open(my_fifo, O_RDWR);

    if (read_fd == -1)
    {
        perror("Opening receiving FIFO");
        cleanup();
        return 1;
    }

    write_fd = open(partner_fifo, O_WRONLY);

    if (write_fd == -1)
    {
        perror("Opening sending FIFO");
        cleanup();
        return 1;
    }

    printf("\n====================================\n");
    printf("       USER %s CHAT SESSION\n", argv[1]);
    printf("====================================\n");
    printf("IPC Mechanism: Named Pipes (FIFO)\n");
    printf("Type /quit to exit.\n");
    printf("====================================\n\n");

    if (pthread_create(&receiver, NULL, receive_messages, NULL) != 0)
    {
        fprintf(stderr, "Failed to create receiving thread.\n");
        cleanup();
        return 1;
    }

    receiver_started = 1;

    while (running)
    {
        pthread_mutex_lock(&output_mutex);
        printf("You: ");
        fflush(stdout);
        pthread_mutex_unlock(&output_mutex);

        errno = 0;

        if (fgets(message, sizeof(message), stdin) == NULL)
        {
            if (errno == EINTR)
            {
                clearerr(stdin);
                continue;
            }

            running = 0;
            break;
        }

        if (!running)
            break;

        /*
         * Reject overlong input rather than silently treating
         * one long line as multiple separate chat messages.
         */
        if (strchr(message, '\n') == NULL && !feof(stdin))
        {
            int ch;

            while ((ch = getchar()) != '\n' && ch != EOF)
            {
                /* Discard remainder of overlong input. */
            }

            fprintf(stderr, "Message too long. Please try again.\n");
            continue;
        }

        if (strcmp(message, "/quit\n") == 0 ||
            strcmp(message, "/quit") == 0)
        {
            if (write_all(write_fd, "/quit\n", 6) == -1)
            {
                if (errno != EPIPE)
                    perror("Sending exit message");
            }

            running = 0;
            break;
        }

        size_t length = strlen(message);

        if (length > 0 && write_all(write_fd, message, length) == -1)
        {
            perror("Sending message");
            running = 0;
            break;
        }
    }

    running = 0;

    /*
     * The receiver uses poll() with a timeout, so it can
     * notice shutdown and finish instead of waiting forever.
     */
    if (receiver_started)
        pthread_join(receiver, NULL);

    cleanup();

    printf("\nChat ended.\n");

    return 0;
}
