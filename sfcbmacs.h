/*
 * Helper macros for SFCB
 */

static CMPIStatus __attribute__ ((unused)) notSupSt = { CMPI_RC_ERR_NOT_SUPPORTED, NULL };
static CMPIStatus okSt = { CMPI_RC_OK, NULL };

#define notSupCMPI_EQ(pfx) \
  pfx##ExecQuery(CMPIInstanceMI __attribute__ ((unused)) *mi,           \
                   const CMPIContext __attribute__ ((unused)) *ctx,     \
                   const CMPIResult __attribute__ ((unused)) *rslt,     \
                   const CMPIObjectPath __attribute__ ((unused)) * cop, \
                   const char __attribute__ ((unused)) *lang,           \
                   const char __attribute__ ((unused)) *query) { return notSupSt; }

#define notSupCMPI_CI(pfx) \
  pfx##CreateInstance(CMPIInstanceMI __attribute__ ((unused)) *mi,         \
                      const CMPIContext __attribute__ ((unused)) *ctx,     \
                      const CMPIResult __attribute__ ((unused)) *rslt,     \
                      const CMPIObjectPath __attribute__ ((unused)) * cop, \
                      const CMPIInstance __attribute__ ((unused)) *ci) { return notSupSt; }

#define notSupCMPI_DI(pfx) \
  pfx##DeleteInstance(CMPIInstanceMI __attribute__ ((unused)) *mi,      \
                      const CMPIContext __attribute__ ((unused)) *ctx,  \
                      const CMPIResult __attribute__ ((unused)) *rslt,  \
                      const CMPIObjectPath __attribute__ ((unused)) * cop) { return notSupSt; }

#define notSupCMPI_SP(pfx) \
  pfx##SetProperty(CMPIPropertyMI __attribute__ ((unused)) *mi,         \
                   const CMPIContext __attribute__ ((unused)) *ctx,     \
                   const CMPIResult __attribute__ ((unused)) *rslt,     \
                   const CMPIObjectPath __attribute__ ((unused)) *ref,  \
                   const char __attribute__ ((unused)) *propName,       \
                   const CMPIData __attribute__ ((unused)) data) { return notSupSt; }

#define notSupCMPI_GP(pfx) \
  pfx##GetProperty(CMPIPropertyMI __attribute__ ((unused)) *mi,         \
                   const CMPIContext __attribute__ ((unused)) *ctx,     \
                   const CMPIResult __attribute__ ((unused)) *rslt,     \
                   const CMPIObjectPath __attribute__ ((unused)) *ref,  \
                   const char __attribute__ ((unused)) *propName) { return notSupSt; }

#define notSupCMPI_IM(pfx) \
  pfx##InvokeMethod(CMPIMethodMI __attribute__ ((unused)) *mi, \
		    const CMPIContext __attribute__ ((unused)) *ctx,	\
		    const CMPIResult __attribute__ ((unused)) *rslt,	\
		    const CMPIObjectPath __attribute__ ((unused)) *ref,	\
		    const char __attribute__ ((unused)) *methodName,	\
		    const CMPIArgs __attribute__ ((unused)) *in,	\
		    CMPIArgs __attribute__ ((unused)) *out)  { return notSupSt; }

#define okCleanup(pfx,mi) \
   pfx##mi##Cleanup(CMPI##mi##MI __attribute__ ((unused)) *mi,        \
		    const CMPIContext __attribute__ ((unused)) *ctx,  \
		    CMPIBoolean __attribute__ ((unused)) terminate) { return okSt; }
