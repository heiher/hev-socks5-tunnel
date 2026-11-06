/*
 ============================================================================
 Name        : socks5d.c
 Description : Minimal SOCKS5 server used as the upstream of the test suite.
               Built on HevSocks5Core (CONNECT, UDP ASSOCIATE, FWD UDP and
               optional username/password authentication).

               usage: socks5d [-l address] [-p port] [-u user -w password]
 ============================================================================
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <hev-task.h>
#include <hev-task-io.h>
#include <hev-task-io-socket.h>
#include <hev-task-system.h>

#include <hev-socks5-server.h>

static HevSocks5Authenticator *auth;

static void
server_entry (void *data)
{
    HevSocks5Server *server = data;

    hev_socks5_server_run (server);
    hev_object_unref (HEV_OBJECT (server));
}

static int
listen_on (const char *addr, int port)
{
    struct sockaddr_in6 sa;
    int one = 1, zero = 0;
    int fd;

    memset (&sa, 0, sizeof (sa));
    sa.sin6_family = AF_INET6;
    sa.sin6_port = htons (port);
    if (!strcmp (addr, "::")) {
        sa.sin6_addr = in6addr_any;
    } else if (inet_pton (AF_INET6, addr, &sa.sin6_addr) != 1) {
        struct in_addr v4;

        if (inet_pton (AF_INET, addr, &v4) != 1)
            return -1;
        /* v4-mapped */
        sa.sin6_addr.s6_addr[10] = 0xff;
        sa.sin6_addr.s6_addr[11] = 0xff;
        memcpy (&sa.sin6_addr.s6_addr[12], &v4, 4);
    }

    fd = hev_task_io_socket_socket (AF_INET6, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof (one));
    setsockopt (fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof (zero));
    if (bind (fd, (struct sockaddr *)&sa, sizeof (sa)) < 0 ||
        listen (fd, 1024) < 0) {
        perror ("socks5d: bind/listen");
        return -1;
    }

    return fd;
}

static void
listener_entry (void *data)
{
    int fd = (int)(intptr_t)data;

    hev_task_add_fd (hev_task_self (), fd, POLLIN);
    printf ("READY\n");
    fflush (stdout);

    for (;;) {
        HevSocks5Server *server;
        HevTask *task;
        int nfd;

        nfd = hev_task_io_socket_accept (fd, NULL, NULL, NULL, NULL);
        if (nfd < 0) {
            if (errno == EAGAIN)
                continue;
            break;
        }

        server = hev_socks5_server_new (nfd);
        if (!server) {
            close (nfd);
            continue;
        }
        if (auth)
            hev_socks5_server_set_auth (server, auth);

        task = hev_task_new (-1);
        hev_task_run (task, server_entry, server);
    }
}

int
main (int argc, char *argv[])
{
    const char *addr = "::";
    const char *user = NULL;
    const char *pass = NULL;
    int port = 1080;
    HevTask *task;
    int fd, opt;

    while ((opt = getopt (argc, argv, "l:p:u:w:")) != -1) {
        switch (opt) {
        case 'l':
            addr = optarg;
            break;
        case 'p':
            port = atoi (optarg);
            break;
        case 'u':
            user = optarg;
            break;
        case 'w':
            pass = optarg;
            break;
        default:
            fprintf (stderr,
                     "usage: %s [-l address] [-p port] [-u user -w pass]\n",
                     argv[0]);
            return 2;
        }
    }

    signal (SIGPIPE, SIG_IGN);

    if (hev_task_system_init () < 0)
        return 1;

    if (user && pass) {
        HevSocks5User *u;

        auth = hev_socks5_authenticator_new ();
        u = hev_socks5_user_new (user, strlen (user), pass, strlen (pass));
        hev_socks5_authenticator_add (auth, u);
    }

    fd = listen_on (addr, port);
    if (fd < 0)
        return 1;

    task = hev_task_new (-1);
    hev_task_run (task, listener_entry, (void *)(intptr_t)fd);
    hev_task_system_run ();
    hev_task_system_fini ();

    return 0;
}
