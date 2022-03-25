#include <openssl/ssl.h>

/* 
 * Demo 1: Client — Client Creates FD — Blocking
 * =============================================
 *
 * This is an example of (part of) an application which uses libssl in a simple,
 * synchronous, blocking fashion. The client is responsible for creating the
 * socket and passing it to libssl. The functions show all interactions with
 * libssl the application makes, and would hypothetically be linked into a
 * larger application.
 */

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
BIO *new_conn(SSL_CTX *ctx, int fd, const char *bare_hostname)
{
    BIO *out;
    SSL *ssl;

    ssl = SSL_new(ctx);
    if (ssl == NULL)
        return NULL;

    SSL_set_connect_state(ssl); /* cannot fail */

    if (SSL_set_fd(ssl, fd) <= 0) {
        SSL_free(ssl);
        return NULL;
    }

    if (SSL_set1_host(ssl, bare_hostname) <= 0) {
        SSL_free(ssl);
        return NULL;
    }

    if (SSL_set_tlsext_host_name(ssl, bare_hostname) <= 0) {
        SSL_free(ssl);
        return NULL;
    }

    out = BIO_new(BIO_f_ssl());
    if (out == NULL) {
        SSL_free(ssl);
        return NULL;
    }

    if (BIO_set_ssl(out, ssl, BIO_CLOSE) <= 0) {
        SSL_free(ssl);
        BIO_free(out);
        return NULL;
    }

    return out;
}

/*
 * The application wants to send some block of data to the peer.
 * This is a blocking call.
 */
int tx(BIO *bio, const void *buf, int buf_len)
{
    return BIO_write(bio, buf, buf_len);
}

/*
 * The application wants to receive some block of data from
 * the peer. This is a blocking call.
 */
int rx(BIO *bio, void *buf, int buf_len)
{
    return BIO_read(bio, buf, buf_len);
}

/*
 * The application wants to close the connection and free bookkeeping
 * structures.
 */
void teardown(BIO *bio)
{
    BIO_free_all(bio);
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

int main(int argc, char **argv)
{
    int rc, fd = -1, l, res = 1;
    const char msg[] = "GET / HTTP/1.0\r\nHost: www.example.com\r\n\r\n";
    struct addrinfo hints = {0}, *result = NULL;
    BIO *b = NULL;
    SSL_CTX *ctx;
    char buf[2048];

    ctx = create_ssl_ctx();
    if (!ctx) {
        fprintf(stderr, "cannot create context\n");
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

    b = new_conn(ctx, fd, "www.example.com");
    if (!b) {
        fprintf(stderr, "cannot create connection\n");
        goto fail;
    }

    if (tx(b, msg, sizeof(msg)-1) < sizeof(msg)-1) {
        fprintf(stderr, "tx error\n");
        goto fail;
    }

    for (;;) {
        l = rx(b, buf, sizeof(buf));
        if (l <= 0)
            break;
        fwrite(buf, 1, l, stdout);
    }

    res = 0;
fail:
    if (b != NULL)
        teardown(b);
    if (ctx != NULL)
        teardown_ctx(ctx);
    if (fd >= 0)
        close(fd);
    if (result != NULL)
        freeaddrinfo(result);
    return res;
}
