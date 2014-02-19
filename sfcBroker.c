
/*
 * sfcBroker.c
 *
 * (C) Copyright IBM Corp. 2005-2007
 *
 * THIS FILE IS PROVIDED UNDER THE TERMS OF THE ECLIPSE PUBLIC LICENSE
 * ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS FILE
 * CONSTITUTES RECIPIENTS ACCEPTANCE OF THE AGREEMENT.
 *
 * You can obtain a current copy of the Eclipse Public License from
 * http://www.opensource.org/licenses/eclipse-1.0.php
 *
 * Author:        Adrian Schuur <schuur@de.ibm.com>
 * Contributions: Sven Schuetz <sven@de.ibm.com>
 *
 * Description:
 *
 * sfcBroker Main.
 *
 */

#include <stdio.h>
#include "native.h"
#include <sfcCommon/utilft.h>
#include "string.h"
#include "cimXmlParser.h"

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "trace.h"
#include "msgqueue.h"
#include <pthread.h>

#include "sfcVersion.h"
#include "control.h"

#include <getopt.h>
#include <syslog.h>
#include <pwd.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

int             sfcBrokerPid = 0;

extern int      sfcbUseSyslog;

extern void     setExFlag(unsigned long f);
extern char    *parseTarget(const char *target);
extern int      init_sfcBroker();
extern CMPIBroker *Broker;
extern void     initProvProcCtl(int);
extern void     processTerminated(int pid);
extern int      httpDaemon(int argc, char *argv[], int sslMode, int adapterNum, char *ipAddr, sa_family_t ipAddrFam);
extern void     processProviderMgrRequests();

extern int      stopNextProc();
extern int      testStartedProc(int pid, int *left);

extern void     uninitProvProcCtl();
extern void     uninitSocketPairs();
extern void     sunsetControl();
extern void     uninitGarbageCollector();

extern int      loadHostnameLib();
extern void     unloadHostnameLib();

extern TraceId  traceIds[];

extern unsigned long exFlags;
static int      startHttp = 0;

char           *name;
extern int      collectStat;

extern unsigned long provSampleInterval;
extern unsigned long provTimeoutInterval;
extern unsigned provAutoGroup;

extern void     dumpTiming(int pid);

static char   **restartArgv;
static int      restartArgc;
static int      adaptersStopped = 0,
    providersStopped = 0,
    restartBroker = 0,
    inaHttpdRestart=0;

long sslMode=0;
static int startHttpd(int argc, char *argv[], int sslMode);

extern char    *configfile;
extern char    *ip4List;
extern char    *ip6List;

extern char   **origArgv;
extern int      origArgc;
extern unsigned int labelProcs;
extern void     append2Argv(char *appendstr);
extern void     restoreOrigArgv(int removePad);

#ifdef HAVE_IPV6
int fallback_ipv4 = 1;
#endif

int trimws = 1;

typedef struct startedAdapter {
  struct startedAdapter *next;
  int             stopped;
  int             pid;
} StartedAdapter;

StartedAdapter *lastStartedAdapter = NULL;

typedef struct startedThreadAdapter {
  struct startedThreadAdapter *next;
  int             stopped;
  pthread_t       tid;
} StartedThreadAdapter;

StartedThreadAdapter *lastStartedThreadAdapter = NULL;

typedef struct ipAddr {
 char * addrStr;
 sa_family_t addrFam;
} IpAddr;

IpAddr *ipAddrList=NULL;
int ipAddrCnt = 0;

void
addStartedAdapter(int pid)
{
  StartedAdapter *sa = malloc(sizeof(*sa));

  sa->stopped = 0;
  sa->pid = pid;
  sa->next = lastStartedAdapter;
  lastStartedAdapter = sa;
}

static int
testStartedAdapter(int pid, int *left)
{
  StartedAdapter *sa = lastStartedAdapter;
  int             stopped = 0;

  *left = 0;
  while (sa) {
    if (sa->pid == pid)
      stopped = sa->stopped = 1;
    if (sa->stopped == 0)
      (*left)++;
    sa = sa->next;
  }
  return stopped;
}

