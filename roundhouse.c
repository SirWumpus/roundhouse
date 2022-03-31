/*
 * roundhouse.c
 *
 * Copyright 2005, 2013 by Anthony Howe. All rights reserved.
 *
 *
 * From http://www.answers.com/roundhouse&r=67
 *
 *	A roundhouse is a maintenance facility used by railroads. Roundhouses
 *	are the large, circular or semicircular structures that are located
 *	surrounding or adjacent to turntables. The roundhouse in a railroad
 *	yard is typically where steam locomotives were stored when they
 *	weren't in use.
 *
 *
 * Description
 * -----------
 *
 * A specialised SMTP proxy server intended to accept mail on port 25 and
 * copy the client input to each SMTP server (unix domain socket or internet
 * host,port) specified.
 *
 * Intended as means to debug and test different mail servers configurations
 * using a production server's live stream of data.
 *
 *
 * Build for Unix using GCC
 * ------------------------
 *
 *	gcc -02 -o -Icom/snert/include roundhouse roundhouse.c -lsnert
 *
 *
 * Build for Windows using GCC
 * ---------------------------
 *
 *	gcc -DNDEBUG -02 -Icom/snert/include -mno-cygwin -mwindows -o roundhouse roundhouse.c -lsnert -lws2_32
 */

/***********************************************************************
 *** Leave this header alone. Its generated from the configure script.
 ***********************************************************************/

#include "config.h"

/***********************************************************************
 *** You can change the stuff below if the configure script doesn't work.
 ***********************************************************************/

#ifndef AUTH_MECHANISMS
/*
 *	LOGIN		Only method supported by Outlook & Outlook Express 6.
 *
 *	PLAIN		Old Netscape 4 mail clients; Thunderbird 1.x, Opera 7
 *
 *	DIGEST-MD5	Thunderbird 1.x, Opera 7
 *
 * Normally LOGIN or PLAIN are used only over a secure connection started
 * with STARTTLS. However if the SMTP multiplexor operates on a different
 * machine from the primary SMTP server that supports STARTTLS, such that
 * the primary SMTP server's certificate does not match the multiplexor's
 * host name, then its not possible to establish a secure connection.
 *
 * The SMTP multiplexor would have to handle all the complexities of doing
 * TLS with the client and present its own certificate, which is not worth
 * the effort at this time for a tool intended for testing.
 */
#define AUTH_MECHANISMS	"PLAIN LOGIN"
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdlib.h>

#ifdef __sun__
# define _POSIX_PTHREAD_SEMANTICS
#endif
#include <signal.h>

#ifndef __MINGW32__
# if defined(HAVE_GRP_H)
#  include <grp.h>
# endif
# if defined(HAVE_PWD_H)
#  include <pwd.h>
# endif
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
# if defined(HAVE_SYS_WAIT_H)
#  include <sys/wait.h>
# endif
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/io/socket3.h>
#include <com/snert/lib/mail/limits.h>
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/net/server.h>
#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/sys/process.h>
#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/sys/Time.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/Base64.h>
#include <com/snert/lib/util/Token.h>
#include <com/snert/lib/util/getopt.h>

#if LIBSNERT_MAJOR < 1 || LIBSNERT_MINOR < 75
# error "LibSnert/1.75 or better is required"
#endif

/***********************************************************************
 *** Constants
 ***********************************************************************/

#define _DISPLAY	"Roundhouse"
#define _BRIEF		"SMTP Multiplexor"

#define LOG_FMT		"%s "
#define LOG_ARG		conn->id

static const char log_io[] = "socket error %s(%d): %s (%d)";
static const char log_init[] = "init error %s(%d): %s (%d)";

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

typedef struct {
	char *id;
	int connected;
	Socket2 *client;
	Socket2 **servers;
	long inputLength;
	char input[SMTP_TEXT_LINE_LENGTH+1];
	char reply[SMTP_REPLY_LINE_LENGTH+1];
	char client_addr[IPV6_STRING_SIZE];
	char client_name[DOMAIN_SIZE];
	char mail[SMTP_PATH_LENGTH+3];
} Connection;

typedef struct {
	Connection *conn;
	Socket2 *source;
	Socket2 *sink;
} Stream;

static int debug;
static int connect_all;
static int server_quit;
static int daemon_mode = 1;
static char *user_id = NULL;
static char *group_id = NULL;
static char *windows_service;
static char *interfaces = "[::0]:" QUOTE(SMTP_PORT) ",0.0.0.0:" QUOTE(SMTP_PORT);
static long socket_timeout = SOCKET_TIMEOUT;
static long connect_timeout = CONNECT_TIMEOUT;

static int nservers;
static char *smtp_host[MAX_ARGV_LENGTH];
static SocketAddress *servers[MAX_ARGV_LENGTH];

