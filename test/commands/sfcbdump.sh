#!/bin/sh
RC=0
if ! sfcbdump ${SFCB_REPODIR}/root/cimv2/classSchemas | grep Linux_CSProcessor > /dev/null
then
    RC=1 
fi

exit $RC