static int
stopNextAdapter()
{
  StartedAdapter *sa = lastStartedAdapter;

  while (sa) {
    if (sa->stopped == 0) {
      sa->stopped = 1;
      kill(sa->pid, SIGUSR1);
      return sa->pid;
    }
    sa = sa->next;
  }
  return 0;
}

/* 3497096 :77022  */
extern pthread_mutex_t syncMtx; /* syncronize provider state */
extern int prov_rdy_state;      /* -1 indicates not ready */

static pthread_mutex_t sdMtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sdCnd = PTHREAD_COND_INITIALIZER;
static int      stopping = 0;
extern int      remSem();

/* secs to wait for a process to die during shutdown [sfcb#94] */
static int      sigChldWaitTime = 3;

static void
stopBroker(void *p)
{
  struct timespec waitTime;
  int sa=0,sp=0, count = 0;

  /* SF 3497096 bugzilla 77022 */
  /* stopping is set to prevent other threads calling this routine */
  pthread_mutex_lock(&syncMtx);
  if (stopping) {
     printf("Stopping sfcb is in progress. Please wait...\n");
     pthread_mutex_unlock(&syncMtx);
     return;
  }
  else {
    stopping=1;
    pthread_mutex_unlock(&syncMtx);
  }

  /* Look for providers ready status. A 5 seconds wait is performed to
   * avoid a hang here in the event of provider looping, crashing etc
  */
  for (;;) {
      pthread_mutex_lock(&syncMtx);
      if (prov_rdy_state == -1) {
        if (count >= 5) break; /* lock will be released later */
         pthread_mutex_unlock(&syncMtx);
         sleep(1);
         count++;
       }
       else break; /* lock will be released later */
  }

  stopLocalConnectServer();

  for (;;) {

    if (adaptersStopped == 0) {
      pthread_mutex_lock(&sdMtx);
      waitTime.tv_sec = time(NULL) + sigChldWaitTime;
      waitTime.tv_nsec = 0;
      if (sa == 0)
        fprintf(stderr, "--- Stopping adapters\n");
      sa++;
      if (stopNextAdapter()) {
        pthread_cond_timedwait(&sdCnd, &sdMtx, &waitTime);
      } else {
        /*
         * no adapters found 
         */
        adaptersStopped = 1;
        fprintf(stderr, "--- All adapters stopped.\n");
      }
      pthread_mutex_unlock(&sdMtx);
    }

    if (adaptersStopped) {
      pthread_mutex_lock(&sdMtx);
      waitTime.tv_sec = time(NULL) + sigChldWaitTime;
      waitTime.tv_nsec = 0;
      if (sp == 0)
        fprintf(stderr, "--- Stopping providers\n");
      sp++;
      if (stopNextProc()) {
        pthread_cond_timedwait(&sdCnd, &sdMtx, &waitTime);
      }
      // else providersStopped=1;
      pthread_mutex_unlock(&sdMtx);
    }
    if (providersStopped)
      break;
  }

  mlogf(M_NOTICE,M_QUIET,"--- %s V" sfcHttpDaemonVersion " stopped - %d\n", name, currentProc);

  remSem();

  uninit_sfcBroker();
  uninitProvProcCtl();
  uninitSocketPairs();
  sunsetControl();
  uninitGarbageCollector();
  closeLogging(1);
  free((void *)sfcBrokerStart);

//  pthread_mutex_unlock(&syncMtx); /* [sfcb#95] */

  unloadHostnameLib();

  _SFCB_TRACE_STOP();

  if (restartBroker) {
    char           *emsg = strerror(errno);
    execvp("sfcbd", restartArgv);
    fprintf(stderr, "--- execv for restart problem: %s\n", emsg);
    abort();
  }

  else
    exit(0);
}

static void
signalBroker()
{
  pthread_mutex_lock(&sdMtx);
  pthread_cond_signal(&sdCnd);
  pthread_mutex_unlock(&sdMtx);
}

#define LOCAL_SFCB

static void
startLocalConnectServer()
{
#ifdef LOCAL_SFCB
  void            localConnectServer();
  pthread_t       t;
  pthread_attr_t  tattr;

  pthread_attr_init(&tattr);
  pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
  pthread_create(&t, &tattr, (void *(*)(void *)) localConnectServer, NULL);
#endif
}

