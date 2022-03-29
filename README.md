Demo-Driven Design
==================

For background information on the purpose of this repository, see [API long-term
evolution](https://github.com/openssl/openssl/issues/17939).

## Background

These demos were developed after analysis of the following open source
applications to determine libssl API usage patterns. The modally occuring usage
patterns were determined and used to determine categories into which to classify
the applications:

|                  | Blk? | FD | 
|------------------|------|----|
| mutt             | S |      AOSF  |
| vsftpd           | S |      AOSF  |
| exim             | S |      AOSFx |
| wget             | S |      AOSF  |
| librabbitmq      | A |      BIOx  |
| ngircd           | A |      AOSF  |
| stunnel          | A |      AOSFx |
| Postfix          | A |      AOSF  |
| socat            | A |      AOSF  |
| HAProxy          | A |      BIOx  |
| Dovecot          | A |      BIOm  |
| Apache httpd     | A |      BIOx  |
| UnrealIRCd       | A |      AOSF  |
| wpa_supplicant   | A |      BIOm  |
| icecast          | A |      AOSF  |
| nginx            | A |      AOSF  |
| curl             | A |      AOSF  |
| Asterisk         | A |      AOSF  |
| Asterisk (DTLS)  | A |      BIOm/x |
| pgbouncer        | A |      AOSF, BIOc  |
| Fossil           | S |      BIOc  |

* Blk: Whether the application uses blocking or non-blocking I/O.
  * S: Blocking
  * A: Nonblocking
* FD: Whether the application creates and owns its own FD.
  * AOSF: Application owns, calls SSL_set_fd.
  * AOSFx: Application owns, calls SSL_set_[rw]fd, different FDs for read/write.
  * BIOs: Application creates a socket/FD BIO and calls SSL_set_bio. Application created the connection.
  * BIOx: Application creates a BIO with a custom BIO method and calls SSL_set_bio.
  * BIOm: Application creates a memory BIO and does its own pumping to/from actual socket, treating libssl as a pure state machine which does no I/O itself.
  * BIOc: Application uses BIO_s_connect-based methods such as BIO_new_ssl_connect and leaves connection establishment to OpenSSL.

## Demos

The demos found in this repository are:

|                 | Type  | Description |
|-----------------|-------|-------------|
| [ddd-01-conn-blocking](ddd-01-conn-blocking.c) | S-BIOc | A `BIO_s_connect`-based blocking example demonstrating exemplary OpenSSL API usage |
| [ddd-02-conn-nonblocking](ddd-02-conn-nonblocking.c) | A-BIOc | A `BIO_s_connect`-based nonblocking example demonstrating exemplary OpenSSL API usage |
| [ddd-03-fd-blocking](ddd-03-fd-blocking.c) | S-AOSF | A `SSL_set_fd`-based blocking example demonstrating real-world OpenSSL API usage (corresponding to S-AOSF applications above) |
| [ddd-04-fd-nonblocking](ddd-04-fd-nonblocking.c) | A-AOSF | A `SSL_set_fd`-based non-blocking example demonstrating real-world OpenSSL API usage (corresponding to A-AOSF applications above) |
| [ddd-05-mem-nonblocking](ddd-05-mem-nonblocking.c) | A-BIOm | A non-blocking example based on use of a memory buffer to feed OpenSSL encrypted data (corresponding to A-BIOm applications above) |

## Discussion

Discussion is welcomed and can be posted in this [dummy PR](https://github.com/hlandau/openssl-ddd/pull/1).