static ServerSignals signals;

static char *ca_chain = NULL;
static char *cert_dir = NULL;
static char *key_pass = NULL;
static char *key_crt_pem = NULL;

#ifdef HAVE_OPENSSL_SSL_H
static const char ehlo_tls[] = "250-AUTH " AUTH_MECHANISMS "\r\n250-PIPELINING\r\n250 STARTTLS\r\n";
# define GETOPT_TLS	"c:C:k:K:"
#else
# define GETOPT_TLS
#endif

static const char reply_421[] = "421 service temporarily unavailable\r\n";
static const char ehlo_basic[] = "250-AUTH " AUTH_MECHANISMS "\r\n250 PIPELINING\r\n";
static const char *ehlo_reply = ehlo_basic;

static char *usage_message =
"usage: " _NAME " [-Adqv][-i ip,...][-t timeout][-u name][-g name]\n"
#ifdef HAVE_OPENSSL_SSL_H
"       [-c ca_pem][-C ca_dir][-k key_crt_pem][-K key_pass]\n"
#endif
"       [-w add|remove] server ...\n"
"\n"
"-A\t\tall down stream servers must connect, else 421 the client.\n"
#ifdef HAVE_OPENSSL_SSL_H
"-c ca_pem\tCertificate Authority root certificate chain file\n"
"-C ca_dir\tCertificate Authority root certificate directory\n"
#endif
"-d\t\tdisable daemon mode and run as a foreground application\n"
"-g name\t\trun as this group\n"
"-i ip,...\tcomma separated list of IPv4 or IPv6 addresses and\n"
"\t\toptional :port number to listen on for SMTP connections;\n"
"\t\tdefault is \"[::0]:25,0.0.0.0:25\"\n"
#ifdef HAVE_OPENSSL_SSL_H
"-k key_crt_pem\tprivate key and certificate chain file.  When left unset\n"
"\t\tor explicitly set to an empty string then disable STARTTLS.\n"
"-K key_pass\tpassword for private key; default no password\n"
#endif
"-q\t\tx1 slow quit, x2 quit now, x3 restart, x4 restart-if\n"
"-t timeout\tclient socket timeout in seconds; default 300\n"
"-u name\t\trun as this user\n"
"-v\t\tx1 log SMTP; x2 SMTP and message headers; x3 everything\n"
"-w add|remove\tadd or remove Windows service; ignored on unix\n"
"\n"
"server\t\thost[:port] of down stream mail server to forward mail to;\n"
"\t\tdefault port " QUOTE(SMTP_PORT) "\n"
"\n"
_NAME " " _VERSION " " _COPYRIGHT "\n"
;

/***********************************************************************
 *** Routines
 ***********************************************************************/

#undef syslog

void
syslog(int level, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	if (logFile == NULL)
		vsyslog(level, fmt, args);
	else
		LogV(level, fmt, args);
	va_end(args);
}

int
savePid(char *file)
{
	FILE *fp;

	/* We have to create the .pid file after we become a daemon process. */
	if ((fp = fopen(file, "w")) == NULL) {
		/* If the daemon is configured to be run typically as root,
		 * then you want to exit when a normal user runs hibachi
		 * and fails to write the pid file into a root-only
		 * location like /var/run.
		 */
		return -1;
	}

	(void) fprintf(fp, "%d\n", getpid());
	(void) fclose(fp);

	return 0;
}

static long
smtpConnPrint(Connection *conn, int index, const char *line)
{
	Socket2 *s;

	if (index < 0)
		/* Sending to the client. */
		syslog(LOG_DEBUG, LOG_FMT "< %s", LOG_ARG, line);

	s = index < 0 ? conn->client : conn->servers[index];
	if (s == NULL) {
		/* Server in this slot has been disconnected. */
		return 0;
	}

	return socketWrite(s, (unsigned char *) line, strlen(line));
}

static int
smtpConnGetResponse(Connection *conn, int index, char *line, long size, int *code)
{
	Socket2 *s;
	char *stop;
	long length, value;

	if (conn == NULL || line == NULL)
		return EFAULT;

	s = conn->servers[index];
	if (s == NULL) {
		/* Server in this slot has been disconnected. */
		return EFAULT;
	}

	/* Ideally we should collect _all_ the response lines into a variable
	 * length buffer (see com/snert/lib/util/Buf.h), but I don't see any
	 * real need for it just yet.
	 */

	value = 450;

	socketSetTimeout(s, socket_timeout / nservers);

	do {
		/* Erase the first 4 bytes of the line buffer, which
		 * corresponds with the 3 ASCII digit status code
		 * followed by either a ASCII hyphen or space.
		 */
		line[0] = line[1] = line[2] = line[4] = '\0';

		errno = 0;
		switch (length = socketReadLine(s, line, size)) {
		case SOCKET_ERROR:
			syslog(LOG_ERR, LOG_FMT "read error: %s (%d)", LOG_ARG, strerror(errno), errno);
			return errno;
		case SOCKET_EOF:
			syslog(LOG_ERR, LOG_FMT "unexpected EOF", LOG_ARG);
			return errno;
		}

		/* Did we read sufficient characters for a response code? */
		if (length < 4)
			return EIO;

		syslog(LOG_DEBUG, LOG_FMT "#%d < %s", LOG_ARG, index, line);

		value = strtol(line, &stop, 10);
	} while (line + 3 == stop && line[3] == '-');

	if (code != NULL)
		*code = value;

	return 0;
}