static void
handleSigquit(int __attribute__ ((unused)) sig)
{

  pthread_t       t;
  pthread_attr_t  tattr;

  if (sfcBrokerPid == currentProc) {
    fprintf(stderr, "--- Winding down %s\n", processName);
    pthread_attr_init(&tattr);
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &tattr, (void *(*)(void *)) stopBroker, NULL);
    uninitGarbageCollector();
  }
}

static void
handleSigHup(int sig)
{

  pthread_t       t;
  pthread_attr_t  tattr;

  if (sfcBrokerPid == currentProc) {
    restartBroker = 1;
    if (labelProcs)
      restoreOrigArgv(1);
    fprintf(stderr, "--- Restarting %s\n", processName);
    pthread_attr_init(&tattr);
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &tattr, (void *(*)(void *)) stopBroker, NULL);
    // uninit_sfcBroker();
  }
}

static void
handleSigChld(int __attribute__ ((unused)) sig)
{

  const int       oerrno = errno;
  pid_t           pid;
  int             status,
                  left;
  pthread_t       t;
  pthread_attr_t  tattr;

  for (;;) {
    pid = wait3(&status, WNOHANG, (struct rusage *) 0);
    if ((int) pid == 0)
      break;
    if ((int) pid < 0) {
      if (errno == EINTR || errno == EAGAIN) {
        // mlogf(M_INFO,M_SHOW, "pid: %d continue \n", pid);
        continue;
      }
      if (errno != ECHILD)
        perror("child wait");
      break;
    } else {
      // mlogf(M_INFO,M_SHOW,"sigchild %d\n",pid);
      if (testStartedAdapter(pid, &left)) {
        if (left == 0) {
          fprintf(stderr, "--- Adapters stopped\n");
          adaptersStopped = 1;
          if (!stopping && !inaHttpdRestart) kill(getpid(),SIGQUIT);
        }
        pthread_attr_init(&tattr);
        pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
        pthread_create(&t, &tattr, (void *(*)(void *)) signalBroker, NULL);
      } else if (testStartedProc(pid, &left)) {
        if (left == 0) {
          fprintf(stderr, "--- Providers stopped\n");
          providersStopped = 1;
        }
        pthread_attr_init(&tattr);
        pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
        pthread_create(&t, &tattr, (void *(*)(void *)) signalBroker, NULL);
      }
    }
  }
  errno = oerrno;
}

static int 
reStartHttpd(void)
{
   /* Restore orig argv or startup banner will be hosed on httpd restart */
   if (labelProcs)
     restoreOrigArgv(0);

   int rc = startHttpd(restartArgc, restartArgv, sslMode);
  
   /* Relabel the process */
   if (labelProcs)
     append2Argv(NULL);

   return rc;
}

static void
handleSigUsr2(int __attribute__ ((unused)) sig)
{
#ifndef LOCAL_CONNECT_ONLY_ENABLE
   struct timespec waitTime;
   int sa=0;

   if (inaHttpdRestart) {
     mlogf(M_ERROR,M_SHOW,"--- %s (%d): HTTP daemon restart already in progress\n",
         processName,getpid());
     return;
   } else {
     mlogf(M_ERROR,M_SHOW,"--- %s (%d): HTTP daemon restart requested\n",
         processName,getpid());
     inaHttpdRestart = 1;
   }
   while(!adaptersStopped) {
       pthread_mutex_lock(&sdMtx);
       waitTime.tv_sec=time(NULL) + sigChldWaitTime;
       waitTime.tv_nsec=0;
       if (sa==0) fprintf(stderr,"--- Stopping http adapters\n");
       sa++;
       if (stopNextAdapter()) {
          pthread_cond_timedwait(&sdCnd,&sdMtx,&waitTime);
       }
       else {
         /* no adapters found */
         fprintf(stderr,"--- All http adapters stopped.\n");
         adaptersStopped=1;
       }
       pthread_mutex_unlock(&sdMtx);
   }

   fprintf(stderr,"--- Restarting http adapters...\n");
   pthread_t       t;
   pthread_attr_t  tattr;
   pthread_attr_init(&tattr);
   pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
   pthread_create(&t, &tattr, (void *(*)(void *)) reStartHttpd, NULL);
   adaptersStopped=0;
   inaHttpdRestart=0;
#endif // LOCAL_CONNECT_ONLY_ENABLE
}

