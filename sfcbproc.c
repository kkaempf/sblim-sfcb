/*
 * $Id$
 *
 * (C) Copyright IBM Corp. 2013
 *
 * THIS FILE IS PROVIDED UNDER THE TERMS OF THE ECLIPSE PUBLIC LICENSE
 * ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS FILE
 * CONSTITUTES RECIPIENTS ACCEPTANCE OF THE AGREEMENT.
 *
 * You can obtain a current copy of the Eclipse Public License from
 * http://www.opensource.org/licenses/eclipse-1.0.php
 *
 * Author: Dave Heller <hellerda@us.ibm.com>
 *
 * Based on the original shell script by Mark Lanzo <marklan@us.ibm.com>
 *
 * Description: Identify running SFCB processes
 *
 */

#define TRUE 1
#define FALSE 0
#define ERROR -1

#define BUFFER_SZ       32
#define MAX_CMD_SZ     256
#define MAX_PATH_SZ     64
#define MAX_PATTERN_SZ  32
#define MAX_PORTS        6
#define MAX_INODES      12

#define DEFAULT_HTTP_PORT  5988
#define DEFAULT_HTTPS_PORT 5989

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <malloc.h>
#include <regex.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *name;
int debug=0, raw=0, verbose=0;

typedef struct {
  char *ip;
  int port;
  int inode;
} SockDescr;

typedef struct {
  int pid;
  char comm[MAX_CMD_SZ];
  char cmd[MAX_CMD_SZ];
  char state;
  int ppid;
  int pgrp;
} ProcStat;

typedef struct {
  char *regex;         /* reg expr for matching against /proc/pid/map entries */
  char *label;         /* display name; if "*", use matched substring */
} ProviderLstEntry;

/*
 * Convert IPv4 or IPv6 address in hex to presentation format.
 *
 *  mallocs space for return string; it is up to the user to free as necessary.
 *
 *  in:  fam (AF_INET or AF_INET6),
 *       ipHex - hex string like that returned by /proc/net/tcp[6]
 *
 *  returns ptr to IP address in presentation format, NULL if conversion failed.
 */
static char* ipHexToPres(sa_family_t fam, char *ipHex) {
  struct in6_addr ip;
  char *ipPres = NULL;
  int n = sscanf(ipHex,
      "%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx",
      &ip.s6_addr[3], &ip.s6_addr[2], &ip.s6_addr[1], &ip.s6_addr[0],
      &ip.s6_addr[7], &ip.s6_addr[6], &ip.s6_addr[5], &ip.s6_addr[4],
      &ip.s6_addr[11], &ip.s6_addr[10], &ip.s6_addr[9], &ip.s6_addr[8],
      &ip.s6_addr[15], &ip.s6_addr[14], &ip.s6_addr[13], &ip.s6_addr[12]);
  if (n==4 || n==16) {
    ipPres = calloc(1, INET6_ADDRSTRLEN);           // not checking calloc()
    inet_ntop(fam, &ip, ipPres, INET6_ADDRSTRLEN);  // TODO: check for failure here
  }
  return ipPres;
}

/*
 * Follow symbolic link to get real filename
 *
 *  mallocs space for return string; it is up to the user to free as necessary.
 *
 */
char *readLink(const char *path) {
  int len = BUFFER_SZ;
  char *buf = NULL;
  while (TRUE) {
    buf = realloc(buf, len);
    int num = readlink(path, buf, len);
    if (num < 0) {
      free(buf);
      return NULL;
    }
    if (num < len) {
      buf = realloc(buf, num+1);
      buf[num] = 0;
      break;
    }
    len *= 2;
  }
  return buf;
}

/*
 * Determine if a process is holding one of the SFCB sockets.
 *
 *  in:  pid, inodeList, inodeCnt
 *  out: fdCntPtr
 *
 *  returns TRUE if an inode in inodeList is found in this process' fd list
 */
