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
    CLSPRE='<?xml version="1.0" encoding="utf-8"?>
<CIM CIMVERSION="2.0" DTDVERSION="2.0">
  <MESSAGE ID="4711" PROTOCOLVERSION="1.0">
    <SIMPLEREQ>
      <IMETHODCALL NAME="DeleteInstance">
        <LOCALNAMESPACEPATH>
          <NAMESPACE NAME="root"/>
          <NAMESPACE NAME="interop"/>
        </LOCALNAMESPACEPATH>
        <IPARAMVALUE NAME="InstanceName">
          <INSTANCENAME CLASSNAME="CIM_IndicationSubscription">
            <KEYBINDING NAME="Filter">
              <VALUE.REFERENCE>
                <INSTANCENAME CLASSNAME="CIM_IndicationFilter">
                  <KEYBINDING NAME="SystemCreationClassName">
                    <KEYVALUE VALUETYPE="string">
                    CIM_ComputerSystem
                    </KEYVALUE>
                  </KEYBINDING>
                  <KEYBINDING NAME="SystemName">
                    <KEYVALUE VALUETYPE="string">
                    localhost.localdomain
                    </KEYVALUE>
                  </KEYBINDING>
                  <KEYBINDING NAME="CreationClassName">
                    <KEYVALUE VALUETYPE="string">
                    CIM_IndicationFilter
                    </KEYVALUE>
                  </KEYBINDING>
                  <KEYBINDING NAME="Name">
                    <KEYVALUE VALUETYPE="string">
                    Test_Indication_Filter_
                    </KEYVALUE>
                  </KEYBINDING>
                </INSTANCENAME>
              </VALUE.REFERENCE>
            </KEYBINDING>
            <KEYBINDING NAME="Handler">
              <VALUE.REFERENCE>
                <INSTANCENAME CLASSNAME="CIM_IndicationHandlerCIMXML">
                  <KEYBINDING NAME="SystemCreationClassName">
                    <KEYVALUE VALUETYPE="string">
                    CIM_ComputerSystem
                    </KEYVALUE>
                  </KEYBINDING>
                  <KEYBINDING NAME="SystemName">
                    <KEYVALUE VALUETYPE="string">
                    localhost.localdomain
                    </KEYVALUE>
                  </KEYBINDING>
                  <KEYBINDING NAME="CreationClassName">
                    <KEYVALUE VALUETYPE="string">
                    CIM_IndicationHandlerCIMXML
                    </KEYVALUE>
                  </KEYBINDING>
                  <KEYBINDING NAME="Name">
                    <KEYVALUE VALUETYPE="string">'
    CLSPOST=' </KEYVALUE>
                  </KEYBINDING>
                </INSTANCENAME>
              </VALUE.REFERENCE>
            </KEYBINDING>
          </INSTANCENAME>
        </IPARAMVALUE>
      </IMETHODCALL>
    </SIMPLEREQ>
  </MESSAGE>
</CIM>'

    CLDPRE='<?xml version="1.0" encoding="utf-8"?>
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
    CLDPOST='</KEYVALUE>
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
        XML=$CLSPRE"limitTest_"$j$CLSPOST
        echo $XML > ./limitTest.xml
        sendxml ./limitTest.xml /dev/null
        XML=$CLDPRE"limitTest_"$j$CLDPOST
        echo $XML > ./limitTest.xml
        sendxml ./limitTest.xml /dev/null
        j=$((j+1))
    done

    XML=$CLSPRE"limitTest_final"$CLSPOST
    echo $XML > ./limitTest.xml
    sendxml ./limitTest.xml /dev/null
    XML=$CLDPRE"limitTest_final"$CLDPOST
    echo $XML > ./limitTest.xml
    sendxml ./limitTest.xml /dev/null
    rm ./limitTest.xml
    rm ./limitTest.result
    wbemcat IndTest7DeleteFilter.xml > /dev/null
    wbemcat limitTestDF2.XML > /dev/null
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
echo -n "  Testing LD limit "
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
        echo -n "X"
    else
        echo -n "."
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