static void
smtpConnDisconnect(Connection *conn, int index)
{
	if (conn->servers[index] != NULL) {
		syslog(LOG_DEBUG, LOG_FMT "#%d disconnecting from %s", LOG_ARG, index, smtp_host[index]);
		socketClose(conn->servers[index]);
		conn->servers[index] = NULL;
		conn->connected--;
	}
}

/*
 * Perform man-in-the-middle AUTH LOGIN dialogue with the client
 * and convert the AUTH LOGIN into an AUTH PLAIN. The conn->input
 * buffer will then be forwarded to the SMTP servers.
 *
 *	>>> AUTH LOGIN
 *	334 VXNlcm5hbWU6
 *	>>> dGVzdA==
 *	334 UGFzc3dvcmQ6
 * 	>>> dEVzdDQy
 *	235 2.0.0 OK Authenticated
 */
int
authLogin(Connection *conn)
{
	int rx;
	int rc = -1;
	Base64 base64;
	long userLen, passLen;
	char userB64[512], passB64[512], *buffer;

	if (conn->input[sizeof ("AUTH LOGIN")-1] == '\0') {
		smtpConnPrint(conn, -1, "334 VXNlcm5hbWU6\r\n");

		if (!socketHasInput(conn->client, socket_timeout))
			goto error0;

		if ((userLen = socketReadLine(conn->client, userB64, sizeof (userB64))) < 0) {
			syslog(LOG_ERR, LOG_FMT "client read error: %s (%d)%c", LOG_ARG, strerror(errno), errno, userLen == SOCKET_EOF ? '!' : ' ');
			goto error0;
		}

		syslog(LOG_DEBUG, LOG_FMT "> %s", LOG_ARG, userB64);
	} else {
		TextCopy(userB64, sizeof (userB64), conn->input+sizeof ("AUTH LOGIN"));
	}

	smtpConnPrint(conn, -1, "334 UGFzc3dvcmQ6\r\n");

	if (!socketHasInput(conn->client, socket_timeout))
		goto error0;

	if ((passLen = socketReadLine(conn->client, passB64, sizeof (passB64))) < 0) {
		syslog(LOG_ERR, LOG_FMT "client read error: %s (%d)%c", LOG_ARG, strerror(errno), errno, passLen == SOCKET_EOF ? '!' : ' ');
		goto error0;
	}

	syslog(LOG_DEBUG, LOG_FMT "> %s", LOG_ARG, passB64);

	if ((base64 = Base64Create()) == NULL) {
		syslog(LOG_ERR, LOG_FMT "Base64Create() error", LOG_ARG);
		goto error0;
	}

	buffer = NULL;
	switch ((rx = base64->decodeBuffer(base64, userB64, userLen, &buffer, &userLen))) {
	case BASE64_NEXT:
	case BASE64_ERROR:
		syslog(LOG_ERR, LOG_FMT "login Base64DecodeBuffer error rc=%d", LOG_ARG, rx);
		goto error1;
	}
	if (255 < userLen) {
		syslog(LOG_ERR, LOG_FMT "login too long, length=%ld", LOG_ARG, userLen);
		goto error2;
	}

	/* Start building the PLAIN authentication details, RFC 2595:
	 *
	 *	[authorize-id] \0 authenticate-id \0 password
	 */
	memcpy(conn->input, buffer, userLen);
	conn->input[userLen] = '\0';
	memcpy(conn->input+userLen+1, buffer, userLen);
	conn->input[userLen+1+userLen] = '\0';
	free(buffer);

	buffer = NULL;
	base64->reset(base64);
	switch ((rx = base64->decodeBuffer(base64, passB64, passLen, &buffer, &passLen))) {
	case BASE64_NEXT:
	case BASE64_ERROR:
		syslog(LOG_ERR, LOG_FMT "password Base64DecodeBuffer error rc=%d", LOG_ARG, rx);
		goto error1;
	}
	if (255 < passLen) {
		syslog(LOG_ERR, LOG_FMT "password too long, length=%ld", LOG_ARG, passLen);
		goto error2;
	}

	memcpy(conn->input+userLen+1+userLen+1, buffer, passLen);
	conn->input[userLen+1+userLen+1+passLen] = '\0';
	free(buffer);

	base64->reset(base64);
	if ((rx = base64->encodeBuffer(base64, conn->input, userLen+1+userLen+1+passLen, &buffer, &conn->inputLength, 1)) != 0) {
		syslog(LOG_ERR, LOG_FMT "plain Base64EncodeBuffer error rc=%d", LOG_ARG, rx);
		goto error1;
	}
	if (sizeof (conn->input) < conn->inputLength+3) {
		syslog(LOG_ERR, LOG_FMT "AUTH PLAIN conversion too long, length=%ld", LOG_ARG, conn->inputLength);
		goto error2;
	}

	syslog(LOG_DEBUG, LOG_FMT "login=%s pass=%s", LOG_ARG, conn->input, conn->input+userLen+1+userLen+1);

	(void) TextCopy(conn->input, sizeof (conn->input), "AUTH PLAIN ");
	memcpy(conn->input+sizeof("AUTH PLAIN ")-1, buffer, conn->inputLength);
	conn->inputLength += sizeof("AUTH PLAIN ")-1;
	conn->input[conn->inputLength] = '\0';

	syslog(LOG_DEBUG, LOG_FMT "plain=%s", LOG_ARG, conn->input);

	rc = 0;
error2:
	free(buffer);
error1:
	Base64Destroy(base64);
error0:
	return rc;
}

