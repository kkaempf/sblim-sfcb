#include "classProviderCommon.h"

//candidate for sfcCommon
int contained_list(const char **list, const char *name)
{
  if (list) {
    while (*list) {
      if (strcasecmp(*list++, name) == 0) return 1;
    }
  }
  return 0;
}

void filterClass(CMPIConstClass* cimClass, const char** props)
{
  _SFCB_ENTER(TRACE_PROVIDERS, "filterClass");
  char* name = NULL;
  int propCount;
  unsigned long quals;
  CMPIData data = { 0, CMPI_notFound, {0} };
  CMPIType mtype;
  char* refName = NULL;
  int i;

  ClClass * cls = (ClClass*)cimClass->hdl;
  ClClassSetHasFilteredProps(cls);
  propCount = ClClassGetPropertyCount(cls);

  ClProperty *property = (ClProperty*)ClObjectGetClSection(&cls->hdr, &cls->properties);

  for(i = 0; i < propCount; i++) {
    ClClassGetPropertyAt(cls, i, &data, &name, &quals, &refName);
    if(name){
      if((!contained_list(props, name)))
	(property + i)->flags |= ClProperty_Filtered;
    }
  }
  
  int methCount = ClClassGetMethodCount(cls);

  ClMethod *method = (ClMethod*)ClObjectGetClSection(&cls->hdr, &cls->methods);
  
  for(i = 0; i < methCount; i++) {
    char * smname;

    ClClassGetMethodAt(cls, i, &mtype, &smname, &quals);

    if(smname){
      if((!contained_list(props, smname)))
	{
	  (method + i)->flags |= ClProperty_Filtered;
	}
    }
  }

}
