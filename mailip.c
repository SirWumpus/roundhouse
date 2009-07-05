/*
 * mailip.c/0.1
 *
 * Copyright 2005 by Anthony Howe. All rights reserved.
 *
 *
 * Description
 * -----------
 *
 * 	mailip [-w][-d delay][-s smtp][-t timeout] mailto
 *
 * This program attempts to discover the dynamic IP address of the network
 * it operates from and sends an email to the given address when ever it
 * changes.
 *
 *
 * Build for Unix using GCC
 * ------------------------
 *
 *	gcc -02 -o -Icom/snert/include mailip mailip.c -lsnert
 *
 *
 * Build for Windows using GCC
 * ---------------------------
 *
 *	gcc -DNDEBUG -02 -Icom/snert/include -mno-cygwin -mwindows -o mailip mailip.c -lsnert -lws2_32
 */

/***********************************************************************
 *** Leave this header alone. Its generated from the configure script.
 ***********************************************************************/

#include "config.h"

/***********************************************************************
 *** You can change the stuff below if the configure script doesn't work.
 ***********************************************************************/

#ifndef EMPTY_DIR
#define EMPTY_DIR		"/var/empty"
#endif

#ifndef CF_FILE
#define CF_FILE			"/etc/" _NAME ".cf"
#endif

#ifndef PID_FILE
#define PID_FILE		"/var/run/" _NAME ".pid"
#endif

#ifndef UPDATE_INTERVAL
#define UPDATE_INTERVAL		900
#endif

#ifndef SOCKET_TIMEOUT
#define SOCKET_TIMEOUT		300000
#endif

#ifndef MAX_ARGV_LENGTH
#define MAX_ARGV_LENGTH		20
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <signal.h>
#include <stdlib.h>

#ifdef HAVE_GRP_H
# include <grp.h>
#endif
#ifdef HAVE_PWD_H
# include <pwd.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/io/Dns.h>
#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/mail/limits.h>
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/Token.h>
#include <com/snert/lib/util/getopt.h>

/***
 *** This socket API will eventually repalce the older com/snert/lib/Socket.h.
 ***/
#include "socket.h"

/***********************************************************************
 *** Constants
 ***********************************************************************/

#define _NAME			"mailip"
#define _DISPLAY		"MailIP"
#define _BRIEF			"IP Discovery Mailer"
#define _VERSION		"0.1"


#define SMTP_REPLY_BUFSIZ	(5 * SMTP_REPLY_LINE_LENGTH)

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

typedef struct {
	unsigned id;
	int nservers;
	Socket *client;
	Socket **servers;
	char input[SMTP_TEXT_LINE_LENGTH+1];
	char reply[SMTP_REPLY_LINE_LENGTH+1];
	char client_addr[IPV6_STRING_LENGTH];
} Connection;

static int debug;
static int install_service;
static int application_mode;
static char *user_id = NULL;
static char *group_id = NULL;
static char *server_root = NULL;
static char *smtp_server = NULL;
static char hostname[SMTP_DOMAIN_LENGTH];
static sigset_t sigpipe_set;

static long socketTimeout = SOCKET_TIMEOUT;
static long updateInterval = UPDATE_INTERVAL;

static char *usageMessage =
"usage: " _NAME " [-avw][-d delay][-g name][-s smtp][-t timeout][-u name] mailto\n"
"\n"
"-a\t\trun as a foreground application, default run in background\n"
"-d delay\t\tnumber of minutes between updates, default 15\n"
"-g name\t\trun as this group (Unix)\n"
"-s smtp\tsend mail via this SMTP server, default uses MX of mailto\n"
"-t timeout\tclient socket timeout in seconds, default 300\n"
"-u name\t\trun as this user (Unix)\n"
"-v\t\tverbose maillog output\n"
"-w\t\ttoggle add/remove Windows service.\n"
"\n"
"mailto\t\tan internet mail address\n"
"\n"
_DISPLAY "/" _VERSION " Copyright 2005 by Anthony Howe. All rights reserved.\n"
;

