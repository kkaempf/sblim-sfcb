
/*
 * sfcCustomLib.c
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
 * Author:        Narasimha Sharoff <nsharoff@us.ibm.com>
 *
 * Description:
 *
 * These routines are used by sfcb. User can customize the functionality.
 * Do not remove any function or the function prototype
 * 
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * _sfcGetSlpHostname: by Tyrel Datwyler <tyreld@us.ibm.com>
 * is from sfcSlpCustomHostname.c - this file will be deprecated
*/

/** \brief _sfcGetSlpHostname - obtains custom hostname to register with SLP DA
 *
 * Obtain custom hostname string to register with SLP DA
 * This is only a sample of how to write the custom routine
 * used to provide the hostname. You will need to replace this
 * with a routine that uses the desired method to obtain the 
 * proper value. 
*/
extern int _sfcGetSlpHostname(char **hostname)
{
   char *sn;
   sn = (char *) malloc((strlen("mycimom.com") + 1) * sizeof(char));
   sn = strncpy(sn, "mycimom.com", strlen("mycimom.com") + 1);
   if (sn == NULL)
      return 0;
   
   printf("-#- Request for custom SLP service hostname: (hostname = %s)\n", sn);
   *hostname = sn;

   /* Return value of 1 for successs and 0 for failure. */
   return 1;
}

/** \brief _sfcbGetResponseHostname - obtains custom hostname
 *
 *  Allows the user to customize the hostname that sfcb will use internally
 *  	httphost - hostname as provided in HTTP header
 *	hostname - sfcb allocated buffer 
 *	len - size of the hostname buffer
*/
extern int _sfcbGetResponseHostname(char *httpHost, char **hostname, unsigned int len)
{
     if (gethostname(*hostname, len) != 0) {
	strcpy(*hostname, "localhost");
     }

     return 0;
}

/** \brief _sfcbIndAuditLog - log create, delete, and modify calls
 *
 *  Provides object information for indicaiton create, delete, and modify
 *  Default action: return
 *      operation - create/delete/modify
 *      objinfo - information on the object
*/
extern void  _sfcbIndAuditLog(char *operation, char *objinfo)
{
       /* example - log to /tmp/indAudit.log
       FILE *fp;
       char *ts = ctime(&t);
       fp = fopen("/tmp/indAudit.log", "a+");
       if (fp != NULL) {
          fwrite(operation, strlen(action), 1, fp);
          fwrite(objinfo, strlen(msg), 1, fp);
          fwrite("\n\n",2,1,fp);
          fflush(fp);
          fclose(fp);
       }
       */
       return;
}