#
# Now check for active subscriptions
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
          <INSTANCE CLASSNAME="CIM_IndicationSubscription">
            <PROPERTY.REFERENCE NAME="Filter" 
                                REFERENCECLASS="CIM_IndicationFilter"> 
              <VALUE.REFERENCE> 
                <INSTANCENAME CLASSNAME="CIM_IndicationFilter">
                  <KEYBINDING NAME="SystemCreationClassName">
                    <KEYVALUE VALUETYPE="string">
                    CIM_ComputerSystem
                    </KEYVALUE>
                  </KEYBINDING>
                  <KEYBINDING NAME="SystemName">
                    <KEYVALUE VALUETYPE="string">
                    localhost.localdomain
                    </KEYVALUE>
                  </KEYBINDING>
                  <KEYBINDING NAME="CreationClassName">
                    <KEYVALUE VALUETYPE="string">
                    CIM_IndicationFilter
                    </KEYVALUE>
                  </KEYBINDING>
                  <KEYBINDING NAME="Name">
                    <KEYVALUE VALUETYPE="string">'
MID='
                    </KEYVALUE>
                  </KEYBINDING>
                </INSTANCENAME>
              </VALUE.REFERENCE>
            </PROPERTY.REFERENCE>
            <PROPERTY.REFERENCE NAME="Handler" 
                                REFERENCECLASS="CIM_IndicationHandler"> 
              <VALUE.REFERENCE> 
                <INSTANCENAME CLASSNAME="CIM_IndicationHandlerCIMXML">
                  <KEYBINDING NAME="SystemCreationClassName">
                    <KEYVALUE VALUETYPE="string">
                    CIM_ComputerSystem
                    </KEYVALUE>
                  </KEYBINDING>
                  <KEYBINDING NAME="SystemName">
                    <KEYVALUE VALUETYPE="string">
                    localhost.localdomain
                    </KEYVALUE>
                  </KEYBINDING>
                  <KEYBINDING NAME="CreationClassName">
                    <KEYVALUE VALUETYPE="string">
                    CIM_IndicationHandlerCIMXML
                    </KEYVALUE>
                  </KEYBINDING>
                  <KEYBINDING NAME="Name">
                    <KEYVALUE VALUETYPE="string">'

POST='</KEYVALUE>
                  </KEYBINDING>
                </INSTANCENAME>
              </VALUE.REFERENCE>
            </PROPERTY.REFERENCE>
            <PROPERTY NAME="SubscriptionState" TYPE="uint16"> 
              <VALUE> 2 </VALUE>
            </PROPERTY>
          </INSTANCE>
        </IPARAMVALUE>
      </IMETHODCALL>
    </SIMPLEREQ>
  </MESSAGE>
</CIM>'

POSTD='</KEYVALUE>
                  </KEYBINDING>
                </INSTANCENAME>
              </VALUE.REFERENCE>
            </PROPERTY.REFERENCE>
            <PROPERTY NAME="SubscriptionState" TYPE="uint16"> 
              <VALUE> 1 </VALUE>
            </PROPERTY>
          </INSTANCE>
        </IPARAMVALUE>
      </IMETHODCALL>
    </SIMPLEREQ>
  </MESSAGE>
</CIM>'

# Create 100 subs
lim=100
j=1
wbemcat IndTest1CreateFilter.xml > /dev/null
wbemcat limitTestCF2.XML > /dev/null
echo -n "  Testing Sub limit "
while [ $j -le $lim ]
do
    XML=$PRE"Test_Indication_Filter_"$MID"limitTest_"$j$POST
    echo $XML > ./limitTest.xml
    sendxml ./limitTest.xml  limitTest.result
    if [ $? -ne 0 ]
    then
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
        echo -n "X"
    else
        echo -n "."
    fi
    j=$((j+1))
done

# Make sure the final one fails
XML=$PRE"limitTest_f2"$MID"limitTest_1"$POST
echo $XML > ./limitTest.xml
sendxml ./limitTest.xml ./limit.result
wbemcat limitTestDS2.XML > /dev/null
grep "limitTest" ./limit.result >/dev/null 2>&1
if [ $? -eq 0 ]
then
    echo " limit not enforced. FAILED"
    cleanup
    exit 1;
fi

# Should be able to create a disabled one
XML=$PRE"limitTest_f2"$MID"limitTest_1"$POSTD
echo $XML > ./limitTest.xml
sendxml ./limitTest.xml ./limit.result
grep "limitTest" ./limit.result >/dev/null 2>&1
if [ $? -ne 0 ]
then
    echo " disabled subscription prevented. FAILED"
    cleanup
    exit 1;
fi
# but we shouldn't be able to activate it.
wbemcat limitTestEnableSub.XML > ./limit.result
wbemcat limitTestDS2.XML > /dev/null
grep "MaxActiveSubscription" ./limit.result >/dev/null 2>&1
if [ $? -ne 0 ]
then
    echo " enable subscription limit not enforced. FAILED"
    cleanup
    exit 1;
fi

echo " PASSED"
cleanup
exit 0
