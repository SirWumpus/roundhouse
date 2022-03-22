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
 * 	roundhouse [-v][-p port] server ...
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

static const char log_oom[] = "out of memory %s(%d)";
static const char log_init[] = "init error %s(%d): %s (%d)";
static const char log_internal[] = "internal error %s(%d): %s (%d)";

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

typedef struct {
	char *id;
	int nservers;
	Socket2 *client;
	Socket2 **servers;
	long inputLength;
	char input[SMTP_TEXT_LINE_LENGTH+1];
	char reply[SMTP_REPLY_LINE_LENGTH+1];
	char client_addr[IPV6_STRING_LENGTH];
} Connection;

typedef struct {
	Connection *conn;
	Socket2 *source;
	Socket2 *sink;
} Stream;

static int debug;
static int server_quit;
static int daemon_mode = 1;
static char *user_id = NULL;
static char *group_id = NULL;
static char *windows_service;
static char *interfaces = "[::0]:" QUOTE(SMTP_PORT) ",0.0.0.0:" QUOTE(SMTP_PORT);
static long socket_timeout = SOCKET_TIMEOUT;

static int nservers;
static char *smtp_host[MAX_ARGV_LENGTH];
static SocketAddress *servers[MAX_ARGV_LENGTH];

static ServerSignals signals;

static char *usage_message =
"usage: " _NAME " [-dqv][-i ip,...][-t timeout] \\\n"
"       [-u name][-g name][-w add|remove] server ...\n"
"\n"
"-d\t\tdisable daemon mode and run as a foreground application\n"
"-g name\t\trun as this group\n"
"-i ip,...\tcomma separated list of IPv4 or IPv6 addresses and\n"
"\t\toptional :port number to listen on for SMTP connections;\n"
"\t\tdefault is \"[::0]:25,0.0.0.0:25\"\n"
"-q\t\tx1 slow quit, x2 quit now, x3 restart, x4 restart-if\n"
"-t timeout\tclient socket timeout in seconds, default 300\n"
"-u name\t\trun as this user\n"
"-v\t\tverbose maillog output\n"
"-w add|remove\tadd or remove Windows service\n"
"\n"
"server\t\ta unix domain socket path or host[:port] specifier. The\n"
"\t\tfirst server specified is the primary host that can handle\n"
"\t\tSMTP STARTTLS.\n"
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
reportAccept(ServerSession *session)
{
	syslog(LOG_INFO, "%s start interface=[%s] client=[%s]", session->id_log, session->if_addr, session->address);
	return 0;
}

int
reportFinish(ServerSession *session)
{
	syslog(LOG_INFO, "%s end interface=[%s] client=[%s]", session->id_log, session->if_addr, session->address);
	return 0;
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
smtpConnPrint(Connection *conn, int index, char *line)
{
	if (index < 0)
		/* Sending to the client. */
		syslog(LOG_DEBUG, LOG_FMT "< %s", LOG_ARG, line);

	return socketWrite(index < 0 ? conn->client : conn->servers[index], (unsigned char *) line, strlen(line));
}

static int
smtpConnGetResponse(Connection *conn, int index, char *line, long size, int *code)
{
	Socket2 *s;
	char *stop;
	long length, value;

	if (conn == NULL || line == NULL)
		return EFAULT;

	/* Ideally we should collect _all_ the response lines into a variable
	 * length buffer (see com/snert/lib/util/Buf.h), but I don't see any
	 * real need for it just yet.
	 */

	value = 450;

	s = conn->servers[index];
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
		conn->nservers--;
	}
}

void *
proxyStream(void *data)
{
	Stream *stream = (Stream *) data;
	Connection *conn = stream->conn;

	(void) pthread_detach(pthread_self());

	while (socketHasInput(stream->source, socket_timeout)) {
		if (socketRead(stream->source, (unsigned char*)conn->input, sizeof (conn->input)) < 0) {
			syslog(LOG_ERR, LOG_FMT "proxyStream() read error: %s (%d)", LOG_ARG, strerror(errno), errno);
			break;
		}

		if (socketWrite(stream->sink, (unsigned char*)conn->input, sizeof (conn->input)) < 0) {
			syslog(LOG_ERR, LOG_FMT "proxyStream() write error: %s (%d)", LOG_ARG, strerror(errno), errno);
			break;
		}
	}

	free(stream);

	return NULL;
}

