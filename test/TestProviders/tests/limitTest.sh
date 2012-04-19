#!/bin/sh
# ============================================================================
# limitTest
#
# (C) Copyright IBM Corp. 2012
#
# THIS FILE IS PROVIDED UNDER THE TERMS OF THE ECLIPSE PUBLIC LICENSE
# ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS FILE
# CONSTITUTES RECIPIENTS ACCEPTANCE OF THE AGREEMENT.
#
# You can obtain a current copy of the Eclipse Public License from
# http://www.opensource.org/licenses/eclipse-1.0.php
#
# Author:       Michael Chase-Salerno, <bratac@linux.vnet.ibm.com>
# Description:
#    Simple test program to create many instances and verify
#    that max limits are enforced
# ============================================================================


sendxml () {
      # Sends the xml file given as argument 1 to wbemcat with appropriate 
      # credentials and protocol. The output of wbemcat will be directed to 
      # argument 2
      if [ -z $SFCB_TEST_PORT ]
      then
            SFCB_TEST_PORT=5988
      fi
      if [ -z $SFCB_TEST_PROTOCOL ]
      then
          SFCB_TEST_PROTOCOL="http"
      fi
      if [ "$SFCB_TEST_USER" != "" ] && [ "$SFCB_TEST_PASSWORD" != "" ]; then
           wbemcat -u $SFCB_TEST_USER -pwd $SFCB_TEST_PASSWORD -p $SFCB_TEST_PORT -t $SFCB_TEST_PROTOCOL $1 2>&1 > $2
       else
           wbemcat -p $SFCB_TEST_PORT -t $SFCB_TEST_PROTOCOL $1 2>&1 > $2
       fi
       if [ $? -ne 0 ]; then
          echo "FAILED to send CIM-XML request $1"
          return 1
       fi
}

cleanup () {
    # Cleans up all created instances
    CLPRE='<?xml version="1.0" encoding="utf-8"?>
    <CIM CIMVERSION="2.0" DTDVERSION="2.0">
    <MESSAGE ID="4711" PROTOCOLVERSION="1.0">
    <SIMPLEREQ>
      <IMETHODCALL NAME="DeleteInstance">
        <LOCALNAMESPACEPATH>
          <NAMESPACE NAME="root"/>
          <NAMESPACE NAME="interop"/>
        </LOCALNAMESPACEPATH>
        <IPARAMVALUE NAME="InstanceName">
          <INSTANCENAME CLASSNAME="CIM_IndicationHandlerCIMXML">
            <KEYBINDING NAME="SystemCreationClassName">
              <KEYVALUE>CIM_ComputerSystem</KEYVALUE>
            </KEYBINDING>
            <KEYBINDING NAME="SystemName">
              <KEYVALUE>localhost.localdomain</KEYVALUE>
            </KEYBINDING>
            <KEYBINDING NAME="CreationClassName">
              <KEYVALUE>CIM_IndicationHandlerCIMXML</KEYVALUE>
            </KEYBINDING>
            <KEYBINDING NAME="Name">
              <KEYVALUE>'
    CLPOST='</KEYVALUE>
            </KEYBINDING>
          </INSTANCENAME>
        </IPARAMVALUE>
      </IMETHODCALL>
    </SIMPLEREQ>
    </MESSAGE>
    </CIM>'

    j=1
    while [ $j -le $lim ]
    do
        XML=$CLPRE"limitTest_"$j$CLPOST
        echo $XML > ./limitTest.xml
        sendxml ./limitTest.xml /dev/null
        j=$((j+1))
    done

    XML=$CLPRE"limitTest_final"$CLPOST
    echo $XML > ./limitTest.xml
    sendxml ./limitTest.xml /dev/null
    rm ./limitTest.xml
    rm ./limitTest.result
}

# Start of main
TESTDIR=.
lim=100

# Check for wbemcat utility
if ! which wbemcat > /dev/null; then
   echo "  Cannot find wbemcat. Please check your PATH"
   exit 1
fi
if ! touch ./limitTest.xml > /dev/null; then
   echo "  Cannot create files, check permissions"
   exit 1
fi

#
# First check that the limit is respected for LDs
#

PRE='<?xml version="1.0" encoding="utf-8"?>
<CIM CIMVERSION="2.0" DTDVERSION="2.0">
  <MESSAGE ID="4711" PROTOCOLVERSION="1.0">
    <SIMPLEREQ>
      <IMETHODCALL NAME="CreateInstance">
        <LOCALNAMESPACEPATH>
          <NAMESPACE NAME="root"/>
          <NAMESPACE NAME="interop"/>
        </LOCALNAMESPACEPATH>
        <IPARAMVALUE NAME="NewInstance">
          <INSTANCE CLASSNAME="CIM_IndicationHandlerCIMXML">
            <PROPERTY NAME="SystemName" TYPE="string">
              <VALUE>localhost.localdomain</VALUE>
            </PROPERTY>
            <PROPERTY NAME="Name" TYPE="string">
              <VALUE>'
POST='</VALUE>
            </PROPERTY>
            <PROPERTY NAME="Destination" TYPE="string">
              <VALUE>file:///tmp/SFCB_Listener.txt</VALUE>
            </PROPERTY>
          </INSTANCE>
        </IPARAMVALUE>
      </IMETHODCALL>
    </SIMPLEREQ>
  </MESSAGE>
</CIM>'

# Create 100 Listener Destinations
j=1
echo -n "  Testing LD limit ..."
while [ $j -le $lim ]
do
    # Use "limitTest_xxx" as the name
    XML=$PRE"limitTest_"$j$POST
    echo $XML > ./limitTest.xml
    sendxml ./limitTest.xml limitTest.result
    if [ $? -ne 0 ]
    then
        # This means an actual wbemcat failure, 
        echo " Create $j FAILED"
        cleanup
        exit 1;
    fi
    grep "limitTest" ./limitTest.result >/dev/null 2>&1
    if [ $? -ne 0 ]
    then
        # This means one of the early creates failed for 
        # some reason. Might be ok, so just flag it 
        # and continue. It's possible other instances 
        # existed before the test was run.
        echo -n " Create $j failed ... continuing ..."
    fi
    j=$((j+1))
done

# Create a final instance that should exceed the 
# maximum, so it should fail.
XML=$PRE"limitTest_final"$POST
echo $XML > ./limitTest.xml
sendxml ./limitTest.xml ./limitTest.result
# If the instance name is in the result, it succeeded, 
# which is bad.
grep "limitTest_final" ./limitTest.result >/dev/null 2>&1
if [ $? -eq 0 ]
then
    echo " limit not enforced. FAILED"
    cleanup
    exit 1;
fi
echo " PASSED"
cleanup

exit 0