static char *ip_reflectors[] = {
/*
	"http://www.whatismyip.com/",
	"http://hibachi.snert.org:8008/test/C/myip/",
*/
	"aol.com",
	"hotmail.com",
	"ovh.net",
	"earthlink.net",
	"sourceforge.net",
	"sendmail.org",
	NULL
};

/***********************************************************************
 *** Routines
 ***********************************************************************/

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

void
signalExit(int signum)
{
	signal(signum, SIG_IGN);
	syslog(LOG_INFO, "signal %d received, program exit", getpid(), signum);
	exit(0);
}

static long
smtpGetResponse(Socket *s, char *buffer, long size, int *code)
{
	int isSingleLineResponse;
	long length, nbytes, value;

	*code = 0;

	if (size < 1)
		return 0;

	*buffer ='\0';

	for (length = 0; length < size - 1; ) {
		if (!socketHasInput(s, socketTimeout))
			break;

		if ((nbytes = socketReadLine(s, buffer+length, size-length)) < 0)
			return 0;

		syslog(LOG_DEBUG, "< %s",  buffer+length);

		/* Did we read sufficient characters for a response code? */
		if (nbytes < 4) {
			errno = EIO;
			return 0;
		}

		isSingleLineResponse = buffer[length+3] != '-';
		length += nbytes;

		/* Add back a newline separator just in case. */
		buffer[length++] = '\n';
		buffer[length] = '\0';

		if (isSingleLineResponse)
			break;
	}

	*code = strtol(buffer, NULL, 10);

	return length;
}

char *
smtpFindIpInResponse(char *buffer)
{
	int span;
	char *p, *ip, ipv6[IPV6_BYTE_LENGTH];

	if ((ip = malloc(IPV6_STRING_LENGTH)) == NULL)
		goto error0;

	/* AOL.com now blocks port 25 from dynamic IP addresses and
	 * returns a response like:
	 *
	 * 554- (RTR:DU)  http://postmaster.info.aol.com/errors/554rtrdu.html
	 * 554- AOL does not accept e-mail transactions from dynamic or residential
	 * 554- IP addresses.
	 * 554  Connecting IP: 213.36.193.81
	 */
	if ((p = strstr(buffer, "Connecting IP: ")) != NULL) {
		p += sizeof ("Connecting IP: ")-1;
		if ((span = parseIPv6(p, ipv6)) <= 0)
			goto error1;

		strncpy(ip, p, span);
		ip[span] = '\0';

		return ip;
	}

	/* ovh.net returns the IP in the HELO response:
	 *
	 * 250 You are: 213.36.193.81:dyn-213-36-193-81.ppp.tiscali.fr
	 */
	if ((p = strstr(buffer, "You are: ")) != NULL) {
		p += sizeof ("You are: ")-1;
		if ((span = parseIPv6(p, ipv6)) <= 0)
			goto error1;

		strncpy(ip, p, span);
		ip[span] = '\0';

		return ip;
	}

	/* hotmail.com, earthlink.net, sendmail 8 servers (sendmail.org), and
	 * Exim servers (sourceforge.net) return the IP address between square
	 * brackets in the HELO response.
	 */
	if (strchr(p, '[')) != NULL) {
		span = strcspn(++p, "]");
		if (p[span] != ']' || (span = parseIPv6(p, ipv6)) <= 0)
			goto error1;

		strncpy(ip, p, span);
		ip[span] = '\0';

		return ip;
	}
error1:
	free(ip);
error0:
	return NULL;
}