static int isProcHoldingSocket(int pid, SockDescr *socketList, int socketCnt,
    int *fdCntPtr, char **matchingIp) {
  int i, rc;
  int match = FALSE;
  char msgbuf[BUFFER_SZ];
  char pathname[MAX_PATH_SZ];
  char pattern[MAX_PATTERN_SZ];
  char *realname;

  struct dirent *entry;
  DIR *dir;
  regex_t regexpr;

  sprintf(pathname, "/proc/%d/fd", pid);
  if ((dir = opendir(pathname)) == 0) {
    // pid already gone
    if (debug)
      fprintf(stderr, "%s: warning: cannot access %s (process may be gone)\n",
          name, pathname);
    return ERROR;
  }
  /*
   * This function takes a shortcut by quitting the search after the first match
   * (although we continue to count fds for other purposes).  This is equivalent
   * to assuming each HTTP adapter only holds one socket.  In fact, with the
   * current design an adapter may hold sockets for both http and https, and we
   * do not differentiate between sockets (ports) here. Instead we report only
   * the IP address that the adapter is holding.  Really. we should find all
   * sockets for an adapter process and report them all to the user.
   */
  *fdCntPtr = 0;
  while ((entry = readdir(dir))) {
    // Each entry is a fd that is potentially a socket.
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
      (*fdCntPtr)++;
      // If we already found a match, only need to count fds from here out.
      if (match) continue;

      sprintf(pathname, "/proc/%d/fd/%s", pid, entry->d_name);

      realname = readLink(pathname);
      if (realname != NULL) {

        for (i=0; i<socketCnt; i++) {

          // Compile regular expression
          sprintf(pattern, ".*\\[%d\\]", socketList[i].inode);
          if ((rc = regcomp(&regexpr, pattern, 0))) {
            fprintf(stderr, "%s: error: failure to compile regex at %s(%d)\n",
                name, __FILE__, __LINE__);
            exit(1);
          }

          // Execute regular expression
          if (!(rc = regexec(&regexpr, realname, 0, NULL, 0))) {
            match = TRUE;
            *matchingIp = socketList[i].ip;
            break;  // iterating over socket list
          }
          else if (rc == REG_NOMATCH) {
            // continue
          }
          else {
            regerror(rc, &regexpr, msgbuf, sizeof(msgbuf));
            fprintf(stderr, "%s: error: regex match failed (%s) at %s(%d)\n",
                name, msgbuf, __FILE__, __LINE__);
            exit(1);
          }
          regfree(&regexpr);
        }
        if (realname) free(realname);
      }
      else {
        // pid (or file descriptor) already gone
        if (debug)
          fprintf(stderr,
              "%s: warning: cannot access %s (process may be gone)\n", name,
              pathname);
        break;
      }
    }
  }
  return match;
}

/*
 * Determine if process is a provider process by checking /proc/pid/maps entries
 * against a list of provider signatures.
 *
 *  in:  pid
 *
 *  returns a pointer to the display name of the provider, if a match is found,
 *  otherwise returns NULL.
 */
char* isProcProviderX(int pid, ProviderLstEntry* provLst) {
  int i, rc;
  int precedent = -1;
  char filename[MAX_PATH_SZ];
  char msgbuf[BUFFER_SZ];
  char pattern[MAX_PATTERN_SZ];
  char *dot, *bestmatch = NULL;
  char *line = NULL;
  char *pathname;
  size_t len = 0;
  ssize_t read;

  FILE *fp;
  regex_t regexpr;

  sprintf(filename, "/proc/%d/maps", pid);
  if ((fp = fopen(filename, "r")) == 0) {
    if (debug)
      fprintf(stderr, "%s: warning: cannot access %s (process may be gone)\n",
          name, filename);
    return NULL;
  }

  while ((read = getline(&line, &len, fp)) != -1) {

    if (!(pathname = strchr(line, '/'))) continue;

    size_t nmatch = 1;
    regmatch_t matchinfo[1];

    for (i=0; provLst[i].regex; i++) {

      // Compile regular expression
      sprintf(pattern, "%s", provLst[i].regex);
      if ((rc = regcomp(&regexpr, pattern, 0))) {
        fprintf(stderr, "%s: error: failure to compile regex at %s(%d)\n", name,
            __FILE__, __LINE__);
        exit(1);
      }

      // Execute regular expression (and determine match substring)
      if (!(rc = regexec(&regexpr, pathname, nmatch, matchinfo, 0))) {
        *(pathname + (int) matchinfo->rm_eo) = '\0';

        if (precedent < 0 || precedent > i) {
          precedent = i;
          if (*(provLst[i].label) == '*') {
            if ((dot = strchr(pathname, '.'))) { *dot = '\0'; }
            // use the match substring as display name
            bestmatch = strdup(pathname + (int) matchinfo->rm_so);
          }
          else {
            // use the label as display name
            bestmatch = provLst[i].label;
          }
        }
        break;  // iterating over provider list
      }
      else if (rc == REG_NOMATCH) {
        // continue
      }
      else {
        regerror(rc, &regexpr, msgbuf, sizeof(msgbuf));
        fprintf(stderr, "%s: error: regex match failed (%s) at %s(%d)\n", name,
            msgbuf, __FILE__, __LINE__);
        exit(1);
      }
      regfree(&regexpr);
    }
  }
  if (line) free(line);
  fclose(fp);
  return bestmatch;
}