static void
handleSigSegv(int __attribute__ ((unused)) sig)
{
  fprintf(stderr, "-#- %s - %d exiting due to a SIGSEGV signal\n",
          processName, currentProc);
}
/*
 * static void handleSigAbort(int sig) { fprintf(stderr,"%s: exiting due
 * to a SIGABRT signal - %d\n", processName, currentProc); kill(0,
 * SIGTERM); } 
 */

#ifndef LOCAL_CONNECT_ONLY_ENABLE

static int
startHttpd(int argc, char *argv[], int sslMode)
{
  int             pid;
  int             httpSFCB,
                  rc;
  char           *httpUser;
  uid_t           httpuid = 0;
  struct passwd  *passwd;

  // Get/check http user info
  if (getControlBool("httpUserSFCB", &httpSFCB)) {
    mlogf(M_ERROR, M_SHOW,
          "--- Error retrieving http user info from config file.\n");
    exit(2);
  }
  if (!httpSFCB) {
    // Get the user specified in the config file
    if (getControlChars("httpUser", &httpUser)) {
      mlogf(M_ERROR, M_SHOW,
            "--- Error retrieving http user info from config file.\n");
      exit(2);
    } else {
      errno = 0;
      passwd = getpwnam(httpUser);
      if (passwd) {
        httpuid = passwd->pw_uid;
      } else {
        mlogf(M_ERROR, M_SHOW,
              "--- Couldn't find http username %s requested in SFCB config file. Errno: %d\n",
              httpUser, errno);
        exit(2);
      }
    }
  }

  int i;
  for (i = 0; i < ipAddrCnt; i++) {
    mlogf(M_INFO, M_SHOW, "--- Starting adapter for IP: %s\n",
        (ipAddrList + i)->addrStr);
    pid = fork();
    if (pid < 0) {
      char *emsg = strerror(errno);
      mlogf(M_ERROR, M_SHOW, "-#- http fork: %s", emsg);
      exit(2);
    }
    if (pid == 0) {
      currentProc = getpid();
      if (!httpSFCB) {
        // Set the real and effective uids
        rc = setreuid(httpuid, httpuid);
        if (rc == -1) {
          mlogf(M_ERROR, M_SHOW, "--- Changing uid for http failed.\n");
          exit(2);
        }
      }
      if (httpDaemon(argc, argv, sslMode, i+1, (ipAddrList + i)->addrStr,
          (ipAddrList + i)->addrFam)) {
        //kill(sfcPid, 3);          /* if port in use, shutdown */
                                    /* (don't do this anymore - 3597806) */
      }
      closeSocket(&sfcbSockets,cRcv,"startHttpd");
      closeSocket(&resultSockets,cAll,"startHttpd");
      exit(0);
    }
    else {
      addStartedAdapter(pid);
    }
  }
  return 0;
}

#endif                          // LOCAL_CONNECT_ONLY_ENABLE

static void
usage(int status)
{
  if (status != 0)
    fprintf(stderr, "Try '%s --help' for more information.\n", name);

  else {
    static const char *help[] = {
      "",
      "Options:",
      " -c, --config-file=<FILE>        use alternative configuration file",
      " -d, --daemon                    run in the background",
      " -h, --help                      display this message and exit",
      " -k, --color-trace               color the trace output of each process",
      " -l, --syslog-level=<LOGLEVEL>   specify the level for syslog",
      "                                 LOGLEVEL can be LOG_INFO, LOG_DEBUG, or LOG_ERR",
      "                                 LOG_ERR is the default",
      " -s, --collect-stats             turn on runtime statistics collecting",
      " -t, --trace-components=<N|?>    activate component-level tracing messages where",
      "                                 N is an OR-ed bitmask integer defining the",
      "                                 components to trace; ? lists the available",
      "                                 components with their bitmask and exits",
      " -v, --version                   output version information and exit",
      " -4, --ipv4-addr-list            comma-separated list of IPv4 addresses to bind",
      " -6, --ipv6-addr-list            comma-separated list of IPv6 addresses to bind",
      " -i, --disable-repository-default-inst-prov To disable entry into the default provider",
      "",
      "For SBLIM package updates and additional information, please see",
      "    the SBLIM homepage at http://sblim.sourceforge.net"
    };

    unsigned int i;

    fprintf(stdout, "Usage: %s [options]\n", name);
    for (i = 0; i < sizeof(help) / sizeof(char *); i++)
      fprintf(stdout, "%s\n", help[i]);
  }

  exit(status);
}

