######################################################################################
# If not stated otherwise in this file or this component's Licenses.txt file the
# following copyright and licenses apply:

#  Copyright 2018 RDK Management

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

# http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#######################################################################################

# This script is used to config the SSID.13, SSID.14 for mesh backhaul GRE
# by zhicheng_qiu@comcast.com
# prash: modified script for writing the mesh para after reading the current value

MODEL_NUM=`grep MODEL_NUM /etc/device.properties | cut -d "=" -f2`
enable_AP=true
sycfgfile=/nvram/syscfg.db
if [ $MODEL_NUM == "DPC3941" ] || [ $MODEL_NUM == "TG1682G" ]  || [ $MODEL_NUM == "DPC3939" ]; then
 if [ `grep mesh_enable $sycfgfile | cut -d "=" -f2` != "true" ]; then
  echo "Mesh Disabled, Dont bringup Mesh interfces"
  enable_AP=false
 fi
 #RDKB-17829: Handle factory reset case for Xb3
 if ! [ -s $sycfgfile ]; then
   echo "XB3 is in factory mode, bringing Mesh SSID down"
   enable_AP="FALSE"
   dmcli eRT setv Device.WiFi.SSID.13.Enable bool false
   dmcli eRT setv Device.WiFi.SSID.14.Enable bool false
 fi
 # RDKB-15951: Create a bridge for Mesh Bhaul and add vlan to it
 echo "Creating Mesh Bhaul bridge"
 brctl addbr br403
 brctl addif br403 eth0.1060
 ifconfig br403 up
fi

for idx in 12 13
do

        if [ "$idx" == "12" ]; then
                brname="br12"
                vlan=112
        else
                brname="br13"
                vlan=113
        fi

        
        uapsd=`wifi_api wifi_getApWmmUapsdEnable $idx | head -n 1`
        if [ "$uapsd" != "FALSE" ]; then
	 wifi_api wifi_setApWmmUapsdEnable $idx 0
	fi

        #AP_BRNAME_13:=.
        if [ `wifi_api wifi_getApBridgeInfo $idx "" "" "" | head -n 1` != "$brname" ]; then
         wifi_api wifi_setApBridgeInfo  $idx $brname "" ""
        fi
        
        wifi_api wifi_setApVlanID $idx $vlan
        
        if [ `wifi_api wifi_getApSsidAdvertisementEnable $idx` != "FALSE" ]; then
         wifi_api wifi_setApSsidAdvertisementEnable $idx 0
        fi

        if [ `wifi_api wifi_getApBeaconType $idx` == "None" ]; then
         wifi_api wifi_setApBeaconType $idx "WPAand11i"
        fi

        #AP_SECFILE_13:=PSK
        wifi_api wifi_setApBasicAuthenticationMode $idx "PSKAuthentication"
      
        if [ `wifi_api wifi_getApWpaEncryptionMode $idx` != "TKIPandAESEncryption" ]; then
         wifi_api wifi_setApWpaEncryptionMode $idx "TKIPandAESEncryption"
        fi

        if [ `wifi_api wifi_getSSIDName $idx` != "we.piranha.off" ]; then
          wifi_api wifi_setSSIDName $idx "we.piranha.off"
        fi
 
        #PSK_KEY_13:=welcome8
        if [ "$MODEL_NUM" == "DPC3941" ] || [ "$MODEL_NUM" == "TG1682G" ] || [ "$MODEL_NUM" == "DPC3939" ]; then
         if [ -z `wifi_api wifi_getApSecurityPreSharedKey $idx` ]; then
          wifi_api wifi_setApSecurityPreSharedKey $idx "welcome8"
         fi
        else
         if [ `wifi_api wifi_setApSecurityPreSharedKey $idx` != "welcome8" ]; then
          wifi_api wifi_setApSecurityPreSharedKey $idx "welcome8"
         fi
        fi

        if [ `wifi_api wifi_getApWpsEnable $idx` != "FALSE" ]; then
         wifi_api wifi_setApWpsEnable $idx 0
        fi
        
        if $enable_AP ; then
         if [ `wifi_api wifi_getApEnable $idx` != "TRUE" ]; then
          wifi_api wifi_setApEnable $idx 1
         fi
        else
         if [ `wifi_api wifi_getApEnable $idx` != "FALSE" ]; then
          wifi_api wifi_setApEnable $idx 0
         fi
        fi
done