static int
smtpConnData(Connection *conn)
{
	time_t now;
	long length;
	struct tm local;
	int i, code, isDot, isEOH = 0;
	char stamp[40], line[SMTP_TEXT_LINE_LENGTH];

	smtpConnPrint(conn, -1, "354 enter mail, end with \".\" on a line by itself\r\n");

	/* Add our Return-Path and Received header. */
	now = time(NULL);
	(void) localtime_r(&now, &local);
	(void) getRFC2821DateTime(&local, stamp, sizeof (stamp));
	/* Note that conn->id is a session ID and does not
	 * change (yet) with each MAIL transaction.
	 */
	(void) snprintf(
		line, sizeof (line),
		"Return-Path:%s\r\nReceived: from %s ([%s])\r\n\tid %s; %s\r\n",
		conn->mail, conn->client_name, conn->client_addr, conn->id, stamp
	);
	if (1 < debug) {
		syslog(LOG_DEBUG, LOG_FMT "> %s", LOG_ARG, line);
	}
	for (i = 0; i < nservers; i++) {
		if (conn->servers[i] == NULL) {
			continue;
		}
		if (smtpConnPrint(conn, i, (const char *) line) < 0) {
			smtpConnDisconnect(conn, i);
			continue;
		}
	}

	/* Relay client's message to each SMTP server in turn. */
	for (isDot = 0; !isDot && socketHasInput(conn->client, socket_timeout); ) {
		if ((length = socketReadLine(conn->client, conn->input, sizeof (conn->input))) < 0) {
			syslog(LOG_ERR, LOG_FMT "client read error during message: %s (%d)%c", LOG_ARG, strerror(errno), errno, length == SOCKET_EOF ? '!' : ' ');
			return -1;
		}
		if (0 < TextInsensitiveStartsWith(conn->input, "Return-Path:")) {
			/* We supply our Return-Path based on MAIL FROM: */
			continue;
		}

		isDot = conn->input[0] == '.' && conn->input[1] == '\0';
		if (!isEOH && length == 0) {
			/* First blank line is EOH. */
			isEOH = 1;
			if (0 < debug && debug < 3) {
				syslog(LOG_DEBUG, LOG_FMT "message content not logged", LOG_ARG);
			}
		}

		/* -v log dot, -vv log only headers, -vvv log everything. */
		if (2 < debug || (1 < debug && !isEOH) || (0 < debug && isDot)) {
			syslog(LOG_DEBUG, LOG_FMT "> %s", LOG_ARG, conn->input);
		}

		/* Add back the CRLF removed by socketReadLine(). */
		conn->input[length++] = '\r';
		conn->input[length++] = '\n';
		conn->input[length] = '\0';

		for (i = 0; i < nservers; i++) {
			if (conn->servers[i] == NULL)
				continue;

			if (smtpConnPrint(conn, i, (const char *) conn->input) < 0) {
				smtpConnDisconnect(conn, i);
				continue;
			}

			if (isDot) {
				/* Get and ignore the response leaving the
				 * connection open for further MAIL.  Tell
				 * the client success, since we can't report
				 * N differnet SMTP replies to the client.
				 */
				(void) smtpConnGetResponse(conn, i, conn->reply, sizeof (conn->reply), &code);
			}
		}
	}

	return 0;
}