/*
 * Get vital data for a single SFCB processes.
 *
 *  in:  pid
 *  out: p is a ProcStat structure to be populated with retrieved data
 *
 *  returns the pid if the process is an SFCB process, otherwise returns 0
 */
static int getProcStat(int pid, ProcStat *p) {
  FILE *fp;
  char filename[MAX_PATH_SZ];
  char *line = NULL;
  size_t len = 0;
  ssize_t read;
  const char *fmt = "%d %s %c %d %d";

  sprintf(filename, "/proc/%d/stat", pid);
  if ((fp = fopen(filename, "r")) == 0) {
    if (debug)
      fprintf(stderr, "%s: warning: cannot access %s (process may be gone)\n",
          name, filename);
    return 0;
  }

  if (5 == fscanf(fp, fmt, &p->pid, p->comm, &p->state, &p->ppid, &p->pgrp)) {
    fclose(fp);

    if (strcmp(p->comm, "(sfcbd)") != 0 && strcmp(p->comm, "(lt-sfcbd)"))
      return 0;

    // Get the full cmdline for the process
    sprintf(filename, "/proc/%d/cmdline", pid);
    if ((fp = fopen(filename, "r")) == 0) {
      if (debug)
        fprintf(stderr, "%s: warning: cannot access %s (process may be gone)\n",
            name, filename);
      return 0;
    }
    int pass = 0;
    *p->cmd = '\0';
    /* 
     * Note getdelim() will normally return 0 if there is no data to be read
     * other than a delim char.  But when the delim char is NULL, getdelim()
     * will always return at least one.  Therefore, read=1 => no more data.
     */
    while ((read = getdelim(&line, &len, 0, fp)) != -1) {
      if ((read <= 1) || strlen(p->cmd) + (read-1) > MAX_CMD_SZ-2) {
        /*
         * Breaking when no data has been read is akin to breaking on the first
         * occurrence of a double-null.  The intent is to clean up --raw output
         * by discarding the trailing nulls in argv when labelProcs is in effect
         * [sfcb-tix:#76].  If for some strange reason there is an occurrence of
         * a double-null somewhere in argv and there is additional data beyond
         * that, this would hide the additional data.
         */
        break;
      }
      else {
        if (pass++ > 0) strcat(p->cmd, " ");
        strcat(p->cmd, line);
      }
    }
    if (line) free(line);
    return pid;
  }
  else {
    if (debug)
      fprintf(stderr,
          "%s: warning: scanf failed for /proc/%d/stat (process may be gone)\n",
          name, pid);
    fclose(fp);
    return ERROR;
  }
}

/*
 * Get vital data for all SFCB processes.
 *
 *  out: p is a dynamically malloced array of ProcStat structures
 *
 *  returns the number of SFCB processes found
 */
int getProcStatList(ProcStat **p) {
  int pid;
  int cnt = 0;
  DIR *procDIR;
  struct dirent *entry;
  ProcStat *procList;

  if ((procList = calloc(1, sizeof(ProcStat))) == 0) {
    fprintf(stderr, "%s: error: malloc failed (%s) at %s(%d)\n", name,
        strerror(errno), __FILE__, __LINE__);
    return ERROR;
  }
  if ((procDIR = opendir("/proc/")) == 0) {
    fprintf(stderr, "%s: error: cannot access /proc (%s)\n", name,
        strerror(errno));
    return ERROR;
  }
  while ((entry = readdir(procDIR))) {
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
      // If dirname is numeric, it's a pid.
      if ((pid = atoi(entry->d_name))) {

        if (getProcStat(pid, &procList[cnt]) > 0) {  // this is a SFCB process
          cnt++;
          if ((procList = realloc(procList, (cnt+1) * sizeof(ProcStat))) == 0) {
            fprintf(stderr, "%s: error: malloc failed (%s) at %s(%d)\n", name,
                strerror(errno), __FILE__, __LINE__);
            return ERROR;
          }
        }
      }
    }
  }
  closedir(procDIR);
  *p = procList;
  return cnt;
}

