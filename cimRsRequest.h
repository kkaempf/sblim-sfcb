/*
 * cimRsRequest.h
 *
 * © Copyright IBM Corp. 2010
 *
 * THIS FILE IS PROVIDED UNDER THE TERMS OF THE ECLIPSE PUBLIC LICENSE
 * ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS FILE
 * CONSTITUTES RECIPIENTS ACCEPTANCE OF THE AGREEMENT.
 *
 * You can obtain a current copy of the Eclipse Public License from
 * http://www.opensource.org/licenses/eclipse-1.0.php
 *
 * Author:        Sean Swehla <smswehla@linux.vnet.ibm.com>
 *
 * Description:
 *
 * Public prototypes for parsing RESTful CIM queries.
 *
 */

RequestHdr scanCimRsRequest(CimRequestContext *ctx, char *cimRsData, int *rc);