int
proxyTLS(Connection *conn)
{
	int i, code;
	pthread_t thread;
	Stream *c2s, *s2c;

	if ((s2c = malloc(sizeof (*s2c))) == NULL)
		goto error0;

	s2c->source = conn->servers[0];
	s2c->sink = conn->client;
	s2c->conn = conn;

	if ((c2s = malloc(sizeof (*c2s))) == NULL)
		goto error1;

	c2s->source = conn->client;
	c2s->sink = conn->servers[0];
	c2s->conn = conn;

	if (conn->servers[0] == NULL)
		goto error2;

	if (smtpConnPrint(conn, 0, "STARTTLS\r\n") < 0)
		goto error2;

	if (smtpConnGetResponse(conn, 0, conn->reply, sizeof (conn->reply), &code) || code != 220)
		goto error2;

	if (pthread_create(&thread, NULL, proxyStream, s2c))
		goto error2;

	smtpConnPrint(conn, -1, "220 Ready to start TLS\r\n");

	/* Close the other SMTP servers that we can't multiplex with. */
	for (i = 1; i < nservers; i++)
		smtpConnDisconnect(conn, i);

	(void) proxyStream(c2s);

	return 0;
error2:
	free(c2s);
error1:
	free(s2c);
error0:
	smtpConnPrint(conn, -1, "454 TLS not available\r\n");

	return -1;
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
	long length;
	int i, code, isDot, serversRemaining;

	smtpConnPrint(conn, -1, "354 enter mail, end with \".\" on a line by itself\r\n");

	/* Relay client's message to each SMTP server in turn. */
	for (isDot = 0; !isDot && socketHasInput(conn->client, socket_timeout); ) {
		if ((length = socketReadLine(conn->client, conn->input, sizeof (conn->input))) < 0) {
			syslog(LOG_ERR, LOG_FMT "client read error during message: %s (%d)%c", LOG_ARG, strerror(errno), errno, length == SOCKET_EOF ? '!' : ' ');
			return -1;
		}

		syslog(LOG_DEBUG, LOG_FMT "> %s", LOG_ARG, conn->input);

		isDot = conn->input[0] == '.' && conn->input[1] == '\0';

		/* Add back the CRLF removed by socketReadLine(). */
		conn->input[length++] = '\r';
		conn->input[length++] = '\n';
		conn->input[length] = '\0';

		serversRemaining = conn->nservers;
		for (i = 0; i < nservers; i++) {
			if (conn->servers[i] == NULL)
				continue;

			if (smtpConnPrint(conn, i, conn->input) < 0) {
				smtpConnDisconnect(conn, i);
				continue;
			}

			if (isDot) {
				if (smtpConnGetResponse(conn, i, conn->reply, sizeof (conn->reply), &code) != 0) {
					smtpConnDisconnect(conn, i);
					continue;
				}

				/* When a SMTP server rejects the client's
				 * most recent command, then close the
				 * session with that server, since we can't
				 * report N differnet SMTP replies to the
				 * client.
				 */
				if (1 < serversRemaining && 400 <= code) {
					smtpConnPrint(conn, i, "QUIT\r\n");
					smtpConnDisconnect(conn, i);
					continue;
				}
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
	int i, code, isQuit, isData, isEhlo, serversRemaining;

	if ((conn = calloc(1, sizeof (*conn))) == NULL)
		return -1;

	session->data = conn;
	conn->id = session->id_log;
	conn->client = session->client;

	(void) socketSetNagle(conn->client, 0);
	(void) socketSetLinger(conn->client, 0);
	(void) socketSetNonBlocking(conn->client, 1);

	if ((conn->servers = calloc(nservers, sizeof (SOCKET))) == NULL) {
		syslog(LOG_ERR, LOG_FMT "out of memory", LOG_ARG);
		goto error0;
	}

	/* Connect to all the SMTP servers. */
	conn->nservers = 0;
	for (i = 0; i < nservers; i++) {
		if ((conn->servers[i] = socketOpen(servers[i], 1)) == NULL)
			continue;

		conn->nservers++;
		syslog(LOG_DEBUG, LOG_FMT "#%d connecting to %s", LOG_ARG, i, smtp_host[i]);

		if (socketClient(conn->servers[i], 0)) {
			syslog(LOG_ERR, LOG_FMT "#%d connection to %s failed", LOG_ARG, i, smtp_host[i]);
			smtpConnDisconnect(conn, i);
			continue;
		}

		(void) socketSetNonBlocking(conn->servers[i], 1);

		if (smtpConnGetResponse(conn, i, conn->reply, sizeof (conn->reply), &code) || code != 220) {
			syslog(LOG_ERR, LOG_FMT "#%d no welcome from %s", LOG_ARG, i, smtp_host[i]);
			smtpConnDisconnect(conn, i);
			continue;
		}
	}

	if (conn->nservers <= 0) {
		smtpConnPrint(conn, -1, "421 service temporarily unavailable\r\n");
		syslog(LOG_ERR, LOG_FMT "no answer from any SMTP server", LOG_ARG);
		goto error0;
	}

	smtpConnPrint(conn, -1, "220 Welcome to " _DISPLAY "/" _VERSION "\r\n");

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

		/* We cannot multiplex STARTTLS command at this time, so
		 * we only deliver it to the primary mail server (first
		 * one specified).
		 *
		 * If there was an error setting up the proxy, we should
		 * be able to continue to multiplex a non-TLS connection;
		 * otherwise we're done.
		 */
		if (0 < TextInsensitiveStartsWith(conn->input, "STARTTLS")) {
			if (proxyTLS(conn))
				continue;
			break;
		}

		if (0 < TextInsensitiveStartsWith(conn->input, "AUTH LOGIN") && authLogin(conn))
			break;

		isEhlo = 0 < TextInsensitiveStartsWith(conn->input, "EHLO");
		isQuit = 0 < TextInsensitiveStartsWith(conn->input, "QUIT");
		isData = 0;

		/* Add back the CRLF removed by socketReadLine(). */
		if (sizeof (conn->input) <= conn->inputLength+3)
			conn->inputLength = sizeof (conn->input)-3;
		conn->input[conn->inputLength++] = '\r';
		conn->input[conn->inputLength++] = '\n';
		conn->input[conn->inputLength] = '\0';

		serversRemaining = conn->nservers;
		for (i = 0; i < nservers; i++) {
			if (conn->servers[i] == NULL)
				continue;

			if (smtpConnPrint(conn, i, conn->input) < 0) {
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
			}

			/* When a SMTP server rejects the client's
			 * most recent command, then close the
			 * session with that server, since we can't
			 * report N differnet SMTP replies to the
			 * client.
			 */
			else if (1 < serversRemaining && 400 <= code) {
				smtpConnPrint(conn, i, "QUIT\r\n");
				smtpConnDisconnect(conn, i);
			}

			else if (code == 354)
				isData++;
		}

		if (isQuit) {
			smtpConnPrint(conn, -1, "221 closing connection\r\n");
			break;
		}

		if (isData && smtpConnData(conn))
			goto error1;

		if (conn->nservers <= 0)
			goto error1;

		/* When there is more than one SMTP server, we want to
		 * forward as much the of the client's connection to all
		 * the servers, so we keep telling the client 250.
		 *
		 * When there is only one SMTP server remaining, then we
		 * can forward the server's response to the client.
		 */
		if (serversRemaining == 1) {
			length = strlen(conn->reply);
			if (sizeof (conn->reply) <= length+3)
				length = sizeof (conn->reply)-3;
			conn->reply[length++] = '\r';
			conn->reply[length++] = '\n';
			conn->reply[length] = '\0';

			smtpConnPrint(conn, -1, conn->reply);
		}

		else if (isEhlo) {
			/* We have to feed a reasonable EHLO response,
			 * because some mail clients will abort if
			 * STARTTLS and AUTH are not supported.
			 */
			smtpConnPrint(conn, -1, "250-AUTH " AUTH_MECHANISMS "\r\n250-PIPELINING\r\n250 STARTTLS\r\n");
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

	return reportFinish(session);
}

int
serverMain(void)
{
	Server *smtp;
	int rc, signal;

	rc = EXIT_FAILURE;

	syslog(LOG_INFO, _DISPLAY "/" _VERSION " " _COPYRIGHT);

	if ((smtp = serverCreate(interfaces, SMTP_PORT)) == NULL)
		goto error0;

	smtp->debug.level = debug;
	smtp->hook.session_accept = reportAccept;
	smtp->hook.session_process = roundhouse;
	serverSetStackSize(smtp, SERVER_STACK_SIZE);

	if (serverSignalsInit(&signals))
		goto error1;

#if defined(__OpenBSD__) || defined(__FreeBSD__)
	(void) processDumpCore(2);
#endif
	if (processDropPrivilages(user_id, group_id, "/tmp", 0))
		goto error2;
#if defined(__linux__)
	(void) processDumpCore(1);
#endif
	if (serverStart(smtp))
		goto error2;

	syslog(LOG_INFO, "ready");
	signal = serverSignalsLoop(&signals);

	syslog(LOG_INFO, "signal %d, stopping sessions", signal);
	serverStop(smtp, signal == SIGQUIT);
	syslog(LOG_INFO, "signal %d, terminating process", signal);

	rc = EXIT_SUCCESS;
error2:
	serverSignalsFini(&signals);
error1:
	serverFree(smtp);
error0:
	return rc;
}

void
serverOptions(int argc, char **argv)
{
	int ch, i;

	optind = 1;
	while ((ch = getopt(argc, argv, "dqvw:u:g:t:i:")) != -1) {
		switch (ch) {
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

		openlog(_NAME, LOG_PID|LOG_NDELAY, LOG_USER);
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






#ifdef OLD

#if defined(__WIN32__)
/***********************************************************************
 *** Windows Service Framework
 ***********************************************************************/

static HANDLE eventLog;
static char *server_root = NULL;

void
ReportLogV(int type, char *fmt, va_list args)
{
	LPCTSTR strings[1];
	char message[1024];

	strings[0] = message;
	vsnprintf(message, sizeof (message), fmt, args);

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

#ifdef NOT_USED

#undef syslog
#undef vsyslog

void
vsyslog(int level, char *fmt, va_list args)
{
	int type = EVENTLOG_INFORMATION_TYPE;

	if (LOG_ERR <= level)
		type = EVENTLOG_ERROR_TYPE;

	ReportLogV(type, fmt, args);
	LogV(level, fmt, args);
}

void
syslog(int level, char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsyslog(level, fmt, args);
	va_end(args);
}
#endif

/*
 * Called from a different thread.
 *
 * @return
 *	If the function handles the control signal, it should return TRUE.
 *	If it returns FALSE, the next handler function in the list of handlers
 *	for this process is used.
 */
BOOL WINAPI
HandlerRoutine(DWORD ctrl)
{
	switch (ctrl) {
	case CTRL_SHUTDOWN_EVENT:
		return FALSE;
	case CTRL_LOGOFF_EVENT:
		return TRUE;
#ifdef NDEBUG
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
		return TRUE;
#else
		;
#endif
	}

	return FALSE;
}

static SERVICE_STATUS_HANDLE ServiceStatus;

/*
 * Called from a different thread.
 */
DWORD WINAPI
ServiceStop(LPVOID ignore)
{
	SERVICE_STATUS status;

	ReportLog(EVENTLOG_INFORMATION_TYPE, "stopping service");

	status.dwCheckPoint = 0;
	status.dwWaitHint = 2000;
	status.dwWin32ExitCode = NO_ERROR;
	status.dwServiceSpecificExitCode = 0;
	status.dwCurrentState = SERVICE_STOPPED;
	status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	status.dwControlsAccepted = SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN;
	SetServiceStatus(ServiceStatus, &status);

	/* Don't know where application is spinning, but if its
	 * important they should have registered one or more
	 * shutdown hooks. Begin normal exit sequence. We will
	 * end up in our ExitHandler() when the application has
	 * finished.
	 */
	signalExit(SIGTERM);

	return 0;
}

/*
 * Called from within Service Control Manager distpatch thread.
 */
DWORD WINAPI
ServiceControl(DWORD code, DWORD eventType, LPVOID eventData, LPVOID userData)
{
	HANDLE stopThread;

	switch (code) {
	case SERVICE_CONTROL_STOP:
	case SERVICE_CONTROL_SHUTDOWN:
		/* Stop the service in another thread allows us to
		 * successful return to the Windows service manager
		 * and stop the service. Otherwise Windows ends up
		 * waiting for us to return from here, which would
		 * not happen since stopping the service terminates
		 * the program.
		 */
		stopThread = CreateThread(NULL, 4096, ServiceStop, NULL, 0, NULL);
		if (stopThread == NULL) {
			ReportLog(EVENTLOG_ERROR_TYPE, "failed to stop %s service", _NAME);
			return GetLastError();
		}
		break;
	default:
		return ERROR_CALL_NOT_IMPLEMENTED;
	}

	return NO_ERROR;
}

VOID WINAPI
ServiceMain(DWORD argc, char **argv)
{
	SERVICE_STATUS status;

	/* Parse options passed from the Windows Service properties dialog. */
	options(argc, argv);

	ServiceStatus = RegisterServiceCtrlHandlerEx(_NAME, ServiceControl, NULL);
	if (ServiceStatus == 0) {
		ReportLog(EVENTLOG_ERROR_TYPE, "failed to register %s service control handler: %lu", _NAME, GetLastError());
		exit(1);
	}

	(void) SetConsoleCtrlHandler(HandlerRoutine, TRUE);

	status.dwCheckPoint = 0;
	status.dwWaitHint = 2000;
	status.dwWin32ExitCode = NO_ERROR;
	status.dwServiceSpecificExitCode = 0;
	status.dwCurrentState = SERVICE_RUNNING;
	status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	status.dwControlsAccepted = SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN;
	SetServiceStatus(ServiceStatus, &status);

	ReportLog(EVENTLOG_INFORMATION_TYPE, "service running");

	ServerMain();
}

int WINAPI
WinMain(HINSTANCE me, HINSTANCE other, LPSTR cmdline, int wstate)
{
	int argc;
	long length;
	static char default_root[256];
	static char *argv[MAX_ARGV_LENGTH+1];
	char *cwd, *backslash, *install_path;

	SC_HANDLE manager;
	SC_HANDLE service;
	SERVICE_DESCRIPTION brief = { _BRIEF };
	SERVICE_TABLE_ENTRY dispatchTable[] = {
		{ _NAME, ServiceMain },
		{ NULL, NULL }
	};

	/* Get this now so we can use the event log, via appLog(). */
	eventLog = RegisterEventSource(NULL, _NAME);

	if ((argc = TokenSplitA(cmdline, NULL, argv+1, MAX_ARGV_LENGTH)) == -1) {
		ReportLog(EVENTLOG_ERROR_TYPE, "command-line split failed");
		return 1;
	}

	argv[0] = _NAME;
	argc++;

	/* Get the absolute path of this executable and set the working
	 * directory to correspond to it so that we can find the options
	 * configuration file along side the executable, when running as
	 * a service.
	 */
	if ((length = GetModuleFileName(NULL, default_root, sizeof default_root)) == 0 || length == sizeof default_root) {
		ReportLog(EVENTLOG_ERROR_TYPE, "failed to find default server root");
		return 1;
	}

	/* Remember the full path of the executable. */
	install_path = strdup(default_root);

	/* Strip off the executable filename, leaving its parent directory. */
	for (backslash = default_root+length; default_root < backslash && *backslash != '\\'; backslash--)
		;

	server_root = default_root;
	*backslash = '\0';

	/* Remember where we are in case we are running in application mode. */
	cwd = getcwd(NULL, 0);

	/* Change to the executable's directory for default configuration file. */
	if (chdir(server_root)) {
		ReportLog(EVENTLOG_ERROR_TYPE, "failed to change directory to '%s': %s (%d)\n", server_root, strerror(errno), errno);
		return 1;
	}

	if (socketInit()) {
		ReportLog(EVENTLOG_ERROR_TYPE, "socket initialisation error (%u)", GetLastError());
		exit(1);
	}

	loadCf(CF_FILE);
	options(argc, argv);

	manager = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);
	if (manager == NULL) {
		ReportLog(EVENTLOG_ERROR_TYPE, "cannot open service manager");
		return 1;
	}

	service = OpenService(manager, _NAME, SERVICE_ALL_ACCESS);

	if (install_service) {
		if (service == NULL) {
			if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
				service = CreateService(
					manager,			// SCManager database
					_NAME,				// name of service
					_DISPLAY,			// name to display
					SERVICE_ALL_ACCESS,		// desired access
					SERVICE_WIN32_OWN_PROCESS,	// service type
					SERVICE_AUTO_START,		// start type
					SERVICE_ERROR_NORMAL,		// error control type
					install_path,			// service's binary
					NULL,				// no load ordering group
					NULL,				// no tag identifier
					"Tcpip\0\0",			// dependencies
					NULL,				// LocalSystem account
					NULL				// no password
				);
				if (service == NULL) {
					MessageBox(NULL, "Failed to install service " _NAME, "Error", MB_OK|MB_ICONERROR);
					return 1;
				}

				(void) ChangeServiceConfig2(service, SERVICE_CONFIG_DESCRIPTION, &brief);

				MessageBox(NULL, _NAME " service installed.", _NAME, MB_OK|MB_ICONINFORMATION);
			} else {
				MessageBox(NULL, "Failed to find service.", _NAME, MB_OK|MB_ICONERROR);
			}
		} else {
			if (DeleteService(service) == 0) {
				MessageBox(NULL, "Failed to remove service " _NAME, "Error", MB_OK|MB_ICONERROR);
				return 1;
			}

			MessageBox(NULL, _NAME " service removed.", _NAME, MB_OK|MB_ICONINFORMATION);
		}

		return 0;
	}

	if (nservers <= 0) {
		ReportLog(EVENTLOG_ERROR_TYPE, "missing server arguments");
		exit(2);
	}

	if (application_mode) {
		if (service != NULL)
			(void) CloseServiceHandle(service);
		if (manager != NULL)
			(void) CloseServiceHandle(manager);

		if (cwd != NULL) {
			(void) chdir(cwd);
			free(cwd);
		}

		/* AllocConsole() only works for Windows GUI applications. */
		if (AllocConsole())
			LogOpen("CONOUT$");
		else
			LogOpen("(standard error)");

		ServerMain();
	} else if (service == NULL) {
		ReportLog(EVENTLOG_ERROR_TYPE, "Cannot start %s service. See -a and -w options.", _NAME);
	} else {
		/* Delay opening the log until we know that were starting
		 * the Windows service. Otherwise, when we start as an
		 * application we get an empty roundhouse.log file created.
		 */
		openlog(_NAME, LOG_PID, LOG_MAIL);

		if (!StartServiceCtrlDispatcher(dispatchTable)) {
			if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
				if (StartService(service, argc-1, (LPCTSTR *) argv+1))
					return 0;
			}

			ReportLog(EVENTLOG_ERROR_TYPE, "Failed to start %s service.", _NAME);
		}
	}

	return 0;
}
#endif /* __WIN32__ */

#ifdef __unix__
/***********************************************************************
 *** Unix Daemon
 ***********************************************************************/

int
main(int argc, char **argv)
{
	if (socketInit())
		exit(1);

	openlog(_NAME, LOG_PID, LOG_MAIL);
	loadCf(CF_FILE);
	options(argc, argv);

	if (nservers <= 0) {
		syslog(LOG_ERR, "missing server arguments");
		(void) fprintf(stderr, usage_message);
		exit(2);
	}

	if (application_mode) {
		closelog();
		LogOpen("(standard error)");
	} else {
		pid_t ppid;

		if ((ppid = fork()) < 0) {
			syslog(LOG_ERR, "process fork failed: %s (%d)", strerror(errno), errno);
			exit(1);
		}

		if (ppid != 0)
			exit(0);

		if (setsid() == -1) {
			syslog(LOG_ERR, "set process group ID failed: %s (%d)", strerror(errno), errno);
			exit(1);
		}
	}

	if ((ruid = getuid()) == 0) {
		gid_t gid = getgid();
		struct group *gr = NULL;
		struct passwd *pw = NULL;

		/* We have to create the .pid file after we become a daemon process
		 * but before we change process ownership, particularly if we intend
		 * to create a file in /var/run, which is owned and writeable by root.
		 */
		if (savePid(PID_FILE)) {
			syslog(LOG_ERR, "create \"%s\" failed: %s (%d)", PID_FILE, strerror(errno), errno);
			exit(1);
		}

# if defined(HAVE_CHROOT)
		if (chroot(EMPTY_DIR)) {
			syslog(LOG_ERR, "chroot(%s) failed: %s (%d)", EMPTY_DIR, strerror(errno), errno);
			exit(1);
		}
# endif
		if (group_id != NULL) {
			if ((gr = getgrnam(group_id)) == NULL) {
				syslog(LOG_ERR, "group \"%s\" not found", group_id);
				exit(1);
			}

			gid = gr->gr_gid;

			/* Drop group privileges permanently. */
# if defined(HAVE_SETRESGID)
			(void) setresgid(gid, gid, gid);
# elif defined(HAVE_SETREGID)
			(void) setregid(gid, gid);
# else
			(void) setgid(gid);
# endif
		}

		if (user_id != NULL) {
			if ((pw = getpwnam(user_id)) == NULL) {
				syslog(LOG_ERR, "user \"%s\" not found", user_id);
				exit(1);
			}

# if defined(HAVE_INITGROUPS)
			/* Make sure to set any supplemental groups  for the new
			 * user ID, which will release root's supplemental groups.
			 */
			if (initgroups(user_id, gid)) {
				syslog(LOG_ERR, "supplemental groups for \"%s\" not set", user_id);
				exit(1);
			}
# endif
			/* We drop root privilages in ServerMain() after binding
			 * to the SMTP port.
			 */
			euid = pw != NULL ? pw->pw_uid : geteuid();
		}

# if defined(HAVE_SETGROUPS)
		else if (setgroups(0, NULL)) {
			syslog(LOG_ERR, "failed to release root's supplemental groups");
			exit(1);
		}
# endif
	}

	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		syslog(LOG_ERR, "failed to set SIGPIPE handler: %s (%d)", strerror(errno), errno);
		exit(1);
	}

	if (signal(SIGTERM, signalExit) == SIG_ERR) {
		syslog(LOG_ERR, "failed to set SIGTERM handler: %s (%d)", strerror(errno), errno);
		exit(1);
	}

	if (signal(SIGINT, signalExit) == SIG_ERR) {
		syslog(LOG_ERR, "failed to set SIGINT handler: %s (%d)", strerror(errno), errno);
		exit(1);
	}

	ServerMain();

	return 0;
}
#endif /* __unix__ */

#endif /* OLD */