/*
 * Get the list of inodes (sockets) for a given TCP port on IPv4 or IPv6.
 * Note there may be more than one if SFCB binds to >1 IP address. .
 *
 *  in: port, addressFam
 *  out: iListPtr is a dynamically malloced array inode numbers
 *
 *  returns the number of inodes (sockets) found
 */
static int getSocketInodeList(int port, int addrFam, SockDescr **sockListPtr) {
  int cnt=0, numFields;
  char bufr[256];
  char filename[MAX_PATH_SZ];
  char *scnfmt;
  FILE *fp;

  // For reading /proc/net/tcp and /proc/net/tcp6
  char *scnfmt4 =
      "%*d: %8s:%4x %8s:%4x %*2x %*8x:%*8x %*2x:%*8x %*8x %d %*d %u \n";
  char *scnfmt6 =
      "%*d: %32s:%4x %32s:%4x %*2x %*8x:%*8x %*2x:%*8x %*8x %d %*d %u \n";

  char locaddr[33], remaddr[33];
  int locport, remport, uid, inode;

  SockDescr *sockList;

  if ((sockList = calloc(1, sizeof(SockDescr))) == 0) {
    fprintf(stderr, "%s: error: malloc failed (%s) at %s(%d)\n", name,
        strerror(errno), __FILE__, __LINE__);
    return ERROR;
  }

  if (addrFam==AF_INET) {
    sprintf(filename, "/proc/net/tcp");
    scnfmt = scnfmt4;
    numFields = 6;
  }
  else if (addrFam==AF_INET6) {
    sprintf(filename, "/proc/net/tcp6");
    scnfmt = scnfmt6;
    numFields = 6;
  }
  else {
    fprintf(stderr,"%s: error: invalid address family: %d\n", name, addrFam);
    return ERROR;
  }

  if ((fp = fopen(filename, "r")) == 0) {
    fprintf(stderr, "%s: error: cannot access %s (%s)\n", name, filename,
        strerror(errno));
    return ERROR;
  }

  while (fgets(bufr, 256, fp)) {

    if (numFields
        != fscanf(fp, scnfmt, locaddr, &locport, remaddr, &remport, &uid,
            &inode)) {
      *sockListPtr = sockList;
      return cnt;
    }

    if (locport == port && inode > 0) {  /* match */

      if (verbose)
        (sockList + cnt)->ip = ipHexToPres(addrFam, locaddr);
      else
        (sockList + cnt)->ip = NULL;  /* more efficient */

      (sockList + cnt)->port = port;
      (sockList + cnt)->inode = inode;
      cnt++;

      if ((sockList = realloc(sockList, (cnt+1) * sizeof(SockDescr))) == 0) {
        fprintf(stderr, "%s: error: malloc failed (%s) at %s(%d)\n", name,
            strerror(errno), __FILE__, __LINE__);
        return ERROR;
      }
    }
  }
  return ERROR;
}

static void usage(int status) {

  if (status != 0)
    fprintf(stderr, "Try '%s --help' for more information.\n", name);

  else
  {
    static const char * help[] = {
      "",
      "Options:",
      " -d, --debug                   show additional debug info",
      " -h, --help                    display this message and exit",
      " -p, --portlist=<PORTLIST>     specify the TCP ports to hunt for",
      "                               PORTLIST is a comma-separated list of port numbers",
      " -r, --raw                     list raw ps info rather than trying to analyze",
      " -v, --verbose                 show more details",
      ""
    };

    int i;

    fprintf(stdout, "Usage: %s [options]\n", name);
    for (i=0; i < sizeof(help) / sizeof(char*); i++)
      fprintf(stdout, "%s\n", help[i]);
  }
  exit(status);
}