char *
smtpWhoAmI(char **domains, char **myip)
{
	long i;
	Dns dns;
	int code;
	char *buffer;
	DnsEntry *mx;
	Vector mxList;
	Socket *server;
	ParsePath *path;
	SocketAddress *addr;
	const char *error = NULL;

	if (domain == NULL || myip == NULL)
		goto error0;

	if ((buffer = malloc(SMTP_REPLY_BUFSIZ)) == NULL)
		goto error1;

	/* For each domain, look their MXes and query the first one. */
	for (domain = domains; *domain != NULL; domain++) {
		/* Get the MX list for the domain. */
		if ((error = parsePath(domain, STRICT_LENGTH, 0, &path)) != NULL)
			goto error2;

		if ((dns = DnsOpen()) == NULL) {
			error = DnsGetError(dns);
			goto error3;
		}

		if ((mxList = DnsGet(dns, DNS_TYPE_MX, 1, path->domain.string)) == NULL) {
			error = DnsGetError(dns);
			goto error4;
		}

		if ((mx = VectorGet(mxList, 0)) == NULL || mx->address == NULL)
			continue;

retry_single_mx_once:

		if ((addr = socketAddressCreate(mx->value, SMTP_PORT)) == NULL)
			continue;

		if ((server = socketOpen(addr, 1)) == NULL)
			continue;

		*buffer = '\0';
		syslog(LOG_DEBUG, "connecting to %s", mx->value);

		if (socketClient(server, 0)) {
			syslog(LOG_ERR, "connection to %s failed",  mx->value);
			goto try_next_mx;
		}

		(void) socketSetNonBlocking(server, 1);

		if (smtpGetResponse(server, buffer, SMTP_REPLY_BUFSIZ, &code) < 0) {
			syslog(LOG_ERR, "no welcome from %s", mx->value);
			goto try_next_mx;
		}

		if (code != 220) {
			goto try_next_mx;

		if (socketWrite(server, "HELO what.is.my.ip\r\n", sizeof ("HELO what.is.my.ip\r\n")-1) <= 0) {
			syslog(LOG_ERR, "socket write error to %s", mx->value);
			goto try_next_mx;
		}

		if (smtpGetResponse(server, buffer, SMTP_REPLY_BUFSIZ, &code))
			syslog(LOG_ERR, "HELO not accepted by %s", mx->value);

		(void) socketWrite(server, "QUIT\r\n", sizeof ("QUIT\r\n")-1);
try_next_mx:
		socketClose(server);

		if ((*myip = smtpFindIpInResponse(buffer)) != NULL)
			break;
	}

	/* For a single MX server, repeat the connection attempt once. */
	if (error != NULL && i == 1 && mx != NULL && mx->address != NULL) {
		sleep(30);

		/* This goto breaks with a long standing coding style I've
		 * maintained - that is to use goto's only to jump forward,
		 * never backwards and especially not into a loop. One day
		 * the above loop and this goto will have to be rewritten
		 * to correct this lapse.
		 */
		goto retry_single_mx_once;
	}

	if (VectorLength(mxList) <= i)
		error = smtp;
error4:
	VectorDestroy(mxList);
error3:
	DnsClose(dns);
error2:
	free(path);
error1:
	free(buffer);
error0:
	return error;
}

static void
smtpDisconnect(Connection *conn, int index)
{
	if (conn->client != NULL)
		socketClose(conn->client);
}

int
options(int argc, char **argv)
{
	int ch, i;

	optind = 1;
	while ((ch = getopt(argc, argv, "ad:g:s:t:u:vw")) != -1) {
		switch (ch) {
		case 'a':
			application_mode = 1;
			break;
		case 'd':
			updateInterval = strtol(optarg, NULL, 10) * 60000;
			break;
		case 'g':
			/* Unix only. */
			group_id = optarg;
			break;
		case 's':
			smtp_server = optarg;
			break;
		case 't':
			socketTimeout = strtol(optarg, NULL, 10) * 1000;
			break;
		case 'u':
			/* Unix only. */
			user_id = optarg;
			break;
		case 'v':
			setlogmask(LOG_UPTO(LOG_DEBUG));
			debug = 1;
			break;
		case 'w':
			/* Windows only. */
			install_service = 1;
			break;
		default:
			syslog(LOG_ERR, "unknown command line option -%c", optopt);
			(void) fprintf(stderr, usageMessage);
			exit(2);
		}
	}

	return optind;
}

int
loadCf(char *cf)
{
	int ac;
	FILE *fp;
	static char *buffer = NULL;
	char *av[MAX_ARGV_LENGTH+1];

	/* Load and parse the options file, if present. */
	if ((fp = fopen(cf, "r")) != NULL) {
		/* This buffer is not freed until the program exits. */
		if (buffer == NULL && (buffer = malloc(BUFSIZ)) == NULL)
			goto error0;

		if ((ac = fread(buffer, 1, BUFSIZ-1, fp)) < 0)
			goto error0;

		(void) fclose(fp);

		buffer[ac] = '\0';
		ac = TokenSplitA(buffer, NULL, av+1, MAX_ARGV_LENGTH)+1;
		options(ac, av);
	}

	return 0;
error0:
	(void) fclose(fp);

	return -1;
}

#if defined(__WIN32__)
/***********************************************************************
 *** Windows Service Framework
 ***********************************************************************/

#undef CF_FILE
#define CF_FILE			_NAME ".cf"

static HANDLE eventLog;

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

	status.dwWaitHint = 2000;
	status.dwWin32ExitCode = NO_ERROR;
	status.dwServiceSpecificExitCode = 0;
	status.dwCurrentState = SERVICE_STOPPED;
	status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
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

	status.dwWaitHint = 2000;
	status.dwWin32ExitCode = NO_ERROR;
	status.dwServiceSpecificExitCode = 0;
	status.dwCurrentState = SERVICE_RUNNING;
	status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	SetServiceStatus(ServiceStatus, &status);

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

	openlog(_NAME, LOG_PID, LOG_MAIL);
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
		ServerMain();
	} else if (service == NULL) {
		ReportLog(EVENTLOG_ERROR_TYPE, "Cannot start %s service. See -a and -w options.", _NAME);
	} else if (!StartServiceCtrlDispatcher(dispatchTable)) {
		if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
			if (StartService(service, argc-1, (LPCTSTR *) argv+1))
				return 0;
		}

		ReportLog(EVENTLOG_ERROR_TYPE, "Failed to start %s service.", _NAME);
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
		(void) fprintf(stderr, usageMessage);
		exit(2);
	}

	if (!application_mode) {
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

	if (getuid() == 0) {
		struct group *gr;
		struct passwd *pw;

		/* We have to create the .pid file after we become a daemon process
		 * but before we change process ownership, particularly if we intend
		 * to create a file in /var/run, which is owned and writeable by root.
		 */
		if (savePid(PID_FILE)) {
			syslog(LOG_ERR, "create \"%s\" failed: %s (%d)", PID_FILE, strerror(errno), errno);
			exit(1);
		}

		if (group_id != NULL) {
			if ((gr = getgrnam(group_id)) == NULL) {
				syslog(LOG_ERR, "group \"%s\" not found", group_id);
				exit(1);
			}
			(void) setgid(gr->gr_gid);
		}

		if (user_id != NULL) {
			if ((pw = getpwnam(user_id)) == NULL) {
				syslog(LOG_ERR, "user \"%s\" not found", user_id);
				exit(1);
			}
		}

		if (chroot(EMPTY_DIR)) {
			syslog(LOG_ERR, "chroot(%s) failed: %s (%d)", EMPTY_DIR, strerror(errno), errno);
			exit(1);
		}

		if (user_id != NULL) {
			/* We have to do the getpwnam() before the chroot()
			 * in order to consult /etc/passwd, but the setuid()
			 * has to happen after the setgid() and chroot(),
			 * while we still have root permissions.
			 */
			(void) setuid(pw->pw_uid);
		}
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
