#include <sys/poll.h>
#include <openssl/ssl.h>
#define API_V 1

/* 
 * Demo 4: Client — Client Creates FD — Nonblocking
 * ================================================
 *
 * This is an example of (part of) an application which uses libssl in an
 * asynchronous, nonblocking fashion. The client is responsible for creating the
 * socket and passing it to libssl. The functions show all interactions with
 * libssl the application makes, and wouldn hypothetically be linked into a
 * larger application.
 */
typedef struct app_conn_st {
    SSL *ssl;
    int fd;
    int rx_need_tx, tx_need_rx;
} APP_CONN;

/*
 * The application is initializing and wants an SSL_CTX which it will use for
 * some number of outgoing connections, which it creates in subsequent calls to
 * new_conn. The application may also call this function multiple times to
 * create multiple SSL_CTX.
 */
SSL_CTX *create_ssl_ctx(void)
{
    SSL_CTX *ctx;

    ctx = SSL_CTX_new(TLS_client_method());
    if (ctx == NULL)
        return NULL;

    /* Enable trust chain verification. */
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    /* Load default root CA store. */
    if (SSL_CTX_set_default_verify_paths(ctx) == 0) {
        SSL_CTX_free(ctx);
        return NULL;
    }

    return ctx;
}

/*
 * The application wants to create a new outgoing connection using a given
 * SSL_CTX.
 *
 * hostname is a string like "example.com" used for certificate validation.
 */
APP_CONN *new_conn(SSL_CTX *ctx, int fd, const char *bare_hostname)
{
    APP_CONN *conn;
    SSL *ssl;

    conn = calloc(1, sizeof(APP_CONN));
    if (conn == NULL)
        return NULL;

    ssl = conn->ssl = SSL_new(ctx);
    if (ssl == NULL) {
        free(conn);
        return NULL;
    }

    SSL_set_connect_state(ssl); /* cannot fail */

    if (SSL_set_fd(ssl, fd) <= 0) {
        SSL_free(ssl);
        free(conn);
        return NULL;
    }

    if (SSL_set1_host(ssl, bare_hostname) <= 0) {
        SSL_free(ssl);
        free(conn);
        return NULL;
    }

    if (SSL_set_tlsext_host_name(ssl, bare_hostname) <= 0) {
        SSL_free(ssl);
        free(conn);
        return NULL;
    }

    conn->fd = fd;
    return conn;
}

/*
 * Non-blocking transmission.
 *
 * Returns -1 on error. Returns -2 if the function would block (corresponds to
 * EWOULDBLOCK).
 */
int tx(APP_CONN *conn, const void *buf, int buf_len)
{
    int rc, l;

    conn->tx_need_rx = 0;

    l = SSL_write(conn->ssl, buf, buf_len);
    if (l <= 0) {
        rc = SSL_get_error(conn->ssl, l);
        switch (rc) {
            case SSL_ERROR_WANT_READ:
                conn->tx_need_rx = 1;
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_WRITE:
                return -2;
            default:
                return -1;
        }
    }

    return l;
}

/*
 * Non-blocking reception.
 *
 * Returns -1 on error. Returns -2 if the function would block (corresponds to
 * EWOULDBLOCK).
 */
int rx(APP_CONN *conn, void *buf, int buf_len)
{
    int rc, l;

    conn->rx_need_tx = 0;

    l = SSL_read(conn->ssl, buf, buf_len);
    if (l <= 0) {
        rc = SSL_get_error(conn->ssl, l);
        switch (rc) {
            case SSL_ERROR_WANT_WRITE:
                conn->rx_need_tx = 1;
            case SSL_ERROR_WANT_READ:
                return -2;
            default:
                return -1;
        }
    }

    return l;
}

/*
 * The application wants to know a fd it can poll on to determine when the
 * SSL state machine needs to be pumped.
 *
 * If the fd returned has:
 *
 *   POLLIN:    SSL_read *may* return data;
 *              if application does not want to read yet, it should call pump().
 *
 *   POLLOUT:   SSL_write *may* accept data
 *
 *   POLLERR:   An application should call pump() if it is not likely to call
 *              SSL_read or SSL_write soon.
 *
 */
int get_conn_fd(APP_CONN *conn)
{
    return conn->fd;
}