int
roundhouse(ServerSession *session)
{
	long length;
	Connection *conn;
	int i, code, isQuit, isData, isEhlo;

	syslog(LOG_INFO, "%s start interface=[%s] client=[%s]", session->id_log, session->if_addr, session->address);

	if ((conn = calloc(1, sizeof (*conn))) == NULL)
		return -1;

	session->data = conn;
	conn->id = session->id_log;
	conn->client = session->client;
	(void) socketAddressGetName(&conn->client->address, conn->client_name, sizeof (conn->client_name));
	(void) socketAddressGetString(&conn->client->address, 0, conn->client_addr, sizeof (conn->client_addr));

	(void) socketSetNagle(conn->client, 0);
	(void) socketSetLinger(conn->client, 0);
	(void) socketSetNonBlocking(conn->client, 1);

	if ((conn->servers = calloc(nservers, sizeof (*conn->servers))) == NULL) {
		syslog(LOG_ERR, LOG_FMT "%s (%d)", LOG_ARG, strerror(errno), errno);
		smtpConnPrint(conn, -1, reply_421);
		goto error0;
	}

	/* Connect to all the SMTP servers. */
	conn->connected = 0;
	for (i = 0; i < nservers; i++) {
		if ((conn->servers[i] = socketOpen(servers[i], 1)) == NULL)
			continue;

		conn->connected++;
		syslog(LOG_DEBUG, LOG_FMT "#%d connecting to %s", LOG_ARG, i, smtp_host[i]);

		if (socketClient(conn->servers[i], CONNECT_TIMEOUT)) {
			syslog(LOG_ERR, LOG_FMT "#%d connection to %s failed", LOG_ARG, i, smtp_host[i]);
			smtpConnDisconnect(conn, i);
			if (connect_all) {
				smtpConnPrint(conn, -1, reply_421);
				goto error1;
			}
			continue;
		}

		(void) socketSetNonBlocking(conn->servers[i], 1);

		if (smtpConnGetResponse(conn, i, conn->reply, sizeof (conn->reply), &code) || code != 220) {
			syslog(LOG_ERR, LOG_FMT "#%d no welcome from %s", LOG_ARG, i, smtp_host[i]);
			smtpConnDisconnect(conn, i);
			continue;
		}
	}

	if (conn->connected <= 0) {
		syslog(LOG_ERR, LOG_FMT "no answer from any SMTP server", LOG_ARG);
		smtpConnPrint(conn, -1, reply_421);
		goto error1;
	}

	/* Multiline welcome message can throw off some spam engines. */
	(void) snprintf(conn->input, sizeof (conn->input), "220-" _DISPLAY " switch yard for mail.\r\n220 Session ID %s.\r\n", conn->id);
	smtpConnPrint(conn, -1, conn->input);

	/* Send XCLIENT ADDR= NAME=, ignore response since its a Postfix thing. */
	int is_ipv4 = conn->client->address.sa.sa_family == AF_INET;
	(void) snprintf(
		conn->input, sizeof (conn->input), "XCLIENT ADDR=%s%s NAME=%s\r\n",
		is_ipv4 ? "" : IPV6_TAG, conn->client_addr, conn->client_name
	);
	syslog(LOG_DEBUG, LOG_FMT "> %s", LOG_ARG, conn->input);
	for (i = 0; i < nservers; i++) {
		(void) smtpConnPrint(conn, i, (const char *) conn->input);
		(void) smtpConnGetResponse(conn, i, conn->reply, sizeof (conn->reply), &code);

		/* See reply codes http://www.postfix.org/XCLIENT_README.html */
		if (code == 421) {
			/* Unable to proceed, disconnecting.  Assume "we don't like them."
			 * A server's ACL could opt to disconnect immediately rather than
			 * return a negative code and wait for QUIT.  Postfix is a little
			 * vague.
			 */
			smtpConnPrint(conn, -1, reply_421);
			goto error1;
		}
	}

	/* Relay client SMTP commands to each SMTP server in turn. */
	while (socketHasInput(conn->client, socket_timeout)) {
		if ((conn->inputLength = socketReadLine(conn->client, conn->input, sizeof (conn->input))) < 0) {
			syslog(LOG_ERR, LOG_FMT "client read error: %s (%d)%c", LOG_ARG, strerror(errno), errno, conn->inputLength == SOCKET_EOF ? '!' : ' ');
			goto error1;
		}

		syslog(LOG_DEBUG, LOG_FMT "> %s", LOG_ARG, conn->input);

		if (conn->input[0] == '\0') {
			smtpConnPrint(conn, -1, "500 command unrecognized: \"\"\r\n");
			continue;
		}

		if (0 < TextInsensitiveStartsWith(conn->input, "STARTTLS")) {
			if (key_crt_pem == NULL) {
				(void) smtpConnPrint(conn, -1, "502 command not recognised\r\n");
				continue;
			}

			if (socket3_is_tls(conn->client->fd)) {
				(void) smtpConnPrint(conn, -1, "503 TLS already started\r\n");
				continue;
			}

			syslog(LOG_INFO, "starting TLS...");

			if (socket3_start_tls(conn->client->fd, SOCKET3_SERVER_TLS, socket_timeout)) {
				syslog(LOG_ERR, log_io, SERVER_FILE_LINENO, strerror(errno), errno);
				(void) smtpConnPrint(conn, -1, "454 TLS not available\r\n");
				continue;
			}

			syslog(LOG_INFO, "TLS started");

			if (smtpConnPrint(conn, -1, "220 OK\r\n") < 0) {
				break;
			}
			continue;
		}

		if (0 < TextInsensitiveStartsWith(conn->input, "AUTH LOGIN") && authLogin(conn))
			break;

		i = TextInsensitiveStartsWith(conn->input, "MAIL FROM:");
		if (0 < i) {
			(void) strncpy(conn->mail, conn->input+i, sizeof (conn->mail));
		}

		isEhlo = 0 < TextInsensitiveStartsWith(conn->input, "EHLO");
		isQuit = 0 < TextInsensitiveStartsWith(conn->input, "QUIT");
		isData = 0;

		/* Add back the CRLF removed by socketReadLine(). */
		if (sizeof (conn->input) <= conn->inputLength+3)
			conn->inputLength = sizeof (conn->input)-3;
		conn->input[conn->inputLength++] = '\r';
		conn->input[conn->inputLength++] = '\n';
		conn->input[conn->inputLength] = '\0';

		for (i = 0; i < nservers; i++) {
			if (conn->servers[i] == NULL)
				continue;

			if (smtpConnPrint(conn, i, (const char *) conn->input) < 0) {
				smtpConnDisconnect(conn, i);
				continue;
			}

			/* We don't wait for SMTP server responses to QUIT
			 * since some SMTP servers just drop the connection
			 * and so no point in waiting for the 221 reply.
			 */
			if (isQuit)
				continue;

			if (smtpConnGetResponse(conn, i, conn->reply, sizeof (conn->reply), &code) != 0) {
				smtpConnDisconnect(conn, i);
			} else if (code == 354) {
				isData++;
			}
		}

		if (isQuit) {
			smtpConnPrint(conn, -1, "221 closing connection\r\n");
			break;
		}

		if (isData && smtpConnData(conn))
			goto error1;

		if (conn->connected <= 0)
			goto error1;

#ifdef HMMM
/* Does not handle multiline replies, in particular EHLO. */
		/* When there is more than one SMTP server, we want to
		 * forward as much the of the client's connection to all
		 * the servers, so we keep telling the client 250.
		 *
		 * When there is only one SMTP server remaining, then we
		 * can forward the server's response to the client.
		 */
		if (conn->connected == 1) {
			length = strlen(conn->reply);
			if (sizeof (conn->reply) <= length+3)
				length = sizeof (conn->reply)-3;
			conn->reply[length++] = '\r';
			conn->reply[length++] = '\n';
			conn->reply[length] = '\0';

			smtpConnPrint(conn, -1, (const char *) conn->reply);
		}

		else
#endif
		if (isEhlo) {
			/* We have to feed a reasonable EHLO response,
			 * because some mail clients will abort if
			 * STARTTLS and AUTH are not supported.
			 */
			smtpConnPrint(conn, -1, ehlo_reply);
		}

		else {
			smtpConnPrint(conn, -1, "250 OK\r\n");
		}
	}
error1:
	for (i = 0; i < nservers; i++)
		smtpConnDisconnect(conn, i);
error0:
	free(conn->servers);
	free(conn);

	syslog(LOG_INFO, "%s end interface=[%s] client=[%s]", session->id_log, session->if_addr, session->address);

	return 0;
}

