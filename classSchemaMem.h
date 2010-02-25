#ifndef CLASSSCHEMAMEM_H
#define CLASSSCHEMAMEM_H

#ifdef __cplusplus
extern          "C" {
#endif

  typedef struct classDir {
    char           *name;
    void           *hdr;
  } ClassDir;

  typedef struct classSchema {
    void           *versionRecord;
    ClassDir       *classes;
  } ClassSchema;

  typedef struct namespaceDir {
    char           *name;
    ClassSchema    *schema;
  } NamespaceDir;

  extern NamespaceDir sfcb_mem_namespaces[];

#ifdef __cplusplus
}
#endif
#endif
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