/*
 * These functions returns zero or more of:
 * 
 *   POLLIN:    The SSL state machine is interested in socket readability events.
 *
 *   POLLOUT:   The SSL state machine is interested in socket writeability events.
 *
 *   POLLERR:   The SSL state machine is interested in socket error events.
 *
 * get_conn_pending_tx returns events which may cause SSL_write to make
 * progress and get_conn_pending_rx returns events which may cause SSL_read
 * to make progress.
 */
int get_conn_pending_tx(APP_CONN *conn)
{
    return (conn->tx_need_rx ? POLLIN : 0) | POLLOUT | POLLERR;
}

int get_conn_pending_rx(APP_CONN *conn)
{
    return (conn->rx_need_tx ? POLLOUT : 0) | POLLIN | POLLERR;
}

/*
 * The application wants to close the connection and free bookkeeping
 * structures.
 */
void teardown(APP_CONN *conn)
{
    SSL_shutdown(conn->ssl);
    SSL_free(conn->ssl);
    free(conn);
}

/*
 * The application is shutting down and wants to free a previously
 * created SSL_CTX.
 */
void teardown_ctx(SSL_CTX *ctx)
{
    SSL_CTX_free(ctx);
}

/*
 * ============================================================================
 * Example driver for the above code. This is just to demonstrate that the code
 * works and is not intended to be representative of a real application.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char **argv)
{
    int rc, fd = -1, res = 1;
    const char tx_msg[] = "GET / HTTP/1.0\r\nHost: www.example.com\r\n\r\n";
    const char *tx_p = tx_msg;
    char rx_msg[2048], *rx_p = rx_msg;
    int l, tx_len = sizeof(tx_msg)-1, rx_len = sizeof(rx_msg);
    int timeout = 2000 /* ms */;
    APP_CONN *conn = NULL;
    struct addrinfo hints = {0}, *result = NULL;
    SSL_CTX *ctx;

    ctx = create_ssl_ctx();
    if (ctx == NULL) {
        fprintf(stderr, "cannot create SSL context\n");
        goto fail;
    }

    hints.ai_family     = AF_INET;
    hints.ai_socktype   = SOCK_STREAM;
    hints.ai_flags      = AI_PASSIVE;
    rc = getaddrinfo("www.example.com", "443", &hints, &result);
    if (rc < 0) {
        fprintf(stderr, "cannot resolve\n");
        goto fail;
    }

    signal(SIGPIPE, SIG_IGN);

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        fprintf(stderr, "cannot create socket\n");
        goto fail;
    }

    rc = connect(fd, result->ai_addr, result->ai_addrlen);
    if (rc < 0) {
        fprintf(stderr, "cannot connect\n");
        goto fail;
    }

    rc = fcntl(fd, F_SETFL, O_NONBLOCK);
    if (rc < 0) {
        fprintf(stderr, "cannot make socket nonblocking\n");
        goto fail;
    }

    conn = new_conn(ctx, fd, "www.example.com");
    if (conn == NULL) {
        fprintf(stderr, "cannot establish connection\n");
        goto fail;
    }

    /* TX */
    while (tx_len != 0) {
        l = tx(conn, tx_p, tx_len);
        if (l > 0) {
            tx_p += l;
            tx_len -= l;
        } else if (l == -1) {
            fprintf(stderr, "tx error\n");
            goto fail;
        } else if (l == -2) {
            struct pollfd pfd = {0};
            pfd.fd = get_conn_fd(conn);
            pfd.events = get_conn_pending_tx(conn);
            if (poll(&pfd, 1, timeout) == 0) {
                fprintf(stderr, "tx timeout\n");
                goto fail;
            }
        }
    }

    /* RX */
    while (rx_len != 0) {
        l = rx(conn, rx_p, rx_len);
        if (l > 0) {
            rx_p += l;
            rx_len -= l;
        } else if (l == -1) {
            break;
        } else if (l == -2) {
            struct pollfd pfd = {0};
            pfd.fd = get_conn_fd(conn);
            pfd.events = get_conn_pending_rx(conn);
            if (poll(&pfd, 1, timeout) == 0) {
                fprintf(stderr, "rx timeout\n");
                goto fail;
            }
        }
    }

    fwrite(rx_msg, 1, rx_p - rx_msg, stdout);

    res = 0;
fail:
    if (conn != NULL)
        teardown(conn);
    if (ctx != NULL)
        teardown_ctx(ctx);
    if (result != NULL)
        freeaddrinfo(result);
    return res;
}