int
serverMain(void)
{
	Server *smtp;
	int rc, signal;

	rc = EXIT_FAILURE;

	syslog(LOG_INFO, _DISPLAY "/" _VERSION " " _COPYRIGHT);

	if (socket3_init_tls()) {
		syslog(LOG_ERR, "socket3_init_tls() failed");
		goto error0;
	}
	if ((cert_dir != NULL || ca_chain != NULL) && socket3_set_ca_certs(cert_dir, ca_chain)) {
		syslog(LOG_ERR, "socket3_set_ca_certs() failed");
		goto error1;
	}
#ifdef HAVE_OPENSSL_SSL_H
	if (key_crt_pem == NULL) {
		syslog(LOG_WARN, "missing server private key and certificate file; see -k option");
	} else
#endif
	if (socket3_set_cert_key_chain(key_crt_pem, key_pass)) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error1;
	}

	if ((smtp = serverCreate(interfaces, SMTP_PORT)) == NULL)
		goto error1;

	smtp->debug.level = debug;
	smtp->hook.session_process = roundhouse;
	serverSetStackSize(smtp, SERVER_STACK_SIZE);

	if (serverSignalsInit(&signals))
		goto error2;

#if defined(__OpenBSD__) || defined(__FreeBSD__)
	(void) processDumpCore(2);
