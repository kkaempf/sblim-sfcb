
/*
 *
 * (C) Copyright IBM Corp. 2012
 *
 * THIS FILE IS PROVIDED UNDER THE TERMS OF THE ECLIPSE PUBLIC LICENSE
 * ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS FILE
 * CONSTITUTES RECIPIENTS ACCEPTANCE OF THE AGREEMENT.
 *
 * You can obtain a current copy of the Eclipse Public License from
 * http://www.opensource.org/licenses/eclipse-1.0.php
 *
 * Author:        Mike Lisanke <mlisanke@us.ibm.com>
 *                Chris Buccella <buccella@linux.vnet.ibm.com>
 *
 * Description:
 *
 * Sets the component trace mask for SFCB trace output
 *
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include "trace.h"

int shmkey = 0xdeb001;
extern TraceId traceIds[];

void print_help() {
  printf( "sfcbtrace - toggle the tracemask for SFCB trace output\n\n");
  printf( "Usage: sfcbtrace <trace_mask> <shm_key>\n");
  printf( "\ttrace_mask - an unsigned long or hex value for component(s) to trace (default=0) \n");
  printf( "\tshm_key - the shared memory ID being used by SFCB (default=%x)\n\n", shmkey);

  printf("Traceable Components:   Int     Hex\n");
  int i;
  for (i = 0; traceIds[i].id; i++)
    printf("  %18s:   %d\t0x%05X\n", traceIds[i].id, traceIds[i].code, traceIds[i].code);

  return;
}

int main(int argc, char **argv) {

  int shmid;
  unsigned long tmask = 0;
  void *vpDP = NULL;
  unsigned long *pulDP = NULL;
	
  if (argc > 3) {
    print_help();
    exit(1);
  }

  if (argc == 1) {
    print_help();
    exit(0);
  }
  else
    tmask = strtoul( argv[1], NULL, 16 );
	
  if (argc == 3) {
    shmkey = strtoul( argv[2], NULL, 16 );
  }

  if (errno) fprintf(stderr, "errno set\n");
	
  if ((shmid = shmget( shmkey, sizeof(unsigned long), 0660 )) < 0) {
    if (errno == ENOENT)
      printf("No segment for key %x (sfcbd not running?)\n", shmkey);
    else if (errno == EACCES)
      printf("Permission denied; can't set segment\n");
    else {
      printf( "shmget(%x,...) failed in %s at line %d\n", shmkey, __FILE__, __LINE__ );
      print_help();
    }
    exit(3);
  }

  vpDP = shmat( shmid, NULL, 0 );
  if ( (vpDP == (void*)-1) || (vpDP == NULL) ) {  /* shmat returns an error */
    printf( "shmat(%x,) returned %08X with errno = %s(%u) in %s at line %d\n", shmid, (unsigned int)vpDP, strerror(errno), errno, __FILE__, __LINE__ );
    exit(4);
  }

  pulDP = (unsigned long *)vpDP;

  *pulDP = tmask;
  printf( "debug key %x was set to value %lx\n", shmkey, *pulDP );

  exit(0);
}