/* SF 3462309 : Check if there is an instance of sfcbd running; use procfs */
static int
sfcb_is_running()
{
    #define STRBUF_LEN 512
    #define BUF_LEN 30
    struct dirent *dp = NULL;
    char *strbuf = malloc(STRBUF_LEN);
    char *buf = malloc(BUF_LEN);
    int mypid = getpid();
    int ret = 0;

    DIR *dir = opendir("/proc");
    while ((dp = readdir(dir)) != NULL) {
        if (isdigit(dp->d_name[0])) {
            sprintf(buf, "/proc/%s/exe", dp->d_name);
            memset(strbuf, 0, STRBUF_LEN);
            if (readlink(buf, strbuf, STRBUF_LEN) == -1) continue;
            if (strstr(strbuf, "sfcbd") != NULL) {
                ret = strtol(dp->d_name, NULL, 0);
                if (ret == mypid) { ret = 0; continue; }
                break;
             }
        }
     }

     closedir(dir);
     free(buf);
     free(strbuf);
     return(ret);
}


static void
version()
{
  fprintf(stdout, "%s " sfcHttpDaemonVersion "\n", name);

  exit(0);
}

int
main(int argc, char *argv[])
{
  int             c,
                  i;
  long            tmask = 0,
      //sslMode = 0,   /* 3597805 */
      tracelevel = 0;
  char           *tracefile = NULL;
#ifdef HAVE_UDS
  int             enableUds = 0;
#endif
  int             enableHttp = 0,
      enableHttps = 0,
      doBa = 0,
      enableInterOp = 0,
      httpLocalOnly = 0;
  int             syslogLevel   = LOG_NOTICE;
  long            dSockets,
                  pSockets;
  char           *pauseStr;
  int             daemonize=0;

  /* Note we will see no mlogf() output prior to this flag being set. If the
   * flag is set but startLogging() has not yet been called, mlogf() will write
   * to syslog directly. After startLogging(), mlogf() will write through the
   * the logging facility. */
  sfcbUseSyslog=1;

  name = strrchr(argv[0], '/');
  if (name != NULL)
    ++name;
  else
    name = argv[0];

  collectStat = 0;
  colorTrace = 0;
  processName = "sfcbd";
  provPauseStr = getenv("SFCB_PAUSE_PROVIDER");
  httpPauseStr = getenv("SFCB_PAUSE_CODEC");
  currentProc = sfcBrokerPid = getpid();
  restartArgc = origArgc = argc;
  restartArgv = origArgv = argv;

  exFlags = 0;

  static struct option const long_options[] = {
    {"config-file", required_argument, 0, 'c'},
    {"daemon", no_argument, 0, 'd'},
    {"help", no_argument, 0, 'h'},
    {"color-trace", no_argument, 0, 'k'},
    {"collect-stats", no_argument, 0, 's'},
    {"syslog-level", required_argument, 0, 'l'},
    {"trace-components", required_argument, 0, 't'},
    {"version", no_argument, 0, 'v'},
    {"ipv4-addr-list", required_argument, 0, '4'},
    {"ipv6-addr-list", required_argument, 0, '6'},
    {"disable-repository-default-inst-provider", no_argument, 0, 'i'},
    {0, 0, 0, 0}
  };

  while ((c =
          getopt_long(argc, argv, "c:dhkst:v4:6:il:", long_options,
                      0)) != -1) {
    switch (c) {
    case 0:
      break;

    case 'c':
      configfile = strdup(optarg);
      break;

    case 'd':
      daemonize=1;
      break;

    case 'h':
      usage(0);

    case 'k':
      colorTrace = 1;
      break;

    case 's':
      collectStat = 1;
      break;

    case 't':
      if (*optarg == '?') {
        fprintf(stdout, "---   Traceable Components:     Int       Hex\n");
        for (i = 0; traceIds[i].id; i++)
          fprintf(stdout, "--- \t%18s:    %d\t0x%05X\n", traceIds[i].id,
                  traceIds[i].code, traceIds[i].code);
        exit(0);
      } else if (isdigit(*optarg)) {
        char           *ep;
        tmask = strtol(optarg, &ep, 0);
      } else {
        fprintf(stderr,
                "Try %s -t ? for a list of the trace components and bitmasks.\n",
                name);
        exit(1);
      }
      break;

    case 'v':
      version();

    case '4':
      ip4List = strdup(optarg);
      break;

    case '6':
      ip6List = strdup(optarg);
      break;

    case 'i':
      disableDefaultProvider = 1;
      break;

    case 'l':
      if (strcmp(optarg, "LOG_ERR") == 0) {
        syslogLevel = LOG_ERR;
      } else if (strcmp(optarg, "LOG_INFO") == 0) {
        syslogLevel = LOG_INFO;
      } else if (strcmp(optarg, "LOG_DEBUG") == 0) {
        syslogLevel = LOG_DEBUG;
      } else {
        fprintf(stderr, "Invalid value for syslog-level.\n");
        usage(3);
      }
      break;

    default:
      usage(3);
    }
  }

  if (optind < argc && *argv[optind] != 'X') { /* Ignore the pad argument */
    fprintf(stderr, "SFCB not started: unrecognized config property %s\n",
            argv[optind]);
    usage(1);
  }

  /* SF 3462309 - If there is an instance running already, return */
  int pid_found = 0;
  if ((pid_found = sfcb_is_running()) != 0) {
      mlogf(M_ERROR, M_SHOW, " --- A previous instance of sfcbd [%d] is running. Exiting.\n", pid_found);
      exit(1);
  }

  if (daemonize){
      daemon(0, 0);
      currentProc=sfcBrokerPid=getpid(); /* req. on some systems */
  }

  char *envLabelProcs = getenv("SFCB_LABELPROCS");
  if (envLabelProcs && *envLabelProcs) {
    char *endptr;
    long val = strtol(envLabelProcs, &endptr, 10);
    if ((*endptr =='\0') && (val > 0) && (val < 1024))
      labelProcs = (unsigned int) val;
  }

  if (labelProcs) {
    if (*argv[argc-1] != 'X') {
      /* Create an expanded argv */
      char **newArgv = malloc(sizeof(char*) * (argc + 2));
      memcpy(newArgv, argv, sizeof(char*) * argc);

      /* Create a pad argument of appropriate length */
      char *padArg = malloc(labelProcs + 1);
      for (i=0; i < (int)labelProcs; i++)
        padArg[i] = 'X';
      padArg[i] = '\0';

      /* Add pad argument and null terminator */
      newArgv[argc] = padArg;
      newArgv[argc + 1] = NULL;

      /* Restart with expanded argv */
      fprintf(stderr,
          "--- %s V" sfcHttpDaemonVersion " performing labelProcs allocation - %d\n",
          name, currentProc);
      execvp(newArgv[0], newArgv);
      fprintf(stderr, "--- failed to execv for labelProcs allocation: %s\n",
          strerror(errno));
      exit(1);
    }
  }

  startLogging(syslogLevel,1);

  mlogf(M_NOTICE, M_SHOW, "--- %s V" sfcHttpDaemonVersion " started - %d\n",
        name, currentProc);

  //get the creation timestamp for the sequence context
  struct timeval  tv;
  struct timezone tz;
  gettimeofday(&tv, &tz);
  struct tm cttm;
  sfcBrokerStart = malloc(15 * sizeof(char));
  memset((void *)sfcBrokerStart, 0, 15 * sizeof(char));
  if (gmtime_r(&tv.tv_sec, &cttm) != NULL) {
    strftime((char *)sfcBrokerStart, 15, "%Y%m%d%H%M%S", &cttm);
  }

  if (collectStat) {
    mlogf(M_INFO, M_SHOW, "--- Statistics collection enabled\n");
    remove("sfcbStat");
  }

  setupControl(configfile);

  _SFCB_TRACE_INIT();

  if (tmask == 0) {
    /*
     * trace mask not specified, check in config file 
     */
    getControlNum("traceMask", &tmask);
  }

  if (getControlNum("traceLevel", &tracelevel) || tracelevel == 0) {
    /*
     * no tracelevel found in config file, use default 
     */
    tracelevel = 1;
  }
  if (getenv("SFCB_TRACE_FILE") == NULL &&
      getControlChars("traceFile", &tracefile) == 0) {
    /*
     * only set tracefile from config file if not specified via env 
     */
    _SFCB_TRACE_SETFILE(tracefile);
  }
  _SFCB_TRACE_START(tracelevel, tmask);
  
  // SFCB_DEBUG
#ifndef SFCB_DEBUG
  if (tmask)
    mlogf(M_ERROR, M_SHOW,
          "--- SCFB_DEBUG not configured. -t %d ignored\n", tmask);
#endif

  if ((pauseStr = getenv("SFCB_PAUSE_PROVIDER"))) {
    printf("--- Provider pausing for: %s\n", pauseStr);
  }

  if (getControlBool("enableHttp", &enableHttp))
    enableHttp = 1;

#ifdef HAVE_UDS
  if (getControlBool("enableUds", &enableUds))
    enableUds = 1;
#endif

#if defined USE_SSL
  if (getControlBool("enableHttps", &enableHttps))
    enableHttps = 0;

  sslMode = enableHttps;
#else
  mlogf(M_INFO, M_SHOW, "--- SSL not configured\n");
  enableHttps = 0;
  sslMode = 0;
#endif

  if (getControlBool("doBasicAuth", &doBa))
    doBa = 0;
  if (!doBa)
    mlogf(M_INFO, M_SHOW, "--- User authentication disabled\n");

  if (getControlBool("enableInterOp", &enableInterOp))
    enableInterOp = 0;

  if (getControlNum("httpProcs", (long *) &dSockets))
    dSockets = 10;
  if (getControlNum("provProcs", (long *) &pSockets))
    pSockets = 16;

  if (getControlBool("httpLocalOnly", &httpLocalOnly))
    httpLocalOnly = 0;
  if (httpLocalOnly)
    mlogf(M_INFO, M_SHOW,
          "--- External HTTP connections disabled; using loopback only\n");

  if (getControlNum
      ("providerSampleInterval", (long *) &provSampleInterval))
    provSampleInterval = 30;
  if (getControlNum
      ("providerTimeoutInterval", (long *) &provTimeoutInterval))
    provTimeoutInterval = 60;
  if (getControlBool("providerAutoGroup", (int *) &provAutoGroup))
    provAutoGroup = 1;

  resultSockets = getSocketPair("sfcbd result");
  sfcbSockets = getSocketPair("sfcbd sfcb");

  if (enableInterOp == 0)
    mlogf(M_INFO, M_SHOW, "--- InterOp namespace disabled\n");
  else
    exFlags = exFlags | 2;

  if ((enableInterOp && pSockets < 4) || pSockets < 3) {
    /*
     * adjusting provider number 
     */
    if (enableInterOp) {
      pSockets = 4;
    } else {
      pSockets = 3;
    }
    mlogf(M_INFO, M_SHOW,
          "--- Max provider process number adjusted to %d\n", pSockets);
  }

  /* Check for whitespace trimming option */
  if (getControlBool("trimWhitespace", &trimws)) {
    trimws = 0;
  }

  if ((enableHttp || enableHttps) && dSockets > 0) {
    startHttp = 1;
  }

  if (loadHostnameLib() == -1) {
     printf("--- Failed to load sfcCustomLib. Exiting\n");
     exit(1);
  }

#ifndef LOCAL_CONNECT_ONLY_ENABLE
  if ((ipAddrList = calloc(1, sizeof(IpAddr))) == 0) {
    mlogf(M_ERROR,M_SHOW,"-#- Failed to alloc memory for ipAddrList.\n");
    exit(2);
  }
  // Command line option takes precedence over config file
  if (!ip4List)
    getControlChars("ip4AddrList",&ip4List);
  if (ip4List && !httpLocalOnly) {
    char* t = strtok(ip4List,",");
    while(t) {
      ipAddrList[ipAddrCnt].addrStr = strdup(t);
      ipAddrList[ipAddrCnt].addrFam = AF_INET;
      ipAddrCnt++;
      t = strtok(NULL,",");
      if ((ipAddrList = realloc(ipAddrList,(ipAddrCnt+1)*sizeof(IpAddr))) == 0) {
        mlogf(M_ERROR,M_SHOW,"-#- Failed to realloc memory for ipAddrList.\n");
        exit(2);
      }
    }
  }
#ifdef HAVE_IPV6
  struct stat buf;
  if (stat("/proc/net/tcp6", &buf) == 0 && (buf.st_mode & S_IFMT) == S_IFREG) {
    if (!ip6List)
      getControlChars("ip6AddrList",&ip6List);
    if (ip6List && !httpLocalOnly) {
      char* t = strtok(ip6List,",");
      while(t) {
        ipAddrList[ipAddrCnt].addrStr = strdup(t);
        ipAddrList[ipAddrCnt].addrFam = AF_INET6;
        ipAddrCnt++;
        t = strtok(NULL,",");
        if ((ipAddrList = realloc(ipAddrList,(ipAddrCnt+1)*sizeof(IpAddr))) == 0) {
          mlogf(M_ERROR,M_SHOW,"-#- Failed to realloc memory for ipAddrList.\n");
          exit(2);
        }
      }
    }
    if (ipAddrCnt == 0) {
      if (httpLocalOnly) {
        mlogf(M_INFO,M_SHOW,"--- Bind to loopback address\n");
        ipAddrList[ipAddrCnt].addrStr = "::1";
      } else {
        mlogf(M_INFO,M_SHOW,"--- Bind to any available IP address\n");
        ipAddrList[ipAddrCnt].addrStr = "::";
      }
      ipAddrList[ipAddrCnt].addrFam = AF_INET6;
      ipAddrCnt++;
    }
    fallback_ipv4 = 0;
  }
#endif
  if (ipAddrCnt == 0) {
    if (httpLocalOnly) {
      mlogf(M_INFO,M_SHOW,"--- Bind to loopback address\n");
      ipAddrList[ipAddrCnt].addrStr = "127.0.0.1";
    } else {
      mlogf(M_INFO,M_SHOW,"--- Bind to any available IP address\n");
      ipAddrList[ipAddrCnt].addrStr = "0.0.0.0";
    }
    ipAddrList[ipAddrCnt].addrFam = AF_INET;
    ipAddrCnt++;
  }
#endif // LOCAL_CONNECT_ONLY_ENABLE

  initSem(pSockets);
  initProvProcCtl(pSockets);
  init_sfcBroker();
  initSocketPairs(pSockets, dSockets);

  setSignal(SIGQUIT, handleSigquit, 0);
  setSignal(SIGINT, handleSigquit, 0);
  setSignal(SIGTERM, handleSigquit, 0);
  setSignal(SIGHUP, handleSigHup, 0);

  atexit(uninitGarbageCollector);

  startLocalConnectServer();

#ifndef LOCAL_CONNECT_ONLY_ENABLE
  if (startHttp) {
    startHttpd(argc, argv, sslMode);
  }
#endif                          // LOCAL_CONNECT_ONLY_ENABLE

  // Display the configured request handlers
  char rtmsg[20]=" ";
#ifdef HANDLER_CIMXML
  strcat(rtmsg,"CIMxml ");
#endif
#ifdef HANDLER_CIMRS
  strcat(rtmsg,"CIMrs ");
#endif
mlogf(M_INFO, M_SHOW, "--- Request handlers enabled:%s\n",rtmsg);

  setSignal(SIGSEGV, handleSigSegv, SA_ONESHOT);
  setSignal(SIGCHLD, handleSigChld, 0);
  setSignal(SIGUSR2, handleSigUsr2, 0);

  /* Label the process by modifying the cmdline */
  if (labelProcs) {
    append2Argv("-proc:Main");
  }

  processProviderMgrRequests();

  stopBroker(NULL);
  return 0;
}
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