#endif
	if (processDropPrivilages(user_id, group_id, "/tmp", 0))
		goto error3;
#if defined(__linux__)
	(void) processDumpCore(1);
#endif
	if (serverStart(smtp))
		goto error3;

	syslog(LOG_INFO, "ready");
	signal = serverSignalsLoop(&signals);

	syslog(LOG_INFO, "signal %d, stopping sessions", signal);
	serverStop(smtp, signal == SIGQUIT);
	syslog(LOG_INFO, "signal %d, terminating process", signal);

	rc = EXIT_SUCCESS;
error3:
	serverSignalsFini(&signals);
error2:
	serverFree(smtp);
error1:
	socket3_fini();
error0:
	return rc;
}

void
serverOptions(int argc, char **argv)
{
	int ch, i;

	optind = 1;
	while ((ch = getopt(argc, argv, "Adqvw:u:g:t:i:" GETOPT_TLS)) != -1) {
		switch (ch) {
#ifdef HAVE_OPENSSL_SSL_H
		case 'c':
			ca_chain = optarg;
			break;
		case 'C':
			cert_dir = optarg;
			break;
		case 'k':
			if (optarg == NULL || *optarg == '\0') {
				ehlo_reply = ehlo_basic;
				key_crt_pem = NULL;
			} else {
				ehlo_reply = ehlo_tls;
				key_crt_pem = optarg;
			}
			break;
		case 'K':
			key_pass = optarg;
			break;
#endif
		case 'A':
			connect_all = 1;
			break;
		case 'u':
			/* Unix only. */
			user_id = optarg;
			break;

		case 'g':
			/* Unix only. */
			group_id = optarg;
			break;

		case 't':
			socket_timeout = strtol(optarg, NULL, 10) * 1000;
			break;

		case 'i':
			interfaces = optarg;
			break;

		case 'd':
			daemon_mode = 0;
			break;

		case 'q':
			server_quit++;
			break;

		case 'v':
			debug++;
			break;

		case 'w':
			if (strcmp(optarg, "add") == 0 || strcmp(optarg, "remove") == 0) {
				windows_service = optarg;
				break;
			}
			/*@fallthrough@*/

		default:
			fprintf(stderr, usage_message);
			exit(EX_USAGE);
		}
	}

	if (windows_service != NULL)
		return;

	for (i = nservers; optind < argc && nservers < MAX_ARGV_LENGTH; optind++, i++) {
		smtp_host[i] = argv[optind];

		if ((servers[i] = socketAddressCreate(smtp_host[i], SMTP_PORT)) == NULL) {
			syslog(LOG_ERR, "server address error '%s': %s (%d)", smtp_host[i], strerror(errno), errno);
			exit(1);
		}
	}

	nservers = i;
}

void
loadCf(char *cf)
{
	int ac;
	FILE *fp;
	static char *buffer = NULL;
	char *av[MAX_ARGV_LENGTH+1];

	/* This buffer is not freed until the program exits. */
	if (buffer == NULL && (buffer = malloc(BUFSIZ)) == NULL)
		return;

	/* Load and parse the options file, if present. */
	if ((fp = fopen(cf, "r")) != NULL) {
		if (0 < (ac = fread(buffer, 1, BUFSIZ-1, fp))) {
			buffer[ac] = '\0';
			ac = TokenSplitA(buffer, NULL, av+1, MAX_ARGV_LENGTH)+1;
			serverOptions(ac, av);
		}

		(void) fclose(fp);
	}
}

# ifdef __unix__
/***********************************************************************
 *** Unix Daemon
 ***********************************************************************/

# ifdef HAVE_UNISTD_H
#  include <unistd.h>
# endif

#include <com/snert/lib/sys/pid.h>

void
atExitCleanUp(void)
{
	(void) unlink(PID_FILE);
	closelog();
}