int main(int argc, char **argv) {
  char *role, *type, *matchIp=NULL, *portlist=NULL;
  int c, i, j, n, procCnt, fdCnt, socketCnt=0;

  int port;
  int portList[MAX_PORTS] = { DEFAULT_HTTP_PORT, DEFAULT_HTTPS_PORT, 0 };

  SockDescr *sList, socketList[MAX_INODES];

  ProcStat *psInfo, *p;

  ProviderLstEntry providerLst[] = {
//  { "REGEX",                     "LABEL" },
    { "libdctmi.so",               "Instrumentation Provider (DCTMI)" },  // not tested
    { "IndCIMXMLHandler",          "IndCIMXMLHandler" },
    { "InteropProvider",           "Interop Provider" },
    { "sfcClassProvider",          "Class Provider" },
    { "sfcProfileProvider",        "Profile Provider" },
    { "sfcInternalProvider",       "SFCB Internal Provider" },
    { "sfcInteropServerProvider",  "SFCB Interop Server Provider" },
    { "[^/]*Provider.so",          "*" },
    { NULL, NULL },
  };

  name = strrchr(argv[0], '/');
  if (name != NULL) ++name;
  else name = argv[0];

  static struct option const long_options[] = {
    { "debug",            no_argument,       0,  'd' },
    { "help",             no_argument,       0,  'h' },
    { "portlist",         required_argument, 0,  'p' },
    { "raw",              no_argument,       0,  'r' },
    { "verbose",          no_argument,       0,  'v' },
    { 0, 0, 0, 0 }
  };

  while ((c = getopt_long(argc, argv, "dhp:rv", long_options, 0)) != -1)
  {
    switch(c)
    {
      case 0:
        break;

      case 'd':
        debug = 1;
        break;

      case 'h':
        usage(0);
        break;

      case 'p':
        portlist = strdup(optarg);
        break;

      case 'r':
        raw = 1;
        break;

      case 'v':
        verbose = 1;
        break;

      default:
        usage(3);
        break;
    }
  }

  if (optind < argc) {
    fprintf(stderr,"%s: unrecognized config property: %s\n", name,argv[optind]);
    usage(1);
  }

  // Find SFCB processes
  if ((procCnt = getProcStatList(&psInfo)) <= 0) {
    printf("No SFCB Processes found.\n");
    exit(0);
  }

  // Raw mode
  if (raw) {
    printf("%5s %5s %5s %-4s %s\n", "PID", "PPID", "PGID", "STAT", "CMD");
    for (i=0; i<procCnt; i++) {
      ProcStat *p = (psInfo+i);
      printf("%5d %5d %5d %-4c %s\n", p->pid,p->ppid,p->pgrp,p->state,p->cmd);
    }
    exit(0);
  }

  // Parse comma-delimited list from cmdline (overrides default list)
  if (portlist) {
    char* t = strtok(portlist,",");
    int portCnt = 0;
    while (t) {
      portList[portCnt] = atoi(t);
      portCnt++;
      t = strtok(NULL,",");
    }
    portList[portCnt] = 0;  // terminate the list
  }

  // For each port, look for open sockets on IPv4 & IPv6, add to socketList
  for (i=0; (port=portList[i]); i++) {

    n = getSocketInodeList(port, AF_INET, &sList);
    for (j=0; j<n; j++) {
      socketList[socketCnt++] = sList[j];
      if (debug)
        printf("Got inode %d for socket %s:%u (%#x)\n", sList[j].inode,
            sList[j].ip, sList[j].port, port);
    }
    n = getSocketInodeList(port, AF_INET6, &sList);
    for (j=0; j<n; j++) {
      socketList[socketCnt++] = sList[j];
      if (debug)
        printf("Got inode %d for socket [%s]:%u (%#x)\n", sList[j].inode,
            sList[j].ip, sList[j].port, port);
    }
  }

  if (debug) {
    printf("\nFound a total of %d SFCB socket inodes: ", socketCnt);
    for (i=0; i<socketCnt; i++)
      printf("%u ", socketList[i].inode);

    printf("\nFound a total of %d SFCB processes\n\n", procCnt);
  }

  // Print process list with role
  printf("%5s %5s %5s %-4s %s\n", "PID", "PPID", "PGID", "STAT", "ROLE");
  for (i=0; i<procCnt; i++) {
    p = &psInfo[i];

    if (p->pid == p->pgrp || p->ppid == 1) {
      role = "SFCB Main";
    }
    else if (isProcHoldingSocket(p->pid,socketList,socketCnt,&fdCnt,&matchIp)) {
      type = (p->ppid == p->pgrp) ? "Http Daemon" : "Http Request Handler";
      if (verbose) {
        role = malloc(INET6_ADDRSTRLEN + 30);
        sprintf(role, "%s [%s]", type, matchIp);  /* show bind ip */
      } else {
        role = type;
      }
    }
    else if (fdCnt > 0 && fdCnt < 45) {
      role = "SFCB Logger";
    }
    else if ((role = isProcProviderX(p->pid, providerLst))) {
      // nothing further need to be done
    }
    else {
      role = "Unknown";
    }

    printf("%5d %5d %5d %-4c %s\n", p->pid, p->ppid, p->pgrp, p->state, role);
  }
  exit(0);
}