int
main(int argc, char **argv)
{
	loadCf(CF_FILE);
	serverOptions(argc, argv);
	LogSetProgramName(_NAME);

	switch (server_quit) {
	case 1:
		/* Slow quit	-q */
		exit(pidKill(PID_FILE, SIGQUIT) != 0);

	case 2:
		/* Quit now	-q -q */
		exit(pidKill(PID_FILE, SIGTERM) != 0);

	default:
		/* Restart	-q -q -q
		 * Restart-If	-q -q -q -q
		 */
		if (pidKill(PID_FILE, SIGTERM) && 3 < server_quit) {
			fprintf(stderr, "no previous instance running: %s (%d)\n", strerror(errno), errno);
			return EXIT_FAILURE;
		}

		sleep(2);
	}

	if (daemon_mode) {
		pid_t ppid;
		int pid_fd;

		openlog(_NAME, LOG_PID|LOG_NDELAY, LOG_MAIL);
		setlogmask(LOG_UPTO(LOG_DEBUG));

		if ((ppid = fork()) < 0) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_OSERR;
		}

		if (ppid != 0)
			return EXIT_SUCCESS;

		if (setsid() == -1) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_OSERR;
		}

		if (atexit(atExitCleanUp)) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_SOFTWARE;
		}

		if (pidSave(PID_FILE)) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_SOFTWARE;
		}

		if ((pid_fd = pidLock(PID_FILE)) < 0) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_SOFTWARE;
		}
	} else {
		LogOpen("(standard error)");
		LogSetLevel(LOG_UPTO(LOG_DEBUG));
	}

	return serverMain();
}
# endif /* __unix__ */

# ifdef __WIN32__

#  include <com/snert/lib/sys/winService.h>

/***********************************************************************
 *** Windows Logging
 ***********************************************************************/

static HANDLE eventLog;

void
ReportInit(void)
{
	eventLog = RegisterEventSource(NULL, _NAME);
}

void
ReportLogV(int type, char *fmt, va_list args)
{
	LPCTSTR strings[1];
	char message[1024];

	strings[0] = message;
	(void) vsnprintf(message, sizeof (message), fmt, args);

	ReportEvent(
		eventLog,	// handle of event source
		type,		// event type
		0,		// event category
		0,		// event ID
		NULL,		// current user's SID
		1,		// strings in lpszStrings
		0,		// no bytes of raw data
		strings,	// array of error strings
		NULL		// no raw data
	);
}

void
ReportLog(int type, char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	ReportLogV(type, fmt, args);
	va_end(args);
}

static DWORD strerror_tls = TLS_OUT_OF_INDEXES;
static const char unknown_error[] = "(unknown error)";

char *
strerror(int error_code)
{
	char *error_string;

	if (strerror_tls == TLS_OUT_OF_INDEXES) {
		strerror_tls = TlsAlloc();
		if (strerror_tls == TLS_OUT_OF_INDEXES)
			return (char *) unknown_error;
	}

	error_string = (char *) TlsGetValue(strerror_tls);
	LocalFree(error_string);

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, error_code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR) &error_string, 0, NULL
	);

	if (!TlsSetValue(strerror_tls, error_string)) {
		LocalFree(error_string);
		return (char *) unknown_error;
	}

	return error_string;
}

void
freeThreadData(void)
{
	if (strerror_tls != TLS_OUT_OF_INDEXES) {
		char *error_string = (char *) TlsGetValue(strerror_tls);
		LocalFree(error_string);
	}
}

/***********************************************************************
 *** Windows Service
 ***********************************************************************/

#define QUIT_EVENT_NAME		"Global\\" _NAME "-quit"

int
main(int argc, char **argv)
{
	/* Get this now so we can use the event log. */
	ReportInit();

	loadCf(CF_FILE);
	serverOptions(argc, argv);

	if (server_quit) {
		HANDLE signal_quit = OpenEvent(EVENT_MODIFY_STATE , 0, QUIT_EVENT_NAME);
		if (signal_quit == NULL) {
			ReportLog(EVENTLOG_ERROR_TYPE, "service %s quit error: %s (%d)", _NAME, strerror(errno), errno);
			exit(EX_OSERR);
		}

		SetEvent(signal_quit);
		CloseHandle(signal_quit);
		exit(EXIT_SUCCESS);
	}

	if (windows_service != NULL) {
		if (winServiceInstall(*windows_service == 'a', _NAME, NULL) < 0) {
			ReportLog(EVENTLOG_ERROR_TYPE, "service %s %s error: %s (%d)", _NAME, windows_service, strerror(errno), errno);
			return EX_OSERR;
		}
		return EXIT_SUCCESS;
	}

	if (daemon_mode) {
		winServiceSetSignals(&signals);
		if (winServiceStart(_NAME, argc, argv) < 0) {
			ReportLog(EVENTLOG_ERROR_TYPE, "service %s start error: %s (%d)", _NAME, strerror(errno), errno);
			return EX_OSERR;
		}
		return EXIT_SUCCESS;
	}

	return serverMain();
}

# endif /* __WIN32__ */
