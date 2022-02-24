/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2015 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

/**********************************************************************
   Copyright [2014] [Cisco Systems, Inc.]
 
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at
 
       http://www.apache.org/licenses/LICENSE-2.0
 
   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
**********************************************************************/


/**************************************************************************

    module: cosa_wifi_dml.c

        For COSA Data Model Library Development

    -------------------------------------------------------------------

    description:

        This file implementes back-end apis for the COSA Data Model Library

    -------------------------------------------------------------------

    environment:

        platform independent

    -------------------------------------------------------------------

    author:

        COSA XML TOOL CODE GENERATOR 1.0

    -------------------------------------------------------------------

    revision:

        01/18/2011    initial revision.

**************************************************************************/
#include "ctype.h"
#include "ansc_platform.h"
#include "safec_lib_common.h"
#include "cosa_wifi_apis.h"
#include "cosa_wifi_dml.h"
#include "cosa_wifi_internal.h"
#include "plugin_main_apis.h"
#include "ccsp_WifiLog_wrapper.h"
#include "ccsp_psm_helper.h"
#include "cosa_dbus_api.h"
#include "collection.h"
#include "wifi_hal.h"
#include "cosa_wifi_passpoint.h"
#include "wifi_monitor.h"
#include "dslh_definitions_database.h"

#if defined (FEATURE_SUPPORT_WEBCONFIG)
#include "../sbapi/wifi_webconfig.h"
#endif

#if defined(_COSA_BCM_MIPS_) || defined(_XB6_PRODUCT_REQ_) || defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_)
#include "ccsp_base_api.h"
#include "messagebus_interface_helper.h"

extern ULONG g_currentBsUpdate;
#endif

#define PATH_X_LGI_COM_ACTIVETIMEOUT "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%lu.X_LGI-COM_ActiveTimeout"

# define DFSCHANCOUNT 15
#define REFRESH_INTERVAL 120
#define TIME_NO_NEGATIVE(x) ((long)(x) < 0 ? 0 : (x))
static ULONG last_tick;

# define WEPKEY_TYPE_SET 3
# define KEYPASSPHRASE_SET 2
# define MFPCONFIG_OPTIONS_SET 3

#define MAX_DM_PATH_LEN 256

#define MAX_BASICRATES_BYTE 6 //2byte + 0x 2byte
#define MAX_SUPPORTRATES_BYTE 10 //4byte + 0x 2byte
#define BASICRATES_24G_LESS_VALUE 0x8000 // last bit must 1
#define BASICRATES_5G_LESS_VALUE  0xF800 // last byte must 1111 1xxx
#define SUPPORTRATES_24G_LESS_VALUE 0xFFF00000 // last 2 byte must 1111 1111 1111 xxxx
#define SUPPORTRATES_5G_LESS_VALUE  0xFFFF0000 // last 2 byte must 1111 1111 1111 1111

extern void* g_pDslhDmlAgent;
extern int gChannelSwitchingCount;
extern BOOL isWifiApplyLibHostapRunning;

/***********************************************************************
 IMPORTANT NOTE:

 According to TR69 spec:
 On successful receipt of a SetParameterValues RPC, the CPE MUST apply 
 the changes to all of the specified Parameters atomically. That is, either 
 all of the value changes are applied together, or none of the changes are 
 applied at all. In the latter case, the CPE MUST return a fault response 
 indicating the reason for the failure to apply the changes. 
 
 The CPE MUST NOT apply any of the specified changes without applying all 
 of them.

 In order to set parameter values correctly, the back-end is required to
 hold the updated values until "Validate" and "Commit" are called. Only after
 all the "Validate" passed in different objects, the "Commit" will be called.
 Otherwise, "Rollback" will be called instead.

 The sequence in COSA Data Model will be:

 SetParamBoolValue/SetParamIntValue/SetParamUlongValue/SetParamStringValue
 -- Backup the updated values;

 if( Validate_XXX())
 {
     Commit_XXX();    -- Commit the update all together in the same object
 }
 else
 {
     Rollback_XXX();  -- Remove the update at backup;
 }
 
***********************************************************************/

static int isHex (char *string);
static BOOL isHotspotSSIDIpdated = FALSE;
static BOOL isBeaconRateUpdate[16] = { FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE };
BOOL IsValidMacAddress(char *mac);
ULONG InterworkingElement_Commit(ANSC_HANDLE hInsContext);
void *Wifi_Hosts_Sync_Func(void *pt, int index, wifi_associated_dev_t *associated_dev, BOOL bCallForFullSync, BOOL bCallFromDisConnCB);
int EVP_DecodeBlock(unsigned char*, unsigned char*, int);
static void* WiFi_DeleteMacFilterTableThread( void *frArgs );
int d2i_EC_PUBKEY(void **a, const unsigned char **key, long length);
ANSC_STATUS CosaDmlWiFi_startDPP(PCOSA_DML_WIFI_AP pWiFiAP, ULONG staIndex);
static BOOL Validate_SSID_Timeout (char *pString);

static ANSC_STATUS CosaDmlWiFi_CheckAndConfigureMFPConfig (BOOLEAN bFeatureMFPConfig);

#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
typedef enum{
    ClientMac,
    InitiatorBootstrapSubjectPublicKeyInfo,
    ResponderBootstrapSubjectPublicKeyInfo,
    Channels,
    MaxRetryCount
} dpp_cmd;

typedef struct  {
    char ClientMac;
    char InitiatorBootstrapSubjectPublicKeyInfo;
    char ResponderBootstrapSubjectPublicKeyInfo;
    char Channels;
    char MaxRetryCount;
} __attribute__((packed)) status;

static status dmcli_status = {0};
static const char *wifi_health_logg = "/rdklogs/logs/wifihealth.txt";

static void set_status(dpp_cmd cmd)
{
    switch(cmd)
    {
        case ClientMac:
            {dmcli_status.ClientMac = 1;}
        break;
        case InitiatorBootstrapSubjectPublicKeyInfo: 
            { dmcli_status.InitiatorBootstrapSubjectPublicKeyInfo = 1;}
        break ;
        case ResponderBootstrapSubjectPublicKeyInfo: 
            { dmcli_status.ResponderBootstrapSubjectPublicKeyInfo = 1;}
        break;
        case Channels:
            { dmcli_status.Channels = 1;}
        break;
        case MaxRetryCount: 
            { dmcli_status.MaxRetryCount = 1;}
        break;
        default:
        break;
    }
}
#endif // !defined(_HUB4_PRODUCT_REQ_)

static ANSC_STATUS
GetInsNumsByWEPKey64(PCOSA_DML_WEPKEY_64BIT pWEPKey, ULONG *apIns, ULONG *wepKeyIdx)
{
    PCOSA_DATAMODEL_WIFI        pWiFi       = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PSINGLE_LINK_ENTRY          pAPLink     = NULL;
    PCOSA_CONTEXT_LINK_OBJECT   pAPLinkObj  = NULL;
    PCOSA_DML_WIFI_AP           pWiFiAP     = NULL;
    int                         i;

    /* for each Device.WiFi.AccessPoint.{i}. */
    for (   pAPLink = AnscSListGetFirstEntry(&pWiFi->AccessPointQueue);
            pAPLink != NULL;
            pAPLink = AnscSListGetNextEntry(pAPLink)
        )
    {
        pAPLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pAPLink);
        if (!pAPLinkObj)
            continue;
        pWiFiAP = (PCOSA_DML_WIFI_AP)pAPLinkObj->hContext;

        /* for each Device.WiFi.AccessPoint.{i}.Security.X_CISCO_COM_WEPKey64Bit.{i}. */
        for (i = 0; i < COSA_DML_WEP_KEY_NUM; i++)
        {
            if ((ANSC_HANDLE)pWEPKey == (ANSC_HANDLE)&pWiFiAP->SEC.WEPKey64Bit[i])
            {
                /* found */
                *apIns = pWiFiAP->AP.Cfg.InstanceNumber;
                *wepKeyIdx = i;
                return ANSC_STATUS_SUCCESS;
            }
        }
    }

    return ANSC_STATUS_FAILURE;
}

static ANSC_STATUS
GetInsNumsByWEPKey128(PCOSA_DML_WEPKEY_128BIT pWEPKey, ULONG *apIns, ULONG *wepKeyIdx)
{
    PCOSA_DATAMODEL_WIFI        pWiFi       = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PSINGLE_LINK_ENTRY          pAPLink     = NULL;
    PCOSA_CONTEXT_LINK_OBJECT   pAPLinkObj  = NULL;
    PCOSA_DML_WIFI_AP           pWiFiAP     = NULL;
    int                         i;

    /* for each Device.WiFi.AccessPoint.{i}. */
    for (   pAPLink = AnscSListGetFirstEntry(&pWiFi->AccessPointQueue);
            pAPLink != NULL;
            pAPLink = AnscSListGetNextEntry(pAPLink)
        )
    {
        pAPLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pAPLink);
        if (!pAPLinkObj)
            continue;
        pWiFiAP = (PCOSA_DML_WIFI_AP)pAPLinkObj->hContext;

        /* for each Device.WiFi.AccessPoint.{i}.Security.X_CISCO_COM_WEPKey128Bit.{i}. */
        for (i = 0; i < COSA_DML_WEP_KEY_NUM; i++)
        {
            if ((ANSC_HANDLE)pWEPKey == (ANSC_HANDLE)&pWiFiAP->SEC.WEPKey128Bit[i])
            {
                /* found */
                *apIns = pWiFiAP->AP.Cfg.InstanceNumber;
                *wepKeyIdx = i;
                return ANSC_STATUS_SUCCESS;
            }
        }
    }

    return ANSC_STATUS_FAILURE;
}

#if 0
static BOOL isValidSSID(char *SSID)
{
    // "alphabet, digit, underscore, hyphen and dot"
    char *validChars = "_-."; 
    int i = 0;
    BOOL result = true;

    for (i = 0; i < AnscSizeOfString(SSID); i++) {
        if ( (isalnum(SSID[i]) == 0) &&
              (strchr(validChars, SSID[i]) == NULL)) {
            result = false;
            break;
        }
    }
    return result;
}
#endif

//zqiu>>
//#define WIFIEXT_DM_HOTSPOTSSID_UPDATE  "Device.X_COMCAST_COM_GRE.Interface.1.DHCPCircuitIDSSID"
#define WIFIEXT_DM_HOTSPOTSSID_UPDATE  "Device.X_COMCAST-COM_GRE.Tunnel.1.EnableCircuitID"
//zqiu<<
static BOOL IsSsidHotspot(ULONG ins)
{
    char rec[128];
    char *sval = NULL;
    BOOL bval;
    extern ANSC_HANDLE bus_handle;
    extern char        g_Subsystem[32];

    snprintf(rec, sizeof(rec), "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.HotSpot", (int)ins);
    if (PSM_Get_Record_Value2(bus_handle, g_Subsystem, rec, NULL, &sval) != CCSP_SUCCESS) {
        AnscTraceError(("%s: fail to get PSM record !\n", __FUNCTION__));
        return FALSE;
    }
    bval = (atoi(sval) == 1) ? TRUE : FALSE;
    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(sval);
    return bval;
}

BOOL UpdateCircuitId()
{
    char *dstComponent = NULL;
	char *dstPath = NULL;
	parameterValStruct_t    **valStructs = NULL;
	char                    *paramNameList[1];
	char                    tmpPath[64];
	extern ANSC_HANDLE bus_handle;
    extern char        g_Subsystem[32];
	int                     valNum = 0;
	sleep(7);

	if (!Cosa_FindDestComp(WIFIEXT_DM_HOTSPOTSSID_UPDATE, &dstComponent, &dstPath) || !dstComponent || !dstPath)
	{
		if(dstComponent)
		{
			AnscFreeMemory(dstComponent);
		}
		if(dstPath)
		{
			AnscFreeMemory(dstPath);
		}
		return FALSE;
	}

	sprintf(tmpPath,"%s",WIFIEXT_DM_HOTSPOTSSID_UPDATE);
    paramNameList[0] = tmpPath;
    if(CcspBaseIf_getParameterValues(bus_handle, dstComponent, dstPath,
                paramNameList, 1, &valNum, &valStructs) != CCSP_SUCCESS)
    {
	    free_parameterValStruct_t(bus_handle, valNum, valStructs);
		if(dstComponent)
		{
			AnscFreeMemory(dstComponent);
		}
		if(dstPath)
		{
			AnscFreeMemory(dstPath);
		}
		return FALSE;
	}

	free_parameterValStruct_t(bus_handle, valNum, valStructs);

	if(dstComponent)
	{
		AnscFreeMemory(dstComponent);
	}
	if(dstPath)
	{
		AnscFreeMemory(dstPath);
	}
    return TRUE;
}

/***********************************************************************

 APIs for Object:

    WiFi.

    *  WiFi_GetParamBoolValue
    *  WiFi_GetParamIntValue
    *  WiFi_GetParamUlongValue
    *  WiFi_GetParamStringValue

***********************************************************************/
/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        WiFi_GetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:

        This function is called to retrieve Boolean parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL*                       pBool
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
WiFi_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    UNREFERENCED_PARAMETER(hInsContext);
    if (strcmp(ParamName, "X_CISCO_COM_FactoryReset") == 0)
    {
        /* always return false when get */
        *pBool = FALSE;
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_EnableTelnet") == 0)
    {
        *pBool = pMyObject->bTelnetEnabled;
	    return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_ResetRadios") == 0)
    {
        /* always return false when get */
        *pBool = FALSE;
        return TRUE;
    }
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_WiFiHost_Sync") == 0)
    {
	
        *pBool = FALSE;
        return TRUE;
    }

    if (strcmp(ParamName, "2G80211axEnable") == 0)
    {
        CosaDmlWiFi_Get2G80211axEnabled(pBool);
        return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_PreferPrivate") == 0)
    {
        CosaDmlWiFi_GetPreferPrivateData(pBool);
        return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_RapidReconnectIndicationEnable") == 0)
    {
        CosaDmlWiFi_GetRapidReconnectIndicationEnable(pBool, false);
        return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_vAPStatsEnable") == 0)
    {
        *pBool = pMyObject->bX_RDKCENTRAL_COM_vAPStatsEnable;
        return TRUE;
    }
    if (strcmp(ParamName, "FeatureMFPConfig") == 0)
    {
        *pBool = pMyObject->bFeatureMFPConfig;
	    return TRUE;
    }

    if (strcmp(ParamName, "TxOverflowSelfheal") == 0)
    {
        *pBool = pMyObject->bTxOverflowSelfheal;
	    return TRUE;
    }
    if (strcmp(ParamName, "X_RDK-CENTRAL_COM_ForceDisable") == 0)
    {
        *pBool = pMyObject->bForceDisableWiFiRadio;
         return TRUE;
    }
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_EnableRadiusGreyList") == 0)
    {
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
        *pBool = pMyObject->bEnableRadiusGreyList;
#else
        *pBool = FALSE;
#endif
        return TRUE;
    }
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_EnableHostapdAuthenticator") == 0)
    {
#if  defined(FEATURE_HOSTAP_AUTHENTICATOR)
        *pBool = pMyObject->bEnableHostapdAuthenticator;
#endif //FEATURE_HOSTAP_AUTHENTICATOR
        return TRUE;
    }

    if (strcmp(ParamName, "DFSatBootUp") == 0)
    {
        *pBool = pMyObject->bDFSAtBootUp;
        return TRUE;
    }

    if (strcmp(ParamName, "DFS") == 0)
    {
        *pBool = pMyObject->bDFS;
        return TRUE;
    }
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        WiFi_GetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int*                        pInt
            );

    description:

        This function is called to retrieve integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int*                        pInt
                The buffer of returned integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
WiFi_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    UNREFERENCED_PARAMETER(hInsContext);

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_GoodRssiThreshold") == 0)
    {
        /* collect value */
        *pInt = pMyObject->iX_RDKCENTRAL_COM_GoodRssiThreshold;
        return TRUE;
    }

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_AssocCountThreshold") == 0)
    {
        /* collect value */
        *pInt = pMyObject->iX_RDKCENTRAL_COM_AssocCountThreshold;
        return TRUE;
    }

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_AssocMonitorDuration") == 0)
    {
        /* collect value */
        *pInt = pMyObject->iX_RDKCENTRAL_COM_AssocMonitorDuration;
        return TRUE;
    }

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_AssocGateTime") == 0)
    {
        /* collect value */
        *pInt = pMyObject->iX_RDKCENTRAL_COM_AssocGateTime;
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        WiFi_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
WiFi_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    UNREFERENCED_PARAMETER(hInsContext);

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Status") == 0)
    {
        BOOL radio_0_Enabled=FALSE;
        BOOL radio_1_Enabled=FALSE;
        wifi_getRadioEnable(1, &radio_1_Enabled);
        wifi_getRadioEnable(0, &radio_0_Enabled);
        int ret = radio_1_Enabled & radio_0_Enabled;
        if(ret == 1)
        {
            *puLong = 2;
        }
        else
        {
            *puLong = 1;
        }
        CcspTraceDebug(("Radio 1 is %d Radio 0 is %d \n", radio_1_Enabled, radio_0_Enabled));
        return TRUE;
    }
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        WiFi_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
WiFi_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    unsigned char *binConf;
    ULONG binSize;
    unsigned char *base64Conf;
    UNREFERENCED_PARAMETER(hInsContext);

    if (!ParamName || !pValue || !pUlSize || *pUlSize < 1)
        return -1;

    PCOSA_DATAMODEL_WIFI    pMyObject               = ( PCOSA_DATAMODEL_WIFI )g_pCosaBEManager->hWifi;

    if (strcmp(ParamName, "X_CISCO_COM_RadioPower") == 0)
    { 
        COSA_DML_WIFI_RADIO_POWER power;
        CosaDmlWiFi_GetRadioPower(&power);

        if ( power == COSA_DML_WIFI_POWER_LOW )
        {
            AnscCopyString(pValue, "PowerLow");
        } else if ( power == COSA_DML_WIFI_POWER_DOWN )
        { 
            AnscCopyString(pValue, "PowerDown");
        } else if ( power == COSA_DML_WIFI_POWER_UP )
        { 
            AnscCopyString(pValue, "PowerUp");
        } 
        return 0;
    }

    if (strcmp(ParamName, "X_CISCO_COM_ConfigFileBase64") == 0)
    {
        /* at lest support 32786 after base64 config */
        if (*pUlSize < 40000)
        {
            *pUlSize = 40000;
            return 1;
        }

        /* 
         * let's translate config file to base64 format since 
         * XXX_SetParamStringValue only support ASCII string
         */

        /* note base64 need 4 bytes for every 3 octets, 
         * we use a smaller buffer to save config file.
         * and save one more byte for debbuging */
        binSize = (*pUlSize - 1) * 4 / 3;
        binConf = AnscAllocateMemory(binSize);
        if (binConf == NULL)
        {
            AnscTraceError(("%s: no memory\n", __FUNCTION__));
            CcspTraceWarning(("RDK_LOG_WARN, %s: no memory\n", __FUNCTION__));
            return -1;
        }

        /* get binary (whatever format) config and it's size from backend */
        if (CosaDmlWiFi_GetConfigFile(binConf, (int *)&binSize) != ANSC_STATUS_SUCCESS)
        {
            AnscFreeMemory(binConf); /*RDKB-6905, CID-32900, free unused resource before exit */
            CcspTraceWarning(("RDK_LOG_WARN, %s: CosaDmlWiFi_GetConfigFile Failed\n", __FUNCTION__));
            return -1;
        }

        base64Conf = AnscBase64Encode(binConf, binSize);
        if (base64Conf == NULL)
        {
            CcspTraceWarning(("RDK_LOG_WARN, %s: base64 encoding error\n", __FUNCTION__));
            AnscTraceError(("%s: base64 encoding error\n", __FUNCTION__));
            AnscFreeMemory(binConf);
            return -1;
        }

        snprintf(pValue, *pUlSize, "%s", base64Conf);
        AnscFreeMemory(base64Conf);
        AnscFreeMemory(binConf);
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_GASConfiguration") == 0)
    {
        if(pMyObject->GASConfiguration) {
            AnscCopyString(pValue, pMyObject->GASConfiguration);
        }
        return 0;
    }
    //LGI add begin
    if (strcmp(ParamName, "X_LGI-COM_ReservedSSIDNames") == 0)
    {
        AnscCopyString(pValue,pMyObject->ReservedSSIDNames);
        return 0;
    }
    if (strcmp(ParamName, "X_LGI-COM_DefaultReservedSSID") == 0)
    {
        AnscCopyString(pValue,DefaultReservedSSIDNames);
        return 0;
    }
    //LGI add end

    return 0;
}

static void *WiFi_HostSyncThread(void *arg)
{
	UNREFERENCED_PARAMETER(arg);
	CcspTraceWarning(("RDK_LOG_WARN, %s-%d \n",__FUNCTION__,__LINE__));
	Wifi_Hosts_Sync_Func(NULL,0, NULL, 1, 0);
	return NULL;
}

static void *mfp_concheck_thread(void *vptr_value)
{
    static BOOL running=0;
    BOOL bval = (BOOL) (intptr_t)vptr_value;
    if(!running)
    {
      running=1;
      CcspTraceError(("%s:mfp_concheck_thread starting with MFP %d \n", __FUNCTION__,bval));
      CosaDmlWiFi_CheckAndConfigureMFPConfig(bval);

    }
    else
    {
        CcspTraceError(("%s: already mfp_concheck_thread is running\n", __FUNCTION__));
    }
    running=0;
    return NULL;
}

static ANSC_STATUS CosaDmlWiFi_CheckAndConfigureMFPConfig( BOOLEAN bFeatureMFPConfig )
{
	PCOSA_DATAMODEL_WIFI	pMyObject		= ( PCOSA_DATAMODEL_WIFI )g_pCosaBEManager->hWifi;
    ULONG 					vAPTotalCount   = 0;
	unsigned int   			iLoopCount;

	//Get the total vAP Count
    vAPTotalCount = AnscSListQueryDepth( &pMyObject->AccessPointQueue );
	
	//Loop to configure all vAP 
	for( iLoopCount = 0; iLoopCount < vAPTotalCount; iLoopCount++ )
	{
		PCOSA_CONTEXT_LINK_OBJECT		pLinkObjAp		= (PCOSA_CONTEXT_LINK_OBJECT)NULL;
		PSINGLE_LINK_ENTRY				pSLinkEntryAp	= (PSINGLE_LINK_ENTRY		)NULL;
		
		pSLinkEntryAp = AnscQueueGetEntryByIndex( &pMyObject->AccessPointQueue, iLoopCount );
		
		if ( pSLinkEntryAp )
		{
			pLinkObjAp	= ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntryAp);

			if( pLinkObjAp )  
			{
				PCOSA_DML_WIFI_AP	pWifiAp = (PCOSA_DML_WIFI_AP)pLinkObjAp->hContext;

				if( pWifiAp )
				{
					/* 
					  * FeatureMFPConfig - Enable
					  * 
					  * If the Security mode is WPA2-Personal or WPA2-Enterprise, 
					  * then the Device.WiFi.AccessPoint.i.Security.MFPConfig should set to Optional.
					  *
					  * If the Security mode is OPEN then 
					  * Device.WiFi.AccessPoint.i.Security.MFPConfig should set to Disabled
					  *
					  *
					  * FeatureMFPConfig - Disable
					  * 
					  * Need to set the Device.WiFi.AccessPoint.{i}.Security.MFPConfig of each VAP to Disabled.
					  */
					    
					if( TRUE == bFeatureMFPConfig )
					{
						if( ( pWifiAp->SEC.Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_WPA2_Personal ) || 
							( pWifiAp->SEC.Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_WPA2_Enterprise )
						  )
						{
							if ( ANSC_STATUS_SUCCESS != CosaDmlWiFiApSecsetMFPConfig( iLoopCount, "Optional" ) )
							{
								return ANSC_STATUS_FAILURE;
							}

							sprintf( pWifiAp->SEC.Cfg.MFPConfig, "%s", "Optional" );
						}

						if( pWifiAp->SEC.Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_None )
						{
							if ( ANSC_STATUS_SUCCESS != CosaDmlWiFiApSecsetMFPConfig( iLoopCount, "Disabled" ) )
							{
								return ANSC_STATUS_FAILURE;
							}

							sprintf( pWifiAp->SEC.Cfg.MFPConfig, "%s", "Disabled" );
						}
					}
					else
					{
						if ( ANSC_STATUS_SUCCESS != CosaDmlWiFiApSecsetMFPConfig( iLoopCount, "Disabled" ) )
						{
							return ANSC_STATUS_FAILURE;
						}
						
						sprintf( pWifiAp->SEC.Cfg.MFPConfig, "%s", "Disabled" );
					}
				}
			}
		}
	}

	return ANSC_STATUS_SUCCESS;
}


BOOL
WiFi_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    UNREFERENCED_PARAMETER(hInsContext);

    if (strcmp(ParamName, "X_CISCO_COM_FactoryReset") == 0)
    {
        if (bValue)
        {
            CosaDmlWiFi_FactoryReset();
        }
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_EnableTelnet") == 0)
    {
        if ( CosaDmlWiFi_EnableTelnet(bValue) == ANSC_STATUS_SUCCESS ) {
	    // Set parameter value only if the command was successful
	    pMyObject->bTelnetEnabled = bValue;
	}
	        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_ResetRadios") == 0)
    {
	CosaDmlWiFi_ResetRadios();
        return TRUE;
    }

    if (strcmp(ParamName, "2G80211axEnable") == 0)
    {
        if(CosaDmlWiFi_Set2G80211axEnabled(bValue) == ANSC_STATUS_SUCCESS)
        {
                pMyObject->b2G80211axEnabled = bValue;
                return TRUE;
        }
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_WiFiHost_Sync") == 0)
    {
		pthread_t WiFi_HostSync_Thread;
    	int res;
        pthread_attr_t attr;
        pthread_attr_t *attrp = NULL;

        attrp = &attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
        res = pthread_create(&WiFi_HostSync_Thread, attrp, WiFi_HostSyncThread, NULL);		

	if(res != 0)
		CcspTraceWarning(("Create Send_Notification_Thread error %d ", res));

        if(attrp != NULL)
                pthread_attr_destroy( attrp );
        //Wifi_Hosts_Sync_Func(NULL,0, NULL, 1);
        return TRUE;
    }
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_PreferPrivate") == 0)
    {
        if(CosaDmlWiFi_SetPreferPrivatePsmData(bValue) == ANSC_STATUS_SUCCESS)
        {
		pMyObject->bPreferPrivateEnabled = bValue;
		return TRUE;
        }
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_RapidReconnectIndicationEnable") == 0)
    {
        if (CosaDmlWiFi_SetRapidReconnectIndicationEnable(bValue) == ANSC_STATUS_SUCCESS) {
            pMyObject->bRapidReconnectIndicationEnabled = bValue;
            return TRUE;
        }
    }
    
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_vAPStatsEnable") == 0)
    {
        if (ANSC_STATUS_SUCCESS == CosaDmlWiFiSetvAPStatsFeatureEnable( bValue ))
        {
            pMyObject->bX_RDKCENTRAL_COM_vAPStatsEnable = bValue;
            return TRUE;
        }
    }
    if (strcmp(ParamName, "FeatureMFPConfig") == 0)
    {
		//Check whether same value setting via DML
		if( bValue == pMyObject->bFeatureMFPConfig )
		{
	        return TRUE;
		}
		//Configure MFPConfig 
		if ( ANSC_STATUS_SUCCESS == CosaDmlWiFi_SetFeatureMFPConfigValue( bValue ) )
        {
            /* for XB3 the processing time is higher, so making everything in a separate thread.*/
            pthread_t mfp_thread;
            BOOL bval=(BOOL)bValue;
            pthread_attr_t attr;
            pthread_attr_t *attrp = NULL;

            attrp = &attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
            int err = pthread_create(&mfp_thread, attrp, mfp_concheck_thread, (void *)&bval);
            if(attrp != NULL)
                pthread_attr_destroy( attrp );
            if(0 != err)
            {
                CcspTraceError(("%s: Error in creating mfp_concheck_thread \n", __FUNCTION__));
                return FALSE;
            }
            else
            {
                pthread_detach(mfp_thread);
			    pMyObject->bFeatureMFPConfig = bValue;
                return TRUE;
            }
        }
    }
    
    if (strcmp(ParamName, "TxOverflowSelfheal") == 0)
    {
        if (ANSC_STATUS_SUCCESS == CosaDmlWiFiSetTxOverflowSelfheal( bValue ))
        {
            pMyObject->bTxOverflowSelfheal = bValue;
            return TRUE;
        }
    }
    if (strcmp(ParamName, "X_RDK-CENTRAL_COM_ForceDisable") == 0)
    {
        if ((pMyObject->bForceDisableWiFiRadio != bValue) && (ANSC_STATUS_SUCCESS == CosaDmlWiFiSetForceDisableWiFiRadio( bValue )))
        {
            pMyObject->bForceDisableWiFiRadio = bValue;
            return TRUE;
        }
    }
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_EnableRadiusGreyList") == 0)
    {
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
        if (ANSC_STATUS_SUCCESS == CosaDmlWiFiSetEnableRadiusGreylist( bValue ))
        {
            pMyObject->bEnableRadiusGreyList = bValue;
	    pMyObject->bPreferPrivateEnabled = !bValue;
            return TRUE;
        }
#endif
    }
    
#if defined(FEATURE_HOSTAP_AUTHENTICATOR)
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_EnableHostapdAuthenticator") == 0)
    {
        if (bValue == pMyObject->bEnableHostapdAuthenticator)
        {
            return TRUE;
        }

        if (CosaDmlWiFiSetHostapdAuthenticatorEnable((ANSC_HANDLE)pMyObject, bValue, FALSE))
        {
            pMyObject->bEnableHostapdAuthenticator = bValue;
            return TRUE;
        }
    }
#endif //FEATURE_HOSTAP_AUTHENTICATOR

    if (strcmp(ParamName, "DFSatBootUp") == 0)
    {
        if (bValue == pMyObject->bDFSAtBootUp)
        {
            return TRUE;
        }

        if (ANSC_STATUS_SUCCESS == CosaDmlWiFiSetDFSAtBootUp(bValue))
        {
            pMyObject->bDFSAtBootUp = bValue;
            return TRUE;
        }
    }

    if (strcmp(ParamName, "DFS") == 0)
    {
        if (bValue == pMyObject->bDFS)
        {
            return TRUE;
        }

        if (ANSC_STATUS_SUCCESS == CosaDmlWiFiSetDFS(bValue))
        {
            pMyObject->bDFS = bValue;
            return TRUE;
        }
    }
    return FALSE;
}

void CosaDmlWiFi_UpdateMfCfg( void )
{
        PCOSA_DATAMODEL_WIFI            pMyObject       = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
        PCOSA_CONTEXT_LINK_OBJECT       pLinkObjAp      = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
        PSINGLE_LINK_ENTRY              pSLinkEntryAp   = (PSINGLE_LINK_ENTRY       )NULL;

        int  idx[4] = {5,6,9,10};
        int  i;
        for(i=0; i<4; i++)
        {
                pSLinkEntryAp = AnscQueueGetEntryByIndex( &pMyObject->AccessPointQueue, idx[i]-1);
                if ( pSLinkEntryAp )
                {
                        pLinkObjAp      = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntryAp);

                        if( pLinkObjAp )
                        {
                                PCOSA_CONTEXT_LINK_OBJECT               pLinkObj           = (PCOSA_CONTEXT_LINK_OBJECT)pLinkObjAp;
                                PCOSA_DML_WIFI_AP                       pWifiAp            = (PCOSA_DML_WIFI_AP           )pLinkObj->hContext;
                                PCOSA_DML_WIFI_AP_MF_CFG                pWifiApMf          = &pWifiAp->MF;

                                PSINGLE_LINK_ENTRY                      pSLinkEntry    = (PSINGLE_LINK_ENTRY             )NULL;
                                PCOSA_CONTEXT_LINK_OBJECT               pSSIDLinkObj   = (PCOSA_CONTEXT_LINK_OBJECT      )NULL;
                                PCOSA_DML_WIFI_SSID                     pWifiSsid      = (PCOSA_DML_WIFI_SSID            )NULL;
                                CHAR                                    PathName[64] = {0};

                                pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);

                                while ( pSLinkEntry )
                                {
                                        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
                                        pWifiSsid        = pSSIDLinkObj->hContext;

                                        sprintf(PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);

					if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
                                        {
                                                break;
                                        }

                                        pSLinkEntry = AnscQueueGetNextEntry(pSLinkEntry);
                                }

                                #if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_)  && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
                                        CosaDmlWiFiApMfGetCfg( NULL, pWifiSsid->SSID.Cfg.SSID, pWifiApMf );
                                #else
                                        CosaDmlWiFiApMfGetCfg( NULL, pWifiSsid->SSID.StaticInfo.Name, pWifiApMf );
                                #endif
                        }
                }
        }
}

static void wifiFactoryReset(void *frArgs)
{
	ULONG indexes=(ULONG)frArgs;
	ULONG radioIndex   =(indexes>>24) & 0xff;
	ULONG radioIndex_2 =(indexes>>16) & 0xff;
	ULONG apIndex      =(indexes>>8) & 0xff;
	ULONG apIndex_2    =indexes & 0xff;

	CosaDmlWiFi_FactoryResetRadioAndAp(radioIndex,radioIndex_2, apIndex, apIndex_2);
	MacFiltTab_CleanAll();
}

BOOL
WiFi_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    unsigned char *binConf;
    ULONG binSize;
	int nRet=0;
	ULONG radioIndex=0, apIndex=0, radioIndex_2=0, apIndex_2=0;
	ULONG indexes=0;

        PCOSA_DATAMODEL_WIFI    pMyObject               = ( PCOSA_DATAMODEL_WIFI )g_pCosaBEManager->hWifi;
#if defined (FEATURE_SUPPORT_WEBCONFIG)
       unsigned char *webConf = NULL;
        int webSize = 0;
#endif
        errno_t rc = -1;
        int ind = -1;

#ifdef USE_NOTIFY_COMPONENT
	char* p_write_id = NULL;
	char* p_new_val = NULL;
	char* p_old_val = NULL;
	char* p_notify_param_name = NULL;
	char* st;
	char* p_val_type = NULL;
	UINT value_type,write_id;
	parameterSigStruct_t param = {0};
        size_t len = 0;
        char *p_tok;
        int i = 0;
#endif
    if (!ParamName || !pString)
    {
        return FALSE;
    }

        rc = strcmp_s("X_RDKCENTRAL-COM_WiFi_Notification", strlen("X_RDKCENTRAL-COM_WiFi_Notification"), ParamName, &ind);
        ERR_CHK(rc);
        if((rc == EOK) && (!ind))
	{
#ifdef USE_NOTIFY_COMPONENT
                        len = strlen(pString);
                        printf(" \n WIFI : Notification Received \n");

                        for( p_tok = strtok_s(pString, &len, ",", &st) ; p_tok ; p_tok = strtok_s(NULL, &len, ",", &st) )
                        {
                                printf("Token p_tok - %s\n", p_tok);
                                switch(i)
                                {
                                       case 0:
                                                  p_notify_param_name = p_tok;
                                                  break;
                                       case 1:
                                                  p_write_id = p_tok;
                                                  break;
                                       case 2:
                                                  p_new_val = p_tok;
                                                  break;
                                       case 3:
                                                  p_old_val = p_tok;
                                                  break;
                                       case 4:
                                                  p_val_type = p_tok;
                                                  break;
                                }
                                i++;
                                if((len == 0) || (i == 5))
                                     break;
                         }

                         if(i < 5)
                         {
                             CcspWifiTrace(("RDK_LOG_ERROR, Value p_val[%d] is  NULL!!! (%s):(%d)\n", i, __func__,  __LINE__));
			     return FALSE;
                         }

                        rc = strcmp_s("NULL", strlen("NULL"), p_new_val, &ind);
                        ERR_CHK(rc);
                        if((rc == EOK) && (!ind))
			{
				p_new_val = "";
			}

                        rc = strcmp_s("NULL", strlen("NULL"), p_old_val, & ind);
                        ERR_CHK(rc);
                        if((rc == EOK) && (!ind))
			{
				p_old_val = "";
			}

			value_type = atoi(p_val_type);
			write_id = atoi(p_write_id);

			printf(" \n Notification : Parameter Name = %s \n", p_notify_param_name);
			printf(" \n Notification : New Value = %s \n", p_new_val);
			printf(" \n Notification : Old Value = %s \n", p_old_val);
			printf(" \n Notification : Value Type = %d \n", value_type);
			printf(" \n Notification : Component ID = %d \n", write_id);

			param.parameterName = p_notify_param_name;
			param.oldValue = p_old_val;
			param.newValue = p_new_val;
			param.type = value_type;
			param.writeID = write_id;

			WiFiPramValueChangedCB(&param,0,NULL);
#endif
			return TRUE;
		}	 
	
                rc = strcmp_s("X_RDKCENTRAL-COM_Connected-Client", strlen("X_RDKCENTRAL-COM_Connected-Client"), ParamName, &ind);
                ERR_CHK(rc);
                if((rc == EOK) && (!ind))
		{
#ifdef USE_NOTIFY_COMPONENT
		        len = strlen(pString);
			printf(" \n WIFI : Connected-Client Received \n");

                        for( p_tok = strtok_s(pString, &len, ",", &st) ; p_tok ; p_tok = strtok_s(NULL, &len, ",", &st) )
                        {
                                printf("Token p_tok - %s\n", p_tok);
                                switch(i)
                                {
                                       case 0:
                                                  p_notify_param_name = p_tok;
                                                  break;
                                       case 1:
                                                  p_write_id = p_tok;
                                                  break;
                                       case 2:
                                                  p_new_val = p_tok;
                                                  break;
                                       case 3:
                                                  p_old_val = p_tok;
                                                  break;
                                }
                                i++;

                                if((len == 0) || (i == 4))
                                    break;
                         }

                         if(i < 4)
                         {
                             CcspWifiTrace(("RDK_LOG_ERROR, Value p_val[%d] is NULL!!! (%s):(%d)!!!\n", i, __func__,  __LINE__));
                             return FALSE;
                         }

			printf(" \n Notification : Parameter Name = %s \n", p_notify_param_name);
			printf(" \n Notification : Interface = %s \n", p_write_id);
			printf(" \n Notification : MAC = %s \n", p_new_val);
			printf(" \n Notification : Status = %s \n", p_old_val);

#endif
			return TRUE;
		}	 

        rc = strcmp_s("X_CISCO_COM_FactoryResetRadioAndAp", strlen("X_CISCO_COM_FactoryResetRadioAndAp"), ParamName, &ind);
        ERR_CHK(rc);
        if((rc == EOK) && (!ind))
        {
		fprintf(stderr, "-- %s X_CISCO_COM_FactoryResetRadioAndAp %s\n", __func__, pString);	

        if(!pString || strlen(pString)<3 || strchr(pString, ';')==NULL)
			return FALSE;
		if(strchr(pString, ',')) { //1,2;1,3 
			nRet = _ansc_sscanf(pString, "%lu,%lu;%lu,%lu",  &radioIndex, &radioIndex_2, &apIndex, &apIndex_2);
			if ( nRet != 4 || radioIndex>2 || radioIndex_2>2 || apIndex>16 || apIndex_2>16) 
				return FALSE;
		} else {
			nRet = _ansc_sscanf(pString, "%lu;%lu",  &radioIndex, &apIndex);
			if ( nRet != 2 || radioIndex>2 || apIndex>16) 
				return FALSE;
		}
	indexes=(radioIndex<<24) + (radioIndex_2<<16) + (apIndex<<8) + apIndex_2;
	pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_t *attrp = NULL;

        attrp = &attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );

        pthread_create(&tid, attrp, (void*)&wifiFactoryReset, (void*)indexes);

        if(attrp != NULL)
            pthread_attr_destroy( attrp );
	//	CosaDmlWiFi_FactoryResetRadioAndAp(radioIndex,radioIndex_2, apIndex, apIndex_2);        
        return TRUE;
    }
	
    rc = strcmp_s("X_CISCO_COM_RadioPower", strlen("X_CISCO_COM_RadioPower"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
        COSA_DML_WIFI_RADIO_POWER TmpPower; 
        rc = strcmp_s("PowerLow", strlen("PowerLow"), pString, &ind);
        ERR_CHK(rc);
        if((rc == EOK) && (!ind))
        {
            TmpPower = COSA_DML_WIFI_POWER_LOW;
        }
        else
        {
           rc = strcmp_s("PowerDown", strlen("PowerDown"), pString, &ind);
           ERR_CHK(rc);
           if((rc == EOK) && (!ind))
           {
               TmpPower = COSA_DML_WIFI_POWER_DOWN;
           }
           else
           {
              rc = strcmp_s("PowerUp", strlen("PowerUp"), pString, &ind);
              ERR_CHK(rc);
              if((rc == EOK) && (!ind))
              {
                  TmpPower = COSA_DML_WIFI_POWER_UP;
              }
              else
              {
                  return FALSE;
              }
          }
        }

        /* save update to backup */
        CosaDmlWiFi_SetRadioPower(TmpPower);
        
        return TRUE;
    }
	
    rc = strcmp_s("X_CISCO_COM_ConfigFileBase64", strlen("X_CISCO_COM_ConfigFileBase64"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
		binConf = AnscBase64Decode((PUCHAR)pString, &binSize);
		if (binConf == NULL)
		{
			AnscTraceError(("%s: base64 decode error\n", __FUNCTION__));
			return FALSE;
		}

		if (CosaDmlWiFi_SetConfigFile(binConf, binSize) != ANSC_STATUS_SUCCESS)
		{
			AnscFreeMemory(binConf);
			return FALSE;
		}
		AnscFreeMemory(binConf); /*RDKB-13101 & CID:-34096*/

		return TRUE;
     }
	
        rc = strcmp_s("X_RDKCENTRAL-COM_Br0_Sync", strlen("X_RDKCENTRAL-COM_Br0_Sync"), ParamName, &ind);
        ERR_CHK(rc);
        if((rc == EOK) && (!ind))
        {
		char buf[128]={0};
		char *pt=NULL;
		
		rc = strcpy_s(buf, sizeof(buf), pString);
                if(rc != EOK)
                {
                    ERR_CHK(rc);
                    return FALSE;
                }
		if ((pt=strstr(buf,","))) {
			*pt=0;
			CosaDmlWiFiGetBridge0PsmData(buf, pt+1);
		}
              return TRUE;
        }
	
    rc = strcmp_s("X_RDKCENTRAL-COM_GASConfiguration", strlen("X_RDKCENTRAL-COM_GASConfiguration"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
        if(ANSC_STATUS_SUCCESS == CosaDmlWiFi_SetGasConfig((ANSC_HANDLE)pMyObject,pString)){
            return TRUE;
        } else {
            CcspTraceWarning(("Failed to Set GAS Configuration\n"));
            return FALSE;
        }
    }
    
    rc = strcmp_s("Private", strlen("Private"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind)){
#if defined (FEATURE_SUPPORT_WEBCONFIG)
        webConf = AnscBase64Decode((PUCHAR)pString, (ULONG*)&webSize);
        CcspTraceWarning(("Decoded privatessid blob %s of size %d\n",webConf,webSize));
        if (CosaDmlWiFi_setWebConfig((char*)webConf,webSize,WIFI_WEBCONFIG_PRIVATESSID) == ANSC_STATUS_SUCCESS) {
            CcspTraceWarning(("Success in parsing privatessid web config blob\n"));
            if (webConf != NULL) {
                free(webConf);
                webConf = NULL;
            }
            return TRUE;
        } else {
            CcspTraceWarning(("Failed to parse webconfig blob\n"));
            if (webConf != NULL) {
                free(webConf);
                webConf = NULL;
            }
            return FALSE;
        }
#else
        return FALSE;
#endif
    }
 
    rc = strcmp_s("Home", strlen("Home"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind)) {
#if defined (FEATURE_SUPPORT_WEBCONFIG)
    webConf = AnscBase64Decode((PUCHAR)pString, (ULONG*)&webSize);
    CcspTraceWarning(("Decoded homessid blob %s of size %d\n",webConf,webSize));
    if (CosaDmlWiFi_setWebConfig((char*)webConf,webSize,WIFI_WEBCONFIG_HOMESSID) == ANSC_STATUS_SUCCESS) {
        CcspTraceWarning(("Success in parsing homessid web config blob\n"));
        if (webConf != NULL) {
                free(webConf);
                webConf = NULL;
            }
            return TRUE;
        } else {
            CcspTraceWarning(("Failed to parse homessid webconfig blob\n"));
            if (webConf != NULL) {
                free(webConf);
                webConf = NULL;
            }
            return FALSE;
        }
#else
        return FALSE;
#endif
    }
    //LGI add begin
    rc = strcmp_s("X_LGI-COM_ReservedSSIDNames", strlen("X_LGI-COM_ReservedSSIDNames"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
        /*
           Commas are used as separators for the lists of reserved SSIDs,
           therefore we need to enforce a rule that no reserved SSID can
           contain a comma. This also protects against attempts to set a
           list of SSIDs in one SPV (which is not supported - reserved
           SSIDs need to be added one at a time).
        */
        if (strchr (pString, ',') != NULL)
        {
            return FALSE;
        }

        if (isReservedSSID (pMyObject->ReservedSSIDNames, pString)) //Check already in the list
        {
            return TRUE;
        }

        if (CosaDmlWiFi_SetWiFiReservedSSIDNames ((ANSC_HANDLE) pMyObject, pString) == ANSC_STATUS_SUCCESS)
        {
            return TRUE;
        }

        return FALSE;
    }
    //LGI add end
 
    rc = strcmp_s("X_RDK_VapData", strlen("X_RDK_VapData"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind)) {
#if defined (FEATURE_SUPPORT_WEBCONFIG)
//    webConf = AnscBase64Decode((PUCHAR)pString, (ULONG*)&webSize);
//    CcspTraceWarning(("Decoded SSID Data blob %s of size %d\n", webConf,webSize));
    if (CosaDmlWiFi_setWebConfig(pString,strlen(pString), WIFI_SSID_CONFIG) == ANSC_STATUS_SUCCESS) {
        CcspTraceWarning(("Success in parsing SSID Config\n"));
        if (webConf != NULL) {
                free(webConf);
                webConf = NULL;
            }
            return TRUE;
        } else {
            CcspTraceWarning(("Failed to parse SSID blob\n"));
            if (webConf != NULL) {
                free(webConf);
                webConf = NULL;
            }
            return FALSE;
        }
#else
        return FALSE;
#endif
    }
    return FALSE;	
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        WiFi_SetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int                         iValue
            );

    description:

        This function is called to set integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int                         iValue
                The updated integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
WiFi_SetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int                         iValue
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    UNREFERENCED_PARAMETER(hInsContext);

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_GoodRssiThreshold") == 0)
    {
		if ( ANSC_STATUS_SUCCESS == CosaDmlWiFi_SetGoodRssiThresholdValue( iValue ) )
		{
			pMyObject->iX_RDKCENTRAL_COM_GoodRssiThreshold = iValue;
	        return TRUE;			
		}
    }

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_AssocCountThreshold") == 0)
    {
		if ( ANSC_STATUS_SUCCESS == CosaDmlWiFi_SetAssocCountThresholdValue( iValue ) )
		{
			pMyObject->iX_RDKCENTRAL_COM_AssocCountThreshold = iValue;
	        return TRUE;			
		}
    }

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_AssocMonitorDuration") == 0)
    {
		if ( ANSC_STATUS_SUCCESS == CosaDmlWiFi_SetAssocMonitorDurationValue( iValue ) )
		{
			pMyObject->iX_RDKCENTRAL_COM_AssocMonitorDuration = iValue;
	        return TRUE;			
		}
    }

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_AssocGateTime") == 0)
    {
		if ( ANSC_STATUS_SUCCESS == CosaDmlWiFi_SetAssocGateTimeValue( iValue ) )
		{
			pMyObject->iX_RDKCENTRAL_COM_AssocGateTime = iValue;
	        return TRUE;			
		}
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        WiFi_SetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG                       uValue
            );

    description:

        This function is called to set ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG                       uValue
                The updated ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
WiFi_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "Status") == 0)
    {
        CosaDmlWiFi_setStatus(uValue, (ANSC_HANDLE)pMyObject);
        return TRUE;
    }

    return FALSE;
}

/***********************************************************************
APIs for Object:
	WiFi.X_RDKCENTRAL-COM_Syndication.WiFiRegion.

	*  WiFiRegion_GetParamStringValue
	*  WiFiRegion_SetParamStringValue

***********************************************************************/
ULONG
WiFiRegion_GetParamStringValue

	(
		ANSC_HANDLE 				hInsContext,
		char*						ParamName,
		char*						pValue,
		ULONG*						pulSize
	)
{
	PCOSA_DATAMODEL_WIFI			 pMyObject		 = (PCOSA_DATAMODEL_WIFI	 )g_pCosaBEManager->hWifi;
    PCOSA_DATAMODEL_RDKB_WIFIREGION            pWifiRegion     = pMyObject->pWiFiRegion;
    UNREFERENCED_PARAMETER(hInsContext);

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Code") == 0)
    {
#if defined(_COSA_BCM_MIPS_) || defined(_XB6_PRODUCT_REQ_) || defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_)
        AnscCopyString(pValue, pWifiRegion->Code.ActiveValue);
#else
        AnscCopyString( pValue, pWifiRegion->Code);
#endif
        *pulSize = AnscSizeOfString( pValue );
        return 0;
    }

    return -1;
}

#if defined(_COSA_BCM_MIPS_) || defined(_XB6_PRODUCT_REQ_) || defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_)
#define BS_SOURCE_WEBPA_STR "webpa"
#define BS_SOURCE_RFC_STR "rfc"
#define PARTNER_ID_LEN 64

char * getRequestorString()
{
   switch(g_currentWriteEntity)
   {
      case DSLH_MPA_ACCESS_CONTROL_WEBPA: //CCSP_COMPONENT_ID_WebPA from webpa_internal.h(parodus2ccsp)
      case DSLH_MPA_ACCESS_CONTROL_XPC: //CCSP_COMPONENT_ID_XPC
         return BS_SOURCE_WEBPA_STR;

      case DSLH_MPA_ACCESS_CONTROL_CLI: //DSLH_MPA_ACCESS_CONTROL_CLI
      case DSLH_MPA_ACCESS_CONTROL_CLIENTTOOL: //DSLH_MPA_ACCESS_CONTROL_CLIENTTOOL
         return BS_SOURCE_RFC_STR;

      default:
         return "unknown";
   }
}

char * getTime()
{
    time_t timer;
    static char buffer[50];
    struct tm* tm_info;
    time(&timer);
    tm_info = localtime(&timer);
    strftime(buffer, 50, "%Y-%m-%d %H:%M:%S ", tm_info);
    return buffer;
}
#endif

BOOL
WiFiRegion_SetParamStringValue


	(
		ANSC_HANDLE 				hInsContext,
		char*						ParamName,
		char*						pString
	)

{
	PCOSA_DATAMODEL_WIFI			 pMyObject		 = (PCOSA_DATAMODEL_WIFI	 )g_pCosaBEManager->hWifi;
    PCOSA_DATAMODEL_RDKB_WIFIREGION            pWifiRegion     = pMyObject->pWiFiRegion;
    UNREFERENCED_PARAMETER(hInsContext);

#if defined(_COSA_BCM_MIPS_) || defined(_XB6_PRODUCT_REQ_) || defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_)
    char * requestorStr = getRequestorString();
    char * currentTime = getTime();

    if ( g_currentBsUpdate == DSLH_CWMP_BS_UPDATE_firmware ||
         (g_currentBsUpdate == DSLH_CWMP_BS_UPDATE_rfcUpdate && !AnscEqualString(requestorStr, BS_SOURCE_RFC_STR, TRUE))
       )
    {
       CcspTraceWarning(("Do NOT allow override of param: %s bsUpdate = %lu, requestor = %s\n", ParamName, g_currentBsUpdate, requestorStr));
       return FALSE;
    }
#endif
    if (strcmp(ParamName, "Code") == 0)
    {
#if defined(_COSA_BCM_MIPS_) || defined(_XB6_PRODUCT_REQ_) || defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_)
                // If the requestor is RFC but the param was previously set by webpa, do not override it.
                if (AnscEqualString(requestorStr, BS_SOURCE_RFC_STR, TRUE) && AnscEqualString(pWifiRegion->Code.UpdateSource, BS_SOURCE_WEBPA_STR, TRUE))
                {
                        CcspTraceWarning(("Do NOT allow override of param: %s requestor = %lu updateSource = %s\n", ParamName, g_currentWriteEntity, pWifiRegion->Code.UpdateSource));
                        return FALSE;
                }
#endif
		if((strcmp(pString, "USI") == 0 ) || (strcmp(pString, "USO") == 0 ) ||  (strcmp(pString, "CAI") == 0 ) || (strcmp(pString, "CAO") == 0 ))
		{
			if ( ANSC_STATUS_SUCCESS == SetWiFiRegionCode( pString ) )
			{
#if defined(_COSA_BCM_MIPS_) || defined(_XB6_PRODUCT_REQ_) || defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_)
				AnscCopyString( pWifiRegion->Code.ActiveValue, pString );
                                AnscCopyString( pWifiRegion->Code.UpdateSource, requestorStr );

                                char PartnerID[PARTNER_ID_LEN] = {0};
                                if((CCSP_SUCCESS == getPartnerId(PartnerID) ) && (PartnerID[ 0 ] != '\0') )
                                   UpdateJsonParam("Device.WiFi.X_RDKCENTRAL-COM_Syndication.WiFiRegion.Code",PartnerID, pString, requestorStr, currentTime);
#else
				AnscCopyString( pWifiRegion->Code, pString );
#endif
				return TRUE;
			}
		}
    }

    return FALSE;
}

/***********************************************************************

 APIs for Object:

    WiFi.Radio.{i}.

    *  Radio_GetEntryCount
    *  Radio_GetEntry
    *  Radio_GetParamBoolValue
    *  Radio_GetParamIntValue
    *  Radio_GetParamUlongValue
    *  Radio_GetParamStringValue
    *  Radio_SetParamBoolValue
    *  Radio_SetParamIntValue
    *  Radio_SetParamUlongValue
    *  Radio_SetParamStringValue
    *  Radio_Validate
    *  Radio_Commit
    *  Radio_Rollback

***********************************************************************/
/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        Radio_GetEntryCount
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to retrieve the count of the table.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The count of the table

**********************************************************************/
ULONG
Radio_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    UNREFERENCED_PARAMETER(hInsContext);

    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    return pMyObject->RadioCount;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ANSC_HANDLE
        Radio_GetEntry
            (
                ANSC_HANDLE                 hInsContext,
                ULONG                       nIndex,
                ULONG*                      pInsNumber
            );

    description:

        This function is called to retrieve the entry specified by the index.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                ULONG                       nIndex,
                The index of this entry;

                ULONG*                      pInsNumber
                The output instance number;

    return:     The handle to identify the entry

**********************************************************************/
ANSC_HANDLE
Radio_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_DML_WIFI_RADIO            pWifiRadio    = NULL;
    if ( pMyObject->pRadio && nIndex < pMyObject->RadioCount )
    {
        pWifiRadio = pMyObject->pRadio+nIndex;

        *pInsNumber = pWifiRadio->Radio.Cfg.InstanceNumber;

        return pWifiRadio;
    }
    return NULL; /* return the handle */
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Radio_GetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:

        This function is called to retrieve Boolean parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL*                       pBool
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Radio_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio     = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;
    BOOLEAN                         bForceDisableFlag = FALSE;

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Enable") == 0)
    {
        /* If WiFiForceRadioDisable Feature has been enabled then the radio status should
           be false, since in the HAL the radio status has been set to down state which is
           not reflected in DML layer.
         */
        if (ANSC_STATUS_SUCCESS != CosaDmlWiFiGetCurrForceDisableWiFiRadio(&bForceDisableFlag))
        {
            return FALSE;
        }
        /* collect value */
        *pBool = (bForceDisableFlag == TRUE) ? FALSE : pWifiRadioFull->Cfg.bEnabled;
        
        return TRUE;
    }

    if (strcmp(ParamName, "Upstream") == 0)
    {
        /* collect value */
        *pBool = pWifiRadioFull->StaticInfo.bUpstream;
        
        return TRUE;
    }

    if (strcmp(ParamName, "AutoChannelSupported") == 0)
    {
        /* collect value */
        *pBool = pWifiRadioFull->StaticInfo.AutoChannelSupported;
        
        return TRUE;
    }

    if (strcmp(ParamName, "AutoChannelEnable") == 0)
    {
        /* collect value */
        *pBool = pWifiRadioFull->Cfg.AutoChannelEnable;
        
        return TRUE;
    }

    if (strcmp(ParamName, "IEEE80211hSupported") == 0)
    {
        /* collect value */
        *pBool = pWifiRadioFull->StaticInfo.IEEE80211hSupported;
        
        return TRUE;
    }

    if (strcmp(ParamName, "IEEE80211hEnabled") == 0)
    {
        /* collect value */
        *pBool = pWifiRadioFull->Cfg.IEEE80211hEnabled;
        
        return TRUE;
    }
	
	if (strcmp(ParamName, "X_COMCAST_COM_DFSSupport") == 0)
    {
        *pBool = pWifiRadioFull->Cfg.X_COMCAST_COM_DFSSupport;
        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST_COM_DFSEnable") == 0)
    {
        /* collect value */
#if defined CONFIG_DFS
        wifi_getRadioDfsEnable((pWifiRadio->Radio.Cfg.InstanceNumber - 1), &pWifiRadioFull->Cfg.X_COMCAST_COM_DFSEnable);
#endif
        *pBool = pWifiRadioFull->Cfg.X_COMCAST_COM_DFSEnable;
        
        return TRUE;
    }
	
	if (strcmp(ParamName, "X_COMCAST-COM_DCSSupported") == 0)
    {
        *pBool = pWifiRadioFull->Cfg.X_COMCAST_COM_DCSSupported;
        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_DCSEnable") == 0)
    {
        *pBool = pWifiRadioFull->Cfg.X_COMCAST_COM_DCSEnable;
        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST_COM_IGMPSnoopingEnable") == 0)
    {
        /* collect value */
        *pBool = pWifiRadioFull->Cfg.X_COMCAST_COM_IGMPSnoopingEnable;
        
        return TRUE;
    }
    
	if (strcmp(ParamName, "X_COMCAST-COM_AutoChannelRefreshPeriodSupported") == 0)
    {
	
        *pBool = pWifiRadioFull->Cfg.X_COMCAST_COM_AutoChannelRefreshPeriodSupported;
       
        return TRUE;
    }

	if (strcmp(ParamName, "X_COMCAST-COM_IEEE80211hSupported") == 0)
    {
        *pBool = pWifiRadioFull->Cfg.X_COMCAST_COM_IEEE80211hSupported;
        return TRUE;
    }
	
	if (strcmp(ParamName, "X_COMCAST-COM_ReverseDirectionGrantSupported") == 0)
    {
        *pBool = pWifiRadioFull->Cfg.X_COMCAST_COM_ReverseDirectionGrantSupported;
        return TRUE;
    }
	
	if (strcmp(ParamName, "X_COMCAST-COM_RtsThresholdSupported") == 0)
    {
	
        *pBool = pWifiRadioFull->Cfg.X_COMCAST_COM_RtsThresholdSupported;
          return TRUE;
    }
	

    if (strcmp(ParamName, "X_CISCO_COM_APIsolation") == 0)
    {
        /* collect value */
        *pBool = pWifiRadioFull->Cfg.APIsolation;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_FrameBurst") == 0)
    {
        /* collect value */
        *pBool = pWifiRadioFull->Cfg.FrameBurst;

        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_ApplySetting") == 0)
    {
        *pBool = pWifiRadioFull->Cfg.ApplySetting; 
        return TRUE;
    }
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_AutoChannelRefreshPeriodSupported") == 0)
    {
        *pBool = pWifiRadioFull->Cfg.AutoChannelRefreshPeriodSupported; 
        return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_RtsThresholdSupported") == 0)
    {
        *pBool = pWifiRadioFull->Cfg.RtsThresholdSupported; 
        return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_ReverseDirectionGrantSupported") == 0)
    {
        *pBool = pWifiRadioFull->Cfg.ReverseDirectionGrantSupported; 
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_ReverseDirectionGrant") == 0)
    {
        *pBool = pWifiRadioFull->Cfg.X_CISCO_COM_ReverseDirectionGrant; 
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_AggregationMSDU") == 0)
    {
        *pBool = pWifiRadioFull->Cfg.X_CISCO_COM_AggregationMSDU; 
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_AutoBlockAck") == 0)
    {
        *pBool = pWifiRadioFull->Cfg.X_CISCO_COM_AutoBlockAck; 
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_DeclineBARequest") == 0)
    {
        *pBool = pWifiRadioFull->Cfg.X_CISCO_COM_DeclineBARequest; 
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_STBCEnable") == 0)
    {
        *pBool = pWifiRadioFull->Cfg.X_CISCO_COM_STBCEnable; 
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_11nGreenfieldEnabled") == 0)
    {
        *pBool = pWifiRadioFull->Cfg.X_CISCO_COM_11nGreenfieldEnabled; 
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_WirelessOnOffButton") == 0)
    {
        *pBool = pWifiRadioFull->Cfg.X_CISCO_COM_WirelessOnOffButton; 
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Radio_GetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int*                        pInt
            );

    description:

        This function is called to retrieve integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int*                        pInt
                The buffer of returned integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Radio_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio     = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "MCS") == 0)
    {
        /* collect value */
        *pInt = pWifiRadioFull->Cfg.MCS;
        
        return TRUE;
    }

    if (strcmp(ParamName, "TransmitPower") == 0)
    {
        /* collect value */
        *pInt = pWifiRadioFull->Cfg.TransmitPower;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_MbssUserControl") == 0)
    {
        *pInt = pWifiRadioFull->Cfg.MbssUserControl; 
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_AdminControl") == 0)
    {
        *pInt = pWifiRadioFull->Cfg.AdminControl; 
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_OnOffPushButtonTime") == 0)
    {
        *pInt = pWifiRadioFull->Cfg.OnOffPushButtonTime; 
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_ObssCoex") == 0)
    {
        *pInt = pWifiRadioFull->Cfg.ObssCoex; 
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_MulticastRate") == 0)
    {
        *pInt = pWifiRadioFull->Cfg.MulticastRate; 
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_ApplySettingSSID") == 0)
    {
        *pInt = pWifiRadioFull->Cfg.ApplySettingSSID; 
        return TRUE;
    }
    if (strcmp(ParamName, "X_COMCAST-COM_CarrierSenseThresholdRange") == 0)
    {
        CosaDmlWiFi_getRadioCarrierSenseThresholdRange((pWifiRadio->Radio.Cfg.InstanceNumber - 1),pInt);
	 return TRUE;
    }
    if (strcmp(ParamName, "X_COMCAST-COM_CarrierSenseThresholdInUse") == 0)
    {
        CosaDmlWiFi_getRadioCarrierSenseThresholdInUse((pWifiRadio->Radio.Cfg.InstanceNumber - 1),pInt); 
        return TRUE;
    }
    if (strcmp(ParamName, "X_COMCAST-COM_ChannelSwitchingCount") == 0)
    {

		*pInt = gChannelSwitchingCount;
        return TRUE;
    }
   if (strcmp(ParamName, "X_RDKCENTRAL-COM_DCSDwelltime") == 0)
    {
        //CosaDmlWiFi_getRadioDcsDwelltime(pWifiRadio->Radio.Cfg.InstanceNumber,pInt);
        return TRUE;
    }
	//if (AnscEqualString(ParamName, "X_RDKCENTRAL-COM_DCSHighChannelUsageThreshold", TRUE))
    //{
	//	CosaDmlWiFi_getRadioDCSHighChannelUsageThreshold(pWifiRadio->Radio.Cfg.InstanceNumber,pInt);
    //    return TRUE;
    //}

	if (strcmp(ParamName, "X_RDKCENTRAL-COM_clientInactivityTimeout") == 0)
	{
	  /* collect value */
	  *pInt = pWifiRadioFull->Cfg.iX_RDKCENTRAL_COM_clientInactivityTimeout;
	  return TRUE;
	}

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Radio_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Radio_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_DML_WIFI_RADIO            pWifiRadio     = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg  = &pWifiRadioFull->Cfg;

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "X_COMCAST_COM_RadioUpTime") == 0)
    {
        /* collect value */
        ULONG TimeInSecs = 0;
        *puLong = TimeInSecs;
        if(ANSC_STATUS_SUCCESS == CosaDmlWiFi_RadioUpTime(&TimeInSecs, (pWifiRadio->Radio.Cfg.InstanceNumber - 1))) {
            *puLong = TimeInSecs;
        }
        return TRUE;
    }
    if (strcmp(ParamName, "Status") == 0)
    {
        CosaDmlWiFiRadioGetDinfo((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiRadio->Radio.Cfg.InstanceNumber, &pWifiRadio->Radio.DynamicInfo);
    
        /* collect value */
        *puLong = pWifiRadioFull->DynamicInfo.Status;
        
        return TRUE;
    }

    if (strcmp(ParamName, "LastChange") == 0)
    {
        /* collect value */
        *puLong = AnscGetTimeIntervalInSeconds(pWifiRadioFull->Cfg.LastChange, AnscGetTickInSeconds()); 
        
        return TRUE;
    }

    if (strcmp(ParamName, "MaxBitRate") == 0)
    {
        /* collect value */
        *puLong = pWifiRadioFull->StaticInfo.MaxBitRate;
        
        return TRUE;
    }

    if (strcmp(ParamName, "SupportedFrequencyBands") == 0)
    {
        /* collect value */
        *puLong = pWifiRadioFull->StaticInfo.SupportedFrequencyBands;
        
        return TRUE;
    }

    if (strcmp(ParamName, "Channel") == 0)
    {
        /* collect value */
        COSA_DML_WIFI_RADIO_CFG cfgTemp = {0};
        memcpy(&cfgTemp, &pWifiRadio->Radio.Cfg, sizeof(cfgTemp));

        CosaDmlWiFiRadioGetDCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, &cfgTemp);
        *puLong = cfgTemp.Channel;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_LGI-COM_ChannelSetting") == 0)
    {
        ULONG ulResult;
        if (wifi_getRadioConfiguredChannel(pWifiRadioCfg->InstanceNumber - 1, &ulResult) == 0)
        {
            *puLong = ulResult;
            return TRUE;
        }
    }

    if (strcmp(ParamName, "RunningChannel") == 0)
    {
        ULONG ulResult = 0;
        if (wifi_getRadioRunningChannel(pWifiRadioCfg->InstanceNumber - 1, &ulResult) == 0)
        {
            *puLong = ulResult;
        }
        return TRUE;
    }

    if (strcmp(ParamName, "AutoChannelRefreshPeriod") == 0)
    {
		//zqiu:  Reason for change: Device.WiFi.Radio.10000.AutoChannelRefreshPeriod parameter is getting updated even after Device.WiFi.Radio.10000.AutoChannelEnable is disabled.
		if(pWifiRadioFull->Cfg.AutoChannelEnable == TRUE)
		{
			/* collect value */
			*puLong = pWifiRadioFull->Cfg.AutoChannelRefreshPeriod;
        }
        return TRUE;
    }

    if (strcmp(ParamName, "OperatingChannelBandwidth") == 0)
    {
        /* collect value */

        CosaDmlWiFiRadioGetDBWCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, &pWifiRadio->Radio.Cfg);
	*puLong = pWifiRadioFull->Cfg.OperatingChannelBandwidth;
        
        return TRUE;
    }

    if (strcmp(ParamName, "ExtensionChannel") == 0)
    {
        /* collect value */
        *puLong = pWifiRadioFull->Cfg.ExtensionChannel;
        
        return TRUE;
    }

    if (strcmp(ParamName, "GuardInterval") == 0)
    {
        /* collect value */
        *puLong = pWifiRadioFull->Cfg.GuardInterval;
        
        return TRUE;
    }

    if (strcmp(ParamName, "RetryLimit") == 0)
    {
        /* collect value */
        *puLong = pWifiRadioFull->Cfg.RetryLimit;

        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_RTSThreshold") == 0)
    {
        /* collect value */
        *puLong = pWifiRadioFull->Cfg.RTSThreshold;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_FragmentationThreshold") == 0)
    {
        /* collect value */
        *puLong = pWifiRadioFull->Cfg.FragmentationThreshold;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_DTIMInterval") == 0)
    {
        /* collect value */
        *puLong = pWifiRadioFull->Cfg.DTIMInterval;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_BeaconInterval") == 0)
    {
        /* collect value */
        *puLong = pWifiRadioFull->Cfg.BeaconInterval;
        
        return TRUE;
    }
    if (strcmp(ParamName, "BeaconPeriod") == 0)
    {
        /* collect value */
        *puLong = pWifiRadioFull->Cfg.BeaconInterval;
        CosaDmlWiFi_getRadioBeaconPeriod((pWifiRadio->Radio.Cfg.InstanceNumber -1), (UINT *)puLong);
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_TxRate") == 0)
    {
        /* collect value */
        *puLong = pWifiRadioFull->Cfg.TxRate;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_BasicRate") == 0)
    {
        /* collect value */

       *puLong = pWifiRadioFull->Cfg.BasicRate; 
        
        return TRUE;
    }
    
    if (strcmp(ParamName, "X_CISCO_COM_CTSProtectionMode") == 0)
    {
        /* collect value */
        *puLong = (FALSE == pWifiRadioFull->Cfg.CTSProtectionMode) ? 0 : 1;

        return TRUE;
    }
#if 0
    if (strcmp(ParamName, "X_CISCO_COM_HTTxStream") == 0)
    {
        *puLong = pWifiRadioFull->Cfg.X_CISCO_COM_HTTxStream; 
        return TRUE;
    }
  
    if (strcmp(ParamName, "X_CISCO_COM_HTRxStream") == 0)
    {
        *puLong = pWifiRadioFull->Cfg.X_CISCO_COM_HTRxStream; 
        return TRUE;
    }
 #endif   
    if (strcmp(ParamName, "RadioResetCount") == 0)
	{
	    printf(" **** wifi_dml RadioResetCount : Entry **** \n");
		if (CosaDmlWiFi_RadioGetResetCount((pWifiRadio->Radio.Cfg.InstanceNumber -1),puLong) != ANSC_STATUS_SUCCESS)
			return FALSE;

		return TRUE;
	}
    
	if (strcmp(ParamName, "X_RDKCENTRAL-COM_ChanUtilSelfHealEnable") == 0)
    {
		CosaDmlWiFi_getChanUtilSelfHealEnable(pWifiRadio->Radio.Cfg.InstanceNumber,puLong);
       	return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_ChannelUtilThreshold") == 0)
    {

		CosaDmlWiFi_getChanUtilThreshold(pWifiRadio->Radio.Cfg.InstanceNumber, (PUINT)puLong);
        return TRUE;
    }
  
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        Radio_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
Radio_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio     = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    
    #if !defined(_XB6_PRODUCT_REQ_) || defined(_COSA_BCM_MIPS_)
        int radioIndex=pWifiRadio->Radio.Cfg.InstanceNumber - 1;
    #endif
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Alias") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString(pWifiRadioFull->Cfg.Alias) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiRadioFull->Cfg.Alias);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiRadioFull->Cfg.Alias)+1;
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "Name") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString(pWifiRadioFull->StaticInfo.Name) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiRadioFull->StaticInfo.Name);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiRadioFull->StaticInfo.Name)+1;
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "LowerLayers") == 0)
    {
        /*TR-181: Since Radio is a layer 1 interface, 
          it is expected that LowerLayers will not be used
         */
         /* collect value */
        AnscCopyString(pValue, "Not Applicable");
        return 0;
    }

    if (strcmp(ParamName, "OperatingFrequencyBand") == 0)
    {
        /* collect value */
        if ( 5 < *pUlSize)
        {
            if ( pWifiRadioFull->Cfg.OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_2_4G )
            {
                AnscCopyString(pValue, "2.4GHz");
            }
            else if ( pWifiRadioFull->Cfg.OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_5G )
            {
                AnscCopyString(pValue, "5GHz");
            }
            return 0;
        }
        else
        {
            *pUlSize = 6;
            
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "OperatingStandards") == 0)
    {
        /* collect value */
        char buf[512] = {0};
        if (pWifiRadioFull->Cfg.OperatingStandards & COSA_DML_WIFI_STD_a )
        {
            strcat(buf, "a");
        }
        
        if (pWifiRadioFull->Cfg.OperatingStandards & COSA_DML_WIFI_STD_b )
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",b");
            }
            else
            {
                strcat(buf, "b");
            }
        }
        
        if (pWifiRadioFull->Cfg.OperatingStandards & COSA_DML_WIFI_STD_g )
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",g");
            }
            else
            {
                strcat(buf, "g");
            }
        }
        
        if (pWifiRadioFull->Cfg.OperatingStandards & COSA_DML_WIFI_STD_n )
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",n");
            }
            else
            {
                strcat(buf, "n");
            }
        }

        if (pWifiRadioFull->Cfg.OperatingStandards & COSA_DML_WIFI_STD_ac )
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",ac");
            }
            else
            {
                strcat(buf, "ac");
            }
        }
#ifdef _WIFI_AX_SUPPORT_
        if (pWifiRadioFull->Cfg.OperatingStandards & COSA_DML_WIFI_STD_ax )
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",ax");
            }
            else
            {
                strcat(buf, "ax");
            }
        }
#endif
        if ( AnscSizeOfString(buf) < *pUlSize)
        {
            AnscCopyString(pValue, buf);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(buf)+1;
            
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "PossibleChannels") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString(pWifiRadioFull->StaticInfo.PossibleChannels) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiRadioFull->StaticInfo.PossibleChannels);
            
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiRadioFull->StaticInfo.PossibleChannels)+1;
            
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "ChannelsInUse") == 0)
    {
        CosaDmlWiFiRadioGetChannelsInUse((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiRadio->Radio.Cfg.InstanceNumber, &pWifiRadio->Radio.DynamicInfo);
        /* collect value */
        if ( AnscSizeOfString(pWifiRadioFull->DynamicInfo.ChannelsInUse) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiRadioFull->DynamicInfo.ChannelsInUse);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiRadioFull->DynamicInfo.ChannelsInUse)+1;
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "X_CISCO_COM_ApChannelScan") == 0)
    {
        CosaDmlWiFiRadioGetApChannelScan((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiRadio->Radio.Cfg.InstanceNumber, &pWifiRadio->Radio.DynamicInfo);
        /* collect value */
        if ( AnscSizeOfString(pWifiRadioFull->DynamicInfo.ApChannelScan) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiRadioFull->DynamicInfo.ApChannelScan);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiRadioFull->DynamicInfo.ApChannelScan)+1;
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "TransmitPowerSupported") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString(pWifiRadioFull->StaticInfo.TransmitPowerSupported) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiRadioFull->StaticInfo.TransmitPowerSupported);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiRadioFull->StaticInfo.TransmitPowerSupported)+1;
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "RegulatoryDomain") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString(pWifiRadioFull->Cfg.RegulatoryDomain) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiRadioFull->Cfg.RegulatoryDomain);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiRadioFull->Cfg.RegulatoryDomain)+1;
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "SupportedStandards") == 0)
    {
        /* collect value */
        char buf[512] = {0};
        if(CosaDmlWiFiRadiogetSupportedStandards(pWifiRadio->Radio.Cfg.InstanceNumber-1, &pWifiRadioFull->StaticInfo.SupportedStandards) != ANSC_STATUS_SUCCESS)
        {
            CcspTraceError(("CosaDmlWiFiRadiogetSupportedStandards returns error\n"));
            return -1;
        }
        if (pWifiRadioFull->StaticInfo.SupportedStandards & COSA_DML_WIFI_STD_a )
        {
            strcat(buf, "a");
        }
        
        if (pWifiRadioFull->StaticInfo.SupportedStandards & COSA_DML_WIFI_STD_b )
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",b");
            }
            else
            {
                strcat(buf, "b");
            }
        }
        
        if (pWifiRadioFull->StaticInfo.SupportedStandards & COSA_DML_WIFI_STD_g )
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",g");
            }
            else
            {
                strcat(buf, "g");
            }
        }
        
        if (pWifiRadioFull->StaticInfo.SupportedStandards & COSA_DML_WIFI_STD_n )
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",n");
            }
            else
            {
                strcat(buf, "n");
            }
        }
        
        if (pWifiRadioFull->StaticInfo.SupportedStandards & COSA_DML_WIFI_STD_ac )
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",ac");
            }
            else
            {
                strcat(buf, "ac");
            }
        }
#ifdef _WIFI_AX_SUPPORT_
        if (pWifiRadioFull->StaticInfo.SupportedStandards & COSA_DML_WIFI_STD_ax )
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",ax");
            }
            else
            {
                strcat(buf, "ax");
            }
        }
#endif
        
        if ( AnscSizeOfString(buf) < *pUlSize)
        {
            AnscCopyString(pValue, buf);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(buf)+1;
            
            return 1;
        }
        return 0;
    }

	if (strcmp(ParamName, "BasicDataTransmitRates") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString(pWifiRadioFull->Cfg.BasicDataTransmitRates) < *pUlSize)
        {
#if defined(_COSA_BCM_MIPS_) || defined(DUAL_CORE_XB3)
	    char buf[1024] = {0};
	    if(CosaDmlWiFiGetRadioBasicDataTransmitRates(radioIndex,buf) == 0)
	    {
            	AnscCopyString(pValue,buf);
               CcspTraceDebug(("%s Radio %d has BasicDataTransmitRates - %s  \n", __FUNCTION__,radioIndex,pValue));
	    }
	    else
            {
            	CcspTraceError(("%s:%d CosaDmlWiFiGetRadioBasicDataTransmitRates returning Error \n",__func__, __LINE__));
            }      
#else
	    AnscCopyString(pValue, pWifiRadioFull->Cfg.BasicDataTransmitRates);
#endif
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiRadioFull->Cfg.BasicDataTransmitRates)+1;
            return 1;
        }
        return 0;        
    }
    	if (strcmp(ParamName, "SupportedDataTransmitRates") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString(pWifiRadioFull->Cfg.SupportedDataTransmitRates) < *pUlSize)
        {
#if defined(_COSA_BCM_MIPS_)      
	    char buf[1024] = {0};
	    if(CosaDmlWiFiGetRadioSupportedDataTransmitRates(radioIndex,buf) == 0)
	    {
            	AnscCopyString(pValue,buf);
               CcspTraceDebug(("%s  Radio %d has SupportedDataTransmitRates - %s  \n", __FUNCTION__,radioIndex,pValue));
            }
	    else
            {
            	CcspTraceError(("%s:%d CosaDmlWiFiGetRadioSupportedDataTransmitRates returning Error \n",__func__, __LINE__));
            }
#else
            AnscCopyString(pValue, pWifiRadioFull->Cfg.SupportedDataTransmitRates);
#endif
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiRadioFull->Cfg.SupportedDataTransmitRates)+1;
            return 1;
        }
        return 0;
    }
    	if (strcmp(ParamName, "OperationalDataTransmitRates") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString(pWifiRadioFull->Cfg.OperationalDataTransmitRates) < *pUlSize)
        {
            /* No validation to check for Unsupported values is there in XB6 devices. For both
               radios the values are same in HAL itself. Hence the value is not fetched diretly
               from HAL for XB6 devices.
             */
#if !defined(_XB6_PRODUCT_REQ_)
	    char buf[1024] = {0};
	    if(CosaDmlWiFiGetRadioOperationalDataTransmitRates(radioIndex,buf) == 0)
	    {	
            	AnscCopyString(pValue,buf);
               CcspTraceDebug(("%s Radio %d has OperationalDataTransmitRates - %s  \n", __FUNCTION__,radioIndex,pValue));
	    }
	    else
	    {
            	CcspTraceError(("%s:%d CosaDmlWiFiGetRadioOperationalDataTransmitRates returning Error \n",__func__, __LINE__));
	    }
#else
            AnscCopyString(pValue, pWifiRadioFull->Cfg.OperationalDataTransmitRates);
#endif
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiRadioFull->Cfg.OperationalDataTransmitRates)+1;
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "CurrentOperatingChannelBandwidth") == 0)
    {
        char output_string[64] = {};
        if(!wifi_getCurrentRadioOperatingChannelBandwidth(pWifiRadio->Radio.Cfg.InstanceNumber - 1, output_string))
        {
            AnscCopyString(pValue, output_string);
            return 0;
        }
        else
        {
            CcspTraceError(("%s:%d wifi_getCurrentRadioOperatingChannelBandwidth returning error \n",__func__, __LINE__));
            return 1;
        }
    }
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return -1;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Radio_SetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL                        bValue
            );

    description:

        This function is called to set BOOL parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL                        bValue
                The updated BOOL value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Radio_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio     = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;
    BOOLEAN                         bForceDisableFlag = FALSE;


    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "Enable") == 0)
    {
        if (ANSC_STATUS_SUCCESS != CosaDmlWiFiGetForceDisableWiFiRadio(&bForceDisableFlag))
        {
            return FALSE;
        }
        if(!bForceDisableFlag)
        {
             if ( pWifiRadioFull->Cfg.bEnabled == bValue )
             {
                 return  TRUE;
             }
            /* save update to backup */
            pWifiRadioFull->Cfg.bEnabled = bValue;
            pWifiRadio->bRadioChanged = TRUE;
            return TRUE;
        } else {
            CcspWifiTrace(("RDK_LOG_ERROR, WIFI_ATTEMPT_TO_CHANGE_CONFIG_WHEN_FORCE_DISABLED\n" ));
            return FALSE;
        }
    }

    if (strcmp(ParamName, "AutoChannelEnable") == 0)
    {
        if ( pWifiRadioFull->Cfg.AutoChannelEnable == bValue )
        {
            return  TRUE;
        }

        /* save update to backup */
        pWifiRadioFull->Cfg.AutoChannelEnable = bValue;
        pWifiRadio->bRadioChanged = TRUE;
        return TRUE;
    }

    if (strcmp(ParamName, "IEEE80211hEnabled") == 0)
    {
        if ( pWifiRadioFull->Cfg.IEEE80211hEnabled == bValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.IEEE80211hEnabled = bValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST_COM_DFSEnable") == 0)
    {
        if ( pWifiRadioFull->Cfg.X_COMCAST_COM_DFSEnable == bValue )
        {
            return  TRUE;
        }

        // Only allow disabling DFS if ExcludeDFS is enabled.
        if ((pWifiRadioFull->Cfg.EnhancedACS.ExcludeDFS == FALSE) && (bValue == FALSE))
        {
            return FALSE;
        }

        /* save update to backup */
        pWifiRadioFull->Cfg.X_COMCAST_COM_DFSEnable = bValue;
        pWifiRadio->bRadioChanged = TRUE;

        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_DCSEnable") == 0)
    {
        if ( pWifiRadioFull->Cfg.X_COMCAST_COM_DCSEnable == bValue )
        {
            return  TRUE;
        }

        if ((TRUE == bValue) && is_mesh_enabled())
        {
            CcspWifiTrace(("RDK_LOG_WARN,DCS_ERROR:Fail to enable DCS when Mesh is on \n"));
            return FALSE;
        }

        /* save update to backup */
        pWifiRadioFull->Cfg.X_COMCAST_COM_DCSEnable = bValue;
        pWifiRadio->bRadioChanged = TRUE;

        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST_COM_IGMPSnoopingEnable") == 0)
    {
        if ( pWifiRadioFull->Cfg.X_COMCAST_COM_IGMPSnoopingEnable == bValue )
        {
            return  TRUE;
        }

        /* save update to backup */
        pWifiRadioFull->Cfg.X_COMCAST_COM_IGMPSnoopingEnable = bValue;
        pWifiRadio->bRadioChanged = TRUE;

        return TRUE;
    }


    if (strcmp(ParamName, "X_CISCO_COM_APIsolation") == 0)
    {
        if ( pWifiRadioFull->Cfg.APIsolation == bValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.APIsolation = bValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_FrameBurst") == 0)
    {
        if ( pWifiRadioFull->Cfg.FrameBurst == bValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.FrameBurst = bValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_ApplySetting") == 0)
    {
        if ( pWifiRadioFull->Cfg.ApplySetting == bValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.ApplySetting = bValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_ReverseDirectionGrant") == 0)
    {
        if ( pWifiRadioFull->Cfg.X_CISCO_COM_ReverseDirectionGrant == bValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.X_CISCO_COM_ReverseDirectionGrant = bValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_AggregationMSDU") == 0)
    {
        if ( pWifiRadioFull->Cfg.X_CISCO_COM_AggregationMSDU == bValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.X_CISCO_COM_AggregationMSDU = bValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_AutoBlockAck") == 0)
    {
        if ( pWifiRadioFull->Cfg.X_CISCO_COM_AutoBlockAck == bValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.X_CISCO_COM_AutoBlockAck = bValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_DeclineBARequest") == 0)
    {
        if ( pWifiRadioFull->Cfg.X_CISCO_COM_DeclineBARequest == bValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.X_CISCO_COM_DeclineBARequest = bValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_STBCEnable") == 0)
    {
        if ( pWifiRadioFull->Cfg.X_CISCO_COM_STBCEnable == bValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.X_CISCO_COM_STBCEnable = bValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_11nGreenfieldEnabled") == 0)
    {
        if ( pWifiRadioFull->Cfg.X_CISCO_COM_11nGreenfieldEnabled == bValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.X_CISCO_COM_11nGreenfieldEnabled = bValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_WirelessOnOffButton") == 0)
    {
        if ( pWifiRadioFull->Cfg.X_CISCO_COM_WirelessOnOffButton == bValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.X_CISCO_COM_WirelessOnOffButton = bValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

#if 0 //need to turn on this after X_RDKCENTRAL-COM_DCSEnable get into stable2
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_DCSEnable") == 0)
	{
		CcspTraceInfo(("%s X_RDKCENTRAL-COM_DCSEnable %d\n", __FUNCTION__, bValue));
		if ( pWifiRadioFull->Cfg.X_RDKCENTRAL_COM_DCSEnable == bValue )
		{
		    return  TRUE;
		}
		if ((TRUE == bValue) && is_mesh_enabled())
		{
			CcspWifiTrace(("RDK_LOG_WARN,DCS_ERROR:Fail to enable DCS when Mesh is on \n"));
			return FALSE;
		}
		/* save update to backup */
		pWifiRadioFull->Cfg.X_RDKCENTRAL_COM_DCSEnable= bValue;
		pWifiRadio->bRadioChanged = TRUE;
		CosaDmlWiFi_setDCSScan(pWifiRadioFull->Cfg.InstanceNumber,bValue);
		return TRUE;
	}
#endif
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

#if defined CONFIG_DFS
BOOL
IsValidDFSChannel(int apIndex, int channel)
{
    BOOLEAN dfsEnable;
    wifi_getRadioDfsEnable(apIndex, &dfsEnable);

    if(!dfsEnable) {
        int i;
        int channelList_5G_dfs [] = {52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140};
        for (i=0; i<15; i++)
        {
            if(channel == channelList_5G_dfs[i]) {
                return FALSE;
            }
        }
    }
    return TRUE;
}
#endif

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Radio_SetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int                         iValue
            );

    description:

        This function is called to set integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int                         iValue
                The updated integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Radio_SetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int                         iValue
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio     = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;

 
    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "MCS") == 0)
    {
        if ( pWifiRadioFull->Cfg.MCS == iValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.MCS = iValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "TransmitPower") == 0)
    {
        if (pWifiRadioFull->Cfg.TransmitPower == iValue )
        {
            return  TRUE;
        }

        /* save update to backup */
        pWifiRadioFull->Cfg.TransmitPower = iValue;
        pWifiRadio->bRadioChanged = TRUE;

        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_MbssUserControl") == 0)
    {
        if ( pWifiRadioFull->Cfg.MbssUserControl == iValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.MbssUserControl = iValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_AdminControl") == 0)
    {
        if ( pWifiRadioFull->Cfg.AdminControl == iValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.AdminControl = iValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_OnOffPushButtonTime") == 0)
    {
        if ( pWifiRadioFull->Cfg.OnOffPushButtonTime == iValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.OnOffPushButtonTime = iValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_ObssCoex") == 0)
    {
        if ( pWifiRadioFull->Cfg.ObssCoex == iValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.ObssCoex = iValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_MulticastRate") == 0)
    {
        if ( pWifiRadioFull->Cfg.MulticastRate == iValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.MulticastRate = iValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_ApplySettingSSID") == 0)
    {
        if ( pWifiRadioFull->Cfg.ApplySettingSSID == iValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.ApplySettingSSID = iValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }
    if (strcmp(ParamName, "X_COMCAST-COM_CarrierSenseThresholdInUse") == 0)
    {         
        CosaDmlWiFi_setRadioCarrierSenseThresholdInUse((pWifiRadio->Radio.Cfg.InstanceNumber - 1),iValue);
        return TRUE;
    }
    
	if (strcmp(ParamName, "X_RDKCENTRAL-COM_DCSDwelltime") == 0)
    {

	//CosaDmlWiFi_setRadioDcsDwelltime(pWifiRadio->Radio.Cfg.InstanceNumber,iValue);
	return TRUE;
    }
    //if( AnscEqualString(ParamName, "X_RDKCENTRAL-COM_DCSHighChannelUsageThreshold", TRUE))
    //{
	//CosaDmlWiFi_setRadioDCSHighChannelUsageThreshold(pWifiRadio->Radio.Cfg.InstanceNumber,iValue);
	//return TRUE;
    //}

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Radio_SetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG                       uValue
            );

    description:

        This function is called to set ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG                       uValue
                The updated ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Radio_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio     = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;


    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "Channel") == 0)
    {
        /* save update to backup */
        pWifiRadioFull->Cfg.Channel = uValue;
        pWifiRadioFull->Cfg.AutoChannelEnable = FALSE; /* User has manually set a channel */
        pWifiRadio->bRadioChanged = TRUE;
       // pWifiRadioFull->Cfg.ChannelSwitchingCount++;
        gChannelSwitchingCount++;
        return TRUE;
    }

    if (strcmp(ParamName, "AutoChannelRefreshPeriod") == 0)
    {
#if defined(_HUB4_PRODUCT_REQ_)
        if ( pWifiRadioFull->Cfg.AutoChannelEnable  == FALSE )
        {
            return  FALSE;
        }
#endif

        if ( pWifiRadioFull->Cfg.AutoChannelRefreshPeriod == uValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.AutoChannelRefreshPeriod = uValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "OperatingChannelBandwidth") == 0)
    {
        if ( pWifiRadioFull->Cfg.OperatingChannelBandwidth == uValue )
        {
            return  TRUE;
        }

        /* save update to backup */
        pWifiRadioFull->Cfg.OperatingChannelBandwidth = uValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "ExtensionChannel") == 0)
    {
        if ( pWifiRadioFull->Cfg.ExtensionChannel == uValue )
        {
            return  TRUE;
        }
        /* save update to backup */
        pWifiRadioFull->Cfg.ExtensionChannel = uValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "GuardInterval") == 0)
    {
        if ( pWifiRadioFull->Cfg.GuardInterval == uValue )
        {
            return  TRUE;
        }

        /* save update to backup */
        pWifiRadioFull->Cfg.GuardInterval = uValue;
        pWifiRadio->bRadioChanged = TRUE;

        return TRUE;
    }

    if (strcmp(ParamName, "RetryLimit") == 0)
    {
        if ( pWifiRadioFull->Cfg.RetryLimit == uValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.RetryLimit = uValue;
        pWifiRadio->bRadioChanged = TRUE;

        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_RTSThreshold") == 0)
    {
        if ( pWifiRadioFull->Cfg.RTSThreshold == uValue )
        {
            return  TRUE;
        }
        /* save update to backup */
        pWifiRadioFull->Cfg.RTSThreshold = uValue;
        pWifiRadio->bRadioChanged = TRUE;
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_FragmentationThreshold") == 0)
    {
        if ( pWifiRadioFull->Cfg.FragmentationThreshold == uValue )
        {
            return  TRUE;
        }
        /* save update to backup */
        pWifiRadioFull->Cfg.FragmentationThreshold = uValue;
        pWifiRadio->bRadioChanged = TRUE;

        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_DTIMInterval") == 0)
    {
        if ( pWifiRadioFull->Cfg.DTIMInterval == uValue )
        {
            return  TRUE;
        }

        /* save update to backup */
        pWifiRadioFull->Cfg.DTIMInterval = uValue;
		pWifiRadio->bRadioChanged = TRUE;

        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_BeaconInterval") == 0)
    {
       if ( pWifiRadioFull->Cfg.BeaconInterval == uValue )
	{
            return  TRUE;
        }

        /* save update to backup */
        pWifiRadioFull->Cfg.BeaconInterval = uValue;
        CosaDmlWiFi_setRadioBeaconPeriod((pWifiRadio->Radio.Cfg.InstanceNumber - 1),uValue);
        return TRUE;
    }
    if (strcmp(ParamName, "BeaconPeriod") == 0)
    {
        if ( pWifiRadioFull->Cfg.BeaconInterval == uValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.BeaconInterval = uValue;
	CosaDmlWiFi_setRadioBeaconPeriod((pWifiRadio->Radio.Cfg.InstanceNumber - 1),uValue);
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_TxRate") == 0)
    {
        if ( pWifiRadioFull->Cfg.TxRate == uValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.TxRate = uValue;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_BasicRate") == 0)
    {
        if ( pWifiRadioFull->Cfg.BasicRate == uValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.BasicRate = uValue; 
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }
	
    if (strcmp(ParamName, "X_CISCO_COM_CTSProtectionMode") == 0)
    {
        if ( pWifiRadioFull->Cfg.CTSProtectionMode == (0 != uValue) )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.CTSProtectionMode = (0 == uValue) ? FALSE : TRUE;
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_ChanUtilSelfHealEnable") == 0)
    {
		CosaDmlWiFi_setChanUtilSelfHealEnable(pWifiRadioFull->Cfg.InstanceNumber,uValue);
        return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_ChannelUtilThreshold") == 0)
    {

		CosaDmlWiFi_setChanUtilThreshold(pWifiRadio->Radio.Cfg.InstanceNumber,uValue);
        return TRUE;
    }
#if 0
    if (strcmp(ParamName, "X_CISCO_COM_HTTxStream") == 0)
    {
        if ( pWifiRadioFull->Cfg.X_CISCO_COM_HTTxStream == uValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.X_CISCO_COM_HTTxStream = uValue; 
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_HTRxStream") == 0)
    {
        if ( pWifiRadioFull->Cfg.X_CISCO_COM_HTRxStream == uValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiRadioFull->Cfg.X_CISCO_COM_HTRxStream = uValue; 
        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
#endif
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Radio_SetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pString
            );

    description:

        This function is called to set string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pString
                The updated string value;

    return:     TRUE if succeeded.

**********************************************************************/
static BOOL isValidTransmitRate(char *Btr)
{
    BOOL isValid=false;
    if (!Btr) 
    {
        return isValid;
    }
    else
    {
	int i=0;
	int len;
	len=strlen(Btr);
	for(i=0;i<len;i++)
	{
	   if(isdigit(Btr[i]) || Btr[i]==',' || Btr[i]=='.')
	   {
	      isValid=true;
	   }
	   else
	   {
	      isValid=false;
	      break;
	   }
	 }    
     }
     return isValid;
}
//Check the input has only a,b,g,n,ac and ax
BOOL isValidOperStdString(char *OperStd)
{
    char *p,str[32];
    BOOL valid = FALSE;
    int i, count =0;
    strncpy(str,OperStd,(sizeof(str) -1));
    str[sizeof(str) - 1] = '\0';
//Remove Spaces!
    for ( i = 0; str[ i ] != '\0'; i++ )
    {
        if ( str[i] != ' ' )
        {
            str[count++] = str[i];
        }
    }
    str[count] = '\0';
    p = strtok( str, ",");
    while ( p!= NULL )
    {
        if( (0 == strcmp( p, "a")) ||
            (0 == strcmp( p, "b")) ||
            (0 == strcmp( p, "g")) ||
            (0 == strcmp( p, "n")) ||
            (0 == strcmp( p, "ac")) ||
            (0 == strcmp( p, "ax"))
           )
            valid = TRUE;
        else
            return FALSE;
        p = strtok(NULL, ",");
    }
    return valid;
}
BOOL
Radio_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio     = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;


    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "Alias") == 0)
    {
        /* save update to backup */
        AnscCopyString( pWifiRadioFull->Cfg.Alias, pString );
        return TRUE;
    }

    if (strcmp(ParamName, "LowerLayers") == 0)
    {
        /*TR-181: Since Radio is a layer 1 interface, 
          it is expected that LowerLayers will not be used
         */
        /* User shouldnt be able to set a value for this */
        return FALSE;
    }

    if (strcmp(ParamName, "OperatingFrequencyBand") == 0)
    {
        COSA_DML_WIFI_FREQ_BAND     TmpFreq;
        /* save update to backup */
        if (( AnscEqualString(pString, "2.4GHz", TRUE) ) && (1 == pWifiRadioFull->Cfg.InstanceNumber))
        {
            TmpFreq = COSA_DML_WIFI_FREQ_BAND_2_4G;
        }
        else if (( AnscEqualString(pString, "5GHz", TRUE) ) && (2 == pWifiRadioFull->Cfg.InstanceNumber))
        {
            TmpFreq = COSA_DML_WIFI_FREQ_BAND_5G;
        }
        else
    	{
    	    return FALSE; /* Radio can only support these two frequency bands and can not change dynamically */
    	}

        if ( pWifiRadioFull->Cfg.OperatingFrequencyBand == TmpFreq )
        {
            return  TRUE;
        }
    	pWifiRadioFull->Cfg.OperatingFrequencyBand = TmpFreq;
        pWifiRadio->bRadioChanged = TRUE;

        return TRUE;
    }

    if (strcmp(ParamName, "OperatingStandards") == 0)
    {
        if(!isValidOperStdString(pString))
            return FALSE;

        ULONG                       TmpOpStd;
        char *a = _ansc_strchr(pString, 'a');
        char *ac = _ansc_strstr(pString, "ac");
#ifdef _WIFI_AX_SUPPORT_
        char *ax = _ansc_strstr(pString, "ax");
#endif
		//zqiu
	//	if( (a!=NULL) && (ac==NULL) )
	//		return FALSE;
        
        /* save update to backup */
        TmpOpStd = 0;

        // if a and ac are not NULL and they are the same string, then move past the ac and search for an a by itself
        if (a && ac && (a  == ac)) {
            a = a+1;
            a = _ansc_strchr(a,'a');
        }

#ifdef _WIFI_AX_SUPPORT_
        if (a && ax && (a  == ax))
        {
            a = a+1;
            a = _ansc_strchr(a,'a');

            if ( a && ac && (a == ac)) // handling for ax/ac and ax/ac/n/a operational mode
            {
                a = a+1;
                a = _ansc_strchr(a,'a');
            }
        }
#endif

        if ( a != NULL )
        {
            TmpOpStd |= COSA_DML_WIFI_STD_a;
        }
        
        if ( ac != NULL )
        {
            TmpOpStd |= COSA_DML_WIFI_STD_ac;
        }
#ifdef _WIFI_AX_SUPPORT_
        if ( ax != NULL )
        {
            TmpOpStd |= COSA_DML_WIFI_STD_ax;
        }
#endif
        if ( strchr(pString, 'b') != NULL )
        {
            TmpOpStd |= COSA_DML_WIFI_STD_b;
        }
        if ( strchr(pString, 'g') != NULL )
        {
            TmpOpStd |= COSA_DML_WIFI_STD_g;
        }
        if ( strchr(pString, 'n') != NULL )
        {
            TmpOpStd |= COSA_DML_WIFI_STD_n;
        }

        if ( pWifiRadioFull->Cfg.OperatingStandards == TmpOpStd )
        {
            return  TRUE;
        }
        
        pWifiRadioFull->Cfg.OperatingStandards = TmpOpStd;

	if(pWifiRadioFull->Cfg.OperatingStandards == (COSA_DML_WIFI_STD_g | COSA_DML_WIFI_STD_n) ) {

		isBeaconRateUpdate[0] = isBeaconRateUpdate[2] = isBeaconRateUpdate[4] =  isBeaconRateUpdate[6] = isBeaconRateUpdate[8] = isBeaconRateUpdate[10] = TRUE;	
		CosaWifiAdjustBeaconRate(1, "6Mbps");
		CcspTraceDebug(("WIFI OperatingStandards = g/n  Beacon Rate 6Mbps  Function %s \n",__FUNCTION__));
	}
	else if (pWifiRadioFull->Cfg.OperatingStandards == (COSA_DML_WIFI_STD_b |COSA_DML_WIFI_STD_g | COSA_DML_WIFI_STD_n) ) {

		isBeaconRateUpdate[0] = isBeaconRateUpdate[2] = isBeaconRateUpdate[4] =  isBeaconRateUpdate[6] = isBeaconRateUpdate[8] = isBeaconRateUpdate[10] = TRUE;	
		CosaWifiAdjustBeaconRate(1, "1Mbps");
		CcspTraceDebug(("WIFI OperatingStandards = b/g/n  Beacon Rate 1Mbps %s \n",__FUNCTION__));
	}

        pWifiRadio->bRadioChanged = TRUE;
        
        return TRUE;
    }
    if (strcmp(ParamName, "RegulatoryDomain") == 0)
    {
        if ( AnscEqualString(pWifiRadioFull->Cfg.RegulatoryDomain, pString, TRUE) )
        {
                return  TRUE;
        }
        /* save update to backup */
        AnscCopyString( pWifiRadioFull->Cfg.RegulatoryDomain, pString );
        pWifiRadio->bRadioChanged = TRUE;
        return TRUE;
    }

    if (strcmp(ParamName, "BasicDataTransmitRates") == 0)
    {
        if(isValidTransmitRate(pString))
        {
            if ( AnscEqualString(pWifiRadioFull->Cfg.BasicDataTransmitRates, pString, TRUE) )
            {
                return  TRUE;
            }

            /* save update to backup */
            AnscCopyString( pWifiRadioFull->Cfg.BasicDataTransmitRates, pString );
            pWifiRadio->bRadioChanged = TRUE;
            return TRUE;
        }
    }
    if (strcmp(ParamName, "OperationalDataTransmitRates") == 0)
    {
        if(isValidTransmitRate(pString))
        {
            if ( AnscEqualString(pWifiRadioFull->Cfg.OperationalDataTransmitRates, pString, TRUE) )
            {
                return  TRUE;
            }
        /* save update to backup */
        AnscCopyString( pWifiRadioFull->Cfg.OperationalDataTransmitRates, pString );
        pWifiRadio->bRadioChanged = TRUE;
        return TRUE;
        }
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

BOOL isValidOperatingStandards (ULONG OperatingStandards, COSA_DML_WIFI_FREQ_BAND OperatingFrequencyBand)
{
#ifdef _WIFI_AX_SUPPORT_
    if(OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_2_4G){
        if( (OperatingStandards == WIFI_STD_AXNGB) || (OperatingStandards == WIFI_STD_AXNG) || (OperatingStandards == WIFI_STD_AXN) || (OperatingStandards == WIFI_STD_AXONLY) )
            return TRUE;
    } else {
	if( (OperatingStandards == WIFI_STD_AXACNA) || (OperatingStandards == WIFI_STD_AXACN) || (OperatingStandards == WIFI_STD_AXAC) || (OperatingStandards == WIFI_STD_AXONLY) )
            return TRUE;
    }

#else
    if(OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_2_4G){
        if((OperatingStandards == WIFI_STD_NONLY) || (OperatingStandards == WIFI_STD_NG) || (OperatingStandards == WIFI_STD_NGB))
            return TRUE;
    } else {
        if((OperatingStandards == WIFI_STD_ACONLY) || (OperatingStandards ==  WIFI_STD_ACN) || (OperatingStandards == WIFI_STD_ACNA))
            return TRUE;
    }

#endif
   return FALSE;

}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Radio_Validate
            (
                ANSC_HANDLE                 hInsContext,
                char*                       pReturnParamName,
                ULONG*                      puLength
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       pReturnParamName,
                The buffer (128 bytes) of parameter name if there's a validation. 

                ULONG*                      puLength
                The output length of the param name. 

    return:     TRUE if there's no validation.

**********************************************************************/
BOOL
Radio_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject      = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_DML_WIFI_RADIO            pWifiRadio     = hInsContext;
	PCOSA_DML_WIFI_RADIO_STATS      pWifiStats 	   = &pWifiRadio->Stats;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;
    PCOSA_DML_WIFI_RADIO            pWifiRadio2    = NULL;
    ULONG                           idx            = 0;
    ULONG                           maxCount       = 0;
  
    /*Alias should be non-empty*/
    if (AnscSizeOfString(pWifiRadio->Radio.Cfg.Alias) == 0)
    {
        CcspTraceWarning(("********Radio Validate:Failed Alias \n"));
        AnscCopyString(pReturnParamName, "Alias");
        *puLength = AnscSizeOfString("Alias");
        return FALSE;
    }
 
    for ( idx = 0; idx < pMyObject->RadioCount; idx++)
    {
        if ( pWifiRadio == (pMyObject->pRadio + idx) )
        {
            continue;
        }
        
        pWifiRadio2     = pMyObject->pRadio + idx;
        
        if ( AnscEqualString(pWifiRadio->Radio.Cfg.Alias, pWifiRadio2->Radio.Cfg.Alias, TRUE) )
        {
            CcspTraceWarning(("********Radio Validate:Failed Alias \n"));
            AnscCopyString(pReturnParamName, "Alias");
            *puLength = AnscSizeOfString("Alias");
            return FALSE;
        }
    }

#if defined(_INTEL_BUG_FIXES_)
    if ( pWifiRadioFull->Cfg.OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_5G &&
         pWifiRadioFull->Cfg.X_COMCAST_COM_DFSEnable == FALSE                     &&
       ((pWifiRadioFull->Cfg.Channel >= 52 && pWifiRadioFull->Cfg.Channel <= 64)  ||
        (pWifiRadioFull->Cfg.Channel >= 100 && pWifiRadioFull->Cfg.Channel <= 144) ) &&
         pWifiRadioFull->Cfg.AutoChannelEnable == FALSE )
    {
        CcspTraceWarning(("********Radio Validate:Failed DFS\n"));
        AnscCopyString(pReturnParamName, "DFS");
        *puLength = AnscSizeOfString("DFS");
        return FALSE;
    }
#endif
    if (!(CosaUtilChannelValidate(pWifiRadioFull->Cfg.OperatingFrequencyBand,pWifiRadioFull->Cfg.Channel,pWifiRadioFull->StaticInfo.PossibleChannels))) {
        CcspTraceWarning(("********Radio Validate:Failed Channel\n"));
        AnscCopyString(pReturnParamName, "Channel");
        *puLength = AnscSizeOfString("Channel");
        return FALSE;
    }

#if defined(_HUB4_PRODUCT_REQ_)
    // The Hub4 only supports the full set of standards for both radios. You can not set any
    // other configuration
    if ( pWifiRadioFull->StaticInfo.SupportedStandards != pWifiRadioFull->Cfg.OperatingStandards ) {
        CcspTraceWarning(("********Radio Validate:Failed OperatingStandards\n"));
        AnscCopyString(pReturnParamName, "OperatingStandards");
        *puLength = AnscSizeOfString("OperatingStandards");
        return FALSE;
    }
#else
#if defined(_INTEL_BUG_FIXES_)
    fprintf(stderr, "%s: SupportedStandards = %lu, OperatingStandards = %lu\n",
            __FUNCTION__, pWifiRadioFull->StaticInfo.SupportedStandards, pWifiRadioFull->Cfg.OperatingStandards);
#endif
    if ( ((pWifiRadioFull->StaticInfo.SupportedStandards & pWifiRadioFull->Cfg.OperatingStandards) !=  pWifiRadioFull->Cfg.OperatingStandards) ||
         (!isValidOperatingStandards(pWifiRadioFull->Cfg.OperatingStandards ,pWifiRadioFull->Cfg.OperatingFrequencyBand )) ) {
        fprintf(stderr, "%s: Mismatch of SupportedStandards(%lu) and OperatingStandards(%lu) causing Radio Validation failure\n",
                __FUNCTION__, pWifiRadioFull->StaticInfo.SupportedStandards, pWifiRadioFull->Cfg.OperatingStandards);
        CcspTraceWarning(("********Radio Validate:Failed OperatingStandards\n"));
        AnscCopyString(pReturnParamName, "OperatingStandards");
        *puLength = AnscSizeOfString("OperatingStandards");
        return FALSE;
    }
#endif

    // If the Channel Bandwidth is 80 or 160 MHz then the radio must support 11ac
    if ( (    (pWifiRadioFull->Cfg.OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_80M)
           || (pWifiRadioFull->Cfg.OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_160M)) &&
         !(pWifiRadioFull->StaticInfo.SupportedStandards & COSA_DML_WIFI_STD_ac) ) {
        CcspTraceWarning(("********Radio Validate:Failed OperatingChannelBandwidth\n"));
        AnscCopyString(pReturnParamName, "OperatingChannelBandwidth");
        *puLength = AnscSizeOfString("OperatingChannelBandwidth");
        return FALSE;
    }

    // If the Channel is 165 then Channel Bandwidth must be 20 MHz, otherwise reject the change
    if ((pWifiRadioFull->Cfg.AutoChannelEnable == FALSE) &&
        (pWifiRadioFull->Cfg.Channel == 165) && 
         !(pWifiRadioFull->Cfg.OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_20M) ) {
        CcspTraceWarning(("********Radio Validate:Failed Channel and OperatingChannelBandwidth mismatch\n"));
        AnscCopyString(pReturnParamName, "Channel");
        *puLength = AnscSizeOfString("Channel");
        return FALSE;
    }
    
    if( (pWifiRadioFull->Cfg.TransmitPower != 12) &&  
        (pWifiRadioFull->Cfg.TransmitPower != 25) && 
        (pWifiRadioFull->Cfg.TransmitPower != 50) && 
        (pWifiRadioFull->Cfg.TransmitPower != 75) && 
        (pWifiRadioFull->Cfg.TransmitPower != 100) )
    {
         CcspTraceWarning(("********Radio Validate:Failed Transmit Power\n"));
         AnscCopyString(pReturnParamName, "TransmitPower");
         *puLength = AnscSizeOfString("TransmitPower");
         return FALSE;
    }    

    if( pWifiRadioFull->Cfg.BeaconInterval > 65535 )
    {
         CcspTraceWarning(("********Radio Validate:Failed BeaconInterval\n"));
         AnscCopyString(pReturnParamName, "BeaconInterval");
         *puLength = AnscSizeOfString("BeaconInterval");
         return FALSE;
    }

    if( pWifiRadioFull->Cfg.DTIMInterval > 255 )
    {
         CcspTraceWarning(("********Radio Validate:Failed DTIMInterval\n"));
         AnscCopyString(pReturnParamName, "DTIMInterval");
         *puLength = AnscSizeOfString("DTIMInterval");
         return FALSE;
    }

// need to fix temporary in order for the set to work
// Value of 0 == off
    if( (pWifiRadioFull->Cfg.FragmentationThreshold > 2346) )
    {
         CcspTraceWarning(("********Radio Validate:Failed FragThreshhold\n"));
         AnscCopyString(pReturnParamName, "FragmentationThreshold");
         *puLength = AnscSizeOfString("FragmentationThreshold");
         return FALSE;
    }

    if( (pWifiRadioFull->Cfg.RTSThreshold > 2347) )
    {
         CcspTraceWarning(("********Radio Validate:Failed RTSThreshhold\n"));
         AnscCopyString(pReturnParamName, "RTSThreshold");
         *puLength = AnscSizeOfString("RTSThreshold");
         return FALSE;
    }

    maxCount = (60 * 71582787ULL);
    if(pWifiRadioFull->Cfg.AutoChannelRefreshPeriod > maxCount)
    {
         CcspTraceWarning(("********Radio Validate:Failed AutoChannelRefreshPeriod\n"));
         AnscCopyString(pReturnParamName, "AutoChannelRefreshPeriod");
         *puLength = AnscSizeOfString("AutoChannelRefreshPeriod");
         return FALSE;
    }
	
	if(pWifiStats->RadioStatisticsMeasuringInterval==0 || pWifiStats->RadioStatisticsMeasuringInterval<=pWifiStats->RadioStatisticsMeasuringRate)
	{
		CcspTraceWarning(("********Radio Validate:Failed RadioStatisticsMeasuringInterval \n"));
        AnscCopyString(pReturnParamName, "RadioStatisticsMeasuringInterval");
        *puLength = AnscSizeOfString("RadioStatisticsMeasuringInterval");
		return FALSE;
	}
	
	if(pWifiStats->RadioStatisticsMeasuringRate==0 || (pWifiStats->RadioStatisticsMeasuringInterval%pWifiStats->RadioStatisticsMeasuringRate)!=0)
	{
		CcspTraceWarning(("********Radio Validate:Failed RadioStatisticsMeasuringRate \n"));
        AnscCopyString(pReturnParamName, "RadioStatisticsMeasuringRate");
        *puLength = AnscSizeOfString("RadioStatisticsMeasuringRate");
		return FALSE;
	}
	if((pWifiStats->RadioStatisticsMeasuringInterval/pWifiStats->RadioStatisticsMeasuringRate)>=RSL_MAX)
	{
		CcspTraceWarning(("********Radio Validate:Failed RadioStatisticsMeasuringInterval is too big \n"));
        AnscCopyString(pReturnParamName, "RadioStatisticsMeasuringInterval");
        *puLength = AnscSizeOfString("RadioStatisticsMeasuringInterval");
		return FALSE;
	}
	;

#if 0
    if(pWifiRadioFull->Cfg.RegulatoryDomain[2] != 'I')
    {
         /* Currently driver supports only Inside a country code */
         CcspTraceWarning(("********Radio Validate:Failed Regulatory Domain \n"));
         AnscCopyString(pReturnParamName, "RegulatoryDomain");
         *puLength = AnscSizeOfString("RegulatoryDomain");
         return FALSE;
    }
#endif

    return TRUE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        Radio_Commit
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
Radio_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_DML_WIFI_RADIO            pWifiRadio     = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg  = &pWifiRadioFull->Cfg;
	ANSC_STATUS                     returnStatus    = ANSC_STATUS_SUCCESS;
#if defined(_ENABLE_BAND_STEERING_)
    PCOSA_DML_WIFI_BANDSTEERING	 pBandSteering = pMyObject->pBandSteering;
    BOOL radio_0_Enabled=FALSE;
    BOOL radio_1_Enabled=FALSE;
    BOOL ret=FALSE;
#endif
    if ( !pWifiRadio->bRadioChanged )
    {
        return  ANSC_STATUS_SUCCESS;
    }
    else
    {
        pWifiRadio->bRadioChanged = FALSE;
        CcspTraceInfo(("WiFi Radio -- apply the change...\n"));
    }
    
    returnStatus = CosaDmlWiFiRadioSetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiRadioCfg);
#if defined(_ENABLE_BAND_STEERING_)
    wifi_getRadioEnable(1, &radio_1_Enabled);
    wifi_getRadioEnable(0, &radio_0_Enabled);
    ret= radio_1_Enabled & radio_0_Enabled;
    if(!ret && NULL !=pBandSteering && NULL != &(pBandSteering->BSOption)) 
    {	
	if(pBandSteering->BSOption.bEnable)
	{
		pBandSteering->BSOption.bLastBSDisableForRadio=true; 
		pBandSteering->BSOption.bEnable=false;
		CosaDmlWiFi_SetBandSteeringOptions( &pBandSteering->BSOption );
    	}	
    }
    if(ret && NULL !=pBandSteering &&  NULL != &(pBandSteering->BSOption) && pBandSteering->BSOption.bLastBSDisableForRadio) 
    {
	pBandSteering->BSOption.bLastBSDisableForRadio=false;
	pBandSteering->BSOption.bEnable=true;
	CosaDmlWiFi_SetBandSteeringOptions( &pBandSteering->BSOption );
    }
#endif

    if (returnStatus == ANSC_STATUS_SUCCESS && isHotspotSSIDIpdated)
    {

	pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_t *attrp = NULL;

        attrp = &attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
        pthread_create(&tid, attrp, (void*)&UpdateCircuitId, NULL);
        if(attrp != NULL)
            pthread_attr_destroy( attrp );
	isHotspotSSIDIpdated = FALSE;
    }
    return returnStatus; 
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        Radio_Rollback
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to roll back the update whenever there's a 
        validation found.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
Radio_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject      = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_DML_WIFI_RADIO            pWifiRadio     = pMyObject->pRadio;
    ULONG                           idx            = 0;    
    PCOSA_DML_WIFI_RADIO            pWifiRadio2    = hInsContext;
    
    for (idx=0; idx < pMyObject->RadioCount; idx++)
    {
        if ( pWifiRadio2 == pWifiRadio+idx )
        {
            return CosaDmlWiFiRadioGetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, &pWifiRadio2->Radio.Cfg) ;
        }
    }

    return ANSC_STATUS_SUCCESS;
}

ULONG
ReceivedSignalLevel_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
	PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
	PCOSA_DML_WIFI_RADIO_STATS      pWifiStats = &pWifiRadio->Stats;
	PCOSA_DML_WIFI_RADIO_STATS_RSL	pRsl = &pWifiStats->RslInfo;
	if(pRsl->Count==0) {
		if(pWifiStats->RadioStatisticsMeasuringRate==0)
			pWifiStats->RadioStatisticsMeasuringRate=30;
		if(pWifiStats->RadioStatisticsMeasuringInterval==0)
			pWifiStats->RadioStatisticsMeasuringInterval=1800;
		pRsl->Count=pWifiStats->RadioStatisticsMeasuringInterval/pWifiStats->RadioStatisticsMeasuringRate;
	}
	return pRsl->Count;
}

ANSC_HANDLE
ReceivedSignalLevel_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
	PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
	PCOSA_DML_WIFI_RADIO_STATS      pWifiStats = &pWifiRadio->Stats;
	PCOSA_DML_WIFI_RADIO_STATS_RSL	pRsl = &pWifiStats->RslInfo;

	if (nIndex > pRsl->Count)   {
        return NULL;
    } else {
        *pInsNumber  = nIndex + 1; 
        return pRsl->ReceivedSignalLevel+((pRsl->StartIndex+pRsl->Count-nIndex)%pRsl->Count);
    }
    return NULL; 
}


BOOL
ReceivedSignalLevel_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
	INT	*pReceivedSignalLevel   = (INT*)hInsContext;    
	
	if(!hInsContext)
		return FALSE;
	
	if (strcmp(ParamName, "ReceivedSignalLevel") == 0)   {
		*pInt = *pReceivedSignalLevel;
        return TRUE;
    }
	return FALSE;		
}

/***********************************************************************

 APIs for Object:

    WiFi.Radio.{i}.Stats.

    *  Stats3_SetParamBoolValue
    *  Stats3_GetParamIntValue
    *  Stats3_GetParamUlongValue
    *  Stats3_GetParamStringValue

***********************************************************************/
/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Stats3_SetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:

        This function is called to retrieve Boolean parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL*                       pBool
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Stats3_IsUpdated
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_STATS      pWifiRadioStats = &pWifiRadio->Stats;

	if ( ( AnscGetTickInSeconds() - pWifiRadioStats->LastSampling ) < pWifiRadioStats->RadioStatisticsMeasuringRate )
		return FALSE;
	else {
    	pWifiRadioStats->LastSampling =  AnscGetTickInSeconds();
    	return TRUE;
	}	
	return TRUE;
}

ULONG
Stats3_Synchronize
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_STATS      pWifiRadioStats = &pWifiRadio->Stats;	
	PCOSA_DML_WIFI_RADIO_STATS_RSL	pRsl = &pWifiRadioStats->RslInfo;
	INT								iRSL=0;
	ULONG							uIndex=0;	
		
	CosaDmlWiFi_getRadioStatsReceivedSignalLevel(pWifiRadio->Radio.Cfg.InstanceNumber, &iRSL);
	uIndex=(pRsl->StartIndex+1)%pRsl->Count;
	pRsl->ReceivedSignalLevel[uIndex]=iRSL;
	pRsl->StartIndex=uIndex;
	
	//zqiu: TODO: other noise statistic
	
	return ANSC_STATUS_SUCCESS;
}

BOOL
Stats3_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        *pBool
    )
{
    //PCOSA_DATAMODEL_WIFI            pMyObject       = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_STATS      pWifiRadioStats = &pWifiRadio->Stats;
        
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "X_COMCAST-COM_RadioStatisticsEnable") == 0)    {
		*pBool = pWifiRadioStats->RadioStatisticsEnable;
         return TRUE;
    }	
    return FALSE;
}

BOOL
Stats3_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
	PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
	PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg  = &pWifiRadioFull->Cfg;	
    PCOSA_DML_WIFI_RADIO_STATS      pWifiRadioStats = &pWifiRadio->Stats;	
    if (strcmp(ParamName, "X_COMCAST-COM_RadioStatisticsEnable") == 0)   {
		if(pWifiRadioStats->RadioStatisticsEnable!=bValue) {
			//apply changes		
			pWifiRadioStats->RadioStatisticsEnable=bValue;
			CosaDmlWiFiRadioStatsSet(pWifiRadioCfg->InstanceNumber, pWifiRadioStats);	
		}			
        return TRUE;
    }
    return FALSE;
}


/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Stats3_GetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int*                        pInt
            );

    description:

        This function is called to retrieve integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int*                        pInt
                The buffer of returned integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Stats3_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject       = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_STATS      pWifiRadioStats = &pWifiRadio->Stats;
    PCOSA_DML_WIFI_RADIO_CHANNEL_STATS      pWifiRadioChStats = &pWifiRadio->ChStats;
    UINT percentage = 0 ;

    CosaDmlWiFiRadioGetStats((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiRadio->Radio.Cfg.InstanceNumber, pWifiRadioStats);

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "X_COMCAST-COM_NoiseFloor") == 0)    {
		*pInt = pWifiRadioStats->NoiseFloor;
         return TRUE;
    }
    if (strcmp(ParamName, "Noise") == 0)    {
	    *pInt = pWifiRadioStats->NoiseFloor;
	    return TRUE;
    }
    if( AnscEqualString(ParamName, "X_RDKCENTRAL-COM_AFTX", TRUE) || AnscEqualString(ParamName, "X_RDKCENTRAL-COM_AFRX", TRUE)) {
        CosaDmlWiFiRadioChannelGetStats(ParamName, pWifiRadio->Radio.Cfg.InstanceNumber, pWifiRadioChStats, &percentage);
        *pInt = round( (float) pWifiRadioStats->ActivityFactor * percentage / 100 ) ;
        return TRUE;
    }
    if( AnscEqualString(ParamName, "X_COMCAST-COM_ActivityFactor", TRUE) || AnscEqualString(ParamName, "X_RDKCENTRAL-COM_AF", TRUE))    {
		*pInt = pWifiRadioStats->ActivityFactor;
        return TRUE;
    }
        if( AnscEqualString(ParamName, "X_COMCAST-COM_CarrierSenseThreshold_Exceeded", TRUE) || AnscEqualString(ParamName, "X_RDKCENTRAL-COM_CSTE", TRUE))    {
        *pInt = pWifiRadioStats->CarrierSenseThreshold_Exceeded;
        return TRUE;
    }
	if (strcmp(ParamName, "X_COMCAST-COM_RetransmissionMetric") == 0)     {
        *pInt = pWifiRadioStats->RetransmissionMetric;
        return TRUE;
    }
	if (strcmp(ParamName, "X_COMCAST-COM_MaximumNoiseFloorOnChannel") == 0)    {
        *pInt = pWifiRadioStats->MaximumNoiseFloorOnChannel;
        return TRUE;
    }
	if (strcmp(ParamName, "X_COMCAST-COM_MinimumNoiseFloorOnChannel") == 0)   {
        *pInt = pWifiRadioStats->MinimumNoiseFloorOnChannel;
        return TRUE;
    }
	if (strcmp(ParamName, "X_COMCAST-COM_MedianNoiseFloorOnChannel") == 0)    {
        *pInt = pWifiRadioStats->MedianNoiseFloorOnChannel;
        return TRUE;
    }
	if (strcmp(ParamName, "X_COMCAST-COM_RadioStatisticsMeasuringRate") == 0) {
        //CosaDmlWiFi_getRadioStatsRadioStatisticsMeasuringRate(pWifiRadio->Radio.Cfg.InstanceNumber ,pInt);
		*pInt = pWifiRadioStats->RadioStatisticsMeasuringRate;
        return TRUE;
    }
	if (strcmp(ParamName, "X_COMCAST-COM_RadioStatisticsMeasuringInterval") == 0)    {
        //CosaDmlWiFi_getRadioStatsRadioStatisticsMeasuringInterval(pWifiRadio->Radio.Cfg.InstanceNumber,pInt);
		*pInt = pWifiRadioStats->RadioStatisticsMeasuringInterval;
        return TRUE;
    }
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Stats3_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Stats3_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject       = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_STATS      pWifiRadioStats = &pWifiRadio->Stats;
    
    CosaDmlWiFiRadioGetStats((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiRadio->Radio.Cfg.InstanceNumber, pWifiRadioStats);
    
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "BytesSent") == 0)   {
        *puLong = pWifiRadioStats->BytesSent;
        return TRUE;
    }
    if (strcmp(ParamName, "BytesReceived") == 0)    {
        *puLong = pWifiRadioStats->BytesReceived;
        return TRUE;
    }
    if (strcmp(ParamName, "PacketsSent") == 0)    {
        *puLong = pWifiRadioStats->PacketsSent;
        return TRUE;
    }
    if (strcmp(ParamName, "PacketsReceived") == 0)    {
        *puLong = pWifiRadioStats->PacketsReceived;
        return TRUE;
    }
    if (strcmp(ParamName, "ErrorsSent") == 0)    {
        *puLong = pWifiRadioStats->ErrorsSent;
        return TRUE;
    }
    if (strcmp(ParamName, "ErrorsReceived") == 0)    {
        *puLong = pWifiRadioStats->ErrorsReceived;
        return TRUE;
    }
    if (strcmp(ParamName, "DiscardPacketsSent") == 0)    {
        *puLong = pWifiRadioStats->DiscardPacketsSent;
        return TRUE;
    }
    if (strcmp(ParamName, "DiscardPacketsReceived") == 0)    {
        *puLong = pWifiRadioStats->DiscardPacketsReceived;
        return TRUE;
    }  
	if (strcmp(ParamName, "PLCPErrorCount") == 0)    {
        *puLong = pWifiRadioStats->PLCPErrorCount;
        return TRUE;
    }
	if (strcmp(ParamName, "FCSErrorCount") == 0)    {
        *puLong = pWifiRadioStats->FCSErrorCount;
        return TRUE;
    }
	if (strcmp(ParamName, "InvalidMACCount") == 0)    {
        *puLong = pWifiRadioStats->InvalidMACCount;
        return TRUE;
    }
	if (strcmp(ParamName, "PacketsOtherReceived") == 0)    {
        *puLong = pWifiRadioStats->PacketsOtherReceived;
        return TRUE;
    }
	if (strcmp(ParamName, "X_COMCAST-COM_ChannelUtilization") == 0)    {
        *puLong = pWifiRadioStats->ChannelUtilization;
        return TRUE;
    }
	if (strcmp(ParamName, "X_COMCAST-COM_StatisticsStartTime") == 0)    {
        *puLong = pWifiRadioStats->StatisticsStartTime;
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Stats3_SetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int                        iValue
            );

    description:

        This function is called to set integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int                        iValue
                The buffer of returned integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Stats3_SetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int                         iValue
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
	PCOSA_DML_WIFI_RADIO_STATS      pWifiRadioStats = &pWifiRadio->Stats;
 
	if (strcmp(ParamName, "X_COMCAST-COM_RadioStatisticsMeasuringRate") == 0)   {
		pWifiRadioStats->RadioStatisticsMeasuringRate=iValue;
		CosaDmlWiFi_setRadioStatsRadioStatisticsMeasuringRate(pWifiRadio->Radio.Cfg.InstanceNumber, pWifiRadioStats->RadioStatisticsMeasuringRate);
	    return TRUE;
    }
	if (strcmp(ParamName, "X_COMCAST-COM_RadioStatisticsMeasuringInterval") == 0)    {
		pWifiRadioStats->RadioStatisticsMeasuringInterval=iValue;	
		CosaDmlWiFi_setRadioStatsRadioStatisticsMeasuringInterval(pWifiRadio->Radio.Cfg.InstanceNumber, pWifiRadioStats->RadioStatisticsMeasuringInterval);
        return TRUE;
    }
	return FALSE;
}

BOOL
Stats3_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio     = hInsContext;
	PCOSA_DML_WIFI_RADIO_STATS      pWifiStats 	   = &pWifiRadio->Stats;
    	
	if(pWifiStats->RadioStatisticsMeasuringInterval==0 || pWifiStats->RadioStatisticsMeasuringInterval<=pWifiStats->RadioStatisticsMeasuringRate)
	{
		CcspTraceWarning(("********Radio Validate:Failed RadioStatisticsMeasuringInterval \n"));
        AnscCopyString(pReturnParamName, "RadioStatisticsMeasuringInterval");
        *puLength = AnscSizeOfString("RadioStatisticsMeasuringInterval");
		return FALSE;
	}
	
	if(pWifiStats->RadioStatisticsMeasuringRate==0 || (pWifiStats->RadioStatisticsMeasuringInterval%pWifiStats->RadioStatisticsMeasuringRate)!=0)
	{
		CcspTraceWarning(("********Radio Validate:Failed RadioStatisticsMeasuringRate \n"));
        AnscCopyString(pReturnParamName, "RadioStatisticsMeasuringRate");
        *puLength = AnscSizeOfString("RadioStatisticsMeasuringRate");
		return FALSE;
	}
	if((pWifiStats->RadioStatisticsMeasuringInterval/pWifiStats->RadioStatisticsMeasuringRate)>=RSL_MAX)
	{
		CcspTraceWarning(("********Radio Validate:Failed RadioStatisticsMeasuringInterval is too big \n"));
        AnscCopyString(pReturnParamName, "RadioStatisticsMeasuringInterval");
        *puLength = AnscSizeOfString("RadioStatisticsMeasuringInterval");
		return FALSE;
	}

    return TRUE;
}

ULONG
Stats3_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    //PCOSA_DML_WIFI_RADIO            pWifiRadio     = hInsContext;
	//PCOSA_DML_WIFI_RADIO_STATS      pWifiStats 	   = &pWifiRadio->Stats;
	
	//CosaDmlWiFi_setRadioStatsRadioStatisticsMeasuringRate(pWifiRadio->Radio.Cfg.InstanceNumber, pWifiStats->RadioStatisticsMeasuringRate);
	//CosaDmlWiFi_setRadioStatsRadioStatisticsMeasuringInterval(pWifiRadio->Radio.Cfg.InstanceNumber, pWifiStats->RadioStatisticsMeasuringInterval);
	//CosaDmlWiFi_resetRadioStats(pWifiStats);
    return ANSC_STATUS_SUCCESS; 
}

/***********************************************************************

 APIs for Object:

    WiFi.Radio.{i}.X_LGI-COM_ZeroWaitDFS.

    *  ZeroWaitDFS_GetParamBoolValue
    *  ZeroWaitDFS_SetParamBoolValue
    *  ZeroWaitDFS_Validate
    *  ZeroWaitDFS_Commit
    *  ZeroWaitDFS_Rollback

***********************************************************************/

BOOL
ZeroWaitDFS_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
#ifdef _LG_MV2_PLUS_
    PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg  = &pWifiRadioFull->Cfg;
    PCOSA_DML_WIFI_RADIO_ZEROWAITDFS     pWifiRadioZeroWaitDFS = &pWifiRadioCfg->ZeroWaitDFS;
    int wlanIndex = (ULONG) pWifiRadioCfg->InstanceNumber-1;

    if ((wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX)) {
        return FALSE;
    }
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Enable") == 0) {
         *pBool = pWifiRadioZeroWaitDFS->Enable;
         return TRUE;
    }
#endif //_LG_MV2_PLUS_
    return FALSE;
}

BOOL
ZeroWaitDFS_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
#ifdef _LG_MV2_PLUS_
    PCOSA_DATAMODEL_WIFI                  pMyObject       = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_DML_WIFI_RADIO                  pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL             pWifiRadioFull = &pWifiRadio->Radio;
    PCOSA_DML_WIFI_RADIO_CFG              pWifiRadioCfg  = &pWifiRadioFull->Cfg;
    PCOSA_DML_WIFI_RADIO_ZEROWAITDFS      pWifiRadioZeroWaitDFS = &pWifiRadioCfg->ZeroWaitDFS;

    int wlanIndex = (ULONG) pWifiRadioCfg->InstanceNumber-1;

    if ((wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX)) 
    {
        return ANSC_STATUS_FAILURE;
    }
    if ( (strcmp(ParamName, "Enable") == 0))
    {
        if (wlanIndex==0)
        {
              CcspTraceError(("No ZWDFS for 2G4 \n"));
              return FALSE;
        }	
        // TODO Activate the bellow code once HAL API are implemented 

	// wifi_setZeroDFSState(wlanIndex,bValue,true);
        pWifiRadioZeroWaitDFS->Enable = bValue;
    }

    return TRUE;
#else
    return FALSE;
#endif //_LG_MV2_PLUS_
}

BOOL
ZeroWaitDFS_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    return TRUE;
}

ULONG
ZeroWaitDFS_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    return ANSC_STATUS_SUCCESS;
}

ULONG
ZeroWaitDFS_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    return ANSC_STATUS_SUCCESS;
}


/***********************************************************************

 APIs for Object:

    WiFi.Radio.{i}.X_LGI-COM_ZeroWaitDFS.DfsChannels.

    *  DfsChannels_GetEntryCount
    *  DfsChannels_GetEntry
    *  DfsChannels_GetParamUlongValue
    *  DfsChannels_IsUpdated
    *  DfsChannels_Synchronize

***********************************************************************/

ULONG
DfsChannels_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg  = &pWifiRadioFull->Cfg;

    int wlanIndex = (ULONG) pWifiRadioCfg->InstanceNumber-1;

    ULONG entryCount = 0;
    if ((wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX)) {
        return 0; //Error getting radio index
    }
    if (wlanIndex == 1)
         entryCount = DFSCHANCOUNT; // Number of European zone DFS channels 
#ifdef _LG_MV2_PLUS_
    return entryCount;
#else
    return 0;
#endif
}

ANSC_HANDLE
DfsChannels_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
    PCOSA_DML_WIFI_RADIO                  pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL             pWifiRadioFull = &pWifiRadio->Radio;
    PCOSA_DML_WIFI_RADIO_CFG              pWifiRadioCfg  = &pWifiRadioFull->Cfg;
    PCOSA_DML_WIFI_RADIO_ZEROWAITDFS      pWifiRadioZeroWaitDFS = &pWifiRadioCfg->ZeroWaitDFS;

    int wlanIndex = (ULONG) pWifiRadioCfg->InstanceNumber-1;

    if ((wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX)) {
        return (ANSC_HANDLE)NULL;
    }

    if (wlanIndex == 0) {
        *pInsNumber = 0; // Empty list for 2G4
    } else {
        *pInsNumber = nIndex;
    }
    return (ANSC_HANDLE)&pWifiRadioZeroWaitDFS->DFSChannels[nIndex];

}

BOOL
DfsChannels_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    PCOSA_DML_WIFI_RADIO_ZEROWAITDFS_CHANLIST  DFSChannel = (PCOSA_DML_WIFI_RADIO_ZEROWAITDFS_CHANLIST) hInsContext;
    if (DFSChannel == NULL)
        return FALSE;

    if (strcmp(ParamName, "Channel") == 0)
    {
        *puLong= DFSChannel->Channel;
    }

    if (strcmp(ParamName, "Status") == 0)
    {
        *puLong= DFSChannel->Status;
    }

    return TRUE;
}

BOOL
DfsChannels_IsUpdated
    (
        ANSC_HANDLE                 hInsContext
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    if (!last_tick)
    {
        last_tick = AnscGetTickInSeconds();
        return TRUE;
    }

    if (last_tick >= TIME_NO_NEGATIVE(AnscGetTickInSeconds() - REFRESH_INTERVAL))
    {
        return FALSE;
    }
    else
    {
        last_tick = AnscGetTickInSeconds();
        return TRUE;
    }

}

ULONG
DfsChannels_Synchronize
    (
        ANSC_HANDLE                 hInsContext
    )
{
#ifdef _LG_MV2_PLUS_
    PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg  = &pWifiRadioFull->Cfg;
    PCOSA_DML_WIFI_RADIO_ZEROWAITDFS      pWifiRadioZeroWaitDFS = &pWifiRadioCfg->ZeroWaitDFS;

    // Activate the bellow code once the HAL API are implimented 
    // int idx;
    // bool Status; //HAL API wifi_getZeroDFSState need this param but is is not used in OFW requiremet. Trash param
    // wifi_getZeroDFSState( pWifiRadioCfg->InstanceNumber-1,&pWifiRadioZeroWaitDFS->Enable,&Status );
    // if (pWifiRadioCfg->InstanceNumber == 2)
    // {
    //     wifi_zwdf_list_t* intermidiat_status_list = NULL;
    //     wifi_getZeroWaitDFSChannels(pWifiRadioCfg.InstanceNumber-1 , &intermidiat_status_list);
    //     //copy to cfg
    //     if (intermediate_status_list == NULL)
    //         return 0;
    //     while (intermediate_status_list[idx]->Channel != 0 && idx < DFSCHANCOUNT )) //could be replaced by sizeof(intermediate_status_list)/sizeof(intermediate_status_list[0]) 
    //     {
    //         pWifiRadioZeroWaitDFS->DFSChannels[idx].channel = intermediate_status_list[idx]->Channel ;
    //         pWifiRadioZeroWaitDFS->DFSChannels[idx].Status  = intermediate_status_list[idx]->Status ;
    //         idx++:
    //     }
    //     free(intermediate_status_list);
    // }
#endif
    return 0;
}

/***********************************************************************

 APIs for Object:

    WiFi.Radio.{i}.X_LGI-COM_EnhancedACS.

    *  EnhancedACS_GetParamBoolValue
    *  EnhancedACS_SetParamBoolValue
    *  EnhancedACS_Validate
    *  EnhancedACS_Commit
    *  EnhancedACS_Rollback

***********************************************************************/
BOOL
EnhancedACS_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg  = &pWifiRadioFull->Cfg;
    PCOSA_DML_WIFI_RADIO_ENHANCEDACS      pWifiRadioEnhancedACS = &pWifiRadioCfg->EnhancedACS;
    int wlanIndex = (ULONG) pWifiRadioCfg->InstanceNumber-1;

    if ((wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX)) {
        return ANSC_STATUS_FAILURE;
    }
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "DFSMoveBack") == 0) {
         *pBool = pWifiRadioEnhancedACS->DFSMoveBack;
         return TRUE;
    }
    if (strcmp(ParamName, "ExcludeDFS") == 0) {
         *pBool = pWifiRadioEnhancedACS->ExcludeDFS;
         return TRUE;
    }
    return FALSE;
}

BOOL
EnhancedACS_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject       = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg  = &pWifiRadioFull->Cfg;	
    PCOSA_DML_WIFI_RADIO_ENHANCEDACS      pWifiRadioEnhancedACS = &pWifiRadioCfg->EnhancedACS;
    int wlanIndex = (ULONG) pWifiRadioCfg->InstanceNumber-1;

    if ((wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX)) {
        return ANSC_STATUS_FAILURE;
    }
    if (strcmp(ParamName, "DFSMoveBack") == 0) {
        pWifiRadioEnhancedACS->DFSMoveBack = bValue;
        return TRUE;
    }
    if (strcmp(ParamName, "ExcludeDFS") == 0) {
        if (pWifiRadioEnhancedACS->ExcludeDFS != bValue) {
            // Only allow NOT excluding DFS if DFS is enabled.
            if ((pWifiRadioFull->Cfg.X_COMCAST_COM_DFSEnable == FALSE) && (bValue == FALSE)) {
                return FALSE;
            }
            pWifiRadioEnhancedACS->ExcludeDFS = bValue;
        }
        return TRUE;
    }
    return FALSE;
}

BOOL
EnhancedACS_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    return TRUE;
}

ULONG
EnhancedACS_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    return ANSC_STATUS_SUCCESS;
}

ULONG
EnhancedACS_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    return ANSC_STATUS_SUCCESS;
}

static const int channels24G[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
static const int channels5G[] = {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140};

ULONG
Channel_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg  = &pWifiRadioFull->Cfg;
    int wlanIndex = (ULONG) pWifiRadioCfg->InstanceNumber-1;
    ULONG entryCount = 0;
    if ((wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX)) {
        return 0;
    }
    if (wlanIndex == 0) {
        entryCount = sizeof(channels24G)/sizeof(channels24G[0]);
    } else {
        entryCount = sizeof(channels5G)/sizeof(channels5G[0]);
    }
    return entryCount;
}

ANSC_HANDLE
Channel_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg  = &pWifiRadioFull->Cfg;
    PCOSA_DML_WIFI_RADIO_ENHANCEDACS      pWifiRadioEnhancedACS = &pWifiRadioCfg->EnhancedACS;
    int wlanIndex = (ULONG) pWifiRadioCfg->InstanceNumber-1;

    if ((wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX)) {
        return (ANSC_HANDLE)NULL;
    }

    if (wlanIndex == 0) {
        *pInsNumber = channels24G[nIndex];
    } else {
        *pInsNumber = channels5G[nIndex];
    }
    return (ANSC_HANDLE)&pWifiRadioEnhancedACS->ChannelWeights[nIndex];
}

static BOOL isDFSChannel(ULONG channel) {
    switch (channel) {
        case 52:
        case 56:
        case 60:
        case 64:
        case 100:
        case 104:
        case 108:
        case 112:
        case 116:
        case 120:
        case 124:
        case 128:
        case 132:
        case 136:
        case 140:
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL isWeatherChannel(ULONG channel) {
    switch (channel) {
        case 116:
        case 120:
        case 124:
        case 128:
            return TRUE;
        default:
            return FALSE;
    }
}

static ANSC_STATUS GetInsNumsByWifiChannelWeight( ULONG* pWeight, ULONG *channel, BOOL *excludeDFS, COSA_DML_WIFI_CHAN_BW *bandwidth)
{
    PCOSA_DATAMODEL_WIFI        pWiFi       = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_DML_WIFI_RADIO            pWifiRadio      = NULL;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = NULL;
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg  = NULL;
    PCOSA_DML_WIFI_RADIO_ENHANCEDACS      pWifiRadioEnhancedACS = NULL;
    ULONG wlanIndex;
    int i, uIndex;

    for( uIndex = 0; uIndex < pWiFi->RadioCount; uIndex++)
    {
        pWifiRadio = pWiFi->pRadio+uIndex;
        pWifiRadioFull = &pWifiRadio->Radio;
        pWifiRadioCfg  = &pWifiRadioFull->Cfg;
        pWifiRadioEnhancedACS = &pWifiRadioCfg->EnhancedACS;
        wlanIndex = (ULONG) pWifiRadioCfg->InstanceNumber-1;
        if ((wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX)) {
            return ANSC_STATUS_FAILURE;
        }
        for (i = 0; i < sizeof(pWifiRadioEnhancedACS->ChannelWeights) / sizeof(pWifiRadioEnhancedACS->ChannelWeights[0]); ++i) {
            if (pWeight == &pWifiRadioEnhancedACS->ChannelWeights[i]) {
                *channel = (wlanIndex == 0)? channels24G[i]: channels5G[i];
                *excludeDFS = pWifiRadioEnhancedACS->ExcludeDFS;
                *bandwidth = pWifiRadioCfg->OperatingChannelBandwidth;
                return ANSC_STATUS_SUCCESS;
            }
        }
    }

    CcspTraceError(("%s:%d:FAILED\n",__func__, __LINE__));
    return ANSC_STATUS_FAILURE;
}

BOOL
Channel_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    ULONG*      pWeight  = (ULONG*)hInsContext;
    ULONG channel = 0;
    BOOL excludeDFS = FALSE;
    COSA_DML_WIFI_CHAN_BW bandwidth;

    if (strcmp(ParamName, "ChannelWeight") == 0)
    {
        GetInsNumsByWifiChannelWeight(pWeight, &channel, &excludeDFS, &bandwidth);
        if (excludeDFS && isDFSChannel(channel)) {
            *puLong = 0;
        } else {
            *puLong = *pWeight;
        }
        return TRUE;
    }
    return FALSE;
}

BOOL
Channel_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
    ULONG*      pWeight  = (ULONG*)hInsContext;
    ULONG channel = 0;
    BOOL excludeDFS = FALSE;
    COSA_DML_WIFI_CHAN_BW bandwidth;

    if (strcmp(ParamName, "ChannelWeight") == 0)
    {
        GetInsNumsByWifiChannelWeight(pWeight, &channel, &excludeDFS, &bandwidth);
        if ((channel >= 36 && channel <= 140 && !isWeatherChannel(channel)) || channel == 1 || channel == 6 || channel == 11) {
            *pWeight = uValue;
        }
        return TRUE;
    }
    return FALSE;
}

/***********************************************************************

 APIs for Object:

    WiFi.SSID.{i}.

    *  SSID_GetEntryCount
    *  SSID_GetEntry
    *  SSID_AddEntry
    *  SSID_DelEntry
    *  SSID_GetParamBoolValue
    *  SSID_GetParamIntValue
    *  SSID_GetParamUlongValue
    *  SSID_GetParamStringValue
    *  SSID_SetParamBoolValue
    *  SSID_SetParamIntValue
    *  SSID_SetParamUlongValue
    *  SSID_SetParamStringValue
    *  SSID_Validate
    *  SSID_Commit
    *  SSID_Rollback

***********************************************************************/
/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        SSID_GetEntryCount
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to retrieve the count of the table.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The count of the table

**********************************************************************/
ULONG
SSID_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    ULONG                           entryCount    = 0;
    UNREFERENCED_PARAMETER(hInsContext);
    
    entryCount = AnscSListQueryDepth(&pMyObject->SsidQueue);
    
    return entryCount;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ANSC_HANDLE
        SSID_GetEntry
            (
                ANSC_HANDLE                 hInsContext,
                ULONG                       nIndex,
                ULONG*                      pInsNumber
            );

    description:

        This function is called to retrieve the entry specified by the index.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                ULONG                       nIndex,
                The index of this entry;

                ULONG*                      pInsNumber
                The output instance number;

    return:     The handle to identify the entry

**********************************************************************/
ANSC_HANDLE
SSID_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj      = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PSINGLE_LINK_ENTRY              pSLinkEntry   = (PSINGLE_LINK_ENTRY       )NULL;
    UNREFERENCED_PARAMETER(hInsContext);

    pSLinkEntry = AnscQueueGetEntryByIndex(&pMyObject->SsidQueue, nIndex);
    
    if (pSLinkEntry)
    {
        pLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        
        *pInsNumber = pLinkObj->InstanceNumber;
    }
    
    return pLinkObj; /* return the handle */
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ANSC_HANDLE
        SSID_AddEntry
            (
                ANSC_HANDLE                 hInsContext,
                ULONG*                      pInsNumber
            );

    description:

        This function is called to add a new entry.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                ULONG*                      pInsNumber
                The output instance number;

    return:     The handle of new added entry.

**********************************************************************/
ANSC_HANDLE
SSID_AddEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG*                      pInsNumber
    )
{

    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj      = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid     = (PCOSA_DML_WIFI_SSID      )NULL;
    UNREFERENCED_PARAMETER(hInsContext);

    if ( WIFI_INDEX_MAX < pMyObject->ulSsidNextInstance)
       {
           return NULL;
       }

    pLinkObj                   = (PCOSA_CONTEXT_LINK_OBJECT)AnscAllocateMemory(sizeof(COSA_CONTEXT_LINK_OBJECT));
    if (!pLinkObj)
    {
        return NULL;
    }
    
    pWifiSsid                  = AnscAllocateMemory(sizeof(COSA_DML_WIFI_SSID));
    if (!pWifiSsid)
    {
        AnscFreeMemory(pLinkObj);
        return NULL;
    }
    
    if (TRUE)
    {
        pLinkObj->InstanceNumber           = pMyObject->ulSsidNextInstance;
    
        pWifiSsid->SSID.Cfg.InstanceNumber = pMyObject->ulSsidNextInstance;
    
        pMyObject->ulSsidNextInstance++;

        if ( pMyObject->ulSsidNextInstance == 0 )
        {
            pMyObject->ulSsidNextInstance = 1;
        }
        /*Set default Name, SSID & Alias*/
#if defined (MULTILAN_FEATURE)
        _ansc_sprintf(pWifiSsid->SSID.StaticInfo.Name, "SSID%lu", pLinkObj->InstanceNumber);
#if !defined(_INTEL_BUG_FIXES_)
        _ansc_sprintf(pWifiSsid->SSID.Cfg.Alias, "SSID%lu", pLinkObj->InstanceNumber);
        _ansc_sprintf(pWifiSsid->SSID.Cfg.SSID, "Cisco-SSID-%lu", pLinkObj->InstanceNumber);
#else
        _ansc_sprintf(pWifiSsid->SSID.Cfg.Alias, "cpe-SSID%lu", pLinkObj->InstanceNumber);
        _ansc_sprintf(pWifiSsid->SSID.Cfg.SSID, "SSID-%lu", pLinkObj->InstanceNumber);
#endif
#else
        _ansc_sprintf(pWifiSsid->SSID.StaticInfo.Name, "SSID%lu", *pInsNumber);
        _ansc_sprintf(pWifiSsid->SSID.Cfg.Alias, "SSID%lu", *pInsNumber);
        _ansc_sprintf(pWifiSsid->SSID.Cfg.SSID, "Cisco-SSID-%lu", *pInsNumber);
#endif    
        pLinkObj->hContext         = (ANSC_HANDLE)pWifiSsid;
        pLinkObj->hParentTable     = NULL;
        pLinkObj->bNew             = TRUE;
       
     
        CosaSListPushEntryByInsNum((PSLIST_HEADER)&pMyObject->SsidQueue, pLinkObj);

        CosaWifiRegAddSsidInfo((ANSC_HANDLE)pMyObject, (ANSC_HANDLE)pLinkObj);
    }
    
    *pInsNumber = pLinkObj->InstanceNumber;
    
    return (ANSC_HANDLE)pLinkObj; /* return the handle */
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        SSID_DelEntry
            (
                ANSC_HANDLE                 hInsContext,
                ANSC_HANDLE                 hInstance
            );

    description:

        This function is called to delete an exist entry.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                ANSC_HANDLE                 hInstance
                The exist entry handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
SSID_DelEntry
    (
        ANSC_HANDLE                 hInsContext,
        ANSC_HANDLE                 hInstance
    )
{

    PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInstance;
    PCOSA_DML_WIFI_SSID             pWifiSsid    = (PCOSA_DML_WIFI_SSID      )NULL;
    PSINGLE_LINK_ENTRY              pSLinkEntry  = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pAPLinkObj   = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )NULL;
    CHAR                            PathName[64] = {0};
    UNREFERENCED_PARAMETER(hInsContext);

    /*RDKB-6905, CID-33430, null check before use*/
    if(!pMyObject || !pLinkObj)
    {
        AnscTraceError(("%s: null object passed !\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    pWifiSsid    = (PCOSA_DML_WIFI_SSID      )pLinkObj->hContext;
    if(!pWifiSsid)
    {
        AnscTraceError(("%s: null pWifiSsid passed !\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    if ( CosaDmlWiFiSsidDelEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.InstanceNumber) != ANSC_STATUS_SUCCESS )
    {
        return ANSC_STATUS_FAILURE;
    }
    
    AnscQueuePopEntryByLink(&pMyObject->SsidQueue, &pLinkObj->Linkage);
    
    /*Reset the SSIDReference in AccessPoint table*/
    sprintf(PathName, "Device.WiFi.SSID.%lu.", pLinkObj->InstanceNumber);
    
    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->AccessPointQueue);
    while ( pSLinkEntry )
    {
        pAPLinkObj   = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        if(pAPLinkObj)
        {
            pWifiAp      = pAPLinkObj->hContext;

            if (pWifiAp && AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE))
            {
                memset(pWifiAp->AP.Cfg.SSID, 0, sizeof(pWifiAp->AP.Cfg.SSID));

                pAPLinkObj->bNew = TRUE;

                CosaWifiRegAddAPInfo((ANSC_HANDLE)pMyObject, (ANSC_HANDLE)pAPLinkObj);
            }
        }
        pSLinkEntry             = AnscQueueGetNextEntry(pSLinkEntry);
    }
    
    if (pLinkObj->bNew)
    {
        CosaWifiRegDelSsidInfo((ANSC_HANDLE)pMyObject, (ANSC_HANDLE)pLinkObj);
    }
    
    if (pLinkObj->hContext)
    {
        AnscFreeMemory(pLinkObj->hContext);
    }
    
    if (pLinkObj)
    {
        AnscFreeMemory(pLinkObj);
    }

#if !defined(_INTEL_BUG_FIXES_)
    return 0; /* succeeded */
#else
    return ANSC_STATUS_SUCCESS; /* succeeded */
#endif
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        SSID_GetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:

        This function is called to retrieve Boolean parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL*                       pBool
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
SSID_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_SSID             pWifiSsid    = (PCOSA_DML_WIFI_SSID      )pLinkObj->hContext;
    BOOLEAN                         bForceDisableFlag = FALSE;


    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Enable") == 0)
    {
        /* If WiFiForceRadioDisable Feature has been enabled then the radio status should
           be false, since in the HAL the radio status has been set to down state which is
           not reflected in DML layer.
         */
        if (ANSC_STATUS_SUCCESS != CosaDmlWiFiGetCurrForceDisableWiFiRadio(&bForceDisableFlag))
        {
            return FALSE;
        }
        /* collect value */
        wifi_getApEnable(pLinkObj->InstanceNumber - 1, &pWifiSsid->SSID.Cfg.bEnabled);
        *pBool = (bForceDisableFlag == TRUE) ? FALSE : pWifiSsid->SSID.Cfg.bEnabled;

        return TRUE;
    }
    
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "X_CISCO_COM_EnableOnline") == 0)
    {
        /* collect value */
        *pBool = pWifiSsid->SSID.Cfg.EnableOnline;

        return TRUE;
    }

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "X_CISCO_COM_RouterEnabled") == 0)
    {
        /* collect value */
        *pBool = pWifiSsid->SSID.Cfg.RouterEnabled;

        return TRUE;
    }


    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        SSID_GetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int*                        pInt
            );

    description:

        This function is called to retrieve integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int*                        pInt
                The buffer of returned integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
SSID_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
    /* check the parameter name and return the corresponding value */
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(pInt);

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        SSID_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
SSID_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_SSID             pWifiSsid    = (PCOSA_DML_WIFI_SSID      )pLinkObj->hContext;

    CosaDmlWiFiSsidGetDinfo((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.InstanceNumber, &pWifiSsid->SSID.DynamicInfo);
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Status") == 0)
    {
        /* collect value */
        *puLong  = pWifiSsid->SSID.DynamicInfo.Status;
        return TRUE;
    }

    if (strcmp(ParamName, "LastChange") == 0)
    {
        /* collect value */
        *puLong  = AnscGetTimeIntervalInSeconds(pWifiSsid->SSID.Cfg.LastChange, AnscGetTickInSeconds());
        return TRUE;
    }


    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        SSID_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
SSID_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_SSID             pWifiSsid    = (PCOSA_DML_WIFI_SSID      )pLinkObj->hContext;
    PUCHAR                          pLowerLayer  = NULL;

    
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Alias") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString(pWifiSsid->SSID.Cfg.Alias) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiSsid->SSID.Cfg.Alias);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiSsid->SSID.Cfg.Alias)+1;
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "Name") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString(pWifiSsid->SSID.StaticInfo.Name) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiSsid->SSID.StaticInfo.Name);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiSsid->SSID.StaticInfo.Name)+1;
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "LowerLayers") == 0)
    {
        /* collect value */
        pLowerLayer = CosaUtilGetLowerLayers((PUCHAR)"Device.WiFi.Radio.", (PUCHAR)pWifiSsid->SSID.Cfg.WiFiRadioName);
        
        if (pLowerLayer != NULL)
        {
            AnscCopyString(pValue, (char*)pLowerLayer);
            
            AnscFreeMemory(pLowerLayer);
        }     
        return 0;
    }

    if (strcmp(ParamName, "BSSID") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString((char*)pWifiSsid->SSID.StaticInfo.BSSID) < *pUlSize)
        {
		 if( ANSC_STATUS_SUCCESS == CosaDmlWiFiSsidGetSinfo(hInsContext,pWifiSsid->SSID.Cfg.InstanceNumber,&(pWifiSsid->SSID.StaticInfo))){
		    _ansc_sprintf
        	    (
                	pValue,
			"%02X:%02X:%02X:%02X:%02X:%02X",
			pWifiSsid->SSID.StaticInfo.BSSID[0],
                	pWifiSsid->SSID.StaticInfo.BSSID[1],
	                pWifiSsid->SSID.StaticInfo.BSSID[2],
	                pWifiSsid->SSID.StaticInfo.BSSID[3],
	               	pWifiSsid->SSID.StaticInfo.BSSID[4],
	                pWifiSsid->SSID.StaticInfo.BSSID[5]
        	    );
	            return 0;
		}
		else{
			memset(pWifiSsid->SSID.StaticInfo.BSSID,0,sizeof(pWifiSsid->SSID.StaticInfo.BSSID));
		  	memcpy(pValue, pWifiSsid->SSID.StaticInfo.BSSID, strlen((char*)pWifiSsid->SSID.StaticInfo.BSSID)+1);
			return 0;
		}
        }
        else
        {
            *pUlSize = AnscSizeOfString((char*)pWifiSsid->SSID.StaticInfo.BSSID)+1;
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "MACAddress") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString((char*)pWifiSsid->SSID.StaticInfo.MacAddress) < *pUlSize)
        {
	    if( ANSC_STATUS_SUCCESS == CosaDmlWiFiSsidGetSinfo(hInsContext,pWifiSsid->SSID.Cfg.InstanceNumber,&(pWifiSsid->SSID.StaticInfo)))
	    {
	    _ansc_sprintf
            (
                pValue,
                "%02X:%02X:%02X:%02X:%02X:%02X",
		pWifiSsid->SSID.StaticInfo.MacAddress[0],
                pWifiSsid->SSID.StaticInfo.MacAddress[1],
                pWifiSsid->SSID.StaticInfo.MacAddress[2],
                pWifiSsid->SSID.StaticInfo.MacAddress[3],
                pWifiSsid->SSID.StaticInfo.MacAddress[4],
                pWifiSsid->SSID.StaticInfo.MacAddress[5]
            );
            return 0;
	    }
	    else
	    {
	     memset(pWifiSsid->SSID.StaticInfo.MacAddress,0,sizeof(pWifiSsid->SSID.StaticInfo.MacAddress));
             *pValue = 0;
             return 0;
	    }
        }
        else
        {
            *pUlSize = AnscSizeOfString((char*)pWifiSsid->SSID.StaticInfo.MacAddress)+1;
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "SSID") == 0)
    {
        /* collect value */
        if ( ( AnscSizeOfString(pWifiSsid->SSID.Cfg.SSID) < *pUlSize) &&
             ( AnscSizeOfString("OutOfService") < *pUlSize) )
        {
            //zqiu: R5401
            AnscCopyString(pValue, pWifiSsid->SSID.Cfg.SSID);
            if ( (pWifiSsid->SSID.Cfg.InstanceNumber == 5) || (pWifiSsid->SSID.Cfg.InstanceNumber == 6) || 
             (pWifiSsid->SSID.Cfg.InstanceNumber == 9) || (pWifiSsid->SSID.Cfg.InstanceNumber == 10) ) {
                if ( ( IsSsidHotspot(pWifiSsid->SSID.Cfg.InstanceNumber) == TRUE ) && ( pWifiSsid->SSID.Cfg.bEnabled == FALSE ) ) {
	            AnscCopyString(pValue, "OutOfService");
		    return 0;
                }
            }
			if  ((pWifiSsid->SSID.Cfg.InstanceNumber == 16) && ( pWifiSsid->SSID.Cfg.bEnabled == FALSE ) )
			{
				AnscCopyString(pValue, "OutOfService");
				return 0;
			}
            AnscCopyString(pValue, pWifiSsid->SSID.Cfg.SSID);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiSsid->SSID.Cfg.SSID)+1;
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_DefaultSSID") == 0)
    {
	AnscCopyString(pValue, pWifiSsid->SSID.Cfg.DefaultSSID);
	return 0;
    }
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return -1;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        SSID_SetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL                        bValue
            );

    description:

        This function is called to set BOOL parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL                        bValue
                The updated BOOL value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
SSID_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_SSID             pWifiSsid    = (PCOSA_DML_WIFI_SSID      )pLinkObj->hContext;
    BOOLEAN                         bForceDisableFlag = FALSE;
    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "Enable") == 0)
    {
        if (ANSC_STATUS_SUCCESS != CosaDmlWiFiGetCurrForceDisableWiFiRadio(&bForceDisableFlag))
        {
            return FALSE;
        }
        /* SSID Enable object can be modified only when ForceDisableRadio feature is disabled */
        if(!bForceDisableFlag)
        {
            BOOL apEnabled;

            /* save update to backup */
            pWifiSsid->SSID.Cfg.bEnabled = bValue;
            wifi_getApEnable(pLinkObj->InstanceNumber - 1, &apEnabled);
            if (pWifiSsid->SSID.Cfg.bEnabled == apEnabled)
            {
                return  TRUE;
            }

            pWifiSsid->bSsidChanged = TRUE;
        } else {
            CcspWifiTrace(("RDK_LOG_ERROR, WIFI_ATTEMPT_TO_CHANGE_CONFIG_WHEN_FORCE_DISABLED\n" ));
            return FALSE;
        }
        return TRUE;
    }

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "X_CISCO_COM_EnableOnline") == 0)
    {
        if ( pWifiSsid->SSID.Cfg.EnableOnline == bValue )
        {
            return  TRUE;
        }

        /* save update to backup */
        pWifiSsid->SSID.Cfg.EnableOnline = bValue;
        pWifiSsid->bSsidChanged = TRUE; 

        return TRUE;
    }

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "X_CISCO_COM_RouterEnabled") == 0)
    {
        if ( pWifiSsid->SSID.Cfg.RouterEnabled == bValue )
        {
            return  TRUE;
        }

        /* save update to backup */
        pWifiSsid->SSID.Cfg.RouterEnabled = bValue;
        pWifiSsid->bSsidChanged = TRUE; 

        return TRUE;
    }


    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        SSID_SetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int                         iValue
            );

    description:

        This function is called to set integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int                         iValue
                The updated integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
SSID_SetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int                         iValue
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(iValue);
    /* check the parameter name and set the corresponding value */

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        SSID_SetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG                       uValue
            );

    description:

        This function is called to set ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG                       uValue
                The updated ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
SSID_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(uValue);
    /* check the parameter name and set the corresponding value */

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        SSID_SetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pString
            );

    description:

        This function is called to set string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pString
                The updated string value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
SSID_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj              = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_SSID             pWifiSsid             = (PCOSA_DML_WIFI_SSID      )pLinkObj->hContext;
    ULONG                           ulEntryNameLen;
    CHAR                            ucEntryParamName[256] = {0};
    CHAR                            ucEntryNameValue[256] = {0};
    BOOLEAN                         bForceDisableFlag = FALSE;

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "Alias") == 0)
    {
        /* save update to backup */
        AnscCopyString( pWifiSsid->SSID.Cfg.Alias, pString );

        return TRUE;
    }

    if (strcmp(ParamName, "LowerLayers") == 0)
    {
        /* save update to backup */
    #ifdef _COSA_SIM_
        _ansc_sprintf(ucEntryParamName, "%s%s", pString, "Name");
        
        ulEntryNameLen = sizeof(ucEntryNameValue);

        if ( ( 0 == CosaGetParamValueString(ucEntryParamName, ucEntryNameValue, &ulEntryNameLen)) &&
             (AnscSizeOfString(ucEntryNameValue) != 0) )
        {
            AnscCopyString(pWifiSsid->SSID.Cfg.WiFiRadioName, ucEntryNameValue);
            
            return TRUE;
        }
    #else
    	if(0 == strlen(pWifiSsid->SSID.Cfg.WiFiRadioName)) { /*Lower layers can only be specified during creation */
    
                _ansc_sprintf(ucEntryParamName, "%s%s", pString, "Name");
            
                if ( ( 0 == g_GetParamValueString(g_pDslhDmlAgent, ucEntryParamName, ucEntryNameValue, &ulEntryNameLen)) &&
                     (AnscSizeOfString(ucEntryNameValue) != 0) )
                {
                    AnscCopyString(pWifiSsid->SSID.Cfg.WiFiRadioName, ucEntryNameValue);
                    return TRUE;
                }
        } else
            return FALSE;
    #endif
    }

    if (strcmp(ParamName, "SSID") == 0)
    {
        if ( AnscEqualString(pWifiSsid->SSID.Cfg.SSID, pString, TRUE) )
        {
            return  TRUE;
        }

    if (IsSsidHotspot(pWifiSsid->SSID.Cfg.InstanceNumber) )
	{
		if(AnscEqualString(pString, "OutOfService", FALSE)) /* case insensitive */
		{
		    pWifiSsid->SSID.Cfg.bEnabled = FALSE;
		    fprintf(stderr, "%s: Disable HHS SSID since it's set to OutOfService\n", __FUNCTION__);
		}
	     else
		{

		    isHotspotSSIDIpdated = TRUE;
		}
	}

//RDKB-20043 - We should not restrict here
#if 0
	if ( (pWifiSsid->SSID.Cfg.InstanceNumber == 1) || (pWifiSsid->SSID.Cfg.InstanceNumber == 2) )
	{

	        if ( AnscEqualString(pWifiSsid->SSID.Cfg.DefaultSSID, pString, TRUE) )
	        {
        	    return  FALSE;
        	}

	}        
#endif /* 0 */

        /* If WiFiForceRadioDisable Feature has been enabled then the radio status should
           be false, since in the HAL the radio status has been set to down state which is
           not reflected in DML layer.
         */
        if (ANSC_STATUS_SUCCESS != CosaDmlWiFiGetCurrForceDisableWiFiRadio(&bForceDisableFlag))
        {
            return FALSE;
        }
        if(!bForceDisableFlag) {
            /* save update to backup */
            AnscCopyString( pWifiSsid->SSID.Cfg.SSID, pString );
            pWifiSsid->bSsidChanged = TRUE;

#if defined(FEATURE_HOSTAP_AUTHENTICATOR) && !defined (_XB7_PRODUCT_REQ_)
            /* RDKB-30035 Run time config change */
            BOOLEAN isNativeHostapdDisabled = FALSE;
            CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
            if (isNativeHostapdDisabled &&
                !(hapd_reload_ssid(pWifiSsid->SSID.Cfg.InstanceNumber - 1, pWifiSsid->SSID.Cfg.SSID)))
            {
                CcspWifiTrace(("RDK_LOG_INFO, WIFI_SSID_CHANGE_PUSHED_SUCCEESSFULLY\n"));
            }
#endif //FEATURE_HOSTAP_AUTHENTICATOR
        } else {
            CcspWifiTrace(("RDK_LOG_ERROR, WIFI_ATTEMPT_TO_CHANGE_CONFIG_WHEN_FORCE_DISABLED\n" ));
            return FALSE;
        }
        return TRUE;
    }


    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        SSID_Validate
            (
                ANSC_HANDLE                 hInsContext,
                char*                       pReturnParamName,
                ULONG*                      puLength
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       pReturnParamName,
                The buffer (128 bytes) of parameter name if there's a validation. 

                ULONG*                      puLength
                The output length of the param name. 

    return:     TRUE if there's no validation.

**********************************************************************/
BOOL
SSID_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj      = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_SSID             pWifiSsid     = (PCOSA_DML_WIFI_SSID      )pLinkObj->hContext;
    PSINGLE_LINK_ENTRY              pSLinkEntry   = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj  = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid2    = (PCOSA_DML_WIFI_SSID      )NULL;
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    PUCHAR                          pLowerLayer   = (PUCHAR                   )NULL;
    UINT                            radioIndex    = 0;
    UINT                            APsOnRadio    = 0;
#endif
  
    /*Alias should be non-empty*/
    if (AnscSizeOfString(pWifiSsid->SSID.Cfg.Alias) == 0)
    {
        AnscCopyString(pReturnParamName, "Alias");
        *puLength = AnscSizeOfString("Alias");
        return FALSE;
    }

    /* Lower Layers has to be non-empty */
    if (AnscSizeOfString(pWifiSsid->SSID.Cfg.WiFiRadioName) == 0)
    {
        AnscCopyString(pReturnParamName, "LowerLayers");
        *puLength = AnscSizeOfString("LowerLayers");
        return FALSE;
    }
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    else
    {
        if(pLinkObj->bNew)
        {
            pLowerLayer = CosaUtilGetLowerLayers((PUCHAR)"Device.WiFi.Radio.", (PUCHAR)pWifiSsid->SSID.Cfg.WiFiRadioName);
                if (pLowerLayer != NULL)
                {
                    sscanf((char*)pLowerLayer, (char*)"Device.WiFi.Radio.%d.", &radioIndex);
                    CosaDmlWiFiGetNumberOfAPsOnRadio(radioIndex-1, &APsOnRadio);
                    if (APsOnRadio >= WIFI_MAX_ENTRIES_PER_RADIO)
                    {
                        AnscCopyString(pWifiSsid->SSID.Cfg.WiFiRadioName, ""); /* Reset LowerLayers parameter */
                        AnscCopyString(pReturnParamName, "LowerLayers");
                        *puLength = AnscSizeOfString("LowerLayers");
                        return FALSE;
                    }
                }
        }
    }
#endif
    /* SSID should be non-empty */
    if (AnscSizeOfString(pWifiSsid->SSID.Cfg.SSID) == 0)
    {
        AnscCopyString(pReturnParamName, "SSID");
        *puLength = AnscSizeOfString("SSID");
        return FALSE;
    }
    // "alphabet, digit, underscore, hyphen and dot" 
/*    if (CosaDmlWiFiSsidValidateSSID() == TRUE) {
        if ( isValidSSID(pWifiSsid->SSID.Cfg.SSID) == false ) {
            // Reset to current value because snmp request will not rollback on invalid values 
            AnscTraceError(("SSID '%s' is invalid.  \n", pWifiSsid->SSID.Cfg.SSID));
            CosaDmlWiFiSsidGetSSID(pWifiSsid->SSID.Cfg.InstanceNumber, pWifiSsid->SSID.Cfg.SSID);
            AnscTraceError(("SSID is treated as a special case and will be rolled back even for snmp to old value '%s' \n", pWifiSsid->SSID.Cfg.SSID));
            AnscCopyString(pReturnParamName, "SSID");
            *puLength = AnscSizeOfString("SSID");
            return FALSE;
        }
    }*/
    if (!IsSsidHotspot(pWifiSsid->SSID.Cfg.InstanceNumber))
    {
        if(isReservedSSID(pMyObject->ReservedSSIDNames,pWifiSsid->SSID.Cfg.SSID) || pWifiSsid->SSID.Cfg.SSID[0] == ' ')
        {
            AnscTraceError(("SSID '%s' contains preserved name\n", pWifiSsid->SSID.Cfg.SSID));
            CosaDmlWiFiSsidGetSSID(pWifiSsid->SSID.Cfg.InstanceNumber, pWifiSsid->SSID.Cfg.SSID);
            AnscTraceError(("SSID is treated as a special case and will be rolled back even for snmp to old value '%s' \n", pWifiSsid->SSID.Cfg.SSID));
            AnscCopyString(pReturnParamName, "SSID");
            *puLength = AnscSizeOfString("SSID");
            return FALSE;
        }
    }
 
    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
    while ( pSLinkEntry )
    {
        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        pSLinkEntry  = AnscQueueGetNextEntry(pSLinkEntry);
        pWifiSsid2   = pSSIDLinkObj->hContext;

        if (pSSIDLinkObj == pLinkObj)
        {
            continue;
        }

#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
        if (!strlen(pWifiSsid2->SSID.StaticInfo.Name))
        {
            continue;
        }
#endif

#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        if (AnscEqualString(pWifiSsid->SSID.Cfg.SSID, pWifiSsid2->SSID.Cfg.SSID, TRUE))
        {
            AnscCopyString(pReturnParamName, "SSID");

            *puLength = AnscSizeOfString("SSID");
            return FALSE;
        }
#else
        if (AnscEqualString(pWifiSsid->SSID.StaticInfo.Name, pWifiSsid2->SSID.StaticInfo.Name, TRUE))
        {
        
            AnscCopyString(pReturnParamName, "Name");

            *puLength = AnscSizeOfString("Name");
            return FALSE;
        }
#endif
    }

    if ( (pWifiSsid->SSID.Cfg.bEnabled == TRUE) && 
         IsSsidHotspot(pWifiSsid->SSID.Cfg.InstanceNumber) && 
         AnscEqualString(pWifiSsid->SSID.Cfg.SSID, "OutOfService", FALSE) /* case insensitive */)
    {
        AnscCopyString(pReturnParamName, "SSID");

        *puLength = AnscSizeOfString("SSID");

        fprintf(stderr, "%s: Cannot Enable HHS, SSID is %s\n", 
                __FUNCTION__, pWifiSsid->SSID.Cfg.SSID);

        return FALSE;
    }
    return TRUE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        SSID_Commit
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
SSID_Commit
    (
        ANSC_HANDLE                 hInsContext
    )

{
    PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_SSID             pWifiSsid    = (PCOSA_DML_WIFI_SSID      )pLinkObj->hContext;
    ANSC_STATUS                     returnStatus = ANSC_STATUS_SUCCESS;

    if (pLinkObj->bNew)
    {
        pLinkObj->bNew = FALSE;

        returnStatus = CosaDmlWiFiSsidAddEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, &pWifiSsid->SSID);
        
        if (returnStatus != ANSC_STATUS_SUCCESS)
        {
            return ANSC_STATUS_FAILURE;
        }
        
        CosaWifiRegDelSsidInfo((ANSC_HANDLE)pMyObject, (ANSC_HANDLE)pLinkObj);
    }
    else
    {
        if ( !pWifiSsid->bSsidChanged )
        {
            return  ANSC_STATUS_SUCCESS;
        }
        else
        {
            pWifiSsid->bSsidChanged = FALSE;
            CcspTraceInfo(("WiFi SSID -- apply the changes...\n"));
        }
        return CosaDmlWiFiSsidSetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, &pWifiSsid->SSID.Cfg);  
    }
    return 0;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        SSID_Rollback
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to roll back the update whenever there's a 
        validation found.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
SSID_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj      = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PSINGLE_LINK_ENTRY              pSLinkEntry   = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj  = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid     = (PCOSA_DML_WIFI_SSID      )NULL;
    ULONG                           idx           = 0;
    
    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
    
    while ( pSLinkEntry )
    {
        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        
        if (pSSIDLinkObj == pLinkObj)
        {
            break;
        }
        idx++;
        
        pSLinkEntry             = AnscQueueGetNextEntry(pSLinkEntry);
    }
    
    pWifiSsid   = (PCOSA_DML_WIFI_SSID)pLinkObj->hContext;
    
    return CosaDmlWiFiSsidGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, idx, &pWifiSsid->SSID);  
}

/***********************************************************************

 APIs for Object:

    WiFi.SSID.{i}.Stats.

    *  Stats4_GetParamBoolValue
    *  Stats4_GetParamIntValue
    *  Stats4_GetParamUlongValue
    *  Stats4_GetParamStringValue

***********************************************************************/
/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Stats4_GetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:

        This function is called to retrieve Boolean parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL*                       pBool
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Stats4_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(pBool);
    /* check the parameter name and return the corresponding value */

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Stats4_GetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int*                        pInt
            );

    description:

        This function is called to retrieve integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int*                        pInt
                The buffer of returned integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Stats4_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(pInt);
    /* check the parameter name and return the corresponding value */

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Stats4_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Stats4_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject      = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj       = (PCOSA_CONTEXT_LINK_OBJECT )hInsContext;
    PCOSA_DML_WIFI_SSID             pWifiSsid      = (PCOSA_DML_WIFI_SSID       )pLinkObj->hContext;
    PCOSA_DML_WIFI_SSID_STATS       pWifiSsidStats = (PCOSA_DML_WIFI_SSID_STATS )&pWifiSsid->Stats;
    
    CosaDmlWiFiSsidGetStats((ANSC_HANDLE)pMyObject->hPoamWiFiDm,pWifiSsid->SSID.Cfg.InstanceNumber, pWifiSsidStats);

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "BytesSent") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->BytesSent;
        return TRUE;
    }

    if (strcmp(ParamName, "BytesReceived") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->BytesReceived;
        return TRUE;
    }

    if (strcmp(ParamName, "PacketsSent") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->PacketsSent;
        return TRUE;
    }

    if (strcmp(ParamName, "PacketsReceived") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->PacketsReceived;
        return TRUE;
    }

    if (strcmp(ParamName, "ErrorsSent") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->ErrorsSent;
        return TRUE;
    }

    if (strcmp(ParamName, "ErrorsReceived") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->ErrorsReceived;
        return TRUE;
    }

    if (strcmp(ParamName, "UnicastPacketsSent") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->UnicastPacketsSent;
        return TRUE;
    }

    if (strcmp(ParamName, "UnicastPacketsReceived") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->UnicastPacketsReceived;
        return TRUE;
    }

    if (strcmp(ParamName, "DiscardPacketsSent") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->DiscardPacketsSent;
        return TRUE;
    }

    if (strcmp(ParamName, "DiscardPacketsReceived") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->DiscardPacketsReceived;
        return TRUE;
    }

    if (strcmp(ParamName, "MulticastPacketsSent") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->MulticastPacketsSent;
        return TRUE;
    }

    if (strcmp(ParamName, "MulticastPacketsReceived") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->MulticastPacketsReceived;
        return TRUE;
    }

    if (strcmp(ParamName, "BroadcastPacketsSent") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->BroadcastPacketsSent;
        return TRUE;
    }

    if (strcmp(ParamName, "BroadcastPacketsReceived") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->BroadcastPacketsReceived;
        return TRUE;
    }

    if (strcmp(ParamName, "UnknownProtoPacketsReceived") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->UnknownProtoPacketsReceived;
        return TRUE;
    }

    if (strcmp(ParamName, "RetransCount") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->RetransCount;
        return TRUE;
    }

    if (strcmp(ParamName, "FailedRetransCount") == 0)
    {
       /* collect value */
        *puLong = pWifiSsidStats->FailedRetransCount;
        return TRUE;
    }

    if (strcmp(ParamName, "RetryCount") == 0)
    {
        /* collect value */
       *puLong = pWifiSsidStats->RetryCount;
        return TRUE;
    }

    if (strcmp(ParamName, "MultipleRetryCount") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->MultipleRetryCount;
        return TRUE;
    }
    

    if (strcmp(ParamName, "ACKFailureCount") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->ACKFailureCount;
        return TRUE;
    }

    if (strcmp(ParamName, "AggregatedPacketCount") == 0)
    {
        /* collect value */
        *puLong = pWifiSsidStats->AggregatedPacketCount;
        return TRUE;
    }
	/* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        Stats4_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
Stats4_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(pValue);
    UNREFERENCED_PARAMETER(pUlSize);
    /* check the parameter name and return the corresponding value */

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return -1;
}

/***********************************************************************

 APIs for Object:

    WiFi.SSID.{i}.X_LGI-COM_WifiSupportedRates.

    *  WifiSupportedRates_GetParamStringValue
    *  WifiSupportedRates_SetParamStringValue

***********************************************************************/

ULONG
WifiSupportedRates_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject      = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj       = (PCOSA_CONTEXT_LINK_OBJECT )hInsContext;
    PCOSA_DML_WIFI_SSID             pWifiSsid      = (PCOSA_DML_WIFI_SSID       )pLinkObj->hContext;
    PCOSA_DML_WIFI_SSID_SUPPORTED_RATES pWifiSsidSupRate = (PCOSA_DML_WIFI_SSID_SUPPORTED_RATES )&pWifiSsid->SupportedRate;

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "WiFiDisableBasicRates") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString(pWifiSsidSupRate->disableBasicRates) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiSsidSupRate->disableBasicRates);
            return 0;
        }
        else
        {
            return 1;
        }
    }

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "WiFiDisableSupportedRates") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString(pWifiSsidSupRate->disableSupportedRates) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiSsidSupRate->disableSupportedRates);
            return 0;
        }
        else
        {
            return 1;
        }
    }

    return 0;
}

/*Below function verify whether given string is Hexadecimal digit or not*/
static int CheckStringIsHexString(char *value)
{
    char buff[16];
    int len = 0;
    int i = 0;

    memset(buff, 0, sizeof(buff));

    if(sscanf(value, "0x%s", buff) != 1)
    {
           return -1;
    }
    len = strlen(buff);
    for(i = 0;i < len;i++)
    {
        if(isxdigit(buff[i]) == 0)
            return -2;
    }
    return 0;
}

BOOL
WifiSupportedRates_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject      = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj       = (PCOSA_CONTEXT_LINK_OBJECT )hInsContext;
    PCOSA_DML_WIFI_SSID             pWifiSsid      = (PCOSA_DML_WIFI_SSID       )pLinkObj->hContext;
    PCOSA_DML_WIFI_SSID_SUPPORTED_RATES pWifiSsidSupRate = (PCOSA_DML_WIFI_SSID_SUPPORTED_RATES )&pWifiSsid->SupportedRate;
    ULONG HexValue = 0xFFFF;

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "WiFiDisableBasicRates") == 0)
    {
        if(strlen(pValue) != MAX_BASICRATES_BYTE)
        {
            CcspWifiTrace(("RDK_LOG_ERROR, Input string:%s len error.\n",pValue));
            return FALSE;
        }

        if(CheckStringIsHexString(pValue) != 0)
        {
            CcspWifiTrace(("RDK_LOG_ERROR, Input string:%s is not hexadecimal number\n",pValue));
            return FALSE;
        }

        HexValue = strtoul(pValue, NULL, 16);

        if((pWifiSsid->SSID.Cfg.InstanceNumber)%2 ==  1)
        {
            if((HexValue & BASICRATES_24G_LESS_VALUE) != BASICRATES_24G_LESS_VALUE)
            {
                CcspWifiTrace(("RDK_LOG_ERROR, Input string:%s fail\n",pValue));
                return FALSE;
            }
        }
        else
        {
            if((HexValue & BASICRATES_5G_LESS_VALUE) != BASICRATES_5G_LESS_VALUE)
            {
                CcspWifiTrace(("RDK_LOG_ERROR, Input string:%s fail\n",pValue));
                return FALSE;
            }
        }

        if(AnscEqualString(pWifiSsidSupRate->disableBasicRates, pValue, TRUE))
        {
            return  TRUE;
        }

        /* collect value */
        if(!wifi_setSupportRatesDisableBasicRates(pWifiSsid->SSID.Cfg.InstanceNumber-1,pValue))
        {
            AnscCopyString(pWifiSsidSupRate->disableBasicRates,pValue);
            enable_reset_radio_flag(pWifiSsid->SSID.Cfg.InstanceNumber-1);
            return TRUE;
        }
        else
        {
            CcspWifiTrace(("RDK_LOG_ERROR, fail in hal fun:wifi_setSupportRatesDisableBasicRates \n"));
            return FALSE;
        }
    }

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "WiFiDisableSupportedRates") == 0)
    {
        /* collect value */
        if(strlen(pValue) != MAX_SUPPORTRATES_BYTE)
        {
            CcspWifiTrace(("RDK_LOG_ERROR, Input string:`%s len error.\n",pValue));
            return FALSE;
        }

        if(CheckStringIsHexString(pValue) != 0)
        {
            CcspWifiTrace(("RDK_LOG_ERROR, Input string:%s is not hexadecimal number\n",pValue));
            return FALSE;
        }

        HexValue = strtoul(pValue, NULL, 16);

        if((pWifiSsid->SSID.Cfg.InstanceNumber)%2 ==  1)
        {
            if((HexValue & SUPPORTRATES_24G_LESS_VALUE) != SUPPORTRATES_24G_LESS_VALUE)
            {
                CcspWifiTrace(("RDK_LOG_ERROR, Input string:%s fail\n",pValue));
                return FALSE;
            }
        }
        else
        {
            if((HexValue & SUPPORTRATES_5G_LESS_VALUE) != SUPPORTRATES_5G_LESS_VALUE)
            {
                CcspWifiTrace(("RDK_LOG_ERROR, Input string:%s fail\n",pValue));
                return FALSE;
            }
        }

        if(AnscEqualString(pWifiSsidSupRate->disableSupportedRates, pValue, TRUE))
        {
            return  TRUE;
        }

        if(!wifi_setSupportRatesDisableSupportedRates(pWifiSsid->SSID.Cfg.InstanceNumber-1,pValue))
        {
            AnscCopyString(pWifiSsidSupRate->disableSupportedRates,pValue);
            enable_reset_radio_flag(pWifiSsid->SSID.Cfg.InstanceNumber-1);
            return TRUE;
        }
        else
        {
            CcspWifiTrace(("RDK_LOG_ERROR, fail in hal fun:wifi_setSupportRatesDisableSupportedRates \n"));
            return FALSE;
        }
    }

    return TRUE;
}
/***********************************************************************

 APIs for Object:

    WiFi.AccessPoint.{i}.

    *  AccessPoint_GetEntryCount
    *  AccessPoint_GetEntry
    *  AccessPoint_AddEntry
    *  AccessPoint_DelEntry
    *  AccessPoint_GetParamBoolValue
    *  AccessPoint_GetParamIntValue
    *  AccessPoint_GetParamUlongValue
    *  AccessPoint_GetParamStringValue
    *  AccessPoint_SetParamBoolValue
    *  AccessPoint_SetParamIntValue
    *  AccessPoint_SetParamUlongValue
    *  AccessPoint_SetParamStringValue
    *  AccessPoint_Validate
    *  AccessPoint_Commit
    *  AccessPoint_Rollback

***********************************************************************/
/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        AccessPoint_GetEntryCount
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to retrieve the count of the table.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The count of the table

**********************************************************************/
ULONG
AccessPoint_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    ULONG                           entryCount    = 0;
    UNREFERENCED_PARAMETER(hInsContext);    
    entryCount = AnscSListQueryDepth(&pMyObject->AccessPointQueue);
    
    return entryCount;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ANSC_HANDLE
        AccessPoint_GetEntry
            (
                ANSC_HANDLE                 hInsContext,
                ULONG                       nIndex,
                ULONG*                      pInsNumber
            );

    description:

        This function is called to retrieve the entry specified by the index.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                ULONG                       nIndex,
                The index of this entry;

                ULONG*                      pInsNumber
                The output instance number;

    return:     The handle to identify the entry

**********************************************************************/
ANSC_HANDLE
AccessPoint_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject       = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObjAp      = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PSINGLE_LINK_ENTRY              pSLinkEntryAp   = (PSINGLE_LINK_ENTRY       )NULL;
    UNREFERENCED_PARAMETER(hInsContext);
    pSLinkEntryAp = AnscQueueGetEntryByIndex(&pMyObject->AccessPointQueue, nIndex);
    
    if ( pSLinkEntryAp )
    {
        pLinkObjAp  = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntryAp);

        *pInsNumber = pLinkObjAp->InstanceNumber;
    }

    return pLinkObjAp; /* return the handle */
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ANSC_HANDLE
        AccessPoint_AddEntry
            (
                ANSC_HANDLE                 hInsContext,
                ULONG*                      pInsNumber
            );

    description:

        This function is called to add a new entry.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                ULONG*                      pInsNumber
                The output instance number;

    return:     The handle of new added entry.

**********************************************************************/
ANSC_HANDLE
AccessPoint_AddEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG*                      pInsNumber
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj      = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_AP               pWifiAp       = (PCOSA_DML_WIFI_AP        )NULL;
#ifndef MULTILAN_FEATURE
    return NULL; /* Temporarily dont allow addition/deletion of AccessPoints */
#endif    
    pLinkObj   = (PCOSA_CONTEXT_LINK_OBJECT)AnscAllocateMemory(sizeof(COSA_CONTEXT_LINK_OBJECT));
    
    if (!pLinkObj)
    {
        return NULL;
    }
    
    pWifiAp        = AnscAllocateMemory(sizeof(COSA_DML_WIFI_AP));
    
    if (!pWifiAp)
    {
        AnscFreeMemory(pLinkObj);
        
        return NULL;
    }
    
    if (TRUE)
    {
        pLinkObj->InstanceNumber       = pMyObject->ulAPNextInstance;
    
        pWifiAp->AP.Cfg.InstanceNumber = pMyObject->ulAPNextInstance;
    
        pMyObject->ulAPNextInstance++;

        if ( pMyObject->ulAPNextInstance == 0 )
        {
            pMyObject->ulAPNextInstance = 1;
        }
        
        pLinkObj->hContext     = pWifiAp;
        pLinkObj->hParentTable = NULL;
        pLinkObj->bNew         = TRUE;

        /*Set default Alias*/
#if defined (MULTILAN_FEATURE)
#if !defined(_INTEL_BUG_FIXES_)
        _ansc_sprintf(pWifiAp->AP.Cfg.Alias, "AccessPoint%lu", pLinkObj->InstanceNumber);
#else
        _ansc_sprintf(pWifiAp->AP.Cfg.Alias, "cpe-AccessPoint%lu", pLinkObj->InstanceNumber);
        _ansc_sprintf(pWifiAp->AP.Cfg.SSID, "Device.WiFi.SSID.%lu.", pLinkObj->InstanceNumber);
#endif
#else
        _ansc_sprintf(pWifiAp->AP.Cfg.Alias, "AccessPoint%lu", *pInsNumber);
#endif    
        CosaSListPushEntryByInsNum((PSLIST_HEADER)&pMyObject->AccessPointQueue, pLinkObj);
    
        CosaWifiRegAddAPInfo((ANSC_HANDLE)pMyObject, (ANSC_HANDLE)pLinkObj);
    }
    
    *pInsNumber = pLinkObj->InstanceNumber;
    
    return (ANSC_HANDLE)pLinkObj; /* return the handle */
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        AccessPoint_DelEntry
            (
                ANSC_HANDLE                 hInsContext,
                ANSC_HANDLE                 hInstance
            );

    description:

        This function is called to delete an exist entry.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                ANSC_HANDLE                 hInstance
                The exist entry handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
AccessPoint_DelEntry
    (
        ANSC_HANDLE                 hInsContext,
        ANSC_HANDLE                 hInstance
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInstance;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )NULL;
    PSINGLE_LINK_ENTRY              pSLinkEntry   = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj  = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
#if !defined(_INTEL_BUG_FIXES_)
    PCOSA_DML_WIFI_SSID             pWifiSsid     = (PCOSA_DML_WIFI_SSID      )NULL;
#endif
    CHAR                            PathName[64]  = {0};

#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    return ANSC_STATUS_FAILURE; /*Temporarily we dont allow addition/deletion of AccessPoint entries */
#endif
    
    /*
      When an AP is deleted, if it is an orphan one, DM adapter deletes it internally. 
      If it is associated with a SSID, CosaDmlWiFiApSetCfg() is called to pass the configuration to backend,
      with all AP configuration set to default values. 
    */
    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
    
    pWifiAp = pLinkObj->hContext;

    while ( pSLinkEntry )
    {
        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        
        sprintf(PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);
        
        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }    
        
        pSLinkEntry             = AnscQueueGetNextEntry(pSLinkEntry);
    }

    if ( pSLinkEntry )
    {
#if !defined(_INTEL_BUG_FIXES_)
        pWifiSsid = pSSIDLinkObj->hContext;
#endif
        
        /*Set the default Cfg to backend*/
        pWifiAp->AP.Cfg.bEnabled = FALSE;
        pWifiAp->AP.Cfg.SSID[0]  = 0;
        
#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        CosaDmlWiFiApSetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->AP.Cfg);
#else
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
        CosaDmlWiFiApSetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->AP.Cfg);
#else
        CosaDmlWiFiApDelEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiAp->AP.Cfg.InstanceNumber);
#endif
#endif
    }
    
    AnscQueuePopEntryByLink(&pMyObject->AccessPointQueue, &pLinkObj->Linkage);
    
    if (pLinkObj->bNew)
    {
        CosaWifiRegDelAPInfo((ANSC_HANDLE)pMyObject, (ANSC_HANDLE)pLinkObj);
    }
    
    if (pWifiAp->AssocDevices)
    {
        AnscFreeMemory(pWifiAp->AssocDevices);
    }
    if (pLinkObj->hContext)
    {
        AnscFreeMemory(pLinkObj->hContext);
    }
    if (pLinkObj)
    {
        AnscFreeMemory(pLinkObj);
    }
    
    return ANSC_STATUS_SUCCESS;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        AccessPoint_GetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:

        This function is called to retrieve Boolean parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL*                       pBool
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
AccessPoint_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PSINGLE_LINK_ENTRY              pSLinkEntry  = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid    = (PCOSA_DML_WIFI_SSID      )NULL;
    CHAR                            PathName[64] = {0};


    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "IsolationEnable") == 0)
    {

        /* collect value */
        *pBool = pWifiAp->AP.Cfg.IsolationEnable;
        
        return TRUE;
    }

    if (strcmp(ParamName, "Enable") == 0)
    {
        /* collect value */
        int wlanIndex;
        BOOL enabled = FALSE;

        pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
        while ( pSLinkEntry )
        {
            pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
            pWifiSsid    = pSSIDLinkObj->hContext;
            sprintf(PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);

            if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
            {
                break;
            }
            pSLinkEntry = AnscQueueGetNextEntry(pSLinkEntry);
        }
        if (pSLinkEntry)
        {
            /*CID: 58064 Unchecked return value*/
            if(wifi_getIndexFromName(pWifiSsid->SSID.StaticInfo.Name, &wlanIndex))
               CcspTraceInfo(("%s : wlanIndex[%d]\n",__FUNCTION__, wlanIndex));
            wifi_getApEnable(wlanIndex,&enabled);
        }

        *pBool = enabled;
        return TRUE;
    }

    if (strcmp(ParamName, "SSIDAdvertisementEnabled") == 0)
    {

        /* collect value */
        *pBool = pWifiAp->AP.Cfg.SSIDAdvertisementEnabled;
        return TRUE;
    }

    if (strcmp(ParamName, "WMMCapability") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Info.WMMCapability;
        return TRUE;
    }

    if (strcmp(ParamName, "UAPSDCapability") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Info.UAPSDCapability;
        return TRUE;
    }

    if (strcmp(ParamName, "WMMEnable") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Cfg.WMMEnable;
        return TRUE;
    }

    if (strcmp(ParamName, "UAPSDEnable") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Cfg.UAPSDEnable;
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_BssCountStaAsCpe") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Cfg.BssCountStaAsCpe;
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_BssHotSpot") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Cfg.BssHotSpot;
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_KickAssocDevices") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Cfg.KickAssocDevices;
        return TRUE;
    }

#if defined (FEATURE_SUPPORT_INTERWORKING)

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_InterworkingServiceCapability") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Cfg.InterworkingCapability;
        return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_InterworkingServiceEnable") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Cfg.InterworkingEnable;
        return TRUE;
    }
#else
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_InterworkingServiceCapability") == 0)
    {
        *pBool = FALSE;
        return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_InterworkingServiceEnable") == 0)
    {
        /* collect value */
        *pBool = FALSE;
        return TRUE;
    }
#endif
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_InterworkingApplySettings") == 0)
    {
        *pBool = FALSE;
        return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_rapidReconnectCountEnable") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_rapidReconnectCountEnable;
        return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_StatsEnable") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_StatsEnable;
        return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_WirelessManagementImplemented") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Cfg.WirelessManagementImplemented;
        return TRUE;
    }
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_BSSTransitionImplemented") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Cfg.BSSTransitionImplemented;
        return TRUE;
    }
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_BSSTransitionActivated") == 0)
    {
        /* collect value */
        int ret = 0, wlanIndex = -1;
        wlanIndex = pWifiAp->AP.Cfg.InstanceNumber - 1 ;
        ret = wifi_getBSSTransitionActivation( wlanIndex, &pWifiAp->AP.Cfg.BSSTransitionActivated);
        if ( ret == 0)
        {
            *pBool = pWifiAp->AP.Cfg.BSSTransitionActivated;
        }
        else
        {
            *pBool = 0;
            pWifiAp->AP.Cfg.BSSTransitionActivated = FALSE;
        }
        return TRUE;
    }
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_NeighborReportActivated") == 0)
    {
        /* collect value */
        INT wlanIndex = pWifiAp->AP.Cfg.InstanceNumber - 1;
        BOOL rrmEnabled;

        if (wifi_getNeighborReportActivation(wlanIndex , &rrmEnabled) != RETURN_OK)
        {
            *pBool = pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_NeighborReportActivated;
        }
        else
        {
            *pBool = rrmEnabled;
        }

        return TRUE;
    }

    if (strcmp(ParamName, "MACAddressControlEnabled") == 0)
    {
        *pBool = (pWifiAp->MF.FilterAsBlackList != TRUE) && (pWifiAp->MF.bEnabled == TRUE) ? TRUE : FALSE;
         return TRUE;
    }
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        AccessPoint_GetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int*                        pInt
            );

    description:

        This function is called to retrieve integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int*                        pInt
                The buffer of returned integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
AccessPoint_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
    /* check the parameter name and return the corresponding value */
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "X_CISCO_COM_WmmNoAck") == 0)
    {
        *pInt = pWifiAp->AP.Cfg.WmmNoAck;
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_MulticastRate") == 0)
    {
        *pInt = pWifiAp->AP.Cfg.MulticastRate;
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_BssMaxNumSta") == 0)
    {
        *pInt = pWifiAp->AP.Cfg.BssMaxNumSta;
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_BssUserStatus") == 0)
    {
        *pInt = pWifiAp->AP.Info.BssUserStatus;
        return TRUE;
    }
        if (strcmp(ParamName, "X_RDKCENTRAL-COM_ManagementFramePowerControl") == 0)
    {
        INT wlanIndex = -1;
        wlanIndex = pWifiAp->AP.Cfg.InstanceNumber -1 ;
        wifi_getApManagementFramePowerControl(wlanIndex , &pWifiAp->AP.Cfg.ManagementFramePowerControl);
        *pInt = pWifiAp->AP.Cfg.ManagementFramePowerControl;
        return TRUE;
    }

	if (strcmp(ParamName, "X_RDKCENTRAL-COM_rapidReconnectMaxTime") == 0)
	{
       *pInt = pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_rapidReconnectMaxTime;
	  return TRUE;
	}

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        AccessPoint_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
AccessPoint_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PSINGLE_LINK_ENTRY              pSLinkEntry  = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid    = (PCOSA_DML_WIFI_SSID      )NULL;
    CHAR                            PathName[64] = {0};
    
    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
    
    while ( pSLinkEntry )
    {
        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        pWifiSsid    = pSSIDLinkObj->hContext;
        
        sprintf(PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);
        
        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }    
        
        pSLinkEntry             = AnscQueueGetNextEntry(pSLinkEntry);
    }
    
    if (pSLinkEntry)
    {
#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        CosaDmlWiFiApGetInfo((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->AP.Info);
#else
        CosaDmlWiFiApGetInfo((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->AP.Info);
        CosaDmlWiFiApAssociatedDevicesHighWatermarkGetVal((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->AP.Cfg);
#endif
    }
    
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Status") == 0)
    {
        /* collect value */
        *puLong = pWifiAp->AP.Info.Status;
        return TRUE;
    }

    if (strcmp(ParamName, "RetryLimit") == 0)
    {
        /* collect value */
        *puLong = pWifiAp->AP.Cfg.RetryLimit;
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_LongRetryLimit") == 0)
    {
        *puLong = pWifiAp->AP.Cfg.LongRetryLimit; 
        return TRUE;
    }
  
    if ((strcmp(ParamName, "MaxAssociatedDevices") == 0) || (strcmp(ParamName, "MaxAllowedAssociations") == 0))
    {
        *puLong = pWifiAp->AP.Cfg.MaxAssociatedDevices; 
        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_AssociatedDevicesHighWatermarkThreshold") == 0)
    {
        *puLong = pWifiAp->AP.Cfg.HighWatermarkThreshold; 
        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_AssociatedDevicesHighWatermarkThresholdReached") == 0)
    {
        *puLong = pWifiAp->AP.Cfg.HighWatermarkThresholdReached; 
        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_AssociatedDevicesHighWatermark") == 0)
    {
        *puLong = pWifiAp->AP.Cfg.HighWatermark; 
        return TRUE;
    }
	
	//zqiu
    if (strcmp(ParamName, "X_COMCAST-COM_AssociatedDevicesHighWatermarkDate") == 0)
    {
		//TODO: need cacultion for the time
		*puLong = AnscGetTickInSeconds();
        //CosaDmlGetHighWatermarkDate(NULL,pWifiSsid->SSID.StaticInfo.Name,pValue);
        return TRUE;
    }
	
    if (strcmp(ParamName, "X_COMCAST-COM_TXOverflow") == 0)
    {
#if !defined(_XB7_PRODUCT_REQ_) && !defined(_PLATFORM_TURRIS_) && !defined(_HUB4_PRODUCT_REQ_)
        *puLong = pWifiAp->AP.Cfg.TXOverflow; 
        return TRUE;
#else
	*puLong = 0;
	return TRUE;
#endif
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        AccessPoint_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
AccessPoint_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj      = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp       = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_AP_MF_CFG        pWifiApMf    = &pWifiAp->MF;

    PSINGLE_LINK_ENTRY              pSLinkEntry  = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
//    PCOSA_DML_WIFI_SSID             pWifiSsid    = (PCOSA_DML_WIFI_SSID      )NULL;
    CHAR                            PathName[64] = {0};

    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
    INT wlanIndex;


    while ( pSLinkEntry )
    {
        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        //pWifiSsid    = pSSIDLinkObj->hContext;

        sprintf(PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);

        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }

        pSLinkEntry             = AnscQueueGetNextEntry(pSLinkEntry);
    }


    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Alias") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString(pWifiAp->AP.Cfg.Alias) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiAp->AP.Cfg.Alias);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiAp->AP.Cfg.Alias)+1;
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "SSIDReference") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString(pWifiAp->AP.Cfg.SSID) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiAp->AP.Cfg.SSID);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiAp->AP.Cfg.SSID)+1;
            return 1;
        }
        return 0;
    }

	//zqiu
    //if( AnscEqualString(ParamName, "X_COMCAST-COM_AssociatedDevicesHighWatermarkDate", TRUE))
    //{
    //    CosaDmlGetHighWatermarkDate(NULL,pWifiSsid->SSID.StaticInfo.Name,pValue);
    //    return 0;
    //}
//RDKB-18000 sometimes if we change the beaconrate value through wifi_api it is not getting reflected in dmcli.
	if (strcmp(ParamName, "X_RDKCENTRAL-COM_BeaconRate") == 0)
    {
		wlanIndex = pWifiAp->AP.Cfg.InstanceNumber -1 ;

//		if(isBeaconRateUpdate[wlanIndex] == TRUE) {
			CosaDmlWiFiGetApBeaconRate(wlanIndex, pWifiAp->AP.Cfg.BeaconRate );
			AnscCopyString(pValue, pWifiAp->AP.Cfg.BeaconRate);
//			isBeaconRateUpdate[wlanIndex] = FALSE;
			CcspTraceDebug(("WIFI   wlanIndex  %d  getBeaconRate %s  Function %s \n",wlanIndex,pWifiAp->AP.Cfg.BeaconRate,__FUNCTION__)); 
			return 0;
    }

	/*	else {
				if ( AnscSizeOfString(pWifiAp->AP.Cfg.BeaconRate) < *pUlSize)
				{
					AnscCopyString(pValue, pWifiAp->AP.Cfg.BeaconRate);
					return 0;
				}
				else
				{
					*pUlSize = AnscSizeOfString(pWifiAp->AP.Cfg.BeaconRate)+1;
					return 1;
				}
		}
		return 0;
    }*/

    if (strcmp(ParamName, "X_COMCAST-COM_MAC_FilteringMode") == 0)
    {


	if ( pWifiApMf->bEnabled == TRUE )
	{
		if ( pWifiApMf->FilterAsBlackList == TRUE )
		{	
			AnscCopyString(pWifiAp->AP.Cfg.MacFilterMode,"Deny");
		}
		else
		{
			AnscCopyString(pWifiAp->AP.Cfg.MacFilterMode,"Allow");
		}
	}
	else
	{
		AnscCopyString(pWifiAp->AP.Cfg.MacFilterMode,"Allow-ALL");
		
	}
        /* collect value */
       
	AnscCopyString(pValue, pWifiAp->AP.Cfg.MacFilterMode);
        return 0;

    }

    if (strcmp(ParamName, "X_LGI-COM_ActiveTimeout") == 0)
    {
        char strName[100];
        char *strValue = NULL;

        snprintf(strName, sizeof(strName), PATH_X_LGI_COM_ACTIVETIMEOUT, pLinkObj->InstanceNumber);

        if( CCSP_SUCCESS == PSM_Get_Record_Value2(bus_handle, g_Subsystem, strName, NULL, &strValue))
        {
            if (AnscSizeOfString(strValue) < *pUlSize)
            {
                AnscCopyString(pValue, strValue);
                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
                return 0;
            }
            else
            {
                *pUlSize = AnscSizeOfString(strValue)+1;
                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
                return 1;
            }
        }
    }

    if (strcmp(ParamName, "AllowedMACAddress") == 0)
    {
        ULONG nIndex = 0;
        ULONG insNum = 0;
        char allowedList[COSA_DML_WIFI_MAX_MAC_FILTER_NUM * COSA_DML_MAC_ADDR_LENGTH] = {0};
        PCOSA_CONTEXT_LINK_OBJECT pSubCosaContext = MacFiltTab_GetEntry(hInsContext, nIndex++, &insNum);
        while(pSubCosaContext)
        {
            PCOSA_DML_WIFI_AP_MAC_FILTER pMacFilt = (PCOSA_DML_WIFI_AP_MAC_FILTER)pSubCosaContext->hContext;
            strncat(allowedList, IsValidMacAddress(pMacFilt->MACAddress)?pMacFilt->MACAddress:"", (sizeof(allowedList) - strlen(allowedList) - 1));
            pSubCosaContext = MacFiltTab_GetEntry(hInsContext, nIndex++, &insNum);
            strncat(allowedList, (pSubCosaContext)?",":"", (sizeof(allowedList) - strlen(allowedList) - 1));
        }
        AnscCopyString(pValue, allowedList);
        return 0;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return -1;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        AccessPoint_SetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL                        bValue
            );

    description:

        This function is called to set BOOL parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL                        bValue
                The updated BOOL value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
AccessPoint_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue

    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    BOOLEAN                         bForceDisableFlag = FALSE;
    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "IsolationEnable") == 0)
    {
        if ( pWifiAp->AP.Cfg.IsolationEnable == bValue )
        {
            return  TRUE;
        }

#if defined(_LG_OFW_)
        if ( pWifiAp->AP.Cfg.InstanceNumber == 5 || pWifiAp->AP.Cfg.InstanceNumber == 6 )
        {
            CcspWifiTrace(("RDK_LOG_ERROR, Isolation is not allowed to Disable for community WiFi AccessPoints\n"));
            return FALSE;
        }
#endif
	
        /* save update to backup */
        pWifiAp->AP.Cfg.IsolationEnable = bValue;
        pWifiAp->bApChanged = TRUE;

        return TRUE;
    }

    if (strcmp(ParamName, "Enable") == 0)
    {
        if (ANSC_STATUS_SUCCESS != CosaDmlWiFiGetCurrForceDisableWiFiRadio(&bForceDisableFlag))
        {
            return FALSE;
        }
        if (bForceDisableFlag)
        {
            CcspWifiTrace(("RDK_LOG_ERROR, WIFI_ATTEMPT_TO_CHANGE_CONFIG_WHEN_FORCE_DISABLED\n" ));
            return FALSE;
        }
        /* Currently APs are always enabled */
        /* pWifiAp->AP.Cfg.bEnabled = bValue; */
        /* return TRUE; */
        return FALSE;
    }

    if (strcmp(ParamName, "SSIDAdvertisementEnabled") == 0)
    {
        if ( pWifiAp->AP.Cfg.SSIDAdvertisementEnabled == bValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiAp->AP.Cfg.SSIDAdvertisementEnabled = bValue;
        pWifiAp->bApChanged = TRUE;
        return TRUE;
    }

    if (strcmp(ParamName, "WMMEnable") == 0)
    {
        if ( pWifiAp->AP.Cfg.WMMEnable == bValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiAp->AP.Cfg.WMMEnable = bValue;
        /*  when disable WMM, make sure UAPSD is disabled as well */
        if(bValue == FALSE) {
             pWifiAp->AP.Cfg.UAPSDEnable = bValue;
        }
        pWifiAp->bApChanged = TRUE;
        return TRUE;
    }

    if (strcmp(ParamName, "UAPSDEnable") == 0)
    {
        if ( pWifiAp->AP.Cfg.UAPSDEnable == bValue )
        {
            return  TRUE;
        }
        if ( TRUE == bValue && FALSE == pWifiAp->AP.Cfg.WMMEnable )
        {
            return FALSE;
        }
        /* save update to backup */
        pWifiAp->AP.Cfg.UAPSDEnable = bValue;
        pWifiAp->bApChanged = TRUE;
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_BssCountStaAsCpe") == 0)
    {
        if ( pWifiAp->AP.Cfg.BssCountStaAsCpe == bValue )
        {
            return  TRUE;
        }
        /* save update to backup */
        pWifiAp->AP.Cfg.BssCountStaAsCpe = bValue;
        pWifiAp->bApChanged = TRUE;

        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_BssHotSpot") == 0)
    {
        if ( pWifiAp->AP.Cfg.BssHotSpot == bValue )
        {
            return  TRUE;
        }
        /* save update to backup */
        pWifiAp->AP.Cfg.BssHotSpot = bValue;
        pWifiAp->bApChanged = TRUE;
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_KickAssocDevices") == 0)
    {
        if ( pWifiAp->AP.Cfg.KickAssocDevices == bValue )
        {
            return  TRUE;
        }
        /* save update to backup */
        pWifiAp->AP.Cfg.KickAssocDevices = bValue;
        pWifiAp->bApChanged = TRUE;

        return TRUE;
    }

#if defined (FEATURE_SUPPORT_INTERWORKING)

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_InterworkingServiceEnable") == 0)
    {

	if(pWifiAp->AP.Cfg.InterworkingCapability == TRUE) {
	    if ( pWifiAp->AP.Cfg.InterworkingEnable == bValue )
	    {
		return  TRUE;
	    }
	    /* save update to backup */
	    pWifiAp->AP.Cfg.InterworkingEnable = bValue;
            if((!pWifiAp->AP.Cfg.InterworkingEnable) && (pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Status)){
                CosaDmlWiFi_SetHS2Status(&pWifiAp->AP.Cfg,false,true);
                pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Capability = false;
            }

        pWifiAp->bApChanged = TRUE;

	    return TRUE;
	} else {
	    CcspWifiTrace(("RDK_LOG_ERROR, (%s) Interworking is not supported in this VAP !!!\n", __func__));
	    return FALSE;
	}
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_InterworkingApplySettings") == 0)
    {
	char *strValue = NULL;
	int retPsmGet = CCSP_SUCCESS;

	retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.WiFi-Interworking.Enable", NULL, &strValue);
	if (retPsmGet != CCSP_SUCCESS) {

	    CcspTraceError(("PSM RFC Interworking read error !!!\n"));
	    return FALSE;
	}

	if((pWifiAp->AP.Cfg.InterworkingCapability == TRUE) && (_ansc_atoi(strValue) == TRUE)) {
	    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
	    char errorCode[128];
	    ULONG	len;
	    if (InterworkingElement_Validate(&pWifiAp->AP.Cfg, errorCode, &len) == FALSE) {
		CcspWifiTrace(("RDK_LOG_ERROR, (%s) Interworking Validate Error !!!\n", __func__));
		return FALSE;
	    }
	    if (InterworkingElement_Commit(hInsContext) == ANSC_STATUS_SUCCESS ) {
		return TRUE;
	    } else {
		CcspWifiTrace(("RDK_LOG_ERROR, (%s) Interworking Commit Error !!!\n", __func__));
		return FALSE;
	    }
	} else {
	    CcspWifiTrace(("RDK_LOG_ERROR, (%s) Interworking Capability is not Available !!!\n", __func__));
        if (strValue)
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
	    return FALSE;
	}
    }

#endif

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_BSSTransitionActivated") == 0)
    {
        if ( pWifiAp->AP.Cfg.BSSTransitionActivated == bValue )
        {
            return  TRUE;
        }
        pWifiAp->AP.Cfg.BSSTransitionActivated = bValue;

        /* save update to backup */
        pWifiAp->bApChanged = TRUE;
#if defined(FEATURE_HOSTAP_AUTHENTICATOR) && !defined (_XB7_PRODUCT_REQ_)
        BOOLEAN isNativeHostapdDisabled = FALSE;
        CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
        if (isNativeHostapdDisabled)
        {
            CosaDmlWifi_setBSSTransitionActivated(&pWifiAp->AP.Cfg, pWifiAp->AP.Cfg.InstanceNumber - 1);
            hapd_reload_bss_transition(pWifiAp->AP.Cfg.InstanceNumber - 1, bValue);
            CcspWifiTrace(("RDK_LOG_INFO, BSS_TRANSITION_CHANGE_PUSHED_SUCCESSFULLY\n"));
        }
#endif //FEATURE_HOSTAP_AUTHENTICATOR
        /* save update to backup */
        pWifiAp->bApChanged = TRUE;

        return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_rapidReconnectCountEnable") == 0)
    {
        if ( pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_rapidReconnectCountEnable == bValue )
        {
			pWifiAp->bApChanged = FALSE;
            return  TRUE;
        }

        /* save update to backup */
		if ( ANSC_STATUS_SUCCESS == CosaDmlWiFi_SetRapidReconnectCountEnable( pWifiAp->AP.Cfg.InstanceNumber - 1, bValue ) )
		{
			/* save update to backup */
			pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_rapidReconnectCountEnable = bValue;
	        pWifiAp->bApChanged = FALSE;

            return TRUE;
		}		
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_StatsEnable") == 0)
    {
        if (pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_StatsEnable == bValue)
        {
            return TRUE;
        }

        /* save update to backup */
        if (ANSC_STATUS_SUCCESS == CosaDmlWiFiApSetStatsEnable(pWifiAp->AP.Cfg.InstanceNumber, bValue))
        {
            pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_StatsEnable = bValue;
        }

	    pWifiAp->bApChanged = FALSE;

        return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_NeighborReportActivated") == 0)
    {
        BOOL rrmEnabled;


        wifi_getNeighborReportActivation(pWifiAp->AP.Cfg.InstanceNumber - 1, &rrmEnabled);

        if (pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_NeighborReportActivated == bValue &&
            pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_NeighborReportActivated == rrmEnabled)
        {
            pWifiAp->bApChanged = FALSE;
            return  TRUE;
        }

        /* save update to backup */
		if ( ANSC_STATUS_SUCCESS == CosaDmlWiFiApSetNeighborReportActivated( pWifiAp->AP.Cfg.InstanceNumber - 1, bValue ) )
		{
			/* save update to backup */
			pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_NeighborReportActivated = bValue;
			pWifiAp->bApChanged = TRUE;
			return TRUE;
		}		
    }

    if (strcmp(ParamName, "MACAddressControlEnabled") == 0)
    {
        if((pWifiAp->MF.FilterAsBlackList != bValue) && (pWifiAp->MF.bEnabled == bValue))
        {
            return  TRUE;
        }
        /* save update to backup */
        pWifiAp->MF.bEnabled = bValue;
        pWifiAp->MF.FilterAsBlackList = (bValue) ? FALSE : TRUE;
        pWifiAp->bApChanged = TRUE;
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        AccessPoint_SetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int                         iValue
            );

    description:

        This function is called to set integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int                         iValue
                The updated integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
AccessPoint_SetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int                         iValue
    )
{
    /* check the parameter name and set the corresponding value */
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "X_CISCO_COM_WmmNoAck") == 0)
    {
        if ( pWifiAp->AP.Cfg.WmmNoAck == iValue )
        {
            return  TRUE;
        }
        /* save update to backup */
        pWifiAp->AP.Cfg.WmmNoAck = iValue;
        pWifiAp->bApChanged = TRUE;

        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_MulticastRate") == 0)
    {
        if ( pWifiAp->AP.Cfg.MulticastRate == iValue )
        {
            return  TRUE;
        }
        /* save update to backup */
        pWifiAp->AP.Cfg.MulticastRate = iValue;

        pWifiAp->bApChanged = TRUE;

        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_BssMaxNumSta") == 0)
    {
        CcspTraceWarning(("Unsupported parameter '%s'\n Use MaxAllowedAssociations to configure \n", ParamName));
        return TRUE;
    }
        if (strcmp(ParamName, "X_RDKCENTRAL-COM_ManagementFramePowerControl") == 0)
    {
        if ( pWifiAp->AP.Cfg.ManagementFramePowerControl == iValue )
        {
            return  TRUE;
        }
        /* save update to backup */
        pWifiAp->AP.Cfg.ManagementFramePowerControl = iValue;
        pWifiAp->bApChanged = TRUE;
        return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_rapidReconnectMaxTime") == 0)
    {
        if ( pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_rapidReconnectMaxTime == iValue )
        {
            pWifiAp->bApChanged = FALSE;
       
            return TRUE;
        }

		if ( ANSC_STATUS_SUCCESS == CosaDmlWiFi_SetRapidReconnectThresholdValue( pWifiAp->AP.Cfg.InstanceNumber - 1, iValue ) )
		{
			/* save update to backup */
			pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_rapidReconnectMaxTime = iValue;
            pWifiAp->bApChanged = FALSE;

			return TRUE;
		}
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        AccessPoint_SetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG                       uValue
            );

    description:

        This function is called to set ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG                       uValue
                The updated ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
AccessPoint_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    
    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "RetryLimit") == 0)
    {
        if ( pWifiAp->AP.Cfg.RetryLimit == uValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiAp->AP.Cfg.RetryLimit = uValue;
        pWifiAp->bApChanged = TRUE;
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_LongRetryLimit") == 0)
    {
        if ( pWifiAp->AP.Cfg.LongRetryLimit == uValue )
        {
            return  TRUE;
        }
        
        /* save update to backup */
        pWifiAp->AP.Cfg.LongRetryLimit = uValue; 
        pWifiAp->bApChanged = TRUE;
        return TRUE;
    }

    if (strcmp(ParamName, "MaxAssociatedDevices") == 0)
    {
        CcspTraceWarning(("Unsupported parameter '%s'\n Use MaxAllowedAssociations to configure \n", ParamName));
        return TRUE;
    }

    if (strcmp(ParamName, "MaxAllowedAssociations") == 0)
    {
        if(pWifiAp->AP.Cfg.InstanceNumber == 5 || pWifiAp->AP.Cfg.InstanceNumber == 6)
        {
            if(uValue < 1 || uValue > 32)
            {
                CcspTraceError((" Invalid configuration of maximum associated devices for Community WiFi\n"));
                return FALSE;
            }
        }

        if ( pWifiAp->AP.Cfg.MaxAssociatedDevices == uValue )
        {
            return  TRUE;
        }

        /* save update to backup */
        pWifiAp->AP.Cfg.MaxAssociatedDevices = uValue;
        pWifiAp->bApChanged = TRUE;
        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_AssociatedDevicesHighWatermarkThreshold") == 0)
    {
        if ( pWifiAp->AP.Cfg.HighWatermarkThreshold == uValue )
        {
            return  TRUE;
        }

        if ( uValue <= pWifiAp->AP.Cfg.MaxAssociatedDevices )
	{
        /* save update to backup */
        pWifiAp->AP.Cfg.HighWatermarkThreshold = uValue;
        pWifiAp->bApChanged = TRUE;

        return TRUE;
	}

	else
		return FALSE;

    }
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        AccessPoint_SetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pString
            );

    description:

        This function is called to set string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pString
                The updated string value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
AccessPoint_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    errno_t                         rc           =  -1;
    int                             ind          =  -1;


    if((pString == NULL) || (ParamName == NULL))
    {
       CcspTraceInfo(("RDK_LOG_WARN, %s %s:%d\n",__FILE__, __FUNCTION__,__LINE__));
       return FALSE;
    }
    
    /* check the parameter name and set the corresponding value */
    rc = strcmp_s("Alias", strlen("Alias"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
        /* save update to backup */
        rc = STRCPY_S_NOCLOBBER(pWifiAp->AP.Cfg.Alias, sizeof(pWifiAp->AP.Cfg.Alias), pString);

        if(rc != EOK)
        {
            ERR_CHK(rc);
            return FALSE;
        }
        return TRUE;
    }

    rc = strcmp_s("SSIDReference", strlen("SSIDReference"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
        /* save update to backup */
    #ifdef _COSA_SIM_

        rc = STRCPY_S_NOCLOBBER(pWifiAp->AP.Cfg.SSID, sizeof(pWifiAp->AP.Cfg.SSID), pString);

        if(rc != EOK)
        {
            ERR_CHK(rc);
            return FALSE;
        }
        return TRUE;
    #elif defined (MULTILAN_FEATURE)

        rc = STRCPY_S_NOCLOBBER(pWifiAp->AP.Cfg.SSID, sizeof(pWifiAp->AP.Cfg.SSID), pString);

        if(rc != EOK)
        {
            ERR_CHK(rc);
            return FALSE;
        }

        pWifiAp->bApChanged = TRUE;

        return TRUE;
    #else
        /* Currently we dont allow to change this - May be when multi-SSID comes in */
        return FALSE;
    #endif
    }
	
    rc = strcmp_s("X_RDKCENTRAL-COM_BeaconRate", strlen("X_RDKCENTRAL-COM_BeaconRate"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
        /* save update to backup */
        rc = STRCPY_S_NOCLOBBER(pWifiAp->AP.Cfg.BeaconRate, sizeof(pWifiAp->AP.Cfg.BeaconRate), pString);
        if(rc != EOK)
        {
            ERR_CHK(rc);
            return FALSE;
        }
	    pWifiAp->bApChanged = TRUE;
        return TRUE;
    }
	
    if (strcmp(ParamName, "X_LGI-COM_ActiveTimeout") == 0)
    {
        char strName[100];

        AnscCopyString(pWifiAp->ActiveTimeout, pString);

        if( Validate_SSID_Timeout(pString) == false )
        {
            fprintf(stderr, "Invalid SSID Timeout value: %s\n", pString);
            return FALSE;
        }

        snprintf(strName, sizeof(strName), PATH_X_LGI_COM_ACTIVETIMEOUT, pLinkObj->InstanceNumber);

        if( CCSP_SUCCESS != PSM_Set_Record_Value2(bus_handle, g_Subsystem, strName, ccsp_string, pWifiAp->ActiveTimeout) )
        {
            fprintf(stderr, "PSM_Set_Record_Value2 failed to set %s\n", strName);
            return FALSE;
        }
        else
        {
            int    iGnIndex24 = 7, iGnIndex50 = 8;

            //remove the current entry from crontab, if any
#if defined(_LG_MV1_CELENO_)
            system("rpcclient2 'sed -i \'/Device.WiFi.SSID./d\' /var/spool/cron/crontabs/root'");
            system("rpcclient2 'sed -i \'/Device.WiFi.Radio./d\' /var/spool/cron/crontabs/root'");
#else
            system("sed -i '/Device.WiFi.SSID./d' /var/spool/cron/crontabs/root");
            system("sed -i '/Device.WiFi.Radio./d' /var/spool/cron/crontabs/root");
#endif

            if (pString[0] != '\0')
            {
                int iMin, iHour, iDay, iMonth, iYear;
                char strCronCmd[256];

                //parse the time date values
                sscanf(pString,"%d/%d/%d-%d:%d", &iDay, &iMonth, &iYear, &iHour, &iMin);

                //prepare the crontab entry
#if defined(_LG_MV1_CELENO_)
                snprintf (strCronCmd, sizeof(strCronCmd), "rpcclient2 'echo \"%d %d %d %d * dmcli eRT setvalue Device.WiFi.SSID.%d.Enable bool false; dmcli eRT setvalue Device.WiFi.Radio.1.X_CISCO_COM_ApplySetting bool true\" >> /var/spool/cron/crontabs/root'",
                    iMin, iHour, iDay, iMonth, iGnIndex24);
#else
                sprintf (strCronCmd, "echo '%d %d %d %d * dmcli eRT setvalue Device.WiFi.SSID.%d.Enable bool false; dmcli eRT setvalue Device.WiFi.Radio.1.X_CISCO_COM_ApplySetting bool true' >> /var/spool/cron/crontabs/root",
                    iMin, iHour, iDay, iMonth, iGnIndex24);
#endif

                //add the crontab entry
                system(strCronCmd);

                //prepare the next crontab entry
#if defined(_LG_MV1_CELENO_)
                snprintf (strCronCmd, sizeof(strCronCmd), "rpcclient2 'echo \"%d %d %d %d * dmcli eRT setvalue Device.WiFi.SSID.%d.Enable bool false; dmcli eRT setvalue Device.WiFi.Radio.2.X_CISCO_COM_ApplySetting bool true\" >> /var/spool/cron/crontabs/root'",
                    iMin, iHour, iDay, iMonth, iGnIndex50);
#else
                sprintf (strCronCmd, "echo '%d %d %d %d * dmcli eRT setvalue Device.WiFi.SSID.%d.Enable bool false; dmcli eRT setvalue Device.WiFi.Radio.2.X_CISCO_COM_ApplySetting bool true' >> /var/spool/cron/crontabs/root",
                    iMin, iHour, iDay, iMonth, iGnIndex50);
#endif

                //add the next crontab entry
                system(strCronCmd);
            }
        }

        pWifiAp->bApChanged = TRUE;
        return TRUE;
    }

    if (strcmp(ParamName, "AllowedMACAddress") == 0)
    {
        char *macStr;
        ULONG nIndex = 0;
        ULONG insNum = 0;
        ULONG entryCnt = AnscSListQueryDepth(&pWifiAp->AP.MacFilterList);

        while(entryCnt > 0)
        {
            PCOSA_CONTEXT_LINK_OBJECT pSubCxt = MacFiltTab_GetEntry(hInsContext, --entryCnt, &insNum);
            MacFiltTab_DelEntry(hInsContext, pSubCxt);
        }

        macStr = strtok(pString, ",");
        while(NULL != macStr)
        {
            char devName[10] = {0};
            PCOSA_CONTEXT_LINK_OBJECT pSubCosaContext = NULL;
            PCOSA_DML_WIFI_AP_MAC_FILTER pMacFilt = NULL;
            if(IsValidMacAddress(macStr) && (nIndex < 3))
            {
                pSubCosaContext = MacFiltTab_GetEntry(hInsContext, nIndex++, &insNum);
                if(!pSubCosaContext)
                {
                    pSubCosaContext = MacFiltTab_AddEntry(hInsContext, &insNum);
                }
                pMacFilt = (PCOSA_DML_WIFI_AP_MAC_FILTER)pSubCosaContext->hContext;
                AnscCopyString(pMacFilt->MACAddress, macStr);
                sprintf(devName, "Device%lu", nIndex);
                AnscCopyString(pMacFilt->DeviceName, devName);
            }
            macStr = strtok(NULL, ",");
        }
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        AccessPoint_Validate
            (
                ANSC_HANDLE                 hInsContext,
                char*                       pReturnParamName,
                ULONG*                      puLength
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       pReturnParamName,
                The buffer (128 bytes) of parameter name if there's a validation. 

                ULONG*                      puLength
                The output length of the param name. 

    return:     TRUE if there's no validation.

**********************************************************************/
BOOL
AccessPoint_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj      = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp       = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_AP               pWifiApE      = (PCOSA_DML_WIFI_AP        )NULL;
    PSINGLE_LINK_ENTRY              pSLinkEntry   = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj  = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pAPLinkObj    = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    CHAR                            PathName[64]  = {0};
  
    /*Alias should be non-empty*/
    if (AnscSizeOfString(pWifiAp->AP.Cfg.Alias) == 0)
    {
        AnscCopyString(pReturnParamName, "Alias");
        *puLength = AnscSizeOfString("Alias");

        return FALSE;
    }
 
    /* Retry Limit should be between 0 and 7 */
    if(pWifiAp->AP.Cfg.RetryLimit > 255)
    {
        AnscCopyString(pReturnParamName, "RetryLimit");
        *puLength = sizeof("RetryLimit");
        goto EXIT;
    }

    /* UAPSD can not be enabled when WMM is disabled */
#if 0
    if((FALSE == pWifiAp->AP.Cfg.WMMEnable) && (TRUE == pWifiAp->AP.Cfg.UAPSDEnable))
    {
       AnscCopyString(pReturnParamName, "UAPSDEnable");
       *puLength = sizeof("UAPSDEnable");
        goto EXIT;
    }
#endif
             /* ManagementFramePowerControl Parameter values higher than 0 shall be converted to value of 0 and Parameter values lower than -20 shall be converted to value of -20 */
    if(pWifiAp->AP.Cfg.ManagementFramePowerControl > 0)
    {
 	pWifiAp->AP.Cfg.ManagementFramePowerControl = 0;
    }
    else if (pWifiAp->AP.Cfg.ManagementFramePowerControl < -20)
    {
    	pWifiAp->AP.Cfg.ManagementFramePowerControl = -20;
    }                       
 
    /*SSIDRefence should be unique*/
    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->AccessPointQueue);
    while ( pSLinkEntry )
    {
        pAPLinkObj   = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        pWifiApE     = pAPLinkObj->hContext;
        pSLinkEntry  = AnscQueueGetNextEntry(pSLinkEntry);
        
        if (pWifiApE == pWifiAp)
        {
            continue;
        }
        pWifiAp->AP.Cfg.SSID[sizeof(pWifiAp->AP.Cfg.SSID) - 1] = '\0';
        pWifiApE->AP.Cfg.SSID[sizeof(pWifiApE->AP.Cfg.SSID) - 1] = '\0';
        if (AnscEqualString(pWifiAp->AP.Cfg.SSID, pWifiApE->AP.Cfg.SSID, TRUE))
        {
            memset(pWifiAp->AP.Cfg.SSID, 0, sizeof(pWifiAp->AP.Cfg.SSID));
    
            AnscCopyString(pReturnParamName, "SSIDReference");
            *puLength = sizeof("SSIDReference");
    
            goto EXIT;
        }
    }
    
    /*SSIDReference should be valid*/
    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
    
    while ( pSLinkEntry )
    {
        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        
        snprintf(PathName, sizeof(PathName), "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);
        
        /*see whether the corresponding SSID entry exists*/
        pWifiAp->AP.Cfg.SSID[sizeof(pWifiAp->AP.Cfg.SSID) - 1] = '\0';
        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            return TRUE;
        }
        
        pSLinkEntry             = AnscQueueGetNextEntry(pSLinkEntry);
    }
    
    memset(pWifiAp->AP.Cfg.SSID, 0, sizeof(pWifiAp->AP.Cfg.SSID));
    
    AnscCopyString(pReturnParamName, "SSIDReference");
    *puLength = sizeof("SSIDReference");
    
    goto EXIT;
    
EXIT:
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        AccessPoint_Commit
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
AccessPoint_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    
    UNREFERENCED_PARAMETER(hInsContext);
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj      = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp       = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PSINGLE_LINK_ENTRY              pSLinkEntry   = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj  = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid     = (PCOSA_DML_WIFI_SSID      )NULL;
    CHAR                            PathName[64]  = {0};
    ANSC_STATUS                     returnStatus  = ANSC_STATUS_SUCCESS;

    MacFilter_Commit(hInsContext);
    if((pWifiAp->MF.bEnabled == TRUE) && (pWifiAp->MF.FilterAsBlackList == FALSE))
    {
        ULONG nIndex = 0;
        ULONG insNum = 0;
        PCOSA_CONTEXT_LINK_OBJECT pSubCosaContext = MacFiltTab_GetEntry(hInsContext, nIndex++, &insNum);
        while(pSubCosaContext)
        {
            MacFiltTab_Commit(pSubCosaContext);
            pSubCosaContext = MacFiltTab_GetEntry(hInsContext, nIndex++, &insNum);
        }
    }
    
    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
   
    while ( pSLinkEntry )
    {
        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        
        sprintf(PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);
        
        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }    
        
        pSLinkEntry             = AnscQueueGetNextEntry(pSLinkEntry);
    }
    
    if ( pSLinkEntry )
    {
        pWifiSsid = pSSIDLinkObj->hContext;

#ifndef MULTILAN_FEATURE
        if ( !pWifiAp->bApChanged )
        {
            return  ANSC_STATUS_SUCCESS;
        }
        else
        {
            pWifiAp->bApChanged = FALSE;
            CcspTraceInfo(("WiFi AP -- apply the changes...\n"));
        }  
        /*Set to backend*/
#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_IPQ_) && !defined(_PLATFORM_TURRIS_)
        returnStatus = CosaDmlWiFiApSetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->AP.Cfg);
#else
        returnStatus = CosaDmlWiFiApSetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->AP.Cfg);
#endif

        if (returnStatus != ANSC_STATUS_SUCCESS)
        {
            return ANSC_STATUS_FAILURE;
        }
    
        /*This is not an orphan entry*/
#endif
        if (pLinkObj->bNew == TRUE)
        {
            pLinkObj->bNew = FALSE;
#if defined (MULTILAN_FEATURE)
#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_IPQ_) && !defined(_PLATFORM_TURRIS_)
            returnStatus = CosaDmlWiFiApAddEntry((ANSC_HANDLE)pMyObject, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->AP);
#else
            returnStatus = CosaDmlWiFiApAddEntry((ANSC_HANDLE)pMyObject, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->AP);
#endif

            if (returnStatus != ANSC_STATUS_SUCCESS)
            {
                return ANSC_STATUS_FAILURE;
            }

            CosaWifiRegDelAPInfo((ANSC_HANDLE)pMyObject, (ANSC_HANDLE)pLinkObj);

            return ANSC_STATUS_SUCCESS;
        }
        else
        {
            if ( !pWifiAp->bApChanged )
            {
                return  ANSC_STATUS_SUCCESS;
            }
            else
            {
                pWifiAp->bApChanged = FALSE;
                CcspTraceInfo(("WiFi AP -- apply the changes...\n"));
            }  
            /*Set to backend*/
#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_IPQ_) && !defined(_PLATFORM_TURRIS_)
            returnStatus = CosaDmlWiFiApSetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->AP.Cfg);
#else
            returnStatus = CosaDmlWiFiApSetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->AP.Cfg);
#endif

            if (returnStatus != ANSC_STATUS_SUCCESS)
            {
                return ANSC_STATUS_FAILURE;
            }

            return ANSC_STATUS_SUCCESS;
        }
#else
            CosaWifiRegDelAPInfo((ANSC_HANDLE)pMyObject, (ANSC_HANDLE)pLinkObj);
        }
        
        return ANSC_STATUS_SUCCESS;
#endif
    }
    else
    {
        /*This is an orphan entry*/
        if (pLinkObj->bNew == FALSE)
        {
            pLinkObj->bNew = TRUE;
        
            CosaWifiRegAddAPInfo((ANSC_HANDLE)pMyObject, (ANSC_HANDLE)pLinkObj);
        }
    
        return ANSC_STATUS_FAILURE;
    }
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 
        ULONG
        AccessPoint_Rollback
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to roll back the update whenever there's a 
        validation found.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
AccessPoint_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject       = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObjAp      = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp         = (PCOSA_DML_WIFI_AP        )pLinkObjAp->hContext;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObjSsid    = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PSINGLE_LINK_ENTRY              pSLinkEntrySsid = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid       = (PCOSA_DML_WIFI_SSID      )NULL;
    CHAR                            PathName[64]    = {0};
    
    /*Get the corresponding SSID entry*/
    pSLinkEntrySsid = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
    while ( pSLinkEntrySsid )
    {
        pLinkObjSsid = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntrySsid);
        
        sprintf(PathName, "Device.WiFi.SSID.%lu.", pLinkObjSsid->InstanceNumber);
        
        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }
                   
        pSLinkEntrySsid             = AnscQueueGetNextEntry(pSLinkEntrySsid);
    }
    
    if ( pSLinkEntrySsid )
    {
        pWifiSsid = pLinkObjSsid->hContext;
        
#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        CosaDmlWiFiApGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->AP);
#else
        CosaDmlWiFiApGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->AP);
#endif
        
        return ANSC_STATUS_SUCCESS;
    }
    return ANSC_STATUS_SUCCESS;
}

static BOOL Validate_SSID_Timeout (char *pString)
{
     BOOL ret = TRUE;
     ULONG MAX_VALID_YR = 9999;
     ULONG MIN_VALID_YR = 1800;
     ULONG iMin=0, iHour=0, iDay=0, iMonth=0, iYear=0;
     if (pString[0] != '\0')
     {
            if (sscanf(pString,"%lu/%lu/%lu-%lu:%lu", &iDay, &iMonth, &iYear, &iHour, &iMin) == 5)
            {
                if ((iYear > MAX_VALID_YR || iYear < MIN_VALID_YR)||
                    (iMonth < 1 || iMonth > 12)||
                    (iDay < 1 || iDay > 31)||
                    (iHour > 24)||
                    (iMin > 60))
                      ret = FALSE;
            }
            else
            {
                fprintf(stderr, "Validate_SSID_Timeout is incorrect:%s\n", pString);
                ret = FALSE;
            }
     }
     return ret;
}

/***********************************************************************

 APIs for Object:

    WiFi.AccessPoint.{i}.Security.

    *  Security_GetParamBoolValue
    *  Security_GetParamIntValue
    *  Security_GetParamUlongValue
    *  Security_GetParamStringValue
    *  Security_SetParamBoolValue
    *  Security_SetParamIntValue
    *  Security_SetParamUlongValue
    *  Security_SetParamStringValue
    *  Security_Validate
    *  Security_Commit
    *  Security_Rollback

***********************************************************************/
/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Security_GetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:

        This function is called to retrieve Boolean parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL*                       pBool
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Security_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec   = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_TransitionDisable") == 0)
    {
        *pBool = FALSE;
        return TRUE;
    }
    if (strcmp(ParamName, "Reset") == 0)
    {
		/* 
		* The value of this parameter is not part of the device configuration and is 
		* always false when read. 
		*/
		*pBool = pWifiApSec->Cfg.bReset;

        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Security_GetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int*                        pInt
            );

    description:

        This function is called to retrieve integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int*                        pInt
                The buffer of returned integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Security_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec   = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;
    /* check the parameter name and return the corresponding value */

    if (strcmp(ParamName, "X_CISCO_COM_RadiusReAuthInterval") == 0)
    {
        /* collect value */
        *pInt = pWifiApSec->Cfg.RadiusReAuthInterval;
        return TRUE;
    }
    if (strcmp(ParamName, "X_CISCO_COM_DefaultKey") == 0)
    {
        /* collect value */
        *pInt = pWifiApSec->Cfg.DefaultKey;
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Security_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Security_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec   = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;
    /* check the parameter name and return the corresponding value */

    if (strcmp(ParamName, "RekeyingInterval") == 0)
    {
        /* collect value */
        *puLong = pWifiApSec->Cfg.RekeyingInterval;
        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_EncryptionMethod") == 0)
    {
        /* collect value */
        *puLong = pWifiApSec->Cfg.EncryptionMethod;
        return TRUE;
    }

    if (strcmp(ParamName, "RadiusServerPort") == 0)
    {
        /* collect value */
        *puLong = pWifiApSec->Cfg.RadiusServerPort;
        return TRUE;
    }
	
	if (strcmp(ParamName, "SecondaryRadiusServerPort") == 0)
    {
        /* collect value */
        *puLong = pWifiApSec->Cfg.SecondaryRadiusServerPort;
        return TRUE;
    }

    if (strcmp(ParamName, "RadiusDASPort") == 0)
    {
        /* collect value */
        *puLong = pWifiApSec->Cfg.RadiusDASPort;
        return TRUE;
    }
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        Security_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
Security_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec   = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;


   
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "ModesSupported") == 0)
    {
        /* collect value */
        char buf[512] = {0};
#ifndef _XB6_PRODUCT_REQ_
        if (pWifiApSec->Info.ModesSupported & COSA_DML_WIFI_SECURITY_None )
        {
            strcat(buf, "None");
        }

        if ( pWifiApSec->Info.ModesSupported & COSA_DML_WIFI_SECURITY_WEP_64 )
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",WEP-64");
            }
            else
            {
                strcat(buf, "WEP-64");
            }
        }

        if (pWifiApSec->Info.ModesSupported & COSA_DML_WIFI_SECURITY_WEP_128 )
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",WEP-128");
            }
            else
            {
                strcat(buf, "WEP-128");
            }
        }

        if ( pWifiApSec->Info.ModesSupported & COSA_DML_WIFI_SECURITY_WPA_Personal)
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",WPA-Personal");
            }
            else
            {
                strcat(buf, "WPA-Personal");
            }
        }

        if ( pWifiApSec->Info.ModesSupported & COSA_DML_WIFI_SECURITY_WPA2_Personal)
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",WPA2-Personal");
            }
            else
            {
                strcat(buf, "WPA2-Personal");
            }
        }

        if ( pWifiApSec->Info.ModesSupported & COSA_DML_WIFI_SECURITY_WPA_WPA2_Personal)
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",WPA-WPA2-Personal");
            }
            else
            {
                strcat(buf, "WPA-WPA2-Personal");
            }
        }

        if ( pWifiApSec->Info.ModesSupported & COSA_DML_WIFI_SECURITY_WPA_Enterprise)
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",WPA-Enterprise");
            }
            else
            {
                strcat(buf, "WPA-Enterprise");
            }
        }

        if ( pWifiApSec->Info.ModesSupported & COSA_DML_WIFI_SECURITY_WPA2_Enterprise)
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",WPA2-Enterprise");
            }
            else
            {
                strcat(buf, "WPA2-Enterprise");
            }
        }
		
        if ( pWifiApSec->Info.ModesSupported & COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise)
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",WPA-WPA2-Enterprise");
            }
            else
            {
                strcat(buf, "WPA-WPA2-Enterprise");
            }
        }
#else
		if (pWifiApSec->Info.ModesSupported & COSA_DML_WIFI_SECURITY_None )
        {
            strcat(buf, "None");
        }
		
		if ( pWifiApSec->Info.ModesSupported & COSA_DML_WIFI_SECURITY_WPA2_Personal)
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",WPA2-Personal");
            }
            else
            {
                strcat(buf, "WPA2-Personal");
            }
        }
		
		if ( pWifiApSec->Info.ModesSupported & COSA_DML_WIFI_SECURITY_WPA2_Enterprise)
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",WPA2-Enterprise");
            }
            else
            {
                strcat(buf, "WPA2-Enterprise");
            }
        }

        if ( pWifiApSec->Info.ModesSupported & COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise)
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",WPA-WPA2-Enterprise");
            }
            else
            {
                strcat(buf, "WPA-WPA2-Enterprise");
            }
        }

		if ( pWifiApSec->Info.ModesSupported & COSA_DML_WIFI_SECURITY_WPA3_Personal)
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",WPA3-Personal");
            }
            else
            {
                strcat(buf, "WPA3-Personal");
            }
        }

		if ( pWifiApSec->Info.ModesSupported & COSA_DML_WIFI_SECURITY_WPA3_Personal_Transition)
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",WPA3-Personal-Transition");
            }
            else
            {
                strcat(buf, "WPA3-Personal-Transition");
            }
        }

		if ( pWifiApSec->Info.ModesSupported & COSA_DML_WIFI_SECURITY_WPA3_Enterprise)
        {
            if (AnscSizeOfString(buf) != 0)
            {
                strcat(buf, ",WPA3-Enterprise");
            }
            else
            {
                strcat(buf, "WPA3-Enterprise");
            }
        }
#endif
        if ( AnscSizeOfString(buf) < *pUlSize)
        {
            AnscCopyString(pValue, buf);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(buf)+1;

            return 1;
        }
        return 0;
    }
 
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "ModeEnabled") == 0)
    {
        /* collect value */

        if ( 20 < *pUlSize)
        {
#ifndef _XB6_PRODUCT_REQ_
            if (pWifiApSec->Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_None )
            {
                AnscCopyString(pValue, "None");
            }
            else if (pWifiApSec->Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_WEP_64 )
            {
                AnscCopyString(pValue, "WEP-64");
            }
            else if (pWifiApSec->Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_WEP_128 )
            {
                AnscCopyString(pValue, "WEP-128");
            }
            else if (pWifiApSec->Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_WPA_Personal )
            {
                AnscCopyString(pValue, "WPA-Personal");
            }
            else if (pWifiApSec->Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_WPA2_Personal )
            {
                AnscCopyString(pValue, "WPA2-Personal");
            }
            else if (pWifiApSec->Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_WPA_WPA2_Personal )
            {
                AnscCopyString(pValue, "WPA-WPA2-Personal");
            }
            else if (pWifiApSec->Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_WPA_Enterprise )
            {
                AnscCopyString(pValue, "WPA-Enterprise");
            }
            else if (pWifiApSec->Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_WPA2_Enterprise )
            {
                AnscCopyString(pValue, "WPA2-Enterprise");
            }
            else if (pWifiApSec->Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise )
            {
                AnscCopyString(pValue, "WPA-WPA2-Enterprise");
            }
#else
			if (pWifiApSec->Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_None )
            {
                AnscCopyString(pValue, "None");
            }
			else if (pWifiApSec->Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_WPA2_Personal )
            {
                AnscCopyString(pValue, "WPA2-Personal");
            }
			else if (pWifiApSec->Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_WPA2_Enterprise )
            {
                AnscCopyString(pValue, "WPA2-Enterprise");
            }
            else if (pWifiApSec->Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise )
            {
                AnscCopyString(pValue, "WPA-WPA2-Enterprise");
            }
            else if (pWifiApSec->Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_WPA3_Personal )
            {
                AnscCopyString(pValue, "WPA3-Personal");
            }
            else if (pWifiApSec->Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_WPA3_Personal_Transition )
            {
                AnscCopyString(pValue, "WPA3-Personal-Transition");
            }
            else if (pWifiApSec->Cfg.ModeEnabled & COSA_DML_WIFI_SECURITY_WPA3_Enterprise )
            {
                AnscCopyString(pValue, "WPA3-Enterprise");
            }
#endif
            return 0;
        }
        else
        {
            *pUlSize = 20;
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "WEPKey") == 0)
    {
#ifdef _COSA_SIM_
        if (pWifiApSec->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_64 )
        {
            _ansc_sprintf
            (
                pValue,
                "%02X%02X%02X%02X%02X",
                pWifiApSec->Cfg.WEPKeyp[0],
                pWifiApSec->Cfg.WEPKeyp[1],
                pWifiApSec->Cfg.WEPKeyp[2],
                pWifiApSec->Cfg.WEPKeyp[3],
                pWifiApSec->Cfg.WEPKeyp[4]
            );
        }
        else if ( pWifiApSec->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_128 )
        {
            _ansc_sprintf
            (
                pValue,
                "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
                pWifiApSec->Cfg.WEPKeyp[0],
                pWifiApSec->Cfg.WEPKeyp[1],
                pWifiApSec->Cfg.WEPKeyp[2],
                pWifiApSec->Cfg.WEPKeyp[3],
                pWifiApSec->Cfg.WEPKeyp[4],
                pWifiApSec->Cfg.WEPKeyp[5],
                pWifiApSec->Cfg.WEPKeyp[6],
                pWifiApSec->Cfg.WEPKeyp[7],
                pWifiApSec->Cfg.WEPKeyp[8],
                pWifiApSec->Cfg.WEPKeyp[9],
                pWifiApSec->Cfg.WEPKeyp[10],
                pWifiApSec->Cfg.WEPKeyp[11],
                pWifiApSec->Cfg.WEPKeyp[12]
            );
        }
        else
        {
            return -1;
        }
#else
        /* WEP Key should always return empty string when read */
        AnscCopyString(pValue, "");
#endif        
        /* collect value */
        return 0;
    }

    if (strcmp(ParamName, "PreSharedKey") == 0)
    {
#ifdef _COSA_SIM_
        /* collect value */
        if ( AnscSizeOfString(pWifiApSec->Cfg.PreSharedKey) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiApSec->Cfg.PreSharedKey);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiApSec->Cfg.PreSharedKey)+1;
            return 1;
        }
#else
        /* PresharedKey should always return empty string when read */
        AnscCopyString(pValue, "");
#endif
        return 0;
    }
#if 0
    if (strcmp(ParamName, "KeyPassphrase") == 0)
    {
#ifdef _COSA_SIM_
        /* collect value */
        if ( AnscSizeOfString(pWifiApSec->Cfg.KeyPassphrase) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiApSec->Cfg.KeyPassphrase);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiApSec->Cfg.KeyPassphrase)+1;
            return 1;
        }
#else
        /* Key Passphrase should always return empty string when read */
        AnscCopyString(pValue, "");
#endif
        return 0;
    }

#endif

    if (strcmp(ParamName, "X_COMCAST-COM_DefaultKeyPassphrase") == 0)
    {
            AnscCopyString(pValue, (char*)pWifiApSec->Cfg.DefaultKeyPassphrase);
            return 0;
    }

    if (strcmp(ParamName, "X_CISCO_COM_WEPKey") == 0)
    {
        AnscCopyString(pValue, "");
        return 0;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_WEPKey") == 0)
    {
        if (pWifiApSec->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_64 )
        {
#ifdef CISCO_XB3_PLATFORM_CHANGES
            AnscCopyString( pValue, pWifiApSec->WEPKey64Bit[pWifiApSec->Cfg.DefaultKey-1].WEPKey );;
#else
            _ansc_sprintf
            (
                pValue,
                "%02X%02X%02X%02X%02X",
                pWifiApSec->Cfg.WEPKeyp[0],
                pWifiApSec->Cfg.WEPKeyp[1],
                pWifiApSec->Cfg.WEPKeyp[2],
                pWifiApSec->Cfg.WEPKeyp[3],
                pWifiApSec->Cfg.WEPKeyp[4]
            );
#endif
        }
        else if ( pWifiApSec->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_128 )
        {
#ifdef CISCO_XB3_PLATFORM_CHANGES
            AnscCopyString( pValue, pWifiApSec->WEPKey128Bit[pWifiApSec->Cfg.DefaultKey-1].WEPKey );
#else
            _ansc_sprintf
            (
                pValue,
                "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
                pWifiApSec->Cfg.WEPKeyp[0],
                pWifiApSec->Cfg.WEPKeyp[1],
                pWifiApSec->Cfg.WEPKeyp[2],
                pWifiApSec->Cfg.WEPKeyp[3],
                pWifiApSec->Cfg.WEPKeyp[4],
                pWifiApSec->Cfg.WEPKeyp[5],
                pWifiApSec->Cfg.WEPKeyp[6],
                pWifiApSec->Cfg.WEPKeyp[7],
                pWifiApSec->Cfg.WEPKeyp[8],
                pWifiApSec->Cfg.WEPKeyp[9],
                pWifiApSec->Cfg.WEPKeyp[10],
                pWifiApSec->Cfg.WEPKeyp[11],
                pWifiApSec->Cfg.WEPKeyp[12]
            );
#endif
        }
        else
        {
            return -1;
        }
        
        /* collect value */
        return 0;
    }

    if (strcmp(ParamName, "KeyPassphrase") == 0)
    {
        AnscCopyString(pValue, "");
        return 0;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_KeyPassphrase") == 0)
    {
	//XH 5G
	if (pWifiAp->AP.Cfg.InstanceNumber == 4 ) {
		CosaDmlWiFiApSecLoadKeyPassphrase(pWifiAp->AP.Cfg.InstanceNumber, &pWifiApSec->Cfg);
	}

        /* collect value */
        if ( AnscSizeOfString((char*)pWifiApSec->Cfg.KeyPassphrase) > 0 ) 
        {
            if  ( AnscSizeOfString((char*)pWifiApSec->Cfg.KeyPassphrase) < *pUlSize) 
	    {
		AnscCopyString(pValue, (char*)pWifiApSec->Cfg.KeyPassphrase);
		return 0;
	    }
	    else
	    {
		*pUlSize = AnscSizeOfString((char*)pWifiApSec->Cfg.KeyPassphrase)+1;
		return 1;
	    }
        } else if ( AnscSizeOfString((char*)pWifiApSec->Cfg.PreSharedKey) > 0 )   
        {
            if  ( AnscSizeOfString((char*)pWifiApSec->Cfg.PreSharedKey) < *pUlSize) 
            {   
                AnscCopyString(pValue, (char*)pWifiApSec->Cfg.PreSharedKey);
                return 0;
            }   
            else
            {   
                *pUlSize = AnscSizeOfString((char*)pWifiApSec->Cfg.PreSharedKey)+1;
                return 1;
            }   
        } else  {
            // if both PreSharedKey and KeyPassphrase are NULL, set to NULL string
	    AnscCopyString(pValue, "");
	    return 0;
        }
    }
    if (strcmp(ParamName, "SAEPassphrase") == 0)
    {
	    AnscCopyString(pValue, "");
	    return 0;
    }
    if (strcmp(ParamName, "RadiusSecret") == 0)
    {
        /* Radius Secret should always return empty string when read */
        AnscCopyString(pValue, "");
        return 0;
    }
	if (strcmp(ParamName, "SecondaryRadiusSecret") == 0)
    {
        /* Radius Secret should always return empty string when read */
        AnscCopyString(pValue, "");
        return 0;
    }

    if (strcmp(ParamName, "RadiusServerIPAddr") == 0)
    {
        /* Radius Secret should always return empty string when read */
	int result;
	result=strcmp((char*)pWifiApSec->Cfg.RadiusServerIPAddr,"");
	if(result)
		AnscCopyString(pValue, (char*)pWifiApSec->Cfg.RadiusServerIPAddr);
	else
		AnscCopyString(pValue,"0.0.0.0");
        return 0;
    }
	if (strcmp(ParamName, "SecondaryRadiusServerIPAddr") == 0)
    {
        /* Radius Secret should always return empty string when read */
	int result;
	result=strcmp((char*)pWifiApSec->Cfg.SecondaryRadiusServerIPAddr,"");
	if(result)
	        AnscCopyString(pValue, (char*)pWifiApSec->Cfg.SecondaryRadiusServerIPAddr);
	else
		AnscCopyString(pValue,"0.0.0.0");
        return 0;
    }
    if (strcmp(ParamName, "MFPConfig") == 0)
    {
        AnscCopyString(pValue, pWifiApSec->Cfg.MFPConfig);
        return 0;
    }
    
    if (strcmp(ParamName, "RadiusDASIPAddr") == 0)
    {
        /* Radius Secret should always return empty string when read */
        int result;
        result=strcmp((char *)pWifiApSec->Cfg.RadiusDASIPAddr,"");
        if(result)
                AnscCopyString(pValue, (char *)pWifiApSec->Cfg.RadiusDASIPAddr);
        else
                AnscCopyString(pValue,"0.0.0.0");
        return 0;
    }
    if (strcmp(ParamName, "RadiusDASSecret") == 0)
    {
        /* Radius Secret should always return empty string when read */
        AnscCopyString(pValue, "");
        return 0;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return -1;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Security_SetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL                        bValue
            );

    description:

        This function is called to set BOOL parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL                        bValue
                The updated BOOL value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Security_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec   = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;
	
    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "X_RDKCENTRAL-COM_TransitionDisable") == 0)
    {
        return TRUE;
    }

    if (strcmp(ParamName, "Reset") == 0)
    {
		/* To set changes made flag */
		pWifiApSec->Cfg.bReset	 = bValue;

		if ( TRUE == pWifiApSec->Cfg.bReset )
		{
			pWifiAp->bSecChanged	 = TRUE;

		}
		return TRUE;		
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Security_SetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int                         iValue
            );

    description:

        This function is called to set integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int                         iValue
                The updated integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Security_SetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int                         iValue
    )
{
    /* check the parameter name and set the corresponding value */
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec   = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;
    
    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "X_CISCO_COM_RadiusReAuthInterval") == 0)
    {
        if ( pWifiApSec->Cfg.RadiusReAuthInterval != iValue )
        {
            /* save update to backup */
            pWifiApSec->Cfg.RadiusReAuthInterval = iValue;
            pWifiAp->bSecChanged = TRUE;
        }

        return TRUE;
    }

    if (strcmp(ParamName, "X_CISCO_COM_DefaultKey") == 0)
    {
        if ( pWifiApSec->Cfg.DefaultKey != iValue )
        {
            /* save update to backup */
            pWifiApSec->Cfg.DefaultKey = iValue;
            pWifiAp->bSecChanged             = TRUE;
        }

        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Security_SetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG                       uValue
            );

    description:

        This function is called to set ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG                       uValue
                The updated ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Security_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec   = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;


    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "RekeyingInterval") == 0)
    {

        if ( pWifiApSec->Cfg.RekeyingInterval != uValue )
        {
            /* save update to backup */
            pWifiApSec->Cfg.RekeyingInterval = uValue;
            pWifiAp->bSecChanged             = TRUE;
        }

        return TRUE;
    }
  
    if (strcmp(ParamName, "X_CISCO_COM_EncryptionMethod") == 0)
    {
        if ( pWifiApSec->Cfg.EncryptionMethod != uValue )
        {
            /* collect value */
            pWifiApSec->Cfg.EncryptionMethod = uValue;
            pWifiAp->bSecChanged             = TRUE;
            /* RDKB-30035 Run time config change */
#if defined(FEATURE_HOSTAP_AUTHENTICATOR) && !defined (_XB7_PRODUCT_REQ_)
            BOOLEAN isNativeHostapdDisabled = FALSE;
            CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
            if (isNativeHostapdDisabled &&
                !(hapd_reload_encryption_method(pWifiAp->AP.Cfg.InstanceNumber - 1, pWifiApSec->Cfg.EncryptionMethod)))
            {
                CcspWifiTrace(("RDK_LOG_INFO, WIFI_ENCRYPTION_METHOD_CHANGE_PUSHED_SUCCEESSFULLY\n"));
            }
#endif //FEATURE_HOSTAP_AUTHENTICATOR
        }
        return TRUE;
    }

    if (strcmp(ParamName, "RadiusServerPort") == 0)
    {
        if ( pWifiApSec->Cfg.RadiusServerPort != uValue )
        {
            /* save update to backup */
            pWifiApSec->Cfg.RadiusServerPort = uValue;
            pWifiAp->bSecChanged             = TRUE;
#if defined(FEATURE_HOSTAP_AUTHENTICATOR) && !defined (_XB7_PRODUCT_REQ_)
            BOOLEAN isNativeHostapdDisabled = FALSE;
            CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
            if (isNativeHostapdDisabled &&
                !(hapd_reload_radius_param(pWifiAp->AP.Cfg.InstanceNumber - 1, NULL, NULL, pWifiApSec->Cfg.RadiusServerPort, 0, TRUE, COSA_WIFI_HAPD_RADIUS_SERVER_PORT)))
            {
                 CcspWifiTrace(("RDK_LOG_INFO, RADIUS_PARAM_CHANGE_PUSHED_SUCCEESSFULLY\n"));
            }
#endif //FEATURE_HOSTAP_AUTHENTICATOR
        }
        return TRUE;
    }

    if (strcmp(ParamName, "SecondaryRadiusServerPort") == 0)
    {
        if ( pWifiApSec->Cfg.SecondaryRadiusServerPort != uValue )
        {
            /* save update to backup */
            pWifiApSec->Cfg.SecondaryRadiusServerPort = uValue;
            pWifiAp->bSecChanged             = TRUE;
#if defined(FEATURE_HOSTAP_AUTHENTICATOR) && !defined (_XB7_PRODUCT_REQ_)
            BOOLEAN isNativeHostapdDisabled = FALSE;
            CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
            if (isNativeHostapdDisabled &&
                !(hapd_reload_radius_param(pWifiAp->AP.Cfg.InstanceNumber - 1, NULL, NULL, pWifiApSec->Cfg.SecondaryRadiusServerPort, 0, FALSE, COSA_WIFI_HAPD_RADIUS_SERVER_PORT)))
            {
                 CcspWifiTrace(("RDK_LOG_INFO, RADIUS_PARAM_CHANGE_PUSHED_SUCCEESSFULLY\n"));
            }
#endif //FEATURE_HOSTAP_AUTHENTICATOR
        }

        return TRUE;
    }

    if (strcmp(ParamName, "RadiusDASPort") == 0)
    {
        if ( pWifiApSec->Cfg.RadiusDASPort != uValue )
        {
            /* save update to backup */
            pWifiApSec->Cfg.RadiusDASPort    = uValue;
            pWifiAp->bSecChanged             = TRUE;
        }
        return TRUE;
    }


    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Security_SetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pString
            );

    description:

        This function is called to set string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pString
                The updated string value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Security_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec   = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;
    UCHAR                           tmpWEPKey[14]= {'\0'};
    BOOLEAN                         bForceDisableFlag = FALSE;
    errno_t                         rc           = -1;
    int                             ind          = -1;

    if (!ParamName || !pString)
        return FALSE;

    /* check the parameter name and set the corresponding value */
    rc = strcmp_s("ModeEnabled", strlen("ModeEnabled"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
        COSA_DML_WIFI_SECURITY      TmpMode;
        /* save update to backup */

         TmpMode = wifi_sec_type_from_name(pString);
         if (TmpMode == COSA_DML_WIFI_SECURITY_INVALID)
         {
              printf("unrecognized type name");
              return FALSE;
         }
#if defined(_XB6_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_) && !defined(_XB8_PRODUCT_REQ_)
         if((TmpMode != COSA_DML_WIFI_SECURITY_None)
            && (TmpMode != COSA_DML_WIFI_SECURITY_WPA2_Personal)
            && (TmpMode != COSA_DML_WIFI_SECURITY_WPA2_Enterprise)
            && (TmpMode != COSA_DML_WIFI_SECURITY_WPA3_Personal)
            && (TmpMode != COSA_DML_WIFI_SECURITY_WPA3_Personal_Transition)
            && (TmpMode != COSA_DML_WIFI_SECURITY_WPA3_Enterprise)
         )
         {
              printf("type not allowed for this device\n");
              return FALSE;
         }

#endif //_XB6_PRODUCT_REQ_

        /* Allow only WPA2-Enterprise or WPA3-Enterprise for hotspot/community wifi */
        if (pWifiAp->AP.Cfg.InstanceNumber == 5 || pWifiAp->AP.Cfg.InstanceNumber == 6)
        {
#if defined (_LG_MV1_CELENO_)
            if (!(COSA_DML_WIFI_SECURITY_WPA2_Enterprise & TmpMode))
            {
                CcspWifiTrace(("RDK_LOG_ERROR, Security mode should be WPA2_Enterprise for hotspot, current input is %d\n", pWifiApSec->Cfg.ModeEnabled));
                return FALSE;
            }
#else
            if (!(COSA_DML_WIFI_SECURITY_WPA2_Enterprise & TmpMode) && !(COSA_DML_WIFI_SECURITY_WPA3_Enterprise & TmpMode))
            {
                CcspWifiTrace(("RDK_LOG_ERROR, Security mode should be WPA2_Enterprise or WPA3_Enterprise for hotspot, current input is %d\n", pWifiApSec->Cfg.ModeEnabled));
                return FALSE;
            }
#endif
        }

        if ( TmpMode == pWifiApSec->Cfg.ModeEnabled )
        {
            return  TRUE;
        }
	pWifiApSec->Cfg.ModeEnabled = TmpMode;
	pWifiAp->bSecChanged        = TRUE;
#if defined(FEATURE_HOSTAP_AUTHENTICATOR) && !defined (_XB7_PRODUCT_REQ_)
	int prevmode = pWifiApSec->Cfg.ModeEnabled;
        /* RDKB-30035 Run time config change */
        BOOLEAN isNativeHostapdDisabled = FALSE;
        CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
        if (isNativeHostapdDisabled)
        {
            PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
            PSINGLE_LINK_ENTRY              pSLinkEntry  = (PSINGLE_LINK_ENTRY       )NULL;
            PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
            PCOSA_DML_WIFI_SSID             pWifiSsid    = (PCOSA_DML_WIFI_SSID      )NULL;
            UCHAR                           PathName[64] = {0};

            pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);

            while ( pSLinkEntry )
            {
                pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
                pWifiSsid    = pSSIDLinkObj->hContext;

                sprintf((char *)PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);

                if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, (char *)PathName, TRUE) )
                {
                    break;
                }

                pSLinkEntry             = AnscQueueGetNextEntry(pSLinkEntry);
            }

            CosaDmlWiFiReConfigAuthKeyMgmt(pMyObject, pWifiAp, pWifiSsid, prevmode, pWifiApSec->Cfg.ModeEnabled);
            CcspWifiTrace(("RDK_LOG_INFO, WIFI_SECURITY_MODE_CHANGE_PUSHED_SUCCEESSFULLY\n"));
        }
#endif //FEATURE_HOSTAP_AUTHENTICATOR

        return TRUE;
    }

    const char *WepKeyType[WEPKEY_TYPE_SET] = {"WEPKey", "X_CISCO_COM_WEPKey", "X_COMCAST-COM_WEPKey"};
    int i = 0;

    for(i = 0; i < WEPKEY_TYPE_SET; i++)
    {
        rc = strcmp_s(WepKeyType[i], strlen(WepKeyType[i]), ParamName, &ind);
        ERR_CHK(rc);
        if((rc == EOK) && (!ind))
        {
            pWifiAp->bSecChanged = TRUE;

            /* save update to backup */
           if ( pString && AnscSizeOfString(pString) == 10)
           {
               _ansc_sscanf
               (
                   pString,
                   "%02X%02X%02X%02X%02X",
                   (UINT*)&tmpWEPKey[0],
                   (UINT*)&tmpWEPKey[1],
                   (UINT*)&tmpWEPKey[2],
                   (UINT*)&tmpWEPKey[3],
                   (UINT*)&tmpWEPKey[4]
               );

              if ( _ansc_memcmp(pWifiApSec->Cfg.WEPKeyp, tmpWEPKey, 5) != 0 )
              {
                 pWifiApSec->Cfg.WEPKeyp[0] = tmpWEPKey[0];
                 pWifiApSec->Cfg.WEPKeyp[1] = tmpWEPKey[1];
                 pWifiApSec->Cfg.WEPKeyp[2] = tmpWEPKey[2];
                 pWifiApSec->Cfg.WEPKeyp[3] = tmpWEPKey[3];
                 pWifiApSec->Cfg.WEPKeyp[4] = tmpWEPKey[4];

                 pWifiAp->bSecChanged = TRUE;

             }
          }
          else if ( pString && AnscSizeOfString(pString) == 26 )
          {
             /* Need to do sscanf to a temp value here as it puts a NULL at the end */

             _ansc_sscanf
             (
                pString,
                "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
                (UINT*)&tmpWEPKey[0],
                (UINT*)&tmpWEPKey[1],
                (UINT*)&tmpWEPKey[2],
                (UINT*)&tmpWEPKey[3],
                (UINT*)&tmpWEPKey[4],
                (UINT*)&tmpWEPKey[5],
                (UINT*)&tmpWEPKey[6],
                (UINT*)&tmpWEPKey[7],
                (UINT*)&tmpWEPKey[8],
                (UINT*)&tmpWEPKey[9],
                (UINT*)&tmpWEPKey[10],
                (UINT*)&tmpWEPKey[11],
                (UINT*)&tmpWEPKey[12]
             );

            if ( _ansc_memcmp(pWifiApSec->Cfg.WEPKeyp, tmpWEPKey, 13) != 0 )
            {
                 pWifiApSec->Cfg.WEPKeyp[0] = tmpWEPKey[0];
                 pWifiApSec->Cfg.WEPKeyp[1] = tmpWEPKey[1];
                 pWifiApSec->Cfg.WEPKeyp[2] = tmpWEPKey[2];
                 pWifiApSec->Cfg.WEPKeyp[3] = tmpWEPKey[3];
                 pWifiApSec->Cfg.WEPKeyp[4] = tmpWEPKey[4];
                 pWifiApSec->Cfg.WEPKeyp[5] = tmpWEPKey[5];
                 pWifiApSec->Cfg.WEPKeyp[6] = tmpWEPKey[6];
                 pWifiApSec->Cfg.WEPKeyp[7] = tmpWEPKey[7];
                 pWifiApSec->Cfg.WEPKeyp[8] = tmpWEPKey[8];
                 pWifiApSec->Cfg.WEPKeyp[9] = tmpWEPKey[9];
                 pWifiApSec->Cfg.WEPKeyp[10] = tmpWEPKey[10];
                 pWifiApSec->Cfg.WEPKeyp[11] = tmpWEPKey[11];
                 pWifiApSec->Cfg.WEPKeyp[12] = tmpWEPKey[12];

               
                pWifiAp->bSecChanged = TRUE;
            }
         }
         else
         {
            if((pWifiApSec->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_64) || 
               (pWifiApSec->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_128))
                return FALSE; /* Return an error only if the security mode enabled is WEP - For UI */
         }
        
        return TRUE;
     }
   }

    const char *KeyPassphraseType[KEYPASSPHRASE_SET] = {"KeyPassphrase", "X_COMCAST-COM_KeyPassphrase"};

    for(i = 0; i < KEYPASSPHRASE_SET; i++)
    {
        rc = strcmp_s(KeyPassphraseType[i], strlen(KeyPassphraseType[i]), ParamName, &ind);
        ERR_CHK(rc);
        if((rc == EOK) && (!ind))
        {
            int j;
            size_t len = strlen(pString);

            if ((len < 8 ) || (len > 63))
            {
                return FALSE;
            }

            for(j = 0; j < len; j++)
            {
                if (isprint(pString[j]) == 0)
                {
                    CcspTraceError(("%s:Invalid %s\n", __FUNCTION__,KeyPassphraseType[i]));
                    return FALSE;
                }
            }

            rc = strcmp_s((char*)pWifiApSec->Cfg.KeyPassphrase, sizeof(pWifiApSec->Cfg.KeyPassphrase), pString, &ind);
            ERR_CHK(rc);
            if((rc == EOK) && (!ind))
            {
                return TRUE;
            }


//RDKB-20043 - We should not restrict here
#if 0
        if ( (pWifiAp->AP.Cfg.InstanceNumber == 1 ) || (pWifiAp->AP.Cfg.InstanceNumber == 2 ) )
        {

            if ( AnscEqualString(pString, pWifiApSec->Cfg.DefaultKeyPassphrase, TRUE) )
            {
                return FALSE;
            }

        }
#endif /* 0 */
        /* If WiFiForceRadioDisable Feature has been enabled then the radio status should
           be false, since in the HAL the radio status has been set to down state which is
           not reflected in DML layer.
         */
           if (ANSC_STATUS_SUCCESS != CosaDmlWiFiGetForceDisableWiFiRadio(&bForceDisableFlag))
           {
                return FALSE;
           }
           if (!bForceDisableFlag)
           {

               /* save update to backup */
               if((pString == NULL) || (strlen(pString) >= sizeof(pWifiApSec->Cfg.KeyPassphrase)))
                    return FALSE;

               rc = strcpy_s((char*)pWifiApSec->Cfg.KeyPassphrase, sizeof(pWifiApSec->Cfg.KeyPassphrase), pString);

               if(rc != EOK)
               {
                    ERR_CHK(rc);
                    return FALSE;
               }
               //zqiu: reason for change: Change 2.4G wifi password not work for the first time
               //AnscCopyString(pWifiApSec->Cfg.PreSharedKey, pWifiApSec->Cfg.KeyPassphrase );
               rc = strcpy_s((char*)pWifiApSec->Cfg.PreSharedKey, sizeof(pWifiApSec->Cfg.PreSharedKey), (char*)pWifiApSec->Cfg.KeyPassphrase);
               if(rc != EOK)
               {
                    ERR_CHK(rc);
                    return FALSE;
               }
#if defined(FEATURE_HOSTAP_AUTHENTICATOR) && !defined (_XB7_PRODUCT_REQ_)
            /* RDKB-30035 Run time config change */
            BOOLEAN isNativeHostapdDisabled = FALSE;
            CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
            if (isNativeHostapdDisabled &&
                !(hapd_reload_authentication(pWifiAp->AP.Cfg.InstanceNumber - 1, pWifiApSec->Cfg.KeyPassphrase)))
            {
                CcspWifiTrace(("RDK_LOG_INFO, WIFI_PASSPHRASE_CHANGE_PUSHED_SUCCEESSFULLY\n"));
            }
#endif //FEATURE_HOSTAP_AUTHENTICATOR
               pWifiAp->bSecChanged = TRUE;
           } else {
               CcspWifiTrace(("RDK_LOG_ERROR, WIFI_ATTEMPT_TO_CHANGE_CONFIG_WHEN_FORCE_DISABLED\n" ));
              return FALSE;
         }
         return TRUE;
      }
    }

    int found = 0;
    rc = strcmp_s("PreSharedKey", strlen("PreSharedKey"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
        found = 1;
    }

    if(found == 0)
    {
        rc = strcmp_s("X_COMCAST-COM_KeyPassphrase", strlen("X_COMCAST-COM_KeyPassphrase"), ParamName, &ind);
        ERR_CHK(rc);
        if((rc == EOK) && (!ind))
        {
             if(AnscSizeOfString(pString) == 64)
             {
                  found = 1;
             }
        }
    }

    if(found == 1)
    {
        rc = strcmp_s((char*)pWifiApSec->Cfg.PreSharedKey, sizeof(pWifiApSec->Cfg.PreSharedKey), pString, &ind);
        ERR_CHK(rc);
        if((rc == EOK) && (!ind))
        {
            return TRUE;
        }

        /* save update to backup */
        if((pString == NULL) || (strlen(pString) >= sizeof(pWifiApSec->Cfg.PreSharedKey)))
             return FALSE;

        rc = strcpy_s((char*)pWifiApSec->Cfg.PreSharedKey, sizeof(pWifiApSec->Cfg.PreSharedKey), pString);
        if(rc != EOK)
        {
           ERR_CHK(rc);
           return FALSE;
        }
        rc = strcpy_s((char*)pWifiApSec->Cfg.KeyPassphrase, sizeof(pWifiApSec->Cfg.KeyPassphrase), "");
        if(rc != EOK)
        {
            ERR_CHK(rc);
            return FALSE;
        }
    
        pWifiAp->bSecChanged = TRUE;

        return TRUE;
    }

    rc = strcmp_s("SAEPassphrase", strlen("SAEPassphrase"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
        return TRUE;
    }

    rc = strcmp_s("RadiusSecret", strlen("RadiusSecret"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
        rc = strcmp_s(pWifiApSec->Cfg.RadiusSecret, sizeof(pWifiApSec->Cfg.RadiusSecret), pString, &ind);
        ERR_CHK(rc);
        if((rc == EOK) && (!ind))
            return TRUE;

		/* save update to backup */
                if((pString == NULL) || (strlen(pString) >= sizeof(pWifiApSec->Cfg.RadiusSecret)))
                      return FALSE;

                rc = strcpy_s(pWifiApSec->Cfg.RadiusSecret, sizeof(pWifiApSec->Cfg.RadiusSecret), pString);
                if(rc != EOK)
                {
                   ERR_CHK(rc);
                   return FALSE;
                }

		pWifiAp->bSecChanged = TRUE;
#if defined(FEATURE_HOSTAP_AUTHENTICATOR) && !defined (_XB7_PRODUCT_REQ_)
                BOOLEAN isNativeHostapdDisabled = FALSE;
                CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
                if (isNativeHostapdDisabled &&
                    !(hapd_reload_radius_param(pWifiAp->AP.Cfg.InstanceNumber - 1, pWifiApSec->Cfg.RadiusSecret, NULL, 0, 0, TRUE, COSA_WIFI_HAPD_RADIUS_SERVER_SECRET)))
                {
                    CcspWifiTrace(("RDK_LOG_INFO, RADIUS_PARAM_CHANGE_PUSHED_SUCCEESSFULLY\n"));
                }
#endif //FEATURE_HOSTAP_AUTHENTICATOR
        return TRUE;
    }
	
    rc = strcmp_s("SecondaryRadiusSecret", strlen("SecondaryRadiusSecret"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
        rc = strcmp_s(pWifiApSec->Cfg.SecondaryRadiusSecret, sizeof(pWifiApSec->Cfg.SecondaryRadiusSecret), pString, &ind);
        ERR_CHK(rc);
        if((rc == EOK) && (!ind))
           return TRUE;
    
	/* save update to backup */
        if((pString == NULL) || (strlen(pString) >= sizeof(pWifiApSec->Cfg.SecondaryRadiusSecret)))
             return FALSE;
        rc = strcpy_s(pWifiApSec->Cfg.SecondaryRadiusSecret, sizeof(pWifiApSec->Cfg.SecondaryRadiusSecret), pString);
        if(rc != EOK)
        {
              ERR_CHK(rc);
              return FALSE;
        }
	pWifiAp->bSecChanged = TRUE;
#if defined(FEATURE_HOSTAP_AUTHENTICATOR) && !defined (_XB7_PRODUCT_REQ_)
        BOOLEAN isNativeHostapdDisabled = FALSE;
        CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
        if (isNativeHostapdDisabled &&
            !(hapd_reload_radius_param(pWifiAp->AP.Cfg.InstanceNumber - 1, pWifiApSec->Cfg.SecondaryRadiusSecret, NULL, 0, 0, FALSE, COSA_WIFI_HAPD_RADIUS_SERVER_SECRET)))
        {
             CcspWifiTrace(("RDK_LOG_INFO, RADIUS_PARAM_CHANGE_PUSHED_SUCCEESSFULLY\n"));
        }
#endif //FEATURE_HOSTAP_AUTHENTICATOR
        return TRUE;
    }

    rc = strcmp_s("RadiusServerIPAddr", strlen("RadiusServerIPAddr"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
        rc = strcmp_s((char*)pWifiApSec->Cfg.RadiusServerIPAddr, sizeof( pWifiApSec->Cfg.RadiusServerIPAddr), pString, &ind);
        ERR_CHK(rc);
        if((rc == EOK) && (!ind))
	    return TRUE;

	/* save update to backup */
        if((pString == NULL) || (strlen(pString) >= sizeof(pWifiApSec->Cfg.RadiusServerIPAddr)))
             return FALSE;
        rc = strcpy_s( (char*)pWifiApSec->Cfg.RadiusServerIPAddr, sizeof(pWifiApSec->Cfg.RadiusServerIPAddr), pString);
        if(rc != EOK)
        {
              ERR_CHK(rc);
              return FALSE;
        }
	pWifiAp->bSecChanged = TRUE;
#if defined(FEATURE_HOSTAP_AUTHENTICATOR) && !defined (_XB7_PRODUCT_REQ_)
        BOOLEAN isNativeHostapdDisabled = FALSE;
        CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
        if (isNativeHostapdDisabled &&
            !(hapd_reload_radius_param(pWifiAp->AP.Cfg.InstanceNumber - 1, NULL, pWifiApSec->Cfg.RadiusServerIPAddr, 0, 0, TRUE, COSA_WIFI_HAPD_RADIUS_SERVER_IP)))
        {
             CcspWifiTrace(("RDK_LOG_INFO, RADIUS_PARAM_CHANGE_PUSHED_SUCCEESSFULLY\n"));
        }
#endif //FEATURE_HOSTAP_AUTHENTICATOR
        return TRUE;
    }
	
    rc = strcmp_s("SecondaryRadiusServerIPAddr", strlen("SecondaryRadiusServerIPAddr"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
        rc = strcmp_s((char*)pWifiApSec->Cfg.SecondaryRadiusServerIPAddr, sizeof(pWifiApSec->Cfg.SecondaryRadiusServerIPAddr), pString, &ind);
        ERR_CHK(rc);
        if((rc == EOK) && (!ind))
            return TRUE;
        
	/* save update to backup */
        if((pString == NULL) || (strlen(pString) >= sizeof(pWifiApSec->Cfg.SecondaryRadiusServerIPAddr)))
             return FALSE;
        rc = strcpy_s((char*)pWifiApSec->Cfg.SecondaryRadiusServerIPAddr, sizeof(pWifiApSec->Cfg.SecondaryRadiusServerIPAddr), pString);
        if(rc != EOK)
        {
              ERR_CHK(rc);
              return FALSE;
        }
	pWifiAp->bSecChanged = TRUE;
#if defined(FEATURE_HOSTAP_AUTHENTICATOR) && !defined (_XB7_PRODUCT_REQ_)
                BOOLEAN isNativeHostapdDisabled = FALSE;
                CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
                if (isNativeHostapdDisabled &&
                    !(hapd_reload_radius_param(pWifiAp->AP.Cfg.InstanceNumber - 1, NULL, pWifiApSec->Cfg.SecondaryRadiusServerIPAddr, 0, 0, FALSE, COSA_WIFI_HAPD_RADIUS_SERVER_IP)))
                {
                     CcspWifiTrace(("RDK_LOG_INFO, RADIUS_PARAM_CHANGE_PUSHED_SUCCEESSFULLY\n"));
                }
#endif //FEATURE_HOSTAP_AUTHENTICATOR
        return TRUE;
    }

    rc = strcmp_s("MFPConfig", strlen("MFPConfig"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
        rc = strcmp_s(pWifiApSec->Cfg.MFPConfig, sizeof(pWifiApSec->Cfg.MFPConfig), pString, &ind);
        ERR_CHK(rc);
        if((rc == EOK) && (!ind))
              return TRUE;
        const char *MFPConfigOptions[MFPCONFIG_OPTIONS_SET] = {"Disabled", "Optional", "Required"};
        int mfpOptions_match = 0;
        for(i = 0; i < MFPCONFIG_OPTIONS_SET; i++)
        {
            rc = strcmp_s(MFPConfigOptions[i], strlen(MFPConfigOptions[i]), pString, &ind);
            ERR_CHK(rc);
            if((rc == EOK) && (!ind))
            {
                mfpOptions_match = 1;
                break;
            }
        }
        if(mfpOptions_match == 1)
        {
            /* save update to backup */
            if((pString == NULL) || (strlen(pString) >= sizeof(pWifiApSec->Cfg.MFPConfig)))
                  return FALSE;

            rc = strcpy_s(pWifiApSec->Cfg.MFPConfig, sizeof(pWifiApSec->Cfg.MFPConfig), pString);
            if(rc != EOK)
            {
                ERR_CHK(rc);
                return FALSE;
            }
            pWifiAp->bSecChanged = TRUE;
            return TRUE;
        }
        else
        {
            CcspTraceWarning(("MFPConfig : Unsupported Value'%s'\n", ParamName));
            return FALSE;
        }
    }

    rc = strcmp_s("RadiusDASIPAddr", strlen("RadiusDASIPAddr"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
        rc = strcmp_s((char *)pWifiApSec->Cfg.RadiusDASIPAddr, sizeof(pWifiApSec->Cfg.RadiusDASIPAddr), pString, &ind);

        ERR_CHK(rc);
        if((rc == EOK) && (!ind))
              return TRUE;

        /* save update to backup */
        if((pString == NULL) || (strlen(pString) >= sizeof(pWifiApSec->Cfg.RadiusDASIPAddr)))
              return FALSE;

        rc = strcpy_s((char *)pWifiApSec->Cfg.RadiusDASIPAddr, sizeof(pWifiApSec->Cfg.RadiusDASIPAddr), pString);
        if(rc != EOK)
        {
            ERR_CHK(rc);
            return FALSE;
        }
        pWifiAp->bSecChanged = TRUE;
        return TRUE;
    }
    rc = strcmp_s("RadiusDASSecret", strlen("RadiusDASSecret"), ParamName, &ind);
    ERR_CHK(rc);
    if((rc == EOK) && (!ind))
    {
        rc = strcmp_s(pWifiApSec->Cfg.RadiusDASSecret, sizeof(pWifiApSec->Cfg.RadiusDASSecret), pString, &ind);
        ERR_CHK(rc);
        if((rc == EOK) && (!ind))
              return TRUE;

        /* save update to backup */
        if((pString == NULL) || (strlen(pString) >= sizeof(pWifiApSec->Cfg.RadiusDASSecret)))
              return FALSE;

        rc = strcpy_s(pWifiApSec->Cfg.RadiusDASSecret, sizeof(pWifiApSec->Cfg.RadiusDASSecret), pString);
        if(rc != EOK)
        {
            ERR_CHK(rc);
            return FALSE;
        }
        pWifiAp->bSecChanged = TRUE;
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Security_Validate
            (
                ANSC_HANDLE                 hInsContext,
                char*                       pReturnParamName,
                ULONG*                      puLength
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       pReturnParamName,
                The buffer (128 bytes) of parameter name if there's a validation. 

                ULONG*                      puLength
                The output length of the param name. 

    return:     TRUE if there's no validation.

**********************************************************************/
BOOL
Security_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec   = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;

    // If PSK is present it must be a 64 hex string
	//zqiu: reason for change: Change 2.4G wifi password not work for the first time
    /*if ( (AnscSizeOfString(pWifiApSec->Cfg.PreSharedKey) > 0) && 
         (!isHex(pWifiApSec->Cfg.PreSharedKey) ||  (AnscSizeOfString(pWifiApSec->Cfg.PreSharedKey) != 64) ) ) {
        AnscCopyString(pReturnParamName, "PreSharedKey");
        *puLength = AnscSizeOfString("PreSharedKey");
        return FALSE;
    }*/

    // Check the ModeEnabled against ModesSupported
    if ((pWifiApSec->Info.ModesSupported & pWifiApSec->Cfg.ModeEnabled) == 0)
    {
        AnscCopyString(pReturnParamName, "ModesSupported");
        *puLength = AnscSizeOfString("ModesSupported");
        return FALSE;
    }

    // WPA is not allowed by itself.  Only in mixed mode WPA/WPA2
    if (pWifiApSec->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_Enterprise ||
        pWifiApSec->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_Personal) {
        AnscCopyString(pReturnParamName, "X_CISCO_COM_EncryptionMethod");
        *puLength = AnscSizeOfString("X_CISCO_COM_EncryptionMethod");
        return FALSE;
    }
    //zqiu: R5422 >>
    if ( ( (pWifiApSec->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise) ||
            (pWifiApSec->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_WPA2_Personal) ) &&
          (pWifiApSec->Cfg.EncryptionMethod != COSA_DML_WIFI_AP_SEC_AES_TKIP) &&
	  (pWifiApSec->Cfg.EncryptionMethod != COSA_DML_WIFI_AP_SEC_AES )   ) {
        AnscCopyString(pReturnParamName, "X_CISCO_COM_EncryptionMethod");
        *puLength = AnscSizeOfString("X_CISCO_COM_EncryptionMethod");
        return FALSE;
    }

    if (
        ((pWifiApSec->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WPA3_Personal) ||
        (pWifiApSec->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WPA3_Personal_Transition) ||
        (pWifiApSec->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WPA3_Enterprise))
        &&
        (pWifiApSec->Cfg.EncryptionMethod != COSA_DML_WIFI_AP_SEC_AES)
    ) {
        AnscCopyString(pReturnParamName, "X_CISCO_COM_EncryptionMethod");
        *puLength = AnscSizeOfString("X_CISCO_COM_EncryptionMethod");
        return FALSE;
    }

    //if ( ( pWifiApSec->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Enterprise ||
    //        pWifiApSec->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Personal ) &&
    //     (pWifiApSec->Cfg.EncryptionMethod != COSA_DML_WIFI_AP_SEC_AES ) ) {
    //    AnscCopyString(pReturnParamName, "X_CISCO_COM_EncryptionMethod");
    //    *puLength = AnscSizeOfString("X_CISCO_COM_EncryptionMethod");
    //    return FALSE;
    //}
    //zqiu: R5422 <<

	/* 
	  * If the parameter cannot be set, the CPE MUST reject the request as an invalid 
	  * parameter value. Possible failure reasons include a lack of default values or 
	  * if ModeEnabled is an Enterprise type, i.e. WPA-Enterprise, WPA2-Enterprise or 
	  * WPA-WPA2-Enterprise.
	  */
	if( ( pWifiApSec->Cfg.bReset == TRUE ) && 
		 ( ( COSA_DML_WIFI_SECURITY_WPA_Enterprise == pWifiApSec->Cfg.ModeEnabled ) ||
	       ( COSA_DML_WIFI_SECURITY_WPA2_Enterprise == pWifiApSec->Cfg.ModeEnabled ) ||
		   ( COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise == pWifiApSec->Cfg.ModeEnabled )
   	     )
	  )
	{
        AnscCopyString(pReturnParamName, "Reset");
        *puLength = AnscSizeOfString("Reset");
        return FALSE;
	}
    return TRUE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        Security_Commit
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
Security_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj      = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp       = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec    = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;
    PCOSA_DML_WIFI_APSEC_CFG        pWifiApSecCfg = (PCOSA_DML_WIFI_APSEC_CFG )&pWifiApSec->Cfg;
    PSINGLE_LINK_ENTRY              pSLinkEntry   = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj  = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid     = (PCOSA_DML_WIFI_SSID      )NULL;
    CHAR                            PathName[64]  = {0};

    if ( !pWifiAp->bSecChanged )
    {
        return  ANSC_STATUS_SUCCESS;
    }
    else
    {
        pWifiAp->bSecChanged = FALSE;
        CcspTraceInfo(("WiFi AP Security commit -- apply the changes...\n"));
    }
    
    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
    
    while ( pSLinkEntry )
    {
        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        
        sprintf(PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);
        
        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }
        
        pSLinkEntry             = AnscQueueGetNextEntry(pSLinkEntry);
    }
    
    if ( pSLinkEntry )
    {
        pWifiSsid = pSSIDLinkObj->hContext;
        
#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        return CosaDmlWiFiApSecSetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, pWifiApSecCfg);
#else
        return CosaDmlWiFiApSecSetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, pWifiApSecCfg);
#endif
    }

    return ANSC_STATUS_FAILURE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        Security_Rollback
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to roll back the update whenever there's a 
        validation found.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
Security_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject       = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObjAp      = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp         = (PCOSA_DML_WIFI_AP        )pLinkObjAp->hContext;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObjSsid    = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PSINGLE_LINK_ENTRY              pSLinkEntrySsid = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid       = (PCOSA_DML_WIFI_SSID      )NULL;
    CHAR                            PathName[64]    = {0};

    /*Get the corresponding SSID entry*/
    pSLinkEntrySsid = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
    while ( pSLinkEntrySsid )
    {
        pLinkObjSsid = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntrySsid);
        
        sprintf(PathName, "Device.WiFi.SSID.%lu.", pLinkObjSsid->InstanceNumber);
        
        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }
                
        pSLinkEntrySsid             = AnscQueueGetNextEntry(pSLinkEntrySsid);
    }
    
    if ( pSLinkEntrySsid )
    {
        pWifiSsid = pLinkObjSsid->hContext;
        
#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        CosaDmlWiFiApSecGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->SEC);
#else
        CosaDmlWiFiApSecGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->SEC);
#endif
    }
    return ANSC_STATUS_SUCCESS;
}

/***********************************************************************

 APIs for Object:

    WiFi.AccessPoint.{i}.WPS.

    *  WPS_GetParamBoolValue
    *  WPS_GetParamIntValue
    *  WPS_GetParamUlongValue
    *  WPS_GetParamStringValue
    *  WPS_SetParamBoolValue
    *  WPS_SetParamIntValue
    *  WPS_SetParamUlongValue
    *  WPS_SetParamStringValue
    *  WPS_Validate
    *  WPS_Commit
    *  WPS_Rollback

***********************************************************************/
/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        WPS_GetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:

        This function is called to retrieve Boolean parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL*                       pBool
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
WPS_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APWPS_FULL       pWifiApWps   = (PCOSA_DML_WIFI_APWPS_FULL)&pWifiAp->WPS;
    INT  wlanIndex = -1;

    wlanIndex = pWifiAp->AP.Cfg.InstanceNumber -1 ;
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Enable") == 0)
    {
	BOOL enableWps = FALSE;

	wifi_getApWpsEnable(wlanIndex, &enableWps);
	pWifiApWps->Cfg.bEnabled = enableWps;
        /* collect value */
        *pBool = pWifiApWps->Cfg.bEnabled;
        return TRUE;
    }

    if (strcmp(ParamName, "X_LGI-COM_ActivatePushButton") == 0)
    {
        /* collect value */
        *pBool = pWifiApWps->Cfg.X_LGI_COM_ActivatePushButton;
        return TRUE;
    }

    if (strcmp(ParamName, "X_Comcast_com_Configured") == 0)
    {
        /* collect value */
        char  configState[32] = {0};
        wifi_getApWpsConfigurationState(wlanIndex, configState);
        if (strstr(configState,"Not configured") != NULL) {
            pWifiApWps->Info.X_Comcast_com_Configured = FALSE;
        } else {
            pWifiApWps->Info.X_Comcast_com_Configured = TRUE;
        }
        *pBool = pWifiApWps->Info.X_Comcast_com_Configured;
        return TRUE;
    }

    if (strcmp(ParamName, "X_LGI-COM_CancelSession") == 0)
    {
        /* collect value */
        *pBool = pWifiApWps->Cfg.X_LGI_COM_CancelSession;
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        WPS_GetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int*                        pInt
            );

    description:

        This function is called to retrieve integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int*                        pInt
                The buffer of returned integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
WPS_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
    /* check the parameter name and return the corresponding value */
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APWPS_FULL       pWifiApWps   = (PCOSA_DML_WIFI_APWPS_FULL)&pWifiAp->WPS;
    

    if (strcmp(ParamName, "X_CISCO_COM_WpsPushButton") == 0)
    {
        *pInt = pWifiApWps->Cfg.WpsPushButton;
        return TRUE;
    }
 
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        WPS_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
WPS_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(puLong);
    /* check the parameter name and return the corresponding value */

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        WPS_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
WPS_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APWPS_FULL       pWifiApWps   = (PCOSA_DML_WIFI_APWPS_FULL)&pWifiAp->WPS;
    
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "ConfigMethodsEnabled") == 0)
    {
        /* collect value */
        char buf[512] = {0};
        if (pWifiApWps->Cfg.ConfigMethodsEnabled & COSA_DML_WIFI_WPS_METHOD_UsbFlashDrive )
        {   
            strcat(buf, "USBFlashDrive");
        }
        if (pWifiApWps->Cfg.ConfigMethodsEnabled & COSA_DML_WIFI_WPS_METHOD_Ethernet )
        {   
            if (AnscSizeOfString(buf) != 0)
            {
               strcat(buf, ",Ethernet");
            }
            else
            {
               strcat(buf, "Ethernet");
            }

        }
        if (pWifiApWps->Cfg.ConfigMethodsEnabled & COSA_DML_WIFI_WPS_METHOD_ExternalNFCToken )
        {
            if (AnscSizeOfString(buf) != 0)
            {
               strcat(buf, ",ExternalNFCToken");
            }
            else
            {
               strcat(buf, "ExternalNFCToken");
            }
        }
        if (pWifiApWps->Cfg.ConfigMethodsEnabled & COSA_DML_WIFI_WPS_METHOD_IntgratedNFCToken )
        {
            if (AnscSizeOfString(buf) != 0)
            {
               strcat(buf, ",IntegratedNFCToken");
            }
            else
            {
               strcat(buf, "IntegratedNFCToken");
            }
        }
        if (pWifiApWps->Cfg.ConfigMethodsEnabled & COSA_DML_WIFI_WPS_METHOD_NFCInterface )
        {
            if (AnscSizeOfString(buf) != 0)
            {
               strcat(buf, ",NFCInterface");
            }
            else
            {
               strcat(buf, "NFCInterface");
            }
        }
        if (pWifiApWps->Cfg.ConfigMethodsEnabled & COSA_DML_WIFI_WPS_METHOD_PushButton )
        {
            if (AnscSizeOfString(buf) != 0)
            {
               strcat(buf, ",PushButton");
            }
            else
            {
               strcat(buf, "PushButton");
            }

         }
         if (pWifiApWps->Cfg.ConfigMethodsEnabled & COSA_DML_WIFI_WPS_METHOD_Pin )
         {
         
            if (AnscSizeOfString(buf) != 0)
            {
               strcat(buf, ",PIN");
            }
            else
            {
               strcat(buf, "PIN");
            }
        }
        if ( AnscSizeOfString(buf) < *pUlSize)
        {
            AnscCopyString(pValue, buf);
            return 0;
        }
        else
        {
           *pUlSize = AnscSizeOfString(buf)+1;
           return 1;
        }
    }
    if (strcmp(ParamName, "ConfigMethodsSupported") == 0)
    {
        /* collect value */
        char buf[512] = {0};
        if (pWifiApWps->Info.ConfigMethodsSupported & COSA_DML_WIFI_WPS_METHOD_UsbFlashDrive )
        {   
            strcat(buf, "USBFlashDrive");
        }
        if (pWifiApWps->Info.ConfigMethodsSupported & COSA_DML_WIFI_WPS_METHOD_Ethernet )
        {   
            if (AnscSizeOfString(buf) != 0)
            {
               strcat(buf, ",Ethernet");
            }
            else
            {
               strcat(buf, "Ethernet");
            }

        }
        if (pWifiApWps->Info.ConfigMethodsSupported & COSA_DML_WIFI_WPS_METHOD_ExternalNFCToken )
        {
            if (AnscSizeOfString(buf) != 0)
            {
               strcat(buf, ",ExternalNFCToken");
            }
            else
            {
               strcat(buf, "ExternalNFCToken");
            }
        }
        if (pWifiApWps->Info.ConfigMethodsSupported & COSA_DML_WIFI_WPS_METHOD_IntgratedNFCToken )
        {
            if (AnscSizeOfString(buf) != 0)
            {
               strcat(buf, ",IntegratedNFCToken");
            }
            else
            {
               strcat(buf, "IntegratedNFCToken");
            }
        }
        if (pWifiApWps->Info.ConfigMethodsSupported & COSA_DML_WIFI_WPS_METHOD_NFCInterface )
        {
            if (AnscSizeOfString(buf) != 0)
            {
               strcat(buf, ",NFCInterface");
            }
            else
            {
               strcat(buf, "NFCInterface");
            }
        }
        if (pWifiApWps->Info.ConfigMethodsSupported & COSA_DML_WIFI_WPS_METHOD_PushButton )
        {
            if (AnscSizeOfString(buf) != 0)
            {
               strcat(buf, ",PushButton");
            }
            else
            {
               strcat(buf, "PushButton");
            }

         }
         if (pWifiApWps->Info.ConfigMethodsSupported & COSA_DML_WIFI_WPS_METHOD_Pin )
         {
            if (AnscSizeOfString(buf) != 0)
            {
               strcat(buf, ",PIN");
            }
            else
            {
               strcat(buf, "PIN");
            }
        }
        if ( AnscSizeOfString(buf) < *pUlSize)
        {
            AnscCopyString(pValue, buf);
            return 0;
        }
        else
        {
           *pUlSize = AnscSizeOfString(buf)+1;
           return 1;
        }
    }

    if (strcmp(ParamName, "X_LGI-COM_Pin") == 0)
    {
        if (*pUlSize <= AnscSizeOfString(pWifiApWps->Info.X_LGI_COM_Pin))
        {
            *pUlSize = AnscSizeOfString(pWifiApWps->Info.X_LGI_COM_Pin) + 1;
            return 1;
        }

        AnscCopyString(pValue, pWifiApWps->Info.X_LGI_COM_Pin);
        return 0;
    }

    if (strcmp(ParamName, "X_LGI-COM_ClientPin") == 0)
    {
        if (*pUlSize <= AnscSizeOfString(pWifiApWps->Cfg.X_LGI_COM_ClientPin))
        {
            *pUlSize = AnscSizeOfString(pWifiApWps->Cfg.X_LGI_COM_ClientPin) + 1;
            return 1;
        }

        AnscCopyString(pValue, pWifiApWps->Cfg.X_LGI_COM_ClientPin);
        return 0;
    }

    // LGI ADD - START
    if (strcmp(ParamName, "X_LGI-COM_WpsStatus") == 0)
    {
        char strbuf[64];
        int len;

        strbuf[0] = 0;
        CosaDmlWiFi_getWpsStatus((pWifiAp->AP.Cfg.InstanceNumber -1), strbuf);

        len = strlen(strbuf);
        if (len < *pUlSize)
        {
            memcpy(pValue, strbuf, len + 1);
            return 0;
        }
        else
        {
           *pUlSize = len + 1;
           return 1;
        }
    }
    // LGI ADD - END

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return -1;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        WPS_SetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL                        bValue
            );

    description:

        This function is called to set BOOL parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL                        bValue
                The updated BOOL value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
WPS_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APWPS_FULL       pWifiApWps   = (PCOSA_DML_WIFI_APWPS_FULL)&pWifiAp->WPS;
    
    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "Enable") == 0)
    {
        /* save update to backup */
        pWifiApWps->Cfg.bEnabled = bValue;
#if defined(FEATURE_HOSTAP_AUTHENTICATOR) && !defined (_XB7_PRODUCT_REQ_)
        BOOLEAN isNativeHostapdDisabled = FALSE;
        CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
        if (isNativeHostapdDisabled && pWifiApWps->Cfg.bEnabled != bValue)
        {
            CosaDmlWiFiWpsConfigUpdate(pWifiAp->AP.Cfg.InstanceNumber - 1, pWifiAp);
            hapd_reload_wps_config(pWifiAp->AP.Cfg.InstanceNumber - 1, COSA_WIFI_HAPD_WPS_STATE, bValue, 0, 0);
            CcspWifiTrace(("RDK_LOG_INFO, WPS_PARAM_CHANGE_PUSHED_SUCCESSFULLY\n"));
        }
#endif //FEATURE_HOSTAP_AUTHENTICATOR
        return TRUE;
    }
    if (strcmp(ParamName, "X_LGI-COM_ActivatePushButton") == 0)
    {
        /* save update to backup */
        pWifiApWps->Cfg.X_LGI_COM_ActivatePushButton = bValue;
        return TRUE;
    }
    if (strcmp(ParamName, "X_LGI-COM_CancelSession") == 0)
    {
        /* save update to backup */
        pWifiApWps->Cfg.X_LGI_COM_CancelSession = bValue;
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        WPS_SetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int                         iValue
            );

    description:

        This function is called to set integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int                         iValue
                The updated integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
WPS_SetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int                         iValue
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APWPS_FULL       pWifiApWps   = (PCOSA_DML_WIFI_APWPS_FULL)&pWifiAp->WPS;
 
    /* check the parameter name and set the corresponding value */

    if (strcmp(ParamName, "X_CISCO_COM_WpsPushButton") == 0)
    {
        pWifiApWps->Cfg.WpsPushButton = iValue;
#if defined(FEATURE_HOSTAP_AUTHENTICATOR) && !defined (_XB7_PRODUCT_REQ_)
        BOOLEAN isNativeHostapdDisabled = FALSE;
        CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
        if (isNativeHostapdDisabled && pWifiApWps->Cfg.WpsPushButton != iValue &&
            !(hapd_reload_wps_config(pWifiAp->AP.Cfg.InstanceNumber - 1, COSA_WIFI_HAPD_WPS_PUSH_BUTTON, 0, 0, iValue)))
        {
             CcspWifiTrace(("RDK_LOG_INFO, WPS_PARAM_CHANGE_PUSHED_SUCCESSFULLY\n"));
        }
#endif //FEATURE_HOSTAP_AUTHENTICATOR
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        WPS_SetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG                       uValue
            );

    description:

        This function is called to set ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG                       uValue
                The updated ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
WPS_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(uValue);
    /* check the parameter name and set the corresponding value */

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        WPS_SetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pString
            );

    description:

        This function is called to set string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pString
                The updated string value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
WPS_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APWPS_FULL       pWifiApWps   = (PCOSA_DML_WIFI_APWPS_FULL)&pWifiAp->WPS;
    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "ConfigMethodsEnabled") == 0)
    {
        int match = 0;
        //Needs to initialize by 0 before setting
        pWifiApWps->Cfg.ConfigMethodsEnabled = 0;
        /* save update to backup */
        if (_ansc_strstr(pString, "USBFlashDrive"))
        {
            match++;
            pWifiApWps->Cfg.ConfigMethodsEnabled = (pWifiApWps->Cfg.ConfigMethodsEnabled | COSA_DML_WIFI_WPS_METHOD_UsbFlashDrive);
        }
        if (_ansc_strstr(pString, "Ethernet"))
        {
            match++;
            pWifiApWps->Cfg.ConfigMethodsEnabled = (pWifiApWps->Cfg.ConfigMethodsEnabled | COSA_DML_WIFI_WPS_METHOD_Ethernet);
        }
        if (_ansc_strstr(pString, "ExternalNFCToken"))
        {
            match++;
            pWifiApWps->Cfg.ConfigMethodsEnabled = (pWifiApWps->Cfg.ConfigMethodsEnabled | COSA_DML_WIFI_WPS_METHOD_ExternalNFCToken);
        }
        if (_ansc_strstr(pString, "IntegratedNFCToken"))
        {
            match++;
            pWifiApWps->Cfg.ConfigMethodsEnabled = (pWifiApWps->Cfg.ConfigMethodsEnabled | COSA_DML_WIFI_WPS_METHOD_IntgratedNFCToken);
        }
        if (_ansc_strstr(pString, "NFCInterface"))
        {
            match++;
            pWifiApWps->Cfg.ConfigMethodsEnabled = (pWifiApWps->Cfg.ConfigMethodsEnabled | COSA_DML_WIFI_WPS_METHOD_NFCInterface);
        }
        if (_ansc_strstr(pString, "PushButton"))
        {
            match++;
            pWifiApWps->Cfg.ConfigMethodsEnabled = (pWifiApWps->Cfg.ConfigMethodsEnabled | COSA_DML_WIFI_WPS_METHOD_PushButton);
        }
        if (_ansc_strstr(pString, "PIN"))
        {
            match++;
            pWifiApWps->Cfg.ConfigMethodsEnabled = (pWifiApWps->Cfg.ConfigMethodsEnabled | COSA_DML_WIFI_WPS_METHOD_Pin);
        }
	if (_ansc_strstr(pString, "NONE"))
        {
            match++;
           pWifiApWps->Cfg.ConfigMethodsEnabled = 0;
        }

	//If match is not there then return error
        if (match == 0)
        {   // Might have passed value that is invalid
            return FALSE;
        }
#if defined(FEATURE_HOSTAP_AUTHENTICATOR) && !defined (_XB7_PRODUCT_REQ_)
        BOOLEAN isNativeHostapdDisabled = FALSE;
        CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
        if (isNativeHostapdDisabled &&
            !(hapd_reload_wps_config(pWifiAp->AP.Cfg.InstanceNumber - 1, COSA_WIFI_HAPD_WPS_CONFIG_METHODS, 0, pWifiApWps->Cfg.ConfigMethodsEnabled, 0)))
        {
             CcspWifiTrace(("RDK_LOG_INFO, WPS_PARAM_CHANGE_PUSHED_SUCCESSFULLY\n"));
        }
#endif //FEATURE_HOSTAP_AUTHENTICATOR
        return TRUE;
    }

    if (strcmp(ParamName, "X_LGI-COM_ClientPin") == 0)
    {
        if (AnscSizeOfString(pString) > sizeof(pWifiApWps->Cfg.X_LGI_COM_ClientPin) - 1)
            return FALSE;

        AnscCopyString(pWifiApWps->Cfg.X_LGI_COM_ClientPin, pString);
        return TRUE;
    }


    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        WPS_Validate
            (
                ANSC_HANDLE                 hInsContext,
                char*                       pReturnParamName,
                ULONG*                      puLength
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       pReturnParamName,
                The buffer (128 bytes) of parameter name if there's a validation. 

                ULONG*                      puLength
                The output length of the param name. 

    return:     TRUE if there's no validation.

**********************************************************************/
BOOL
WPS_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APWPS_FULL       pWifiApWps   = (PCOSA_DML_WIFI_APWPS_FULL)&pWifiAp->WPS;
    INT  			    wlanIndex    =  -1;
    // Only one of these may be set at a given time
    if ( ( (pWifiApWps->Cfg.X_LGI_COM_ActivatePushButton == TRUE) &&
         ( (strlen(pWifiApWps->Cfg.X_LGI_COM_ClientPin) > 0) ||
           (pWifiApWps->Cfg.X_LGI_COM_CancelSession == TRUE) ) ) ||
         ( (strlen(pWifiApWps->Cfg.X_LGI_COM_ClientPin) > 0) &&
           (pWifiApWps->Cfg.X_LGI_COM_CancelSession == TRUE) ) )
    {
	AnscCopyString(pReturnParamName, "X_LGI_COM_ActivatePushButton");
	*puLength = AnscSizeOfString("X_LGI_COM_ActivatePushButton");
        return FALSE;
    }

	//Verify whether current security mode is in open state or not
	wlanIndex = pWifiAp->AP.Cfg.InstanceNumber - 1;

#if 0
    if( ( 0 == wlanIndex ) || \
	( 1 == wlanIndex )
       )
    {
            if ( ( TRUE == pWifiApWps->Cfg.bEnabled ) && \
	         ( TRUE == CosaDmlWiFiApIsSecmodeOpenForPrivateAP( ) )
	        )
                {
                    AnscCopyString(pReturnParamName, "Enable");
                    *puLength = AnscSizeOfString("Enable");
                    return FALSE;
                }
	}
#endif

    return TRUE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        WPS_Commit
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
WPS_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj      = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp       = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PSINGLE_LINK_ENTRY              pSLinkEntry   = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj  = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid     = (PCOSA_DML_WIFI_SSID      )NULL;
    CHAR                            PathName[64]  = {0};
   
    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
    
    while ( pSLinkEntry )
    {
        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        
        sprintf(PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);
        
        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }
                
        pSLinkEntry             = AnscQueueGetNextEntry(pSLinkEntry);
    }
    
    if ( pSLinkEntry )
    {
        pWifiSsid = pSSIDLinkObj->hContext;
#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        return CosaDmlWiFiApWpsSetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->WPS.Cfg);
#else
        return CosaDmlWiFiApWpsSetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->WPS.Cfg);
#endif
    }
    
    return ANSC_STATUS_FAILURE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        WPS_Rollback
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to roll back the update whenever there's a 
        validation found.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
WPS_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject       = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObjAp      = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp         = (PCOSA_DML_WIFI_AP        )pLinkObjAp->hContext;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObjSsid    = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PSINGLE_LINK_ENTRY              pSLinkEntrySsid = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid       = (PCOSA_DML_WIFI_SSID      )NULL;
    CHAR                            PathName[64]    = {0};

    /*Get the corresponding SSID entry*/
    pSLinkEntrySsid = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
    while ( pSLinkEntrySsid )
    {
        pLinkObjSsid = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntrySsid);
        
        sprintf(PathName, "Device.WiFi.SSID.%lu.", pLinkObjSsid->InstanceNumber);
        
        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }
                
        pSLinkEntrySsid             = AnscQueueGetNextEntry(pSLinkEntrySsid);
    }
    
    if ( pSLinkEntrySsid )
    {
        pWifiSsid = pLinkObjSsid->hContext;
#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        CosaDmlWiFiApWpsGetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->WPS.Cfg);
#else
        CosaDmlWiFiApWpsGetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->WPS.Cfg);
#endif
    }
	return ANSC_STATUS_SUCCESS;
}

/**********************************************************************

        BOOL
        IsValidMacAddress
            (
                char*                       mac
            );

    description:

        This function is called to check for valid MAC Address.

    argument:   char*                       mac,
                string mac address buffer.

    return:     TRUE if it's valid mac address.
        FALSE if it's invalid 

**********************************************************************/
#if defined(MAC_ADDR_LEN)
    #undef MAC_ADDR_LEN
#endif
#define MAC_ADDR_LEN 17

BOOL IsValidMacAddress(char *mac)
{
    int iter = 0, len = 0;

    len = strlen(mac);
    if(len != MAC_ADDR_LEN) {
	CcspWifiTrace(("RDK_LOG_ERROR, (%s) MACAddress is not valid!!!\n", __func__));
	return FALSE;
    }
    if(mac[2] == ':' && mac[5] == ':' && mac[8] == ':' && mac[11] == ':' && mac[14] == ':') {
	for(iter = 0; iter < MAC_ADDR_LEN; iter++) {
	    if((iter == 2 || iter == 5 || iter == 8 || iter == 11 || iter == 14)) {
		continue;
	    } 
	    else if((mac[iter] > 47 && mac[iter] <= 57) || (mac[iter] > 64 && mac[iter] < 71) || (mac[iter] > 96 && mac[iter] < 103)) {
		continue;
	    }
	    else {
		CcspWifiTrace(("RDK_LOG_ERROR, (%s), MACAdress is not valid\n", __func__));
		return FALSE;
		break;
	    }
	}
    } else {
	CcspWifiTrace(("RDK_LOG_ERROR, (%s), MACAdress is not valid\n", __func__));
	return FALSE;
    }

    return TRUE;
}


#if defined (FEATURE_SUPPORT_INTERWORKING)

/***********************************************************************

 APIs for Object:

    WiFi.AccessPoint.{i}.X_RDKCENTRAL-COM_InterworkingElement.

    *  InterworkingElement_GetParamBoolValue
    *  InterworkingElement_GetParamIntValue
    *  InterworkingElement_GetParamUlongValue
    *  InterworkingElement_GetParamStringValue
    *  InterworkingElement_SetParamBoolValue
    *  InterworkingElement_SetParamIntValue
    *  InterworkingElement_SetParamUlongValue
    *  InterworkingElement_SetParamStringValue
    *  InterworkingElement_Validate
    *  InterworkingElement_Commit
    *  InterworkingElement_Rollback

***********************************************************************/
/**********************************************************************  

    caller:     owner of this object

    prototype:

        BOOL
        InterworkingElement_GetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:

        This function is called to retrieve Boolean parameter value;

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL*                       pBool
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
InterworkingElement_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{   
 
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Internet") == 0)
    {
        /* collect value */
        if((pWifiAp->AP.Cfg.InstanceNumber == 5) || (pWifiAp->AP.Cfg.InstanceNumber == 6) || (pWifiAp->AP.Cfg.InstanceNumber == 9) || (pWifiAp->AP.Cfg.InstanceNumber == 10) )
        {
            CosaDmlWiFi_GetInterworkingInternetAvailable(pBool);
            if(*pBool)
            {
                pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iInternetAvailable = 1;
            }
            else
            {
                pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iInternetAvailable = 0;
            }
            return TRUE;
        }
        else
        {
            *pBool = pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iInternetAvailable;
            return TRUE;
        }
    }
    
    if (strcmp(ParamName, "ASRA") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iASRA;
        return TRUE;
    }

    if (strcmp(ParamName, "ESR") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iESR;
        return TRUE;
    }

    if (strcmp(ParamName, "UESA") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iUESA;
        return TRUE;
    }

   if (strcmp(ParamName, "VenueOptionPresent") == 0)
     {
        *pBool = pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iVenueOptionPresent;
        return TRUE;
    }

    if (strcmp(ParamName, "HESSOptionPresent") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iHESSOptionPresent;
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}


/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        InterworkingElement_GetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int*                        pInt
            );

    description:

        This function is called to retrieve integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int*                        pInt
                The buffer of returned integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
InterworkingElement_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(pInt);
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        InterworkingElement_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
InterworkingElement_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "AccessNetworkType") == 0)
    {
        /* collect value */
        *puLong = pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iAccessNetworkType;
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        InterworkingElement_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
InterworkingElement_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{   
 
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;


    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "HESSID") == 0)
    {
        /* collect value */
        AnscCopyString(pValue, pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iHESSID);
        return 0;
    }
    
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return -1;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        InterworkingElement_SetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL                        bValue
            );

    description:

        This function is called to set BOOL parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL                        bValue
                The updated BOOL value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
InterworkingElement_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{    
 
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Internet") == 0)
    {   
        pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iInternetAvailable = bValue; 
        return TRUE;
    }
    
    if (strcmp(ParamName, "ASRA") == 0)
    {
        pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iASRA = bValue;
        return TRUE;
    }

    if (strcmp(ParamName, "ESR") == 0)
    {
        pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iESR = bValue;
        return TRUE;
    }

    if (strcmp(ParamName, "UESA") == 0)
    {
        pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iUESA = bValue;
        return TRUE;
    }

    if (strcmp(ParamName, "VenueOptionPresent") == 0)
    {
        pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iVenueOptionPresent = bValue;
        return TRUE;
    }

    if (strcmp(ParamName, "HESSOptionPresent") == 0)
    {
        pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iHESSOptionPresent = bValue;
        return TRUE;
    }


    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
    
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        InterworkingElement_SetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int                         iValue
            );

    description:

        This function is called to set integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int                         iValue
                The updated integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
InterworkingElement_SetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int                         iValue
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(iValue); 
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        InterworkingElement_SetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG                       uValue
            );

    description:

        This function is called to set ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG                       uValue
                The updated ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
InterworkingElement_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{   
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "AccessNetworkType") == 0)
    {
        if ((uValue < 6) || ((uValue < 16) && (uValue > 13)))
        {
            pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iAccessNetworkType = uValue;
            return TRUE;
        }
    }

    return FALSE;


}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        InterworkingElement_SetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pString
            );

    description:

        This function is called to set string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pString
                The updated string value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
InterworkingElement_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{    
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;


    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "HESSID") == 0)
    {
        /* collect value */
        AnscCopyString(pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iHESSID, pString);
        return TRUE;
    }
    
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        InterworkingElement_Validate
            (
                ANSC_HANDLE                 hInsContext,
                char*                       pReturnParamName,
                ULONG*                      puLength
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       pReturnParamName,
                The buffer (128 bytes) of parameter name if there's a validation. 

                ULONG*                      puLength
                The output length of the param name. 

    return:     TRUE if there's no validation.

**********************************************************************/
BOOL
InterworkingElement_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{   
    PCOSA_DML_WIFI_AP_CFG pCfg = (PCOSA_DML_WIFI_AP_CFG)hInsContext;
    PCOSA_DML_WIFI_INTERWORKING_CFG pIntworkingCfg = &pCfg->IEEE80211uCfg.IntwrkCfg;
    BOOL validated = TRUE;

    //VenueGroup must be greater or equal to 0 and less than 12
    if (!((pIntworkingCfg->iVenueGroup < 12) && (pIntworkingCfg->iVenueGroup >= 0))) {
	AnscCopyString(pReturnParamName, "Group");
	*puLength = AnscSizeOfString("Group");
	CcspWifiTrace(("RDK_LOG_ERROR,(%s), VenueGroup validation error!!!\n", __func__));
	validated = FALSE;
    }
    else //VenueType must be as per specifications from WiFi Alliance for every valid value of vnue group
    {
        int updateInvalidType = 0;
        if ((pIntworkingCfg->iVenueType < 256) && (pIntworkingCfg-> iVenueType >= 0))
        {
            switch (pIntworkingCfg->iVenueGroup)
            {
                case 0:
                    if (pIntworkingCfg->iVenueType != 0)
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 1:
                    if (!((pIntworkingCfg->iVenueType < 16) && (pIntworkingCfg-> iVenueType >= 0)))
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 2:
                    if (!((pIntworkingCfg->iVenueType < 10) && (pIntworkingCfg-> iVenueType >= 0)))
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 3:
                    if (!((pIntworkingCfg->iVenueType < 4) && (pIntworkingCfg-> iVenueType >= 0)))
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 4:
                    if (!((pIntworkingCfg->iVenueType < 2) && (pIntworkingCfg-> iVenueType >= 0)))
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 5:
                    if (!((pIntworkingCfg->iVenueType < 6) && (pIntworkingCfg-> iVenueType >= 0)))
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 6:
                    if (!((pIntworkingCfg->iVenueType < 6) && (pIntworkingCfg-> iVenueType >= 0)))
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 7:
                    if (!((pIntworkingCfg->iVenueType < 5) && (pIntworkingCfg-> iVenueType >= 0)))
                    {
                        updateInvalidType = 1;
                    }
                    break;

                case 8:
                    if (pIntworkingCfg->iVenueType != 0)
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 9:
                    if (pIntworkingCfg->iVenueType != 0)
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 10:
                    if (!((pIntworkingCfg->iVenueType < 8) && (pIntworkingCfg-> iVenueType >= 0)))
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 11:
                    if (!((pIntworkingCfg->iVenueType < 7) && (pIntworkingCfg-> iVenueType >= 0)))
                    {
                        updateInvalidType = 1;
                    }
                    break;
            }
        }
        else
        {
            updateInvalidType = 1;
        }

        if(updateInvalidType)
        {
            AnscCopyString(pReturnParamName, "Type");
            *puLength = AnscSizeOfString("Type");
            CcspWifiTrace(("RDK_LOG_ERROR,(%s), VenueType validation error!!!\n", __func__));
            validated = FALSE;
        }

    }
    //AccessNetworkType must be greater or equal to 0 and less than 16
	 if (!(((pIntworkingCfg->iAccessNetworkType < 6) && (pIntworkingCfg->iAccessNetworkType >= 0)) || ((pIntworkingCfg->iAccessNetworkType < 16) && (pIntworkingCfg->iAccessNetworkType > 13)))) 
     {
         AnscCopyString(pReturnParamName, "AccessNetworkType");
         *puLength = AnscSizeOfString("AccessNetworkType");
         CcspWifiTrace(("RDK_LOG_ERROR,(%s), AccessNetworkType validation error!!!\n", __func__));
         validated = FALSE;        
     }

    //InternetAvailable must be greater or equal to 0 and less than 2
    if ((pIntworkingCfg->iInternetAvailable < 0) || (pIntworkingCfg->iInternetAvailable > 1)) {
	AnscCopyString(pReturnParamName, "InternetAvailable");
	*puLength = AnscSizeOfString("InternetAvailable");
	CcspWifiTrace(("RDK_LOG_ERROR,(%s), Internet validation error!!!\n", __func__));
	validated = FALSE;        
    } 

    //ASRA must be greater or equal to 0 and less than 2
    if ((pIntworkingCfg->iASRA < 0) || (pIntworkingCfg->iASRA > 1)) {
	AnscCopyString(pReturnParamName, "ASRA");
	*puLength = AnscSizeOfString("ASRA");
	CcspWifiTrace(("RDK_LOG_ERROR,(%s), ASRA validation error!!!\n", __func__));
	validated = FALSE; 
    } 

    //ESR must be greater or equal to 0 and less than 2
    if ((pIntworkingCfg->iESR < 0) || (pIntworkingCfg->iESR > 1)) {
	AnscCopyString(pReturnParamName, "ESR");
	*puLength = AnscSizeOfString("ESR");
	CcspWifiTrace(("RDK_LOG_ERROR,(%s), ESR validation error!!!\n", __func__));
	validated = FALSE;        
    } 

    //UESA must be greater or equal to 0 and less than 2
    if ((pIntworkingCfg->iUESA < 0) || (pIntworkingCfg->iUESA > 1)) {
	AnscCopyString(pReturnParamName, "UESA");
	*puLength = AnscSizeOfString("UESA");
	CcspWifiTrace(("RDK_LOG_ERROR,(%s), UESA validation error!!!\n", __func__));
	validated = FALSE;        
    } 

    //VenueOptionPresent must be greater or equal to 0 and less than 2
    if ((pIntworkingCfg->iVenueOptionPresent < 0) || (pIntworkingCfg->iVenueOptionPresent > 1)) {
	AnscCopyString(pReturnParamName, "VenueOptionPresent");
	*puLength = AnscSizeOfString("VenueOptionPresent");
	CcspWifiTrace(("RDK_LOG_ERROR,(%s), VenueOption validation error!!!\n", __func__));
	validated = FALSE;        
    } 


    if (pIntworkingCfg->iHESSOptionPresent == TRUE) {
        /*Check for Valid Mac Address*/
	    if (IsValidMacAddress(pIntworkingCfg->iHESSID) != TRUE) {
	    CcspWifiTrace(("RDK_LOG_ERROR,(%s), HESSID validation error!!!\n", __func__));   
	    AnscCopyString(pReturnParamName, "HESSID");
	    *puLength = AnscSizeOfString("HESSID");
	    validated = FALSE;
	}
    }

    return validated;
}




/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        InterworkingElement_Commit
			ANSC_HANDLE                 hInsContext
           );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
InterworkingElement_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj      = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp       = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    if (CosaDmlWiFi_setInterworkingElement(&pWifiAp->AP.Cfg) == ANSC_STATUS_SUCCESS)
    {
        return ANSC_STATUS_SUCCESS;
    }
    
    return ANSC_STATUS_FAILURE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 


        ULONG
        InterworkingElement_Rollback
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to roll back the update whenever there's a 
        validation found.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
InterworkingElement_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    return ANSC_STATUS_SUCCESS;
}

/***********************************************************************

 APIs for Object:

    WiFi.AccessPoint.{i}.X_RDKCENTRAL-COM_InterworkingElement.VenueInfo.

	*	InterworkingElement_Venue_GetParamUlongValue
	*	InterworkingElement_Venue_SetParamUlongValue


***********************************************************************/
BOOL InterworkingElement_Venue_GetParamUlongValue
     (
         ANSC_HANDLE                 hInsContext,
         char*                       ParamName,
         ULONG*                      puLong
     )
 {
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
  
    if (strcmp(ParamName, "Type") == 0)
    {
        /* collect value */
        *puLong = pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iVenueType;
        return TRUE;
    }

    if (strcmp(ParamName, "Group") == 0)
    {
        /* collect value */
        *puLong = pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iVenueGroup;
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        InterworkingElement_Venue_SetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG                       uValue
            );

    description:

        This function is called to set ULONG parameter value;

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG                       uValue
                The updated ULONG value;

    return:     TRUE if succeeded.

************************************************************/
BOOL
InterworkingElement_Venue_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;

    if (strcmp(ParamName, "Type") == 0)
    {
        int updateInvalidType = 0;
        if (uValue < 256)
        {
            switch (pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iVenueGroup)
            {
                case 0:
                    if (uValue != 0)
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 1:
                    if (!(uValue < 16))
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 2:
                    if (!(uValue < 10))
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 3:
                    if (!(uValue < 4))
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 4:
                    if (!(uValue < 2))
                    {
                        updateInvalidType = 1;
                    }
                    break;

                case 5:
                    if (!(uValue < 6))
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 6:
                    if (!(uValue < 6))
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 7:
                    if (!(uValue < 5))
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 8:
                    if (uValue != 0)
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 9:
                    if (uValue != 0)
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 10:
                    if (!(uValue < 8))
                    {
                        updateInvalidType = 1;
                    }
                    break;
                case 11:
                    if (!(uValue < 7))
                    {
                        updateInvalidType = 1;
                    }
                    break;
            }
        }
        else 
        {
            updateInvalidType = 1;
        }


        if (! updateInvalidType)
        {
            pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iVenueType = uValue;
            return TRUE;
        }

    }
    if (strcmp(ParamName, "Group") == 0)
    {
        if (uValue < 12)
        {
            pWifiAp->AP.Cfg.IEEE80211uCfg.IntwrkCfg.iVenueGroup = uValue;
            return TRUE;
        }
    }

       /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;


}

#else // For all non xb3/dual core platforms that do have full support for interworking, we are writting stub functions
BOOL
InterworkingElement_GetParamBoolValue
(
 ANSC_HANDLE                 hInsContext,
 char*                       ParamName,
 BOOL*                       pBool
)
{
    UNREFERENCED_PARAMETER(hInsContext);
    if (strcmp(ParamName, "Internet") == 0)
    {
        *pBool = false;
        return TRUE;
    }

    if (strcmp(ParamName, "ASRA") == 0)
    {
        *pBool = false;
        return TRUE;
    }
    if (strcmp(ParamName, "ESR") == 0)
    {
        *pBool = false;
        return TRUE;
    }
    if (strcmp(ParamName, "UESA") == 0)
    {
        *pBool = false;
        return TRUE;
    }
    if (strcmp(ParamName, "VenueOptionPresent") == 0)
    {
        *pBool = false;
        return TRUE;
    }
    if (strcmp(ParamName, "HESSOptionPresent") == 0)
    {
        *pBool = false;
        return TRUE;
    }
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

BOOL
InterworkingElement_GetParamIntValue
(
 ANSC_HANDLE                 hInsContext,
 char*                       ParamName,
 int*                        pInt
)
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(pInt);
    //  // no param implemented in actual API.
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

BOOL
InterworkingElement_GetParamUlongValue
(
 ANSC_HANDLE                 hInsContext,
 char*                       ParamName,
 ULONG*                      puLong
)
{
    UNREFERENCED_PARAMETER(hInsContext);
    if (strcmp(ParamName, "AccessNetworkType") == 0)
    {
        *puLong = 0;
        return TRUE;
    }
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

ULONG
InterworkingElement_GetParamStringValue
(
 ANSC_HANDLE                 hInsContext,
 char*                       ParamName,
 char*                       pValue,
 ULONG*                      pUlSize
)
{
    UNREFERENCED_PARAMETER(hInsContext);
    if (strcmp(ParamName, "HESSID") == 0)
    {
        AnscCopyString(pValue, "no support for non xb3");
        return 0;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return -1;
}

BOOL
InterworkingElement_Venue_GetParamUlongValue
(
 ANSC_HANDLE                 hInsContext,
 char*                       ParamName,
 ULONG*                      puLong
)
{
    UNREFERENCED_PARAMETER(hInsContext);
    if (strcmp(ParamName, "Type") == 0)
    {
        /* collect value */
        *puLong = 0;
        return TRUE;
    }

    if (strcmp(ParamName, "Group") == 0)
    {
        /* collect value */
        *puLong = 0;
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

#endif // (DUAL_CORE_XB3) || (_XB6_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_))

/***********************************************************************

 APIs for Object:

    WiFi.X_RDKCENTRAL-COM_GASConfig.{i}.

    *   GASConfig_GetEntryCount
    *   GASConfig_GetEntry
    *   GASConfig_AddEntry
    *   GASConfig_DelEntry
    *   GASConfig_GetParamBoolValue
    *   GASConfig_GetParamUlongValue

***********************************************************************/

/***********************************************************************


    caller:     owner of this object

    prototype:

        ULONG
        GASConfig_GetEntryCount
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to retrieve the count of the table.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The count of the table

**********************************************************************/

ULONG
GASConfig_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    ULONG                           GAS_ADVCount    = 1;
    UNREFERENCED_PARAMETER(hInsContext);
    return GAS_ADVCount;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        ANSC_HANDLE
                GASConfig_GetEntry
            (
                ANSC_HANDLE                 hInsContext,
                ULONG                       nIndex,
                ULONG*                      pInsNumber
            );

    description:

        This function is called to retrieve the entry specified by the index.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                ULONG                       nIndex,
                The index of this entry;

                ULONG*                      pInsNumber
                The output instance number;

    return:     The handle to identify the entry

**********************************************************************/

ANSC_HANDLE
GASConfig_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    if (nIndex == 0)
    {
        PCOSA_DATAMODEL_WIFI    pMyObject               = ( PCOSA_DATAMODEL_WIFI )g_pCosaBEManager->hWifi;
        *pInsNumber  = nIndex + 1;
        return (ANSC_HANDLE)&pMyObject->GASCfg[0];
    }
    return (ANSC_HANDLE)NULL;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        GASConfig_GetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:

        This function is called to retrieve Boolean parameter value;

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;
        
                char*                       ParamName,
                The parameter name;

                BOOL*                       pBool
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/

BOOL
GASConfig_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    PCOSA_DML_WIFI_GASCFG  pGASconf   = (PCOSA_DML_WIFI_GASCFG)hInsContext;

    if (strcmp(ParamName, "PauseForServerResponse") == 0)
    {
        /* collect value */
        *pBool  = pGASconf->PauseForServerResponse;
        return TRUE;
    }

    return FALSE;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        GASConfig_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve Integer parameter value;

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;
        
                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/

BOOL
GASConfig_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    PCOSA_DML_WIFI_GASCFG  pGASconf   = (PCOSA_DML_WIFI_GASCFG)hInsContext;

    /* collect value */
    if (strcmp(ParamName, "AdvertisementID") == 0)
    {
        *puLong  = pGASconf->AdvertisementID;

        return TRUE;
    }
    if (strcmp(ParamName, "ResponseTimeout") == 0)
    {
        *puLong  = pGASconf->ResponseTimeout;

        return TRUE;
    }
    if (strcmp(ParamName, "ComeBackDelay") == 0)
    {
        *puLong  = pGASconf->ComeBackDelay;

        return TRUE;
    }
    if (strcmp(ParamName, "ResponseBufferingTime") == 0)
    {
        *puLong  = pGASconf->ResponseBufferingTime;

        return TRUE;
    }
    if (strcmp(ParamName, "QueryResponseLengthLimit") == 0)
    {
        *puLong  = pGASconf->QueryResponseLengthLimit;

        return TRUE;
    }

    return FALSE;
}

/***********************************************************************

 APIs for Object:

    WiFi.X_RDKCENTRAL-COM_GASStats.{i}.

    *   GASStats_GetEntryCount
    *   GASStats_GetEntry
    *   GASStats_AddEntry
    *   GASStats_DelEntry
    *   GASStats_GetParamUlongValue

***********************************************************************/

/***********************************************************************


    caller:     owner of this object

    prototype:

        ULONG
        GASStats_GetEntryCount
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to retrieve the count of the table.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The count of the table

**********************************************************************/

ULONG
GASStats_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    ULONG                           GAS_ADVCount    = 1;
    UNREFERENCED_PARAMETER(hInsContext);
    return GAS_ADVCount;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        ANSC_HANDLE
            GASStats_GetEntry
            (
                ANSC_HANDLE                 hInsContext,
                ULONG                       nIndex,
                ULONG*                      pInsNumber
            );

    description:

        This function is called to retrieve the entry specified by the index.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                ULONG                       nIndex,
                The index of this entry;

                ULONG*                      pInsNumber
                The output instance number;

    return:     The handle to identify the entry

**********************************************************************/

ANSC_HANDLE
GASStats_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    if (nIndex == 0)
    {
        PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
        *pInsNumber  = nIndex + 1;
        return (ANSC_HANDLE)&pMyObject->GASStats[0];
    }
    return (ANSC_HANDLE)NULL;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        GASStats_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve Integer parameter value;

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;
        
                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/

BOOL
GASStats_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    PCOSA_DML_WIFI_GASSTATS  pGASStats   = (PCOSA_DML_WIFI_GASSTATS)hInsContext;

    if(ANSC_STATUS_SUCCESS != CosaDmlWiFi_GetGasStats((PANSC_HANDLE)pGASStats)){
        return FALSE;
    }
    
    /* collect value */
    if (strcmp(ParamName, "AdvertisementID") == 0)
    {
        *puLong  = pGASStats->AdvertisementID;

        return TRUE;
    }
    if (strcmp(ParamName, "Queries") == 0)
    {
        *puLong  = pGASStats->Queries;

        return TRUE;
    }
    if (strcmp(ParamName, "QueryRate") == 0)
    {
        *puLong  = pGASStats->QueryRate;

        return TRUE;
    }
    if (strcmp(ParamName, "Responses") == 0)
    {
        *puLong  = pGASStats->Responses;

        return TRUE;
    }
    if (strcmp(ParamName, "ResponseRate") == 0)
    {
        *puLong  = pGASStats->ResponseRate;

        return TRUE;
    }
    if (strcmp(ParamName, "NoRequestOutstanding") == 0)
    {
        *puLong  = pGASStats->NoRequestOutstanding;

        return TRUE;
    }
    if (strcmp(ParamName, "ResponsesDiscarded") == 0)
    {
        *puLong  = pGASStats->ResponsesDiscarded;

        return TRUE;
    }
    if (strcmp(ParamName, "FailedResponses") == 0)
    {
        *puLong  = pGASStats->FailedResponses;

        return TRUE;
    }

    return FALSE;
}

/***********************************************************************

 APIs for Object:

    WiFi.AccessPoint.{i}.X_CISCO_COM_MACFilter.

    *  MacFilter_GetParamBoolValue
    *  MacFilter_GetParamIntValue
    *  MacFilter_GetParamUlongValue
    *  MacFilter_GetParamStringValue
    *  MacFilter_SetParamBoolValue
    *  Macfilter_SetParamIntValue
    *  MacFilter_SetParamUlongValue
    *  MacFilter_SetParamStringValue
    *  MacFilter_Validate
    *  MacFilter_Commit
    *  MacFilter_Rollback

***********************************************************************/
/**********************************************************************  

    caller:     owner of this object

    prototype:

        BOOL
        MacFilter_GetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:

        This function is called to retrieve Boolean parameter value;

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL*                       pBool
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
MacFilter_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_AP_MF_CFG        pWifiApMf    = &pWifiAp->MF;

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Enable") == 0)
    {
        /* collect value */
        *pBool = pWifiApMf->bEnabled;
        return TRUE;
    }
    
    if (strcmp(ParamName, "FilterAsBlackList") == 0)
    {
        /* collect value */
        *pBool = pWifiApMf->FilterAsBlackList;
        return TRUE;
    }


    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}


/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Macfilter_GetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int*                        pInt
            );

    description:

        This function is called to retrieve integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int*                        pInt
                The buffer of returned integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
MacFilter_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(pInt);
    /* check the parameter name and return the corresponding value */

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        MacFilter_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
MacFilter_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(puLong);
    /* check the parameter name and return the corresponding value */

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}


/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        MacFilter_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
MacFilter_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_AP_MF_CFG        pWifiApMf    = &pWifiAp->MF;
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "FilteringMode") == 0)
    {
        if ( pWifiApMf->bEnabled == TRUE )
        {
            if ( pWifiApMf->FilterAsBlackList == TRUE )
            {
                AnscCopyString(pWifiAp->AP.Cfg.MacFilterMode,"Deny");
            }
            else
            {
                AnscCopyString(pWifiAp->AP.Cfg.MacFilterMode,"Allow");
            }
        }
        else
        {
            AnscCopyString(pWifiAp->AP.Cfg.MacFilterMode,"Allow-ALL");
        }
        /* collect value */

        AnscCopyString(pValue, pWifiAp->AP.Cfg.MacFilterMode);
        return 0;
    }
#if 0	
    if (strcmp(ParamName, "MACAddress") == 0)
    {
        /* collect value */
        char maclist[1024] = {'\0'};
        CosaDmlWiFiApMfGetMacList(pWifiApMf->MacAddrList, maclist, pWifiApMf->NumberMacAddrList);
        AnscCopyString(pValue, maclist);
        *pUlSize = AnscSizeOfString(pValue); 

        return 0;
    }
#endif
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return -1;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        MacFilter_SetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL                        bValue
            );

    description:

        This function is called to set BOOL parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL                        bValue
                The updated BOOL value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
MacFilter_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_AP_MF_CFG        pWifiApMf    = &pWifiAp->MF;

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "Enable") == 0)
    {
        /* save update to backup */
        pWifiApMf->bEnabled = bValue;
        return TRUE;
    }
    if (strcmp(ParamName, "FilterAsBlackList") == 0)
    {
         /* save update to backup */
	     pWifiApMf->FilterAsBlackList = bValue;
         return TRUE;
    }


    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        MacFilter_SetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int                         iValue
            );

    description:

        This function is called to set integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int                         iValue
                The updated integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
MacFilter_SetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int                         iValue
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(iValue);
    /* check the parameter name and set the corresponding value */

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        MacFilter_SetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG                       uValue
            );

    description:

        This function is called to set ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG                       uValue
                The updated ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
MacFilter_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(uValue);
    /* check the parameter name and set the corresponding value */

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        MacFilter_SetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pString
            );

    description:

        This function is called to set string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pString
                The updated string value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
MacFilter_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(pString);
    #if 0
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_AP_MF_CFG        pWifiApMf    = &pWifiAp->MF;
    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "MACAddress") == 0)
    {
        if(pWifiApMf->bEnabled == TRUE)
	{
            CosaDmlWiFiApMfSetMacList(pString, pWifiApMf->MacAddrList, &pWifiApMf->NumberMacAddrList);
            return TRUE;
        }
        else
        {
            return FALSE;
        }
    }
  #endif
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        MacFilter_Validate
            (
                ANSC_HANDLE                 hInsContext,
                char*                       pReturnParamName,
                ULONG*                      puLength
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       pReturnParamName,
                The buffer (128 bytes) of parameter name if there's a validation. 

                ULONG*                      puLength
                The output length of the param name. 

    return:     TRUE if there's no validation.

**********************************************************************/
BOOL
MacFilter_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(pReturnParamName);
    UNREFERENCED_PARAMETER(puLength);  
    return TRUE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        MacFilter_Commit
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
MacFilter_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj      = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp       = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PSINGLE_LINK_ENTRY              pSLinkEntry   = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj  = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid     = (PCOSA_DML_WIFI_SSID      )NULL;
    CHAR                            PathName[64]  = {0};
   
    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
    
    while ( pSLinkEntry )
    {
        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        
        sprintf(PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);
        
        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }
                
        pSLinkEntry             = AnscQueueGetNextEntry(pSLinkEntry);
    }
    
    if ( pSLinkEntry )
    {
        pWifiSsid = pSSIDLinkObj->hContext;
#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        return CosaDmlWiFiApMfSetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->MF);
#else
        return CosaDmlWiFiApMfSetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->MF);
#endif
    }
    
    return ANSC_STATUS_FAILURE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        MacFilter_Rollback
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to roll back the update whenever there's a 
        validation found.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
MacFilter_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject       = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObjAp      = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp         = (PCOSA_DML_WIFI_AP        )pLinkObjAp->hContext;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObjSsid    = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PSINGLE_LINK_ENTRY              pSLinkEntrySsid = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid       = (PCOSA_DML_WIFI_SSID      )NULL;
    CHAR                            PathName[64]    = {0};

    /*Get the corresponding SSID entry*/
    pSLinkEntrySsid = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
    while ( pSLinkEntrySsid )
    {
        pLinkObjSsid = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntrySsid);
        
        sprintf(PathName, "Device.WiFi.SSID.%lu.", pLinkObjSsid->InstanceNumber);
        
        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }
                
        pSLinkEntrySsid             = AnscQueueGetNextEntry(pSLinkEntrySsid);
    }
    
    if ( pSLinkEntrySsid )
    {
        pWifiSsid = pLinkObjSsid->hContext;
#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_)  && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        CosaDmlWiFiApMfGetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->MF);
#else
        CosaDmlWiFiApMfGetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->MF);
#endif
    }
    
    return ANSC_STATUS_SUCCESS;
}

/***********************************************************************

 APIs for Object:

    WiFi.AccessPoint.{i}X_RDKCENTRAL-COM_DPP.

    *  DPP_GetParamBoolValue
    *  DPP_GetParamIntValue
    *  DPP_GetParamUlongValue
    *  DPP_GetParamStringValue
    *  DPP_SetParamBoolValue
    *  DPP_SetParamIntValue
    *  DPP_SetParamUlongValue
    *  DPP_SetParamStringValue
    *  DPP_Validate
    *  DPP_Commit
    *  DPP_Rollback

***********************************************************************/
/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        DPP_Validate
            (
                ANSC_HANDLE                 hInsContext,
                char*                       pReturnParamName,
                ULONG*                      puLength
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       pReturnParamName,
                The buffer (128 bytes) of parameter name if there's a validation.

                ULONG*                      puLength
                The output length of the param name.

    return:     TRUE if there's no validation.

**********************************************************************/
BOOL
DPP_Validate
    (   
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(pReturnParamName);
    UNREFERENCED_PARAMETER(puLength);
    return TRUE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        DPP_Commit
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
DPP_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    return ANSC_STATUS_FAILURE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        DPP_Rollback
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to roll back the update whenever there's a 
        validation found.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
DPP_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    return ANSC_STATUS_SUCCESS;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        DPP_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
DPP_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_DPP_CFG          pWifiDpp  = (PCOSA_DML_WIFI_DPP_CFG)&pWifiAp->DPP;
    if (strcmp(ParamName, "Version") == 0)
    {
        *puLong = pWifiDpp->Version;
        return TRUE;
    }
#else
    if (strcmp(ParamName, "Version") == 0)
    {
        *puLong = 0;
        return TRUE;
    }
#endif // !defined(_HUB4_PRODUCT_REQ_)
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(puLong);
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        DPP_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
DPP_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_DPP_CFG          pWifiDpp  = (PCOSA_DML_WIFI_DPP_CFG)&pWifiAp->DPP;
    if (strcmp(ParamName, "PrivateSigningKey") == 0)
    {
        if( AnscSizeOfString(pWifiDpp->Recfg.PrivateSigningKey) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiDpp->Recfg.PrivateSigningKey);
            return 0;
        }
        else {
            *pUlSize = AnscSizeOfString(pWifiDpp->Recfg.PrivateSigningKey) +1;
            return 1;
        }
    }
    if (strcmp(ParamName, "PrivateReconfigAccessKey") == 0)
    {
        if( AnscSizeOfString(pWifiDpp->Recfg.PrivateReconfigAccessKey) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiDpp->Recfg.PrivateReconfigAccessKey);
            return 0;
        }
        else {
            *pUlSize = AnscSizeOfString(pWifiDpp->Recfg.PrivateReconfigAccessKey) +1;
            return 1;
        }
    }
#else
    if (strcmp(ParamName, "PrivateSigningKey") == 0)
    {
        AnscCopyString(pValue, "");
        return 0;
    }
    if (strcmp(ParamName, "PrivateReconfigAccessKey") == 0)
    {
        AnscCopyString(pValue, "");
        return 0;
    }
#endif // !defined(_HUB4_PRODUCT_REQ_)
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(pValue);
    UNREFERENCED_PARAMETER(pUlSize);
    return -1;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        DPP_SetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG                       uValue
            );

    description:

        This function is called to set ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG                       uValue
                The updated ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
DPP_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_DPP_CFG          pWifiDpp  = (PCOSA_DML_WIFI_DPP_CFG)&pWifiAp->DPP;
    ULONG apIns = pWifiAp->AP.Cfg.InstanceNumber;

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "Version") == 0)
    {
        pWifiDpp->Version = uValue;
        if(ANSC_STATUS_SUCCESS != CosaDmlWiFi_setDppVersion(apIns,uValue)){
            return FALSE;
        }
        return TRUE;
    }
#else // !defined(_HUB4_PRODUCT_REQ_)
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(uValue);
#endif
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        DPP_SetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pString
            );

    description:

        This function is called to set string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pString
                The updated string value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
DPP_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_DPP_CFG          pWifiDpp  = (PCOSA_DML_WIFI_DPP_CFG)&pWifiAp->DPP;
    ULONG apIns = pWifiAp->AP.Cfg.InstanceNumber;
    if (strcmp(ParamName, "PrivateSigningKey") == 0)
    {
        if (AnscSizeOfString(pString) > (sizeof(pWifiDpp->Recfg.PrivateSigningKey) - 1))
            return FALSE;

        AnscZeroMemory(pWifiDpp->Recfg.PrivateSigningKey, sizeof(pWifiDpp->Recfg.PrivateSigningKey));
        AnscCopyString(pWifiDpp->Recfg.PrivateSigningKey, pString);
        if(ANSC_STATUS_SUCCESS != CosaDmlWiFi_setDppReconfig(apIns,ParamName,pString)){
            return FALSE;
        }
        return TRUE;
    }
    if (strcmp(ParamName, "PrivateReconfigAccessKey") == 0)
    {
        if (AnscSizeOfString(pString) > (sizeof(pWifiDpp->Recfg.PrivateReconfigAccessKey) - 1))
            return FALSE;

        AnscZeroMemory(pWifiDpp->Recfg.PrivateReconfigAccessKey, sizeof(pWifiDpp->Recfg.PrivateReconfigAccessKey));
        AnscCopyString(pWifiDpp->Recfg.PrivateReconfigAccessKey, pString);
        if(ANSC_STATUS_SUCCESS != CosaDmlWiFi_setDppReconfig(apIns,ParamName,pString)){
            return FALSE;
        }
        return TRUE;
    }
#else // !defined(_HUB4_PRODUCT_REQ_)
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(pString);
    UNREFERENCED_PARAMETER(ParamName);
#endif
    return FALSE;
}

/***********************************************************************

 APIs for Object:

    WiFi.AccessPoint.{i}X_RDKCENTRAL-COM_DPP.STA.{i}.

    *  DPP_STA_GetParamBoolValue
    *  DPP_STA_GetParamIntValue
    *  DPP_STA_GetParamUlongValue
    *  DPP_STA_GetParamStringValue
    *  DPP_STA_SetParamBoolValue
    *  DPP_STA_SetParamIntValue
    *  DPP_STA_SetParamUlongValue
    *  DPP_STA_SetParamStringValue
    *  DPP_STA_Validate
    *  DPP_STA_Commit
    *  DPP_STA_Rollback

***********************************************************************/

#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
static void wifi_dpp_dml_dbg_print(int level, char *format, ...)
{
    UNREFERENCED_PARAMETER(level);
    char buff[2048] = {0};
    va_list list;
    static FILE *fpg = NULL;

    if ((access("/nvram/wifiMonDbg", R_OK)) != 0) {
        return;
    }

    get_formatted_time(buff);
    strcat(buff, " ");

    va_start(list, format);
    vsprintf(&buff[strlen(buff)], format, list);
    va_end(list);

    if (fpg == NULL) {
        fpg = fopen("/tmp/wifiDMCLI", "a+");
        if (fpg == NULL) {
            return;
        } else {
            fputs(buff, fpg);
        }
    } else {
        fputs(buff, fpg);
    }

    fflush(fpg);
}

#if 0
static int get_channel(const char* char_in, int *channels, int size)
{
    int count = 0;
    char *tmp = NULL;

    if(strlen(char_in))
    {
        tmp=strtok((char *)char_in, ",");
        while (tmp != NULL)
        {
            channels[count] = atoi(tmp);
            tmp = strtok(NULL, ",");
            if((tmp != NULL) && (size > count))
                count++;
            else
                break;
        }
    }
    return count;
}
#endif

static BOOL IsValidChannel(int apIndex, int channel)
{
    BOOL ret = FALSE;
    ULONG IsChanHome = 0;

    switch(channel)
    {
        case 1 ... 11: //2.4 Ghz
        {
            ret = ((apIndex == 0) ? TRUE: FALSE);
        }
        break;
        case 36:
        case 38:
        case 40:
        case 42:
        case 44:
        case 46:
        case 48:
        {
            ret = ((apIndex == 1) ? TRUE: FALSE);
        } 
        break;
        case 50: //DFS
        case 52: //DFS
        case 54: //DFS
        case 56: //DFS
        case 58: //DFS
        case 60: //DFS
        case 62: //DFS
        case 64: //DFS
	{
		wifi_getRadioChannel(apIndex, &IsChanHome);
		if((ULONG)channel == IsChanHome) {
			ret = TRUE;
		}
		else {
			ret = FALSE; //don't allow DFS ch if not home channel
		}
	}
        break;
        case 68: //UNII-2e
        case 96: //UNII-3
        {
            ret = FALSE;
        }
        break;
        case 100: //DFS
        case 102: //DFS
        case 104: //DFS
        case 106:
        case 108: //DFS
        case 110:
        case 112: //DFS
        case 114:
        case 116: //DFS
        case 118:
        case 120: //DFS
        case 122:
        case 124: //DFS
        case 126:
        case 128: //DFS
        case 132: //DFS
        case 134:
        case 136: //DFS
        case 138:
        case 140: //DFS
        case 142:
        case 144:
	{
		wifi_getRadioChannel(apIndex, &IsChanHome);
		if((ULONG)channel == IsChanHome) {
			ret = TRUE;
		}
		else {
			ret = FALSE; //don't allow DFS ch if not home channel
		}
	}
        break;
        case 149:
        case 151:
        case 153:
        case 155:
        case 157:
        case 159:
        case 161:
        case 165:
        {
            ret = ((apIndex == 1) ? TRUE: FALSE);
        }
        break;
        default:
            ret = FALSE;
        break;
    }

    return ret;  
}

static ANSC_STATUS GetInsNumsByWifiDppSta(PCOSA_DML_WIFI_DPP_STA_CFG pWifiDppSta, ULONG *apIns, ULONG *dppStaIdx, UCHAR *dppVersion)
{
    PCOSA_DATAMODEL_WIFI        pWiFi       = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PSINGLE_LINK_ENTRY          pAPLink     = NULL;
    PCOSA_CONTEXT_LINK_OBJECT   pAPLinkObj  = NULL;
    PCOSA_DML_WIFI_AP           pWiFiAP     = NULL;
    int                         i;

    /* for each Device.WiFi.AccessPoint.{i}. */
    for (   pAPLink = AnscSListGetFirstEntry(&pWiFi->AccessPointQueue);
            pAPLink != NULL;
            pAPLink = AnscSListGetNextEntry(pAPLink)
        )
    {
        pAPLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pAPLink);
        if (!pAPLinkObj)
            continue;
        pWiFiAP = (PCOSA_DML_WIFI_AP)pAPLinkObj->hContext;

        /* for each Device.WiFi.AccessPoint.{i}.X_RDKCENTRAL-COM_DPP.STA.{i}. */
        for (i = 0; i < COSA_DML_WIFI_DPP_STA_MAX; i++)
        {
            if ((ANSC_HANDLE)pWifiDppSta == (ANSC_HANDLE)&pWiFiAP->DPP.Cfg[i])
            {
                /* found */
                *apIns = pWiFiAP->AP.Cfg.InstanceNumber;
                *dppStaIdx = i+1;
                *dppVersion = pWiFiAP->DPP.Version;
                return ANSC_STATUS_SUCCESS;
            }
        }
    }
    CcspTraceError(("%s:%d:FAILED\n",__func__, __LINE__));
    return ANSC_STATUS_FAILURE;
}

static PCOSA_DML_WIFI_AP GetApInsByDppSta(PCOSA_DML_WIFI_DPP_STA_CFG pWifiDppSta, ULONG *dppStaIdx)
{
    PCOSA_DATAMODEL_WIFI        pWiFi       = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PSINGLE_LINK_ENTRY          pAPLink     = NULL;
    PCOSA_CONTEXT_LINK_OBJECT   pAPLinkObj  = NULL;
    PCOSA_DML_WIFI_AP           pWiFiAP     = NULL;
    int                         i;

    /* for each Device.WiFi.AccessPoint.{i}. */
    for (   pAPLink = AnscSListGetFirstEntry(&pWiFi->AccessPointQueue);
            pAPLink != NULL;
            pAPLink = AnscSListGetNextEntry(pAPLink)
        )
    {
        pAPLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pAPLink);
        if (!pAPLinkObj)
            continue;
        pWiFiAP = (PCOSA_DML_WIFI_AP)pAPLinkObj->hContext;

        /* for each Device.WiFi.AccessPoint.{i}.X_RDKCENTRAL-COM_DPP.STA.{i}. */
        for (i = 0; i < COSA_DML_WIFI_DPP_STA_MAX; i++)
        {
            if ((ANSC_HANDLE)pWifiDppSta == (ANSC_HANDLE)&pWiFiAP->DPP.Cfg[i])
            {
                /* found */
                *dppStaIdx = i+1;
                return pWiFiAP;
            }
        }
    }
    CcspTraceError(("%s:%d:FAILED\n",__func__, __LINE__));
    return NULL;
}

ULONG
DPP_STA_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    return COSA_DML_WIFI_DPP_STA_MAX;
}

static BOOL DPP_STA_ProvisionStart_Validate(PCOSA_DML_WIFI_DPP_STA_CFG pWifiDppSta)
{

#define LARRAY 32

#if !defined(_BWG_PRODUCT_REQ_)
#if !defined(_XF3_PRODUCT_REQ_) && !defined(_CBR_PRODUCT_REQ_) && !defined(_HUB4_PRODUCT_REQ_) && !defined(_PLATFORM_RASPBERRYPI_) && !defined(_PLATFORM_TURRIS_) && !defined(_LG_MV1_CELENO_)
    int asn1len;
    const unsigned char *key;
    ULONG apIns, staIndex;
    char buff[512];
    UCHAR dppVersion;
    unsigned char keyasn1[1024];
    wifi_dpp_dml_dbg_print(1, "%s:%d: Enter!!!\n", __func__, __LINE__);

    if (GetInsNumsByWifiDppSta(pWifiDppSta, &apIns, &staIndex, &dppVersion) != ANSC_STATUS_SUCCESS) {
        CcspTraceError(("%s:%d:FAILED\n",__func__, __LINE__));
        return FALSE;
    }

    if ((CosaDmlWiFi_IsValidMacAddr(pWifiDppSta->ClientMac) == 0) ||
        (strlen(pWifiDppSta->InitiatorBootstrapSubjectPublicKeyInfo) <= 0) ||
        (strlen(pWifiDppSta->ResponderBootstrapSubjectPublicKeyInfo) <= 0) ||
        (pWifiDppSta->NumChannels == 0)) {
         wifi_dpp_dml_dbg_print(1, "%s:%d One or more parameters were empty\n", __func__, __LINE__);
         return FALSE;
    }

	/*Check for version*/
	if ((dppVersion != 1) && (dppVersion != 2)) 
	{
        wifi_dpp_dml_dbg_print(1, "%s:%d: Version validation error!!!\n", __func__, __LINE__);
        CcspWifiTrace(("RDK_LOG_ERROR,(%s), Version validation error!!!\n", __func__));
        CcspTraceError(("%s:%d:FAILED\n",__func__, __LINE__));
        return FALSE;
	}

    /*Check if device already associated*/
    if (wifi_api_is_device_associated((apIns-1), pWifiDppSta->ClientMac) == TRUE)
    {
        wifi_dpp_dml_dbg_print(1, "%s:%d Device already Associated\n", __func__, __LINE__);
        snprintf(buff, sizeof(buff), "%s MAC-%s\n", "Wifi DPP: Device already Associated", pWifiDppSta->ClientMac);
        write_to_file(wifi_health_logg, buff);
        return FALSE;
    }

    if (pWifiDppSta->Activate == TRUE) {
        wifi_dpp_dml_dbg_print(1, "%s:%d Activation already in progress\n", __func__, __LINE__);
        snprintf(buff, sizeof(buff), "%s\n", "Wifi DPP: Activation already done");
        write_to_file(wifi_health_logg, buff);
        return FALSE;
    }

    /* check the parameter name and return the corresponding value */
    if ((pWifiDppSta->MaxRetryCount < 5) || (pWifiDppSta->MaxRetryCount > 120))
    {
        wifi_dpp_dml_dbg_print(1, "%s:%d: MaxRetryCount validation error!!!\n", __func__, __LINE__);
        CcspWifiTrace(("RDK_LOG_ERROR,(%s), MaxRetryCount validation error!!!\n", __func__));
        CcspTraceError(("%s:%d:FAILED\n",__func__, __LINE__));
        return FALSE;
    }

	/* check the key management parameter. Key management can be only Common-PSK for version 1 and Common-PSK or DPPPSKSAE for version 2 */
	if ((dppVersion == 1) && (strcmp(pWifiDppSta->Cred.KeyManagement, "Common-PSK") != 0)) 
	{
        wifi_dpp_dml_dbg_print(1, "%s:%d: KeyManagement validation error, Key management can be only Common-PSK for version 1 and Common-PSK or DPPPSKSAE for version 2!!!\n", __func__, __LINE__);
        CcspWifiTrace(("RDK_LOG_ERROR,(%s), KeyManagement validation error, Key management can be only Common-PSK for version 1 and Common-PSK or DPPPSKSAE for version 2!!!\n", __func__));
        CcspTraceError(("%s:%d:FAILED\n",__func__, __LINE__));
        return FALSE;
	}

	if ((dppVersion == 2) && 
		((strcmp(pWifiDppSta->Cred.KeyManagement, "Common-PSK") != 0) && (strcmp(pWifiDppSta->Cred.KeyManagement, "DPPPSKSAE") != 0))) 
	{
        wifi_dpp_dml_dbg_print(1, "%s:%d: KeyManagement validation error, Key management can be only Common-PSK for version 1 and Common-PSK or DPPPSKSAE for version 2!!!\n", __func__, __LINE__);
        CcspWifiTrace(("RDK_LOG_ERROR,(%s), KeyManagement validation error, Key management can be only Common-PSK for version 1 and Common-PSK or DPPPSKSAE for version 2!!!\n", __func__));
        CcspTraceError(("%s:%d:FAILED\n",__func__, __LINE__));
        return FALSE;
	}
    memset(keyasn1, 0, sizeof(keyasn1));
    if ((asn1len = EVP_DecodeBlock(keyasn1, (unsigned char *)pWifiDppSta->ResponderBootstrapSubjectPublicKeyInfo,
                strlen(pWifiDppSta->ResponderBootstrapSubjectPublicKeyInfo))) < 0) {
        CcspTraceError(("%s:%d:FAILED\n",__func__, __LINE__));
        wifi_dpp_dml_dbg_print(1, "%s:%d Failed to decode base 64 responder public key\n", __func__, __LINE__);
        return FALSE;
    }
        key = keyasn1;
    if (!(d2i_EC_PUBKEY(NULL, &key, asn1len))) {
        wifi_dpp_dml_dbg_print(1, "%s:%d Failed to decode base 64 responder public key\n", __func__, __LINE__);
        CcspTraceError(("%s:%d:FAILED\n",__func__, __LINE__));
        return FALSE;
    }

    memset(keyasn1, 0, sizeof(keyasn1));
    if ((asn1len = EVP_DecodeBlock(keyasn1, (unsigned char *)pWifiDppSta->InitiatorBootstrapSubjectPublicKeyInfo, 
                strlen(pWifiDppSta->InitiatorBootstrapSubjectPublicKeyInfo))) < 0) {
        wifi_dpp_dml_dbg_print(1, "%s:%d Failed to decode base 64 initiator public key\n", __func__, __LINE__);
        CcspTraceError(("%s:%d:FAILED\n",__func__, __LINE__));
        return FALSE;
    }

    key = keyasn1;
    if (!(d2i_EC_PUBKEY(NULL, &key, asn1len))) {
        wifi_dpp_dml_dbg_print(1, "%s:%d Failed to decode base 64 initiator public key\n", __func__, __LINE__);
        CcspTraceError(("%s:%d:FAILED\n",__func__, __LINE__));
        return FALSE;

    }
#else// !defined(_XF3_PRODUCT_REQ_) && !defined(_CBR_PRODUCT_REQ_) && !defined(_HUB4_PRODUCT_REQ_) && !defined(_PLATFORM_RASPBERRYPI_) && !defined(_PLATFORM_TURRIS_)
    UNREFERENCED_PARAMETER(pWifiDppSta);
#endif
#else// !defined(_BWG_PRODUCT_REQ_)
    UNREFERENCED_PARAMETER(pWifiDppSta);
#endif

    wifi_dpp_dml_dbg_print(1, "%s:%d: Exit!!!\n", __func__, __LINE__);
    return TRUE;
}

ANSC_HANDLE
DPP_STA_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_DPP_CFG     pWifiDppSta  = (PCOSA_DML_WIFI_DPP_CFG)&pWifiAp->DPP;

    if (nIndex >= COSA_DML_WIFI_DPP_STA_MAX)
        return (ANSC_HANDLE)NULL;

    *pInsNumber = nIndex + 1;
    return (ANSC_HANDLE)&pWifiDppSta->Cfg[nIndex];
}
#endif //!defined(_HUB4_PRODUCT_REQ_)

/**********************************************************************  

    caller:     owner of this object

    prototype:

        BOOL
        DPP_STA_GetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:

        This function is called to retrieve Boolean parameter value;

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL*                       pBool
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
DPP_STA_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
    PCOSA_DML_WIFI_DPP_STA_CFG      pWifiDppSta  = (PCOSA_DML_WIFI_DPP_STA_CFG)hInsContext;
    
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Activate") == 0)
    {
        /* collect value */
        *pBool = pWifiDppSta->Activate;
        return TRUE;
    }
#else // !defined(_HUB4_PRODUCT_REQ_)
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(pBool);
#endif
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}


/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        DPP_STA_GetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int*                        pInt
            );

    description:

        This function is called to retrieve integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int*                        pInt
                The buffer of returned integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
DPP_STA_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
    CcspTraceError(("%s: Not Impl %d\n", __func__, __LINE__));
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(pInt);
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        DPP_STA_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
DPP_STA_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
    PCOSA_DML_WIFI_DPP_STA_CFG      pWifiDppSta  = (PCOSA_DML_WIFI_DPP_STA_CFG)hInsContext;

    if (strcmp(ParamName, "MaxRetryCount") == 0)
    {
        *puLong = pWifiDppSta->MaxRetryCount;
        return TRUE;
    }
#else // !defined(_HUB4_PRODUCT_REQ_)
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(puLong);
#endif
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        DPP_STA_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
DPP_STA_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
    PCOSA_DML_WIFI_DPP_STA_CFG      pWifiDppSta  = (PCOSA_DML_WIFI_DPP_STA_CFG)hInsContext;
    ULONG apIns, staIndex;
    UCHAR dppVersion;
	unsigned int i;

    if (GetInsNumsByWifiDppSta(pWifiDppSta, &apIns, &staIndex, &dppVersion) != ANSC_STATUS_SUCCESS)
    {
        return -1;
    }
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "ClientMac") == 0)
    {
        /* collect value */
        if( AnscSizeOfString(pWifiDppSta->ClientMac) < *pUlSize)
        {
            /* collect value */
            AnscCopyString(pValue, pWifiDppSta->ClientMac);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiDppSta->ClientMac)+1;
            return 1;
        }
    }

    if (strcmp(ParamName, "Channels") == 0)
    {
        char channelsList[256];

        channelsList[0] = 0;

        for (i = 0; i < pWifiDppSta->NumChannels && i < 32 ; i++) {
            if (pWifiDppSta->Channels[i] == 0)
            	break;
            sprintf(&channelsList[strlen(channelsList)], "%d,", pWifiDppSta->Channels[i]);
        }

        if (pWifiDppSta->NumChannels > 0) {
            channelsList[strlen(channelsList) - 1] = 0;
        }

        /* collect value */
        if( AnscSizeOfString(channelsList) < *pUlSize)
        { 
            /* collect value */
            strncpy(pValue, channelsList,(strlen(channelsList)+1));
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(channelsList)+1;
            return 1;
        }
    }

    if (strcmp(ParamName, "InitiatorBootstrapSubjectPublicKeyInfo") == 0)
    {
        if( AnscSizeOfString(pWifiDppSta->InitiatorBootstrapSubjectPublicKeyInfo) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiDppSta->InitiatorBootstrapSubjectPublicKeyInfo);
            return 0;
        }
        else {
            *pUlSize = AnscSizeOfString(pWifiDppSta->InitiatorBootstrapSubjectPublicKeyInfo) +1;
            return 1;
        }
    }

    if (strcmp(ParamName, "ResponderBootstrapSubjectPublicKeyInfo") == 0)
    {
        if( AnscSizeOfString(pWifiDppSta->ResponderBootstrapSubjectPublicKeyInfo) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiDppSta->ResponderBootstrapSubjectPublicKeyInfo);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString(pWifiDppSta->ResponderBootstrapSubjectPublicKeyInfo) +1;
            return 1;
        }
    }

    if (strcmp(ParamName, "ActivationStatus") == 0)
    {
        if( AnscSizeOfString((char*)pWifiDppSta->ActivationStatus) < *pUlSize)
        {
            /* collect value */
            AnscCopyString(pValue, (char*)pWifiDppSta->ActivationStatus);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString((char*)pWifiDppSta->ActivationStatus) +1;
            return 1;
        }
    }

    if (strcmp(ParamName, "EnrolleeResponderStatus") == 0)
    {
        /* collect value */
        if( AnscSizeOfString((char*)pWifiDppSta->EnrolleeResponderStatus) < *pUlSize)
        {
            AnscCopyString(pValue, (char*)pWifiDppSta->EnrolleeResponderStatus);
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString((char*)pWifiDppSta->EnrolleeResponderStatus) +1;
            return 1;
        }
    }
#else // !defined(_HUB4_PRODUCT_REQ_)
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(pValue);
    UNREFERENCED_PARAMETER(pUlSize);
#endif
    return -1;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        DPP_STA_SetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL                        bValue
            );

    description:

        This function is called to set BOOL parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL                        bValue
                The updated BOOL value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
DPP_STA_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
    PCOSA_DML_WIFI_DPP_STA_CFG      pWifiDppSta  = (PCOSA_DML_WIFI_DPP_STA_CFG)hInsContext;
    ULONG                           apIns = 0, staIndex = 0;
    PCOSA_DML_WIFI_AP               pWiFiAP     = NULL;

    BOOL ret;
    BOOL rfc;
    char *recName = "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.EasyConnect.Enable";
    char* strValue = NULL;

    if(PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue) != CCSP_SUCCESS) {
        wifi_dpp_dml_dbg_print(1, "%s: fail to get PSM record for RFC EasyConnect\n", __func__);
        CcspTraceError(("%s: fail to get PSM record for RFC EasyConnect\n",__func__));
    }
    else
    {
        rfc = atoi(strValue);
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        if(!rfc)
        {
            CcspTraceError(("%s: RFC for EasyConnect is disabled. Enable RFC to activate DPP\n",__func__));
            return FALSE;
        }
    }
    
    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "Activate") == 0)
    {
        if (bValue == TRUE) {
            if (NULL == (pWiFiAP =  GetApInsByDppSta(pWifiDppSta, &staIndex)))
            {
                CcspTraceError(("%s:%d:GetApIns failed\n",__func__, __LINE__));
                return FALSE;
            }
            if((dmcli_status.MaxRetryCount == 1) &&
               (dmcli_status.ClientMac == 1) &&
               (dmcli_status.InitiatorBootstrapSubjectPublicKeyInfo == 1) &&
               (dmcli_status.ResponderBootstrapSubjectPublicKeyInfo == 1) &&
               (dmcli_status.Channels == 1))
            {
                if (pWifiDppSta->Activate == TRUE) //for new request if activate is true means it's fatched from PS.
                    pWifiDppSta->Activate = FALSE;

                if ((DPP_STA_ProvisionStart_Validate(pWifiDppSta) == FALSE))
                {
                    CcspTraceError(("%s:%d: Validate failed\n",__func__, __LINE__));
                    return FALSE;
                }
            if (CosaDmlWiFi_startDPP(pWiFiAP, staIndex) == ANSC_STATUS_SUCCESS)
                {
                    /* save update to backup */
                    CcspTraceError(("%s:%d:SUCCESS\n",__func__, __LINE__));
                    pWifiDppSta->Activate = bValue;
                    memset(&dmcli_status, 0x0, sizeof(status)); //clear
                    ret = TRUE;
                }else{
                    ret = FALSE;
                }
                if(ANSC_STATUS_SUCCESS != CosaDmlWiFi_setDppValue(apIns,staIndex,"ActivationStatus", (char*)pWifiDppSta->ActivationStatus)){
                    CcspTraceError(("%s\n", "set ActivationStatus to PSM failed"));
                }
                if(ANSC_STATUS_SUCCESS != CosaDmlWiFi_setDppValue(apIns,staIndex,"EnrolleeResponderStatus", (char*)pWifiDppSta->EnrolleeResponderStatus)){
                    CcspTraceError(("%s\n", "set EnrolleeResponderStatus to PSM failed"));
                }
                return ret;
            } else {
                char buff[64];
                CcspTraceError(("%s:%d: Not all expected parameters present\n",__func__, __LINE__));
                snprintf(buff, sizeof(buff), "%s\n", "Wifi DPP: ActStatus_Config_Error");
                write_to_file(wifi_health_logg, buff);
                return FALSE;
            }
          
        }
    }
#else // !defined(_HUB4_PRODUCT_REQ_)
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(bValue);

#endif
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        DPP_STA_SetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int                         iValue
            );

    description:

        This function is called to set integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int                         iValue
                The updated integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
DPP_STA_SetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int                         iValue
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(iValue);
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        DPP_STA_SetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG                       uValue
            );

    description:

        This function is called to set ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG                       uValue
                The updated ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
DPP_STA_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
    PCOSA_DML_WIFI_DPP_STA_CFG      pWifiDppSta  = (PCOSA_DML_WIFI_DPP_STA_CFG)hInsContext;
    ULONG apIns, staIndex;
    UCHAR dppVersion;
    char setValue[8]={0};

    sprintf(setValue,"%li",uValue);

    if (GetInsNumsByWifiDppSta(pWifiDppSta, &apIns, &staIndex, &dppVersion) != ANSC_STATUS_SUCCESS)
    {
        return -1;
    }
    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "MaxRetryCount") == 0)
    {
        pWifiDppSta->MaxRetryCount = uValue;
        if(ANSC_STATUS_SUCCESS != CosaDmlWiFi_setDppValue(apIns,staIndex,ParamName,setValue)){
            return FALSE;
        }
        (void) set_status(MaxRetryCount);
        return TRUE;
    }
#else // !defined(_HUB4_PRODUCT_REQ_)
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(uValue);
#endif
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        DPP_STA_SetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pString
            );

    description:

        This function is called to set string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pString
                The updated string value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
DPP_STA_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
    PCOSA_DML_WIFI_DPP_STA_CFG      pWifiDppSta  = (PCOSA_DML_WIFI_DPP_STA_CFG)hInsContext;
    ULONG                           apIns, staIndex;
    UCHAR dppVersion;
    char    channelsList[128] = {0};

    if (GetInsNumsByWifiDppSta(pWifiDppSta, &apIns, &staIndex, &dppVersion) != ANSC_STATUS_SUCCESS)
    {
        return -1;
    }
        /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "ClientMac") == 0)
    {
        if (AnscSizeOfString(pString) > (sizeof(pWifiDppSta->ClientMac) - 1))
            return FALSE;
        if(CosaDmlWiFi_IsValidMacAddr(pString)  == 0)
            return FALSE;

        AnscCopyString(pWifiDppSta->ClientMac, pString);
        if(ANSC_STATUS_SUCCESS != CosaDmlWiFi_setDppValue(apIns,staIndex,ParamName,pString)){
            return FALSE;
        }
        (void) set_status(ClientMac);
        return TRUE;
    }

    if (strcmp(ParamName, "InitiatorBootstrapSubjectPublicKeyInfo") == 0)
    {
        if (AnscSizeOfString(pString) > (sizeof(pWifiDppSta->InitiatorBootstrapSubjectPublicKeyInfo) - 1))
            return FALSE;

        AnscZeroMemory(pWifiDppSta->InitiatorBootstrapSubjectPublicKeyInfo, sizeof(pWifiDppSta->InitiatorBootstrapSubjectPublicKeyInfo));
        AnscCopyString(pWifiDppSta->InitiatorBootstrapSubjectPublicKeyInfo, pString);
        if(ANSC_STATUS_SUCCESS != CosaDmlWiFi_setDppValue(apIns,staIndex,ParamName,pString)){
            return FALSE;
        }
        (void) set_status(InitiatorBootstrapSubjectPublicKeyInfo);
        return TRUE;
    }

    if (strcmp(ParamName, "ResponderBootstrapSubjectPublicKeyInfo") == 0)
    {
        if (AnscSizeOfString(pString) > (sizeof(pWifiDppSta->ResponderBootstrapSubjectPublicKeyInfo) - 1))
            return FALSE;

        AnscZeroMemory(pWifiDppSta->ResponderBootstrapSubjectPublicKeyInfo, sizeof(pWifiDppSta->ResponderBootstrapSubjectPublicKeyInfo));
        AnscCopyString(pWifiDppSta->ResponderBootstrapSubjectPublicKeyInfo, pString);
        if(ANSC_STATUS_SUCCESS != CosaDmlWiFi_setDppValue(apIns,staIndex,ParamName,pString)){
            return FALSE;
        }
        (void) set_status(ResponderBootstrapSubjectPublicKeyInfo);
        return TRUE;
    }

    if (strcmp(ParamName, "Channels") == 0)
    {
        CcspWifiTrace(("RDK_LOG_WARN, %s-%d\n",__FUNCTION__,__LINE__));
        if((GetInsNumsByWifiDppSta(pWifiDppSta, &apIns, &staIndex, &dppVersion) != ANSC_STATUS_SUCCESS))
        {
            CcspTraceError(("***Error*****DPP: no AP Index\n"));
            return FALSE;
        }

        if(AnscSizeOfString((char *)pWifiDppSta->Channels))
        {
            int channel[32] = {0x0};
            int i = 0;
            char *tmp = NULL, token[256] = {0};

	    AnscZeroMemory(token, sizeof(token));
	    AnscCopyString(token, pString);

	    if ((0 != strlen(token)) && 
		(0 != strncmp(token, " ", 1))) { //Check for Channel is Empty or not RDKB-27958
                tmp=strtok(token, ",");
                if(tmp == NULL)
                {
                    CcspTraceError(("********DPP Validate:Failed Channels\n"));
                    return FALSE;
                }
                while (tmp != NULL && i < 32 )
                {
                    channel[i] = atoi(tmp);
                    tmp = strtok(NULL, ",");
                    if(IsValidChannel(apIns-1, channel[i]) != TRUE)
                    {
                        CcspTraceError(("********DPP Validate:Failed Channels\n"));
                        return FALSE;
                    }
                    i++;
                }
            } else {
                CcspTraceInfo(("DPP empty string case entered !!!\n"));
            }
	}
        if (ANSC_STATUS_SUCCESS != CosaDmlWiFi_ParseEasyConnectEnrolleeChannels(apIns - 1, pWifiDppSta, pString)) {
            CcspTraceError(("***Error*****DPP: no Enrollee channel\n"));
            return FALSE;
        }

        if (ANSC_STATUS_SUCCESS != CosaDmlWiFi_setDppValue(apIns,staIndex,ParamName, CosaDmlWiFi_ChannelsListToString(pWifiDppSta, channelsList))){
            CcspTraceError(("***Error*****DPP: CosaDmlWiFi_setDppValue\n"));
            return FALSE;
        }
        (void) set_status(Channels);
        return TRUE;
    }
    if (strcmp(ParamName, "KeyManagement") == 0)
    {
        if (AnscSizeOfString(pString) > sizeof(pWifiDppSta->Cred.KeyManagement) - 1)
            return FALSE;
        AnscZeroMemory(pWifiDppSta->Cred.KeyManagement, sizeof(pWifiDppSta->Cred.KeyManagement));
        AnscCopyString(pWifiDppSta->Cred.KeyManagement, pString);
        if(ANSC_STATUS_SUCCESS != CosaDmlWiFi_setDppValue(apIns,staIndex,ParamName,pString)){
            return FALSE;
        }
        return TRUE;
    }
#else // !defined(_HUB4_PRODUCT_REQ_)
        /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(pString);
#endif
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        DPP_STA_Validate
            (
                ANSC_HANDLE                 hInsContext,
                char*                       pReturnParamName,
                ULONG*                      puLength
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       pReturnParamName,
                The buffer (128 bytes) of parameter name if there's a validation. 

                ULONG*                      puLength
                The output length of the param name. 

    return:     TRUE if there's no validation.

**********************************************************************/
BOOL
DPP_STA_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(pReturnParamName);
    UNREFERENCED_PARAMETER(puLength);
#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)

#if 0
/* DPP STA Validate */
PCOSA_DML_WIFI_DPP_STA_CFG      pWifiDppSta  = (PCOSA_DML_WIFI_DPP_STA_CFG)hInsContext;

int channel[32] = {0x0};
int i = 0;
char *tmp = NULL;
if(pWifiDppSta != NULL)
{
    if (strcmp(ParamName, "MaxRetryCount") == 0) 
    {
        if(AnscSizeOfString(pWifiDppSta->MaxRetryCount) && ((pWifiDppSta->MaxRetryCount < 5) && (pWifiDppSta->MaxRetryCount > 120)))
        {
            CcspTraceError(("********DPP Validate:Failed MaxRetryCount\n"));
            AnscCopyString(pReturnParamName, "MaxRetryCount");
            *puLength = AnscSizeOfString("MaxRetryCount");
            return FALSE;
        }
    }
    if (strcmp(ParamName, "ClientMac") == 0)
    {
        if(AnscSizeOfString(pWifiDppSta->ClientMac) && (CosaDmlWiFi_IsValidMacAddr(pWifiDppSta->ClientMac) != TRUE))
        {
            CcspTraceError(("********DPP Validate:Failed ClientMac\n"));
            AnscCopyString(pReturnParamName, "ClientMac");
            *puLength = AnscSizeOfString("ClientMac");
            return FALSE;
        }
    }
    if (strcmp(ParamName, "Channels") == 0) 
    {
        if(AnscSizeOfString(pWifiDppSta->Channels))
        {
            tmp=strtok(pWifiDppSta->Channels, ",");
            if(tmp == NULL)
                return FALSE;
            while (tmp != NULL)
            {
                channel[i] = atoi(tmp);
                tmp = strtok(NULL, ",");
                if(IsValidChannel(channel[i]) != TRUE)
                {
                    CcspTraceError(("********DPP Validate:Failed Channels\n"));
                    AnscCopyString(pReturnParamName, "Channels");
                    *puLength = AnscSizeOfString("Channels");
                    return FALSE;
                }
               i++;
            }
        }
    }
    if (strcmp(ParamName, "InitiatorBootstrapSubjectPublicKeyInfo") == 0)
    {
        if(AnscSizeOfString(pWifiDppSta->InitiatorBootstrapSubjectPublicKeyInfo) &&
           (AnscSizeOfString(pWifiDppSta->InitiatorBootstrapSubjectPublicKeyInfo) < 80))
        {
            CcspTraceError(("********DPP Validate:Failed InitiatorBootstrapSubjectPublicKeyInfo\n"));
            AnscCopyString(pReturnParamName, "InitiatorBootstrapSubjectPublicKeyInfo");
            *puLength = AnscSizeOfString("InitiatorBootstrapSubjectPublicKeyInfo");
            return FALSE;
        }
    }
    if (strcmp(ParamName, "ResponderBootstrapSubjectPublicKeyInfo") == 0)
    {
        if(AnscSizeOfString(pWifiDppSta->ResponderBootstrapSubjectPublicKeyInfo) &&
          (AnscSizeOfString(pWifiDppSta->ResponderBootstrapSubjectPublicKeyInfo) < 80))
        {
            CcspTraceError(("********DPP Validate:Failed ResponderBootstrapSubjectPublicKeyInfo\n"));
            AnscCopyString(pReturnParamName, "ResponderBootstrapSubjectPublicKeyInfo");
            *puLength = AnscSizeOfString("ResponderBootstrapSubjectPublicKeyInfo");
            return FALSE;
        }
    }
}
#endif //0
#endif // !defined(_HUB4_PRODUCT_REQ_)
    return TRUE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        DPP_STA_Commit
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
DPP_STA_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    return ANSC_STATUS_FAILURE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        DPP_STA_Rollback
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to roll back the update whenever there's a 
        validation found.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
DPP_STA_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    return ANSC_STATUS_SUCCESS;
}


/***********************************************************************

 APIs for Object:

    WiFi.AccessPoint.{i}X_RDKCENTRAL-COM_DPP.STA.{i}.

    *  DPP_STA_Credential_GetParamStringValue
    *  DPP_STA_Credential_SetParamStringValue
    *  DPP_STA_Credential_Validate
    *  DPP_STA_Credential_Commit
    *  DPP_STA_Credential_Rollback

***********************************************************************/
/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        DPP_STA_Credential_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
DPP_STA_Credential_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    UNREFERENCED_PARAMETER(pUlSize);
#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
    PCOSA_DML_WIFI_DPP_STA_CFG      pWifiDppSta  = (PCOSA_DML_WIFI_DPP_STA_CFG)hInsContext;
    
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "KeyManagement") == 0)
    {
        /* collect value */
        AnscCopyString(pValue, pWifiDppSta->Cred.KeyManagement);
        //AnscCopyString(pValue, "not_allowed_to_show");
        wifi_dpp_dml_dbg_print(1, "%s= '%s'\n", ParamName, pWifiDppSta->Cred.KeyManagement);
        return 0;
    }

    if (strcmp(ParamName, "psk_hex") == 0)
    {
        /* collect value */
        //AnscCopyString(pValue, pWifiDppSta->Cred.psk_hex);
        AnscCopyString(pValue, "not_allowed_to_show");
        wifi_dpp_dml_dbg_print(1, "%s= '%s'\n", ParamName, pWifiDppSta->Cred.psk_hex);
        return 0;
    }

    if (strcmp(ParamName, "password") == 0)
    {
        /* collect value */
        //AnscCopyString(pValue, pWifiDppSta->Cred.password);
        AnscCopyString(pValue, "not_allowed_to_show");
        wifi_dpp_dml_dbg_print(1, "%s= '%s'\n", ParamName, pWifiDppSta->Cred.password);
        return 0;
    }
#else // !defined(_HUB4_PRODUCT_REQ_)
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(pValue);
#endif
    return -1;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        DPP_STA_Credential_SetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pString
            );

    description:

        This function is called to set string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pString
                The updated string value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
DPP_STA_Credential_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
    PCOSA_DML_WIFI_DPP_STA_CFG      pWifiDppSta  = (PCOSA_DML_WIFI_DPP_STA_CFG)hInsContext;
    ULONG                           apIns, staIndex;
    UCHAR dppVersion;

    if (GetInsNumsByWifiDppSta(pWifiDppSta, &apIns, &staIndex, &dppVersion) != ANSC_STATUS_SUCCESS)
    {
        return FALSE;
    }

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "KeyManagement") == 0)
    {
        if (AnscSizeOfString(pString) > sizeof(pWifiDppSta->Cred.KeyManagement) - 1)
            return FALSE;

        AnscCopyString(pWifiDppSta->Cred.KeyManagement, pString);
        if(ANSC_STATUS_SUCCESS != CosaDmlWiFi_setDppValue(apIns,staIndex,ParamName,pString)){
            return FALSE;
        }
        return TRUE;
    }
#else// !defined(_HUB4_PRODUCT_REQ_)
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(ParamName);
    UNREFERENCED_PARAMETER(pString);
#endif
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        DPP_STA_Credential_Validate
            (
                ANSC_HANDLE                 hInsContext,
                char*                       pReturnParamName,
                ULONG*                      puLength
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       pReturnParamName,
                The buffer (128 bytes) of parameter name if there's a validation. 

                ULONG*                      puLength
                The output length of the param name. 

    return:     TRUE if there's no validation.

**********************************************************************/
BOOL
DPP_STA_Credential_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(pReturnParamName);
    UNREFERENCED_PARAMETER(puLength);
    return TRUE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        DPP_STA_Credential_Commit
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
DPP_STA_Credential_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    return ANSC_STATUS_SUCCESS;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        DPP_STA_Credential_Rollback
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to roll back the update whenever there's a 
        validation found.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
DPP_STA_Credential_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    return ANSC_STATUS_SUCCESS;
}

/***********************************************************************

 APIs for Object:

    WiFi.AccessPoint.{i}.Associated{i}.

    *  AssociatedDevice1_GetEntryCount
    *  AssociatedDevice1_GetEntry
    *  AssociatedDevice1_IsUpdated
    *  AssociatedDevice1_Synchronize
    *  AssociatedDevice1_GetParamBoolValue
    *  AssociatedDevice1_GetParamIntValue
    *  AssociatedDevice1_GetParamUlongValue
    *  AssociatedDevice1_GetParamStringValue

***********************************************************************/
/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        AssociatedDevice1_GetEntryCount
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to retrieve the count of the table.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The count of the table

**********************************************************************/
ULONG
AssociatedDevice1_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
       
    return pWifiAp->AssocDeviceCount;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ANSC_HANDLE
        AssociatedDevice1_GetEntry
            (
                ANSC_HANDLE                 hInsContext,
                ULONG                       nIndex,
                ULONG*                      pInsNumber
            );

    description:

        This function is called to retrieve the entry specified by the index.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                ULONG                       nIndex,
                The index of this entry;

                ULONG*                      pInsNumber
                The output instance number;

    return:     The handle to identify the entry

**********************************************************************/
ANSC_HANDLE
AssociatedDevice1_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{

    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT     )hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP             )pLinkObj->hContext;
    PCOSA_DML_WIFI_AP_ASSOC_DEVICE  pWifiApDev   = (PCOSA_DML_WIFI_AP_ASSOC_DEVICE)pWifiAp->AssocDevices;
    
    if (nIndex > pWifiAp->AssocDeviceCount)
    {
        return NULL;
    }
    else
    {
        *pInsNumber  = nIndex + 1; 
        return pWifiApDev+nIndex;
    }
    return NULL; /* return the handle */
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        AssociatedDevice1_IsUpdated
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is checking whether the table is updated or not.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     TRUE or FALSE.

**********************************************************************/
//static ULONG AssociatedDevice1PreviousVisitTime;

#define WIFI_AssociatedDevice_TIMEOUT   10 /*unit is second*/

BOOL
AssociatedDevice1_IsUpdated
    (
        ANSC_HANDLE                 hInsContext
    )
{
    // This function is called once per SSID.  The old implementation always reported the second call as 
    // false and hence the second SSID would not appear to need updating.  This table is very dynamic and
    // clients come and go, so always update it.
    //return TRUE;

	//zqiu: remember AssociatedDevice1PreviousVisitTime for each AP.
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT     )hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP             )pLinkObj->hContext;
    ULONG ticket;
//>>zqiu
// remove index restriction
//    if(pWifiAp->AP.Cfg.InstanceNumber>12) //skip unused ssid 13-16
//	return FALSE;
//<<
    ticket=AnscGetTickInSeconds();

    if ( ticket < (pWifiAp->AssociatedDevice1PreviousVisitTime + WIFI_AssociatedDevice_TIMEOUT ) )
	return FALSE;
    else {
    	pWifiAp->AssociatedDevice1PreviousVisitTime =  ticket;
    	return TRUE;
    }

#if 0
    if ( ( AnscGetTickInSeconds() - AssociatedDevice1PreviousVisitTime ) < WIFI_AssociatedDevice_TIMEOUT )
        return FALSE;
    else
    {
        AssociatedDevice1PreviousVisitTime =  AnscGetTickInSeconds();
        return TRUE;
    }
#endif 
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        AssociatedDevice1_Synchronize
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to synchronize the table.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
AssociatedDevice1_Synchronize
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT     )hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP             )pLinkObj->hContext;
    PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI          )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj = (PCOSA_CONTEXT_LINK_OBJECT     )NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid    = (PCOSA_DML_WIFI_SSID           )NULL;
    PSINGLE_LINK_ENTRY              pSLinkEntry  = (PSINGLE_LINK_ENTRY            )NULL;
    CHAR                            PathName[64] = {0};

#if 0
    /*release data allocated previous time*/
    if (pWifiAp->AssocDeviceCount)
    {
        AnscFreeMemory(pWifiAp->AssocDevices);
        pWifiAp->AssocDevices = NULL;
        pWifiAp->AssocDeviceCount = 0;
    }
#endif

    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
    
    while ( pSLinkEntry )
    {
        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        
        sprintf(PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);
        
        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }
        
        pSLinkEntry             = AnscQueueGetNextEntry(pSLinkEntry);
    }
    
    if ( pSLinkEntry )
    {
        // LGI  ADD BEGIN : keep previous reading in case of error
        PCOSA_DML_WIFI_AP_ASSOC_DEVICE  pAssocDevices    = NULL;
        ULONG                           AssocDeviceCount = -1;
        // LGI ADD END


        pWifiSsid = pSSIDLinkObj->hContext;
        //pWifiAp->AssocDevices = CosaDmlWiFiApGetAssocDevices
        pAssocDevices = CosaDmlWiFiApGetAssocDevices
        (
            (ANSC_HANDLE)pMyObject->hPoamWiFiDm, 
#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
            pWifiSsid->SSID.Cfg.SSID, 
#else
            pWifiSsid->SSID.StaticInfo.Name, 
#endif
            //&pWifiAp->AssocDeviceCount
            &AssocDeviceCount
        );
        
        // LGI ADD BEGIN, keep previous reading in case of error
        // Only release previous data if new reading was successful
        // AssocDeviceCount will be 0 when no clients connected and -1 in case of HAL error
        if ( pAssocDevices || AssocDeviceCount == 0 )
        {
            /*release data allocated previous time*/
            if (pWifiAp->AssocDeviceCount)
            {
                AnscFreeMemory(pWifiAp->AssocDevices);
                pWifiAp->AssocDevices = NULL;
                pWifiAp->AssocDeviceCount = 0;
            }

            pWifiAp->AssocDevices = pAssocDevices;
            pWifiAp->AssocDeviceCount = AssocDeviceCount;
        }
        // LGI ADD END

        return ANSC_STATUS_SUCCESS;
    }
    
     // LGI ADD BEGIN
    else
    {
        // This should not happen in runtime normally as SSID list is fixed
        // Handling as a precaution - release previously allocated memory
        if (pWifiAp->AssocDeviceCount)
        {
            AnscFreeMemory(pWifiAp->AssocDevices);
            pWifiAp->AssocDevices = NULL;
            pWifiAp->AssocDeviceCount = 0;
        }
    }
    // LGI ADD END

    return ANSC_STATUS_SUCCESS;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        AssociatedDevice1_GetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:

        This function is called to retrieve Boolean parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL*                       pBool
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
AssociatedDevice1_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    PCOSA_DML_WIFI_AP_ASSOC_DEVICE  pWifiApDev   = (PCOSA_DML_WIFI_AP_ASSOC_DEVICE)hInsContext;
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "AuthenticationState") == 0)
    {
        /* collect value */
        *pBool = pWifiApDev->AuthenticationState;
        return TRUE;
    }

    if (strcmp(ParamName, "Active") == 0)
    {
        /* collect value */
        *pBool = pWifiApDev->Active;
        return TRUE;
    }


    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        AssociatedDevice1_GetParamIntValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                int*                        pInt
            );

    description:

        This function is called to retrieve integer parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                int*                        pInt
                The buffer of returned integer value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
AssociatedDevice1_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
    PCOSA_DML_WIFI_AP_ASSOC_DEVICE  pWifiApDev   = (PCOSA_DML_WIFI_AP_ASSOC_DEVICE)hInsContext;
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "SignalStrength") == 0)
    {
        /* collect value */
        *pInt = pWifiApDev->SignalStrength;
        return TRUE;
    }


    if (strcmp(ParamName, "X_COMCAST-COM_SNR") == 0)
    {
        /* collect value */
        *pInt = pWifiApDev->SNR;
        return TRUE;
    }

    if (strcmp(ParamName, "X_RDKCENTRAL-COM_SNR") == 0)
    {
        /* collect value */
        *pInt = pWifiApDev->SNR;
        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_RSSI") == 0)
    {
        /* collect value */
        *pInt = pWifiApDev->RSSI;
        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_MinRSSI") == 0)
    {
        /* collect value */
        *pInt = pWifiApDev->MinRSSI;
        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_MaxRSSI") == 0)
    {
        /* collect value */
        *pInt = pWifiApDev->MaxRSSI;
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        AssociatedDevice1_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
AssociatedDevice1_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    PCOSA_DML_WIFI_AP_ASSOC_DEVICE  pWifiApDev   = (PCOSA_DML_WIFI_AP_ASSOC_DEVICE)hInsContext;
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "LastDataDownlinkRate") == 0)
    {
        /* collect value */
        *puLong = pWifiApDev->LastDataDownlinkRate;
        return TRUE;
    }

    if (strcmp(ParamName, "LastDataUplinkRate") == 0)
    {
        /* collect value */
        *puLong = pWifiApDev->LastDataUplinkRate;
        return TRUE;
    }

    if ((strcmp(ParamName, "Retransmissions") == 0) || (strcmp(ParamName, "RetransCount") == 0))
    {
        /* collect value */
        *puLong = pWifiApDev->Retransmissions;
        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_DataFramesSentAck") == 0)
    {
        /* collect value */
        *puLong = pWifiApDev->DataFramesSentAck;
        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_DataFramesSentNoAck") == 0)
    {
        /* collect value */
        *puLong = pWifiApDev->DataFramesSentNoAck;
        return TRUE;
    }

    if ((strcmp(ParamName, "X_COMCAST-COM_BytesSent") == 0) || (strcmp(ParamName, "BytesSent") == 0))
    {
        /* collect value */
        *puLong = pWifiApDev->BytesSent;
        return TRUE;
    }

    if ((strcmp(ParamName, "X_COMCAST-COM_BytesReceived") == 0) || (strcmp(ParamName, "BytesReceived") == 0))
    {
        /* collect value */
        *puLong = pWifiApDev->BytesReceived;
        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_Disassociations") == 0)
    {
        /* collect value */
        *puLong = pWifiApDev->Disassociations;
        return TRUE;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_AuthenticationFailures") == 0)
    {
        /* collect value */
        *puLong = pWifiApDev->AuthenticationFailures;
        return TRUE;
    }

    if (strcmp(ParamName, "PacketsSent") == 0)
    {
        /* collect value */
        return TRUE;
    }

    if (strcmp(ParamName, "PacketsReceived") == 0)
    {
        /* collect value */
        return TRUE;
    }

    if (strcmp(ParamName, "ErrorsSent") == 0)
    {
        /* collect value */
        return TRUE;
    }

    if (strcmp(ParamName, "FailedRetransCount") == 0)
    {
        /* collect value */
        return TRUE;
    }

    if (strcmp(ParamName, "RetryCount") == 0)
    {
        /* collect value */
        return TRUE;
    }

    if (strcmp(ParamName, "MultipleRetryCount") == 0)
    {
        /* collect value */
        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        AssociatedDevice1_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
AssociatedDevice1_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_DML_WIFI_AP_ASSOC_DEVICE  pWifiApDev   = (PCOSA_DML_WIFI_AP_ASSOC_DEVICE)hInsContext;
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "MACAddress") == 0)
    {
        /* collect value */
        if ( AnscSizeOfString((char*)pWifiApDev->MacAddress) < *pUlSize)
        {
             _ansc_sprintf
            (
                pValue,
                "%02X:%02X:%02X:%02X:%02X:%02X",
                pWifiApDev->MacAddress[0],
                pWifiApDev->MacAddress[1],
                pWifiApDev->MacAddress[2],
                pWifiApDev->MacAddress[3],
                pWifiApDev->MacAddress[4],
                pWifiApDev->MacAddress[5]
            );
            return 0;
        }
        else
        {
            *pUlSize = AnscSizeOfString((char*)pWifiApDev->MacAddress)+1;
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_OperatingStandard") == 0)
    {
        /* collect value */
        AnscCopyString(pValue, pWifiApDev->OperatingStandard);
        return 0;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_OperatingChannelBandwidth") == 0)
    {
        /* collect value */
        AnscCopyString(pValue, pWifiApDev->OperatingChannelBandwidth);
        return 0;
    }

    if (strcmp(ParamName, "X_COMCAST-COM_InterferenceSources") == 0)
    {
        /* collect value */
        AnscCopyString(pValue, pWifiApDev->InterferenceSources);
        return 0;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return -1;
}


/***********************************************************************

 APIs for Object:

    WiFi.AccessPoint.{i}.AssociatedDevice.{i}.Stats.

    *  Stats_GetParamUlongValue

***********************************************************************/

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Stats_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      pULong
            );

    description:

        This function is called to retrieve ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      pULong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/

BOOL
Stats_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      pULong
    ) {
    PCOSA_DML_WIFI_AP_ASSOC_DEVICE  pWifiApDev   = (PCOSA_DML_WIFI_AP_ASSOC_DEVICE)hInsContext;

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "BytesSent") == 0)
    {
        /* collect value */
        *pULong = pWifiApDev->BytesSent;
        return TRUE;
    }

    if (strcmp(ParamName, "BytesReceived") == 0)
    {
        /* collect value */
	*pULong = pWifiApDev->BytesReceived;
        return TRUE;
    }

    if (strcmp(ParamName, "PacketsSent") == 0)
    {
        /* collect value */
	*pULong = pWifiApDev->PacketsSent;
        return TRUE;
    }

    if (strcmp(ParamName, "PacketsReceived") == 0)
    {
        /* collect value */
	*pULong = pWifiApDev->PacketsReceived;
        return TRUE;
    }

    if (strcmp(ParamName, "ErrorsSent") == 0)
    {
        /* collect value */
	*pULong = pWifiApDev->ErrorsSent;
        return TRUE;
    }

    if (strcmp(ParamName, "RetransCount") == 0)
    {
        /* collect value */
	*pULong = pWifiApDev->RetransCount;
        return TRUE;
    }

    if (strcmp(ParamName, "FailedRetransCount") == 0)
    {
       /* collect value */
	*pULong = pWifiApDev->FailedRetransCount;
        return TRUE;
    }

    if (strcmp(ParamName, "RetryCount") == 0)
    {
        /* collect value */
	*pULong = pWifiApDev->RetryCount;
        return TRUE;
    }

    if (strcmp(ParamName, "MultipleRetryCount") == 0)
    {
        /* collect value */
	*pULong = pWifiApDev->MultipleRetryCount;
        return TRUE;
    }

    return FALSE;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        Stats_GetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:

        This function is called to retrieve Boolean parameter value;

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL*                       pBool
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Stats_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "InstantMeasurementsEnable") == 0)
    {
        /* collect value */
        *pBool = CosaDmlWiFi_IsInstantMeasurementsEnable( );

        return TRUE;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

ULONG
WEPKey64Bit_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    return COSA_DML_WEP_KEY_NUM;
}

ANSC_HANDLE
WEPKey64Bit_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec   = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;

    if (nIndex >= COSA_DML_WEP_KEY_NUM)
        return (ANSC_HANDLE)NULL;

    *pInsNumber = nIndex + 1;
    return (ANSC_HANDLE)&pWifiApSec->WEPKey64Bit[nIndex];
}

ULONG
WEPKey64Bit_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_DML_WEPKEY_64BIT          pWEPKey64 = (PCOSA_DML_WEPKEY_64BIT)hInsContext;

    if (strcmp(ParamName, "WEPKey") == 0)
    {
        if (*pUlSize <= AnscSizeOfString(pWEPKey64->WEPKey))
        {
            *pUlSize = AnscSizeOfString(pWEPKey64->WEPKey) + 1;
            return 1;
        }

        AnscCopyString(pValue, pWEPKey64->WEPKey);
        return 0;
    }

    return -1;
}

BOOL
WEPKey64Bit_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
    PCOSA_DML_WEPKEY_64BIT          pWEPKey64 = (PCOSA_DML_WEPKEY_64BIT)hInsContext;

    if (strcmp(ParamName, "WEPKey") == 0)
    {
        if (AnscSizeOfString(pString) > sizeof(pWEPKey64->WEPKey) - 1)
            return FALSE;

        AnscZeroMemory(pWEPKey64->WEPKey, sizeof(pWEPKey64->WEPKey));
        AnscCopyString(pWEPKey64->WEPKey, pString);
        return TRUE;
    }

    return FALSE;
}

static int isHex (char *string)
{
    int len = strlen(string);
    int i = 0;

    for (i = 0; i < len; i++)
    {
        char c = string[i];
	if (isdigit(c)||
	    c=='a'||c=='b'||c=='c'||c=='d'||c=='e'||c=='f' ||
	    c=='A'||c=='B'||c=='C'||c=='D'||c=='E'||c=='F')
	    continue;
	else
	    return 0;
    }
    return 1;
}

BOOL
WEPKey64Bit_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    PCOSA_DML_WEPKEY_64BIT          pWEPKey64 = (PCOSA_DML_WEPKEY_64BIT)hInsContext;

    // Only 40 of the 64 is used for key
    // key must be either a alphanumeric string of length 5
    // or a hexstring of length 10
    if ( AnscSizeOfString(pWEPKey64->WEPKey) != 40 / 8 ) 
    {
	if ( !isHex(pWEPKey64->WEPKey) || ( AnscSizeOfString(pWEPKey64->WEPKey) != 40 / 8 * 2 ) ) 
	{
	    AnscCopyString(pReturnParamName, "WEPKey");
	    *puLength = AnscSizeOfString("WEPKey");
	    return FALSE;
	}
    }

    return TRUE;
}

ULONG
WEPKey64Bit_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DML_WEPKEY_64BIT          pWEPKey64 = (PCOSA_DML_WEPKEY_64BIT)hInsContext;
    ULONG                           apIns, wepKeyIdx;

    if (GetInsNumsByWEPKey64(pWEPKey64, &apIns, &wepKeyIdx) != ANSC_STATUS_SUCCESS)
        return -1;

    return CosaDmlWiFi_SetWEPKey64ByIndex(apIns, wepKeyIdx, pWEPKey64);
}

ULONG
WEPKey64Bit_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DML_WEPKEY_64BIT          pWEPKey64 = (PCOSA_DML_WEPKEY_64BIT)hInsContext;
    ULONG                           apIns, wepKeyIdx;

    if (GetInsNumsByWEPKey64(pWEPKey64, &apIns, &wepKeyIdx) != ANSC_STATUS_SUCCESS)
        return -1;

    return CosaDmlWiFi_GetWEPKey64ByIndex(apIns, wepKeyIdx, pWEPKey64);
}

ULONG
WEPKey128Bit_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    return COSA_DML_WEP_KEY_NUM;
}

ANSC_HANDLE
WEPKey128Bit_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec   = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;

    if (nIndex >= COSA_DML_WEP_KEY_NUM)
        return (ANSC_HANDLE)NULL;

    *pInsNumber = nIndex + 1;
    return (ANSC_HANDLE)&pWifiApSec->WEPKey128Bit[nIndex];
}

ULONG
WEPKey128Bit_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_DML_WEPKEY_128BIT          pWEPKey128 = (PCOSA_DML_WEPKEY_128BIT)hInsContext;

    if (strcmp(ParamName, "WEPKey") == 0)
    {
        if (*pUlSize <= AnscSizeOfString(pWEPKey128->WEPKey))
        {
            *pUlSize = AnscSizeOfString(pWEPKey128->WEPKey) + 1;
            return 1;
        }

        AnscCopyString(pValue, pWEPKey128->WEPKey);
        return 0;
    }

    return -1;
}

BOOL
WEPKey128Bit_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
    PCOSA_DML_WEPKEY_128BIT          pWEPKey128 = (PCOSA_DML_WEPKEY_128BIT)hInsContext;

    if (strcmp(ParamName, "WEPKey") == 0)
    {
        if (AnscSizeOfString(pString) > sizeof(pWEPKey128->WEPKey) - 1)
            return FALSE;

        AnscZeroMemory(pWEPKey128->WEPKey, sizeof(pWEPKey128->WEPKey));
        AnscCopyString(pWEPKey128->WEPKey, pString);
        return TRUE;
    }

    return FALSE;
}

BOOL
WEPKey128Bit_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    PCOSA_DML_WEPKEY_128BIT          pWEPKey128 = (PCOSA_DML_WEPKEY_128BIT)hInsContext;

    // Only 104 of the 128 is used for key
    // key must be either a alphanumeric string of length 5
    // or a hexstring of length 10
    if ( AnscSizeOfString(pWEPKey128->WEPKey) != 104 / 8 ) 
    {
	if ( !isHex(pWEPKey128->WEPKey) || ( AnscSizeOfString(pWEPKey128->WEPKey) != 104 / 8 * 2 ) ) 
	{
	    AnscCopyString(pReturnParamName, "WEPKey");
	    *puLength = AnscSizeOfString("WEPKey");
	    return FALSE;
	}
    }

    return TRUE;
}

ULONG
WEPKey128Bit_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DML_WEPKEY_128BIT          pWEPKey128 = (PCOSA_DML_WEPKEY_128BIT)hInsContext;
    ULONG                           apIns, wepKeyIdx;

    if (GetInsNumsByWEPKey128(pWEPKey128, &apIns, &wepKeyIdx) != ANSC_STATUS_SUCCESS)
        return -1;

    return CosaDmlWiFi_SetWEPKey128ByIndex(apIns, wepKeyIdx, pWEPKey128);
}

ULONG
WEPKey128Bit_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DML_WEPKEY_128BIT          pWEPKey128 = (PCOSA_DML_WEPKEY_128BIT)hInsContext;
    ULONG                           apIns, wepKeyIdx;

    if (GetInsNumsByWEPKey128(pWEPKey128, &apIns, &wepKeyIdx) != ANSC_STATUS_SUCCESS)
        return -1;

    return CosaDmlWiFi_GetWEPKey128ByIndex(apIns, wepKeyIdx, pWEPKey128);
}

BOOL
RadiusSettings_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    /* check the parameter name and return the corresponding value */
    PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PSINGLE_LINK_ENTRY              pSLinkEntry  = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    //PCOSA_DML_WIFI_SSID             pWifiSsid    = (PCOSA_DML_WIFI_SSID      )NULL;
    CHAR                            PathName[64] = {0};

    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);

    while ( pSLinkEntry )
    {
        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        //pWifiSsid    = pSSIDLinkObj->hContext;

        sprintf(PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);

        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }

        pSLinkEntry             = AnscQueueGetNextEntry(pSLinkEntry);
    }

    //zqiu: move to CosaDmlWiFiApGetEntry
    //CosaDmlGetApRadiusSettings(NULL,pWifiSsid->SSID.StaticInfo.Name,&pWifiAp->AP.RadiusSetting);

    //PCOSA_DML_WIFI_RadiusSetting  pRadiusSetting   = (PCOSA_DML_WIFI_RadiusSetting)hInsContext;
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "PMKCaching") == 0)
    {
        /* collect value */
        *pBool = pWifiAp->AP.RadiusSetting.bPMKCaching;
        return TRUE;
    }
 
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

BOOL
RadiusSettings_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PSINGLE_LINK_ENTRY              pSLinkEntry  = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    CHAR                            PathName[64];

    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);

    while ( pSLinkEntry )
    {
        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);

        sprintf(PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);
        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }

        pSLinkEntry             = AnscQueueGetNextEntry(pSLinkEntry);
    }

    if (strcmp(ParamName, "PMKCacheInterval") == 0)
    {
        /* collect value */
        *puLong = pWifiAp->AP.RadiusSetting.iPMKCacheInterval;
        return TRUE;
    }

    if (strcmp(ParamName, "ReAuthInterval") == 0)
    {
        /* collect value */
        *puLong = pWifiAp->AP.RadiusSetting.iReAuthInterval;
        return TRUE;
    }

    if (strcmp(ParamName, "PMKLifetime") == 0)
    {
        /* collect value */
        *puLong = pWifiAp->AP.RadiusSetting.iPMKLifetime;
        return TRUE;
    }

    return FALSE;
}

BOOL
RadiusSettings_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
//    PCOSA_DML_WIFI_RadiusSetting  pRadiusSetting   = (PCOSA_DML_WIFI_RadiusSetting)hInsContext;
   PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PSINGLE_LINK_ENTRY              pSLinkEntry  = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    //PCOSA_DML_WIFI_SSID             pWifiSsid    = (PCOSA_DML_WIFI_SSID      )NULL;
    CHAR                            PathName[64] = {0};

    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);

    while ( pSLinkEntry )
    {
        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        //pWifiSsid    = pSSIDLinkObj->hContext;

        sprintf(PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);

        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }

        pSLinkEntry             = AnscQueueGetNextEntry(pSLinkEntry);
    }

    /* check the parameter name and return the corresponding value */
    //zqiu: move to CosaDmlWiFiApGetEntry
    //CosaDmlGetApRadiusSettings(NULL,pWifiSsid->SSID.StaticInfo.Name,&pWifiAp->AP.RadiusSetting);

    if (strcmp(ParamName, "RadiusServerRetries") == 0)
    {
        /* collect value */
        *pInt = pWifiAp->AP.RadiusSetting.iRadiusServerRetries;
        return TRUE;
    }

    if (strcmp(ParamName, "RadiusServerRequestTimeout") == 0)
    {
        /* collect value */
        *pInt = pWifiAp->AP.RadiusSetting.iRadiusServerRequestTimeout;
        return TRUE;
    }

    if (strcmp(ParamName, "MaxAuthenticationAttempts") == 0)
    {
        /* collect value */
        *pInt = pWifiAp->AP.RadiusSetting.iMaxAuthenticationAttempts;
        return TRUE;
    }

    if (strcmp(ParamName, "BlacklistTableTimeout") == 0)
    {
        /* collect value */
        *pInt = pWifiAp->AP.RadiusSetting.iBlacklistTableTimeout;
        return TRUE;
    }

    if (strcmp(ParamName, "IdentityRequestRetryInterval") == 0)
    {
        /* collect value */
        *pInt = pWifiAp->AP.RadiusSetting.iIdentityRequestRetryInterval;
        return TRUE;
    }

    if (strcmp(ParamName, "QuietPeriodAfterFailedAuthentication") == 0)
    {
        /* collect value */
        *pInt = pWifiAp->AP.RadiusSetting.iQuietPeriodAfterFailedAuthentication;
        return TRUE;
    }

    return FALSE;
}

BOOL
RadiusSettings_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
//    PCOSA_DML_WIFI_RadiusSetting  pRadiusSetting   = (PCOSA_DML_WIFI_RadiusSetting)hInsContext;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;

    AnscTraceWarning(("ParamName: %s bvalue: %d\n", ParamName, bValue));

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "PMKCaching") == 0)
    {
        /* save update to backup */
        pWifiAp->AP.RadiusSetting.bPMKCaching = bValue;
#if defined(FEATURE_HOSTAP_AUTHENTICATOR) && !defined (_XB7_PRODUCT_REQ_)
        BOOLEAN isNativeHostapdDisabled = FALSE;
        CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
        if (isNativeHostapdDisabled &&
            !(hapd_reload_radius_param(pWifiAp->AP.Cfg.InstanceNumber - 1, NULL, NULL, 0, bValue, TRUE, COSA_WIFI_HAPD_RADIUS_SERVER_PMK_CACHING)))
        {
             CcspWifiTrace(("RDK_LOG_INFO, RADIUS_PARAM_CHANGE_PUSHED_SUCCEESSFULLY\n"));
        }
#endif //FEATURE_HOSTAP_AUTHENTICATOR
        return TRUE;
    }
return FALSE;
}

BOOL
RadiusSettings_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;

    AnscTraceWarning(("ParamName: %s uValue: %u\n", ParamName, uValue));
	
    if (strcmp(ParamName, "PMKCacheInterval") == 0)
    {
        if(pWifiAp->AP.RadiusSetting.bPMKCaching == FALSE)
        {
            CcspTraceWarning(("'%s' cannot be set as PMKCaching is set to False\n", ParamName));
            return FALSE;
        }

        /* save update to backup */
        pWifiAp->AP.RadiusSetting.iPMKCacheInterval = uValue;
        return TRUE;
    }

    if (strcmp(ParamName, "ReAuthInterval") == 0)
    {
        /* save update to backup */
        if(uValue != pWifiAp->AP.RadiusSetting.iReAuthInterval)
        {
              pWifiAp->AP.RadiusSetting.iReAuthInterval = uValue;
        }
        return TRUE;
    }

    if (strcmp(ParamName, "PMKLifetime") == 0)
    {
        /* save update to backup */
        pWifiAp->AP.RadiusSetting.iPMKLifetime = uValue;
        return TRUE;
    }
	
    return FALSE;
}

BOOL
RadiusSettings_SetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int                         iValue
    )
{
    //PCOSA_DML_WIFI_RadiusSetting  pRadiusSetting   = (PCOSA_DML_WIFI_RadiusSetting)hInsContext;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    AnscTraceWarning(("ParamName: %s iValue: %d\n", ParamName, iValue));

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "RadiusServerRetries") == 0)
    {
        /* save update to backup */
        pWifiAp->AP.RadiusSetting.iRadiusServerRetries = iValue;
        return TRUE;
    }

    if (strcmp(ParamName, "RadiusServerRequestTimeout") == 0)
    {
        /* save update to backup */
        pWifiAp->AP.RadiusSetting.iRadiusServerRequestTimeout = iValue;
        return TRUE;
    }

    if (strcmp(ParamName, "MaxAuthenticationAttempts") == 0)
    {
        /* save update to backup */
        pWifiAp->AP.RadiusSetting.iMaxAuthenticationAttempts = iValue;
        return TRUE;
    }

    if (strcmp(ParamName, "BlacklistTableTimeout") == 0)
    {
        /* save update to backup */
        pWifiAp->AP.RadiusSetting.iBlacklistTableTimeout = iValue;
        return TRUE;
    }

    if (strcmp(ParamName, "IdentityRequestRetryInterval") == 0)
    {
        /* save update to backup */
        pWifiAp->AP.RadiusSetting.iIdentityRequestRetryInterval = iValue;
        return TRUE;
    }

    if (strcmp(ParamName, "QuietPeriodAfterFailedAuthentication") == 0)
    {
        /* save update to backup */
        pWifiAp->AP.RadiusSetting.iQuietPeriodAfterFailedAuthentication = iValue;
        return TRUE;
    }
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

BOOL
RadiusSettings_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(pReturnParamName);
    UNREFERENCED_PARAMETER(puLength);
    return TRUE;
}

ULONG
RadiusSettings_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PSINGLE_LINK_ENTRY              pSLinkEntry  = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid    = (PCOSA_DML_WIFI_SSID      )NULL;
    CHAR                            PathName[64] = {0};

    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);

    while ( pSLinkEntry )
    {
        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        pWifiSsid    = pSSIDLinkObj->hContext;

        sprintf(PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);

        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }

        pSLinkEntry             = AnscQueueGetNextEntry(pSLinkEntry);
    }

    CosaDmlSetApRadiusSettings(NULL,pWifiSsid->SSID.StaticInfo.Name,&pWifiAp->AP.RadiusSetting);

    return 0;
}

BOOL
Authenticator_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP)pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec   = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "EAPOLKeyTimeout") == 0)
    {
        /* collect value */
        *puLong = pWifiApSec->Cfg.uiEAPOLKeyTimeout ;
        return TRUE;
    }

    if (strcmp(ParamName, "EAPOLKeyRetries") == 0)
    {
        /* collect value */
        *puLong = pWifiApSec->Cfg.uiEAPOLKeyRetries ;
        return TRUE;
    }

    if (strcmp(ParamName, "EAPIdentityRequestTimeout") == 0)
    {
        /* collect value */
        *puLong = pWifiApSec->Cfg.uiEAPIdentityRequestTimeout ;
        return TRUE;
    }

    if (strcmp(ParamName, "EAPIdentityRequestRetries") == 0)
    {
	    /* collect value */
        *puLong = pWifiApSec->Cfg.uiEAPIdentityRequestRetries ;
        return TRUE;
    }

    if (strcmp(ParamName, "EAPRequestTimeout") == 0)
    {
        /* collect value */
        *puLong = pWifiApSec->Cfg.uiEAPRequestTimeout ;
        return TRUE;
    }

    if (strcmp(ParamName, "EAPRequestRetries") == 0)
    {
        /* collect value */
        *puLong = pWifiApSec->Cfg.uiEAPRequestRetries ;
        return TRUE;
    }
    return FALSE;
}

BOOL
Authenticator_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP)pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec   = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "EAPOLKeyTimeout") == 0)
    {
        if ( pWifiApSec->Cfg.uiEAPOLKeyTimeout != uValue )
        {
            /* save update to backup */
            pWifiApSec->Cfg.uiEAPOLKeyTimeout = uValue;
            pWifiAp->bSecChanged  = TRUE;
        }
        return TRUE;
    }

    if (strcmp(ParamName, "EAPOLKeyRetries") == 0)
    {
        if ( pWifiApSec->Cfg.uiEAPOLKeyRetries != uValue )
        {
            /* save update to backup */
            pWifiApSec->Cfg.uiEAPOLKeyRetries = uValue;
            pWifiAp->bSecChanged = TRUE;
        }
        return TRUE;
    }

    if (strcmp(ParamName, "EAPIdentityRequestTimeout") == 0)
    {
        if ( pWifiApSec->Cfg.uiEAPIdentityRequestTimeout != uValue )
        {
            /* save update to backup */
            pWifiApSec->Cfg.uiEAPIdentityRequestTimeout = uValue;
            pWifiAp->bSecChanged  = TRUE;
        }
        return TRUE;
    }

    if (strcmp(ParamName, "EAPIdentityRequestRetries") == 0)
    {
        if ( pWifiApSec->Cfg.uiEAPIdentityRequestRetries != uValue )
        {
            /* save update to backup */
            pWifiApSec->Cfg.uiEAPIdentityRequestRetries = uValue;
            pWifiAp->bSecChanged = TRUE;
        }
        return TRUE;
    }

    if (strcmp(ParamName, "EAPRequestTimeout") == 0)
    {
        if ( pWifiApSec->Cfg.uiEAPRequestTimeout != uValue )
        {
            /* save update to backup */
            pWifiApSec->Cfg.uiEAPRequestTimeout = uValue;
            pWifiAp->bSecChanged = TRUE;
        }
        return TRUE;
    }

    if (strcmp(ParamName, "EAPRequestRetries") == 0)
    {
        if ( pWifiApSec->Cfg.uiEAPRequestRetries != uValue )
        {
            /* save update to backup */
            pWifiApSec->Cfg.uiEAPRequestRetries = uValue;
            pWifiAp->bSecChanged = TRUE;
        }
        return TRUE;
    }
    return FALSE;
}

BOOL
Authenticator_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(pReturnParamName);
    UNREFERENCED_PARAMETER(puLength);
    CcspTraceDebug(("Authenticator_validate"));
    return TRUE;
}

ULONG
Authenticator_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj      = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp       = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec    = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;
    PCOSA_DML_WIFI_APSEC_CFG        pWifiApSecCfg = (PCOSA_DML_WIFI_APSEC_CFG )&pWifiApSec->Cfg;
    PSINGLE_LINK_ENTRY              pSLinkEntry   = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj  = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid     = (PCOSA_DML_WIFI_SSID      )NULL;
    CHAR                            PathName[64]  = {0};

    if ( !pWifiAp->bSecChanged )
    {
        return  ANSC_STATUS_SUCCESS;
    }
    else
    {
        pWifiAp->bSecChanged = FALSE;
        CcspTraceInfo(("WiFi AP EAP Authenticator commit -- apply the changes...\n"));
    }

    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);

    while ( pSLinkEntry )
    {
        pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);

        sprintf(PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);

        if ( AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE) )
        {
            break;
        }

        pSLinkEntry  = AnscQueueGetNextEntry(pSLinkEntry);
    }

    if ( pSLinkEntry )
    {
        pWifiSsid = pSSIDLinkObj->hContext;

#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_)
        return CosaDmlWiFiApEapAuthCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, pWifiApSecCfg);
#else
        return CosaDmlWiFiApEapAuthCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, pWifiApSecCfg);
#endif
    }

    return ANSC_STATUS_FAILURE;
}
 
ULONG
RadiusSettings_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP)pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec   = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "OperatorName") == 0)
    {
        /* collect value */
        AnscCopyString(pValue, pWifiApSec->Cfg.cOperatorName);
        return 0;
    }

    if (strcmp(ParamName, "LocationData") == 0)
    {
        /* collect value */
        AnscCopyString(pValue, pWifiApSec->Cfg.cLocationData);
        return 0;
    }
    return -1;
}

BOOL
RadiusSettings_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP)pLinkObj->hContext;
    PCOSA_DML_WIFI_APSEC_FULL       pWifiApSec   = (PCOSA_DML_WIFI_APSEC_FULL)&pWifiAp->SEC;
    int band;

    band = ((pWifiAp->AP.Cfg.InstanceNumber) % 2 == 0) ? 1 : 0;

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "OperatorName") == 0)
    {
        if (strcmp(pWifiApSec->Cfg.cOperatorName, pString) != 0)
        {
            if((setOperatorName(band, pString)) == ANSC_STATUS_SUCCESS)
            {
                AnscCopyString(pWifiApSec->Cfg.cOperatorName, pString);
                pWifiAp->bSecChanged  = TRUE;
            }
            else
            {
                return FALSE;
            }
        }

        return TRUE;
    }

    if (strcmp(ParamName, "LocationData") == 0)
    {
        if (strcmp(pWifiApSec->Cfg.cLocationData, pString) !=  0)
        {
            if((setLocationData(band, pString)) == ANSC_STATUS_SUCCESS)
            {
                AnscCopyString(pWifiApSec->Cfg.cLocationData, pString);
                pWifiAp->bSecChanged = TRUE;
            }
            else
            {
                return FALSE;
            }
        }

        return TRUE;
    }
    return FALSE;
}


ULONG
MacFiltTab_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pParentLinkObj  = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP_FULL          pWiFiAP         = (PCOSA_DML_WIFI_AP_FULL)pParentLinkObj->hContext;
    PSLIST_HEADER                   pListHead       = (PSLIST_HEADER)&pWiFiAP->MacFilterList;

    return AnscSListQueryDepth(pListHead);
}

ANSC_HANDLE
MacFiltTab_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pCosaContext    = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP_FULL          pWiFiAP         = (PCOSA_DML_WIFI_AP_FULL)pCosaContext->hContext;

    PSINGLE_LINK_ENTRY              pLink           = (PSINGLE_LINK_ENTRY)NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSubCosaContext = (PCOSA_CONTEXT_LINK_OBJECT)NULL;

    pLink = AnscSListGetEntryByIndex((PSLIST_HEADER)&pWiFiAP->MacFilterList, nIndex);

    if ( pLink )
    {
        pSubCosaContext = ACCESS_COSA_CONTEXT_LINK_OBJECT(pLink);
        *pInsNumber = pSubCosaContext->InstanceNumber;
    }
    
    return pSubCosaContext;
}

ANSC_HANDLE
MacFiltTab_AddEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG*                      pInsNumber
    )
{
    PCOSA_DATAMODEL_WIFI            pWiFi           = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pParentLinkObj  = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP_FULL          pWiFiAP         = (PCOSA_DML_WIFI_AP_FULL)pParentLinkObj->hContext;
    PCOSA_CONTEXT_LINK_OBJECT       pSubCosaContext = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_AP_MAC_FILTER    pMacFilt        = (PCOSA_DML_WIFI_AP_MAC_FILTER)NULL;
    
    pMacFilt = AnscAllocateMemory(sizeof(COSA_DML_WIFI_AP_MAC_FILTER));
    if ( !pMacFilt )
    {
        return NULL;
    }

    _ansc_sprintf(pMacFilt->Alias, "MacFilterTable%lu", pWiFiAP->ulMacFilterNextInsNum);
    pMacFilt->InstanceNumber = pWiFiAP->ulMacFilterNextInsNum;
    CcspTraceDebug(("pMacFilt->InstanceNumber is %lu\n", pMacFilt->InstanceNumber));
    /* Update the middle layer data */
    pSubCosaContext = (PCOSA_CONTEXT_LINK_OBJECT)AnscAllocateMemory(sizeof(COSA_CONTEXT_LINK_OBJECT));
    if ( !pSubCosaContext )
    {
        AnscFreeMemory(pMacFilt);
        return NULL;
    }
    
    pSubCosaContext->InstanceNumber = pWiFiAP->ulMacFilterNextInsNum;
    pWiFiAP->ulMacFilterNextInsNum++;
    CcspTraceDebug(("Next InstanceNumber is %lu\n", pWiFiAP->ulMacFilterNextInsNum));
    if ( 0 == pWiFiAP->ulMacFilterNextInsNum )
    {
        pWiFiAP->ulMacFilterNextInsNum = 1;
    }

    pSubCosaContext->hContext         = (ANSC_HANDLE)pMacFilt;
    pSubCosaContext->hParentTable     = pWiFiAP;
    pSubCosaContext->hPoamIrepUpperFo = pWiFi->hIrepFolderWifiAP;
    pSubCosaContext->bNew             = TRUE;

    CosaSListPushEntryByInsNum((PSLIST_HEADER)&pWiFiAP->MacFilterList, pSubCosaContext);

    CosaWifiRegAddMacFiltInfo
        (
         (ANSC_HANDLE)pWiFiAP,
         (ANSC_HANDLE)pSubCosaContext
        );

    *pInsNumber = pSubCosaContext->InstanceNumber;
    

    return pSubCosaContext;
}

ULONG
MacFiltTab_DelEntry
    (
        ANSC_HANDLE                 hInsContext,
        ANSC_HANDLE                 hInstance
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pParentLinkObj  = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP_FULL          pWiFiAP         = (PCOSA_DML_WIFI_AP_FULL)pParentLinkObj->hContext;
    PCOSA_CONTEXT_LINK_OBJECT       pSubCosaContext = (PCOSA_CONTEXT_LINK_OBJECT)hInstance;
    PCOSA_DML_WIFI_AP_MAC_FILTER    pMacFilt        = (PCOSA_DML_WIFI_AP_MAC_FILTER)pSubCosaContext->hContext;

    CosaDmlMacFilt_DelEntry(pParentLinkObj->InstanceNumber, pSubCosaContext->InstanceNumber);

    AnscSListPopEntryByLink((PSLIST_HEADER)&pWiFiAP->MacFilterList, &pSubCosaContext->Linkage);

    if ( pSubCosaContext->bNew )
    {
        CosaWifiRegDelMacFiltInfo(pWiFiAP, (ANSC_HANDLE)pSubCosaContext);
    }

    AnscFreeMemory(pMacFilt);
    AnscFreeMemory(pSubCosaContext);

    return 0;
}

ULONG
MacFiltTab_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pCosaContext    = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP_MAC_FILTER    pMacFilt        = (PCOSA_DML_WIFI_AP_MAC_FILTER)pCosaContext->hContext;
    UNREFERENCED_PARAMETER(pUlSize);    
    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "MACAddress") == 0)
    {
        /* collect value */
        AnscCopyString(pValue, pMacFilt->MACAddress);
        return 0;
    }
    if (strcmp(ParamName, "DeviceName") == 0)
    {
        /* collect value */
        AnscCopyString(pValue, pMacFilt->DeviceName);
        return 0;
    }

    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return -1;
}

BOOL
MacFiltTab_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pCosaContext    = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP_MAC_FILTER    pMacFilt        = (PCOSA_DML_WIFI_AP_MAC_FILTER)pCosaContext->hContext;
    
    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "MACAddress") == 0)
    {
        /* save update to backup */
        if (AnscSizeOfString(pString) >= sizeof(pMacFilt->MACAddress))
            return FALSE;
        AnscCopyString(pMacFilt->MACAddress, pString);
        return TRUE;
    }
    if (strcmp(ParamName, "DeviceName") == 0)
    {
        /* save update to backup */
        if (AnscSizeOfString(pString) >= sizeof(pMacFilt->DeviceName))
            return FALSE;
        AnscCopyString(pMacFilt->DeviceName, pString);
        return TRUE;
    }


    return FALSE;
}

BOOL
MacFiltTab_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(pReturnParamName);
    UNREFERENCED_PARAMETER(puLength);
    return TRUE;
}

pthread_mutex_t Delete_MacFilt_ThreadMutex = PTHREAD_MUTEX_INITIALIZER;
static void* WiFi_DeleteMacFilterTableThread(void *frArgs)
{
#define WIFI_COMP				"eRT.com.cisco.spvtg.ccsp.wifi"
#define WIFI_BUS				 "/com/cisco/spvtg/ccsp/wifi"
	char *ptable_name =( char * ) frArgs;


    pthread_mutex_lock(&Delete_MacFilt_ThreadMutex);

	//Validate passed argument
	if( NULL == ptable_name )
	{
		CcspTraceError(("%s MAC_FILTER : Invalid Argument\n",__FUNCTION__));
		pthread_mutex_unlock(&Delete_MacFilt_ThreadMutex);
		return NULL;
	}

	//Delete failed entries
	CcspTraceInfo(("%s MAC_FILTER : Cosa_DelEntry for %s \n",__FUNCTION__,ptable_name));
	Cosa_DelEntry( WIFI_COMP, WIFI_BUS, ptable_name );

	//Free allocated memory
	if( NULL != ptable_name )
	{
		AnscFreeMemory( ptable_name );
		ptable_name = NULL;
	}

    pthread_mutex_unlock(&Delete_MacFilt_ThreadMutex);
    return NULL;
}

ULONG
MacFiltTab_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pCosaContext    = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP_MAC_FILTER    pMacFilt        = (PCOSA_DML_WIFI_AP_MAC_FILTER)pCosaContext->hContext;
    PCOSA_DML_WIFI_AP_FULL          pWiFiAP         = (PCOSA_DML_WIFI_AP_FULL)pCosaContext->hParentTable;

    if ( pCosaContext->bNew )
    {
		// If add entry fails then we have to remove added DML entry
        if( ANSC_STATUS_SUCCESS != CosaDmlMacFilt_AddEntry(pWiFiAP->Cfg.InstanceNumber, pMacFilt) )
    	{
			pthread_t 	 WiFi_DelMacFilter_Thread;
			char		 table_name[128];
			char		 *ptable_name;
			int		 len;

			len = sprintf( table_name, "Device.WiFi.AccessPoint.%lu.X_CISCO_COM_MacFilterTable.%lu.", pWiFiAP->Cfg.InstanceNumber, pMacFilt->InstanceNumber);

			ptable_name = AnscAllocateMemory( len + 1 );
			if( NULL != ptable_name )
			{
                                pthread_attr_t attr;
                                pthread_attr_t *attrp = &attr;

                                memcpy( ptable_name, table_name, len + 1 );

                                pthread_attr_init(&attr);
                                pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
				pthread_create( &WiFi_DelMacFilter_Thread, attrp, WiFi_DeleteMacFilterTableThread, (void *)ptable_name); 	
                                if(attrp != NULL)
                                        pthread_attr_destroy( attrp );
			}
    	}
		else
		{
			pCosaContext->bNew = FALSE;
			CosaWifiRegDelMacFiltInfo(pWiFiAP, (ANSC_HANDLE)pCosaContext);
		}
    }
    else
    {
        if ( ANSC_STATUS_SUCCESS != CosaDmlMacFilt_SetConf(pWiFiAP->Cfg.InstanceNumber, pMacFilt->InstanceNumber, 
		     pMacFilt))
    	{
			CcspTraceWarning(("CosaDmlMacFilt_SetConf Failed to set for Inst: %lu\n", pMacFilt->InstanceNumber));
    		return ANSC_STATUS_FAILURE;
    	}
    }
    
    return 0;
}

ULONG
MacFilterTab_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pCosaContext    = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP_MAC_FILTER    pMacFilt        = (PCOSA_DML_WIFI_AP_MAC_FILTER)pCosaContext->hContext;
    PCOSA_DML_WIFI_AP_FULL          pWiFiAP         = (PCOSA_DML_WIFI_AP_FULL)pCosaContext->hParentTable;

    CosaDmlMacFilt_GetConf(pWiFiAP->Cfg.InstanceNumber, pMacFilt->InstanceNumber, pMacFilt);

    return 0;
}

BOOL
NeighboringWiFiDiagnostic_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    UNREFERENCED_PARAMETER(hInsContext);

    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;

    if (strcmp(ParamName, "Enable") == 0)
    {
	*pBool = pMyObject->Diagnostics.bEnable;
        return TRUE;
    }

	return FALSE;
}

ULONG
NeighboringWiFiDiagnostic_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(pUlSize);
    PCOSA_DATAMODEL_WIFI            pMyObject      = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    if (strcmp(ParamName, "DiagnosticsState") == 0)
    {
        AnscCopyString(pValue,pMyObject->Diagnostics.DiagnosticsState);
        return 0;
    }
    return -1;
}

BOOL
NeighboringWiFiDiagnostic_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    if (strcmp(ParamName, "Enable") == 0)
    {
// Set WiFi Neighbour Diagnostic switch value
        CosaDmlSetNeighbouringDiagnosticEnable(bValue);
	pMyObject->Diagnostics.bEnable = bValue;
        return TRUE;
    }
	return FALSE;
}


BOOL
NeighboringWiFiDiagnostic_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    PCOSA_DATAMODEL_WIFI            pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
  
    if (strcmp(ParamName, "DiagnosticsState") == 0)   {
	if( AnscEqualString(pString, "Requested", TRUE)) {
  	  	 if( pMyObject->Diagnostics.bEnable )   {
			if(AnscEqualString(pMyObject->Diagnostics.DiagnosticsState, "Requested", TRUE))
				return TRUE;
			CosaDmlWiFi_doNeighbouringScan(&pMyObject->Diagnostics);
			return TRUE;
		 }      
      		 else
	         {
			return FALSE;  	
		 }  
        }
    }
	return FALSE;  
 }



ULONG
NeighboringScanResult_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
	PCOSA_DATAMODEL_WIFI            pMyObject      = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    return pMyObject->Diagnostics.ResultCount;
}

ANSC_HANDLE
NeighboringScanResult_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
    UNREFERENCED_PARAMETER(hInsContext);
    PCOSA_DATAMODEL_WIFI            pMyObject      = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    //PCOSA_DML_NEIGHTBOURING_WIFI_RESULT pNeighbourResult = (PCOSA_DML_NEIGHTBOURING_WIFI_RESULT)pMyObject->Diagnostics.pResult;

    if ( nIndex >= pMyObject->Diagnostics.ResultCount ) 
		return NULL;
    
	*pInsNumber  = nIndex + 1; 
	if(nIndex < pMyObject->Diagnostics.ResultCount_2)
		return (ANSC_HANDLE)&pMyObject->Diagnostics.pResult_2[nIndex];
	else
		return (ANSC_HANDLE)&pMyObject->Diagnostics.pResult_5[nIndex-pMyObject->Diagnostics.ResultCount_2];
}

BOOL
NeighboringScanResult_IsUpdated
    (
        ANSC_HANDLE                 hInsContext
    )
{
	UNREFERENCED_PARAMETER(hInsContext);
	return TRUE;

}



BOOL
NeighboringScanResult_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
    PCOSA_DML_NEIGHTBOURING_WIFI_RESULT       pResult       = (PCOSA_DML_NEIGHTBOURING_WIFI_RESULT)hInsContext;

    if (strcmp(ParamName, "SignalStrength") == 0)    {
        *pInt = pResult->SignalStrength;        
        return TRUE;
    }

    if (strcmp(ParamName, "Noise") == 0)    {
        *pInt = pResult->Noise;        
        return TRUE;
    }
    return FALSE;
}

BOOL
NeighboringScanResult_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
	PCOSA_DML_NEIGHTBOURING_WIFI_RESULT       pResult       = (PCOSA_DML_NEIGHTBOURING_WIFI_RESULT)hInsContext;
    
    if (strcmp(ParamName, "DTIMPeriod") == 0)    {
        *puLong = pResult->DTIMPeriod;
        return TRUE;
    }
    if (strcmp(ParamName, "X_COMCAST-COM_ChannelUtilization") == 0)    {
        *puLong = pResult->ChannelUtilization; 
        return TRUE;
    }
    if (strcmp(ParamName, "Channel") == 0)    {
		*puLong = pResult->Channel; 
        return TRUE;  
    }
    if (strcmp(ParamName, "BeaconPeriod") == 0)   {
       *puLong = pResult->BeaconPeriod;
       return TRUE;
    }

    return FALSE;
}

ULONG
NeighboringScanResult_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
	PCOSA_DML_NEIGHTBOURING_WIFI_RESULT       pResult       = (PCOSA_DML_NEIGHTBOURING_WIFI_RESULT)hInsContext;
	UNREFERENCED_PARAMETER(pUlSize);    
	if (strcmp(ParamName, "Radio") == 0)    {
		if(AnscEqualString(pResult->OperatingFrequencyBand, "6GHz", TRUE))    
			AnscCopyString(pValue, "Device.WiFi.Radio.3");  
		else if(AnscEqualString(pResult->OperatingFrequencyBand, "5GHz", TRUE))    
			AnscCopyString(pValue, "Device.WiFi.Radio.2");  
		else
			AnscCopyString(pValue, "Device.WiFi.Radio.1");  		
        return 0;
    }	
    if (strcmp(ParamName, "EncryptionMode") == 0)    {
        AnscCopyString(pValue,pResult->EncryptionMode);   
        return 0;
    }
    if (strcmp(ParamName, "Mode") == 0)    {
        AnscCopyString(pValue,pResult->Mode);  
        return 0;  
    }
    if (strcmp(ParamName, "SecurityModeEnabled") == 0)    {
        AnscCopyString(pValue,pResult->SecurityModeEnabled);  
        return 0;  
    }
    if (strcmp(ParamName, "BasicDataTransferRates") == 0)    {
        AnscCopyString(pValue,pResult->BasicDataTransferRates); 
        return 0;  
    } 
    if (strcmp(ParamName, "SupportedDataTransferRates") == 0)    {
       AnscCopyString(pValue,pResult->SupportedDataTransferRates); 
        return 0;  
    }
    if (strcmp(ParamName, "OperatingChannelBandwidth") == 0)    {
        AnscCopyString(pValue,pResult->OperatingChannelBandwidth); 
        return 0;
    }
    if (strcmp(ParamName, "OperatingStandards") == 0)    {
        AnscCopyString(pValue,pResult->OperatingStandards); 
        return 0;
    } 
    if (strcmp(ParamName, "SupportedStandards") == 0)    {
       AnscCopyString(pValue,pResult->SupportedStandards);  
       return 0;
    } 
    if (strcmp(ParamName, "BSSID") == 0)    {
        AnscCopyString(pValue,pResult->BSSID); 
        return 0;
    }     
    if (strcmp(ParamName, "SSID") == 0)     {
        AnscCopyString(pValue,pResult->SSID); 
        return 0;  
    }
    if (strcmp(ParamName, "OperatingFrequencyBand") == 0)    {
        AnscCopyString(pValue,pResult->OperatingFrequencyBand);
        return 0;
    }    

    return -1; 
 
 }

 /***********************************************************************
 
  APIs for Object:
 
	 WiFi.X_RDKCENTRAL-COM_BandSteering.
 
	 *	BandSteering_GetParamBoolValue
	 *	BandSteering_SetParamBoolValue
	 *    BandSteering_GetParamStringValue
	 *	BandSteering_Validate
	 *	BandSteering_Commit
	 *	BandSteering_Rollback
 
 ***********************************************************************/
 /**********************************************************************  
 
	 caller:	 owner of this object 
 
	 prototype: 
 
		 BOOL
		 BandSteering_GetParamBoolValue
			 (
				 ANSC_HANDLE				 hInsContext,
				 char*						 ParamName,
				 BOOL*						 pBool
			 );
 
	 description:
 
		 This function is called to retrieve Boolean parameter value; 
 
	 argument:	 ANSC_HANDLE				 hInsContext,
				 The instance handle;
 
				 char*						 ParamName,
				 The parameter name;
 
				 BOOL*						 pBool
				 The buffer of returned boolean value;
 
	 return:	 TRUE if succeeded.
 
 **********************************************************************/
 BOOL
 BandSteering_GetParamBoolValue
	 (
		 ANSC_HANDLE				 hInsContext,
		 char*						 ParamName,
		 BOOL*						 pBool
	 )
 {
	 UNREFERENCED_PARAMETER(hInsContext);
	 PCOSA_DATAMODEL_WIFI			 pMyObject		 = (PCOSA_DATAMODEL_WIFI	 )g_pCosaBEManager->hWifi;
 	 PCOSA_DML_WIFI_BANDSTEERING	 pBandSteering   = pMyObject->pBandSteering;
	 /* check the parameter name and return the corresponding value */
	 if (strcmp(ParamName, "Enable") == 0)
	 {
		*pBool = pBandSteering->BSOption.bEnable;
		 return TRUE;
	 }

	 if (strcmp(ParamName, "Capability") == 0)
	 {
		 *pBool = pBandSteering->BSOption.bCapability;
		 return TRUE;
	 }

	 /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
	 return FALSE;
 }

 /**********************************************************************  
 
	 caller:	 owner of this object 
 
	 prototype: 
 
		 BOOL
		 BandSteering_SetParamBoolValue
			 (
				 ANSC_HANDLE				 hInsContext,
				 char*						 ParamName,
				 BOOL						 bValue
			 );
 
	 description:
 
		 This function is called to set BOOL parameter value; 
 
	 argument:	 ANSC_HANDLE				 hInsContext,
				 The instance handle;
 
				 char*						 ParamName,
				 The parameter name;
 
				 BOOL						 bValue
				 The updated BOOL value;
 
	 return:	 TRUE if succeeded.
 
 **********************************************************************/
 BOOL
 BandSteering_SetParamBoolValue
	 (
		 ANSC_HANDLE				 hInsContext,
		 char*						 ParamName,
		 BOOL						 bValue
	 )
 {
	 UNREFERENCED_PARAMETER(hInsContext);
	 PCOSA_DATAMODEL_WIFI			 pMyObject		 = (PCOSA_DATAMODEL_WIFI	 )g_pCosaBEManager->hWifi;
 	 PCOSA_DML_WIFI_BANDSTEERING	 pBandSteering   = pMyObject->pBandSteering;
	 char ssid0[WIFI_AP_MAX_SSID_LEN], ssid1[WIFI_AP_MAX_SSID_LEN];
	 errno_t rc = -1;
	 int ind = -1;

	 /* check the parameter name and set the corresponding value */
	 if (strcmp(ParamName, "Enable") == 0)
	 {
#if defined(_HUB4_PRODUCT_REQ_)
		return FALSE;
#endif /* _HUB4_PRODUCT_REQ_ */

		if( bValue != pBandSteering->BSOption.bEnable )
		{
			if( (TRUE == bValue) && is_mesh_enabled())
			{
				CcspWifiTrace(("RDK_LOG_WARN,BAND_STEERING_ERROR:Fail to enable Band Steering when Mesh is on \n"));
				return FALSE;
			}
#if defined(_PLATFORM_RASPBERRYPI_)
			if( pBandSteering->BSOption.bCapability == FALSE)
				return FALSE;
#endif
			//In Split SSID, BandSteering should not be Enabled
                  	if( bValue == TRUE )
                        {
                          	memset(ssid0, 0, WIFI_AP_MAX_SSID_LEN);
                          	memset(ssid1, 0, WIFI_AP_MAX_SSID_LEN);
                          	//Get SSID of AP Index 0 and 1 
	 			wifi_getSSIDName(0,ssid0);
	 			wifi_getSSIDName(1,ssid1);
								
                          	//Compare SSID Names of 0 and 1 
                          	rc = strcmp_s(ssid1,WIFI_AP_MAX_SSID_LEN,ssid0,&ind);
				if((rc == EOK) && (ind))
				{
					CcspTraceError(("BAND_STEERING_ERROR: Failed to enable Band Steering when SSIDs are Different \n"));
					return FALSE;
				}
				
				memset(ssid0, 0, WIFI_AP_MAX_SSID_LEN);
				memset(ssid1, 0, WIFI_AP_MAX_SSID_LEN);
				//Get KeyPassphrase of SSID 0 and 1
				wifi_getApSecurityKeyPassphrase(0,ssid0);
				wifi_getApSecurityKeyPassphrase(1,ssid1);                          
				
                          	//Compare SSID Passwords of 0 and 1
                          	rc = strcmp_s(ssid1,WIFI_AP_MAX_SSID_LEN,ssid0,&ind);
				if((rc == EOK) && (ind))
				{
					CcspTraceError(("BAND_STEERING_ERROR: Failed enable Band Steering when SSID Passwords are Different \n"));
					return FALSE;
				}
                        }
			pBandSteering->BSOption.bEnable = bValue;
			pBandSteering->bBSOptionChanged = TRUE;
		}
		return TRUE; 
	}

	 /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
	 return FALSE;
 }

 /**********************************************************************  
 
	 caller:	 owner of this object 
 
	 prototype: 
 
		 ULONG
		 BandSteering_GetParamStringValue
			 (
				 ANSC_HANDLE				 hInsContext,
				 char*						 ParamName,
				 char*						 pValue,
				 ULONG* 					 pUlSize
			 );
 
	 description:
 
		 This function is called to retrieve string parameter value; 
 
	 argument:	 ANSC_HANDLE				 hInsContext,
				 The instance handle;
 
				 char*						 ParamName,
				 The parameter name;
 
				 char*						 pValue,
				 The string value buffer;
 
				 ULONG* 					 pUlSize
				 The buffer of length of string value;
				 Usually size of 1023 will be used.
				 If it's not big enough, put required size here and return 1;
 
	 return:	 0 if succeeded;
				 1 if short of buffer size; (*pUlSize = required size)
				 -1 if not supported.
 
 **********************************************************************/
 ULONG
 BandSteering_GetParamStringValue
	 (
		 ANSC_HANDLE				 hInsContext,
		 char*						 ParamName,
		 char*						 pValue,
		 ULONG* 					 pUlSize
	 )
 {
 	 UNREFERENCED_PARAMETER(hInsContext);
	 PCOSA_DATAMODEL_WIFI			 pMyObject		 = (PCOSA_DATAMODEL_WIFI	 )g_pCosaBEManager->hWifi;
 	 PCOSA_DML_WIFI_BANDSTEERING	 pBandSteering   = pMyObject->pBandSteering;
    
	 /* check the parameter name and return the corresponding value */

	 if (strcmp(ParamName, "APGroup") == 0)
	 {
		/* collect value */
		 if ( ( sizeof(pBandSteering->BSOption.APGroup ) - 1 ) < *pUlSize)
		 {
			 AnscCopyString(pValue, pBandSteering->BSOption.APGroup);

			 return 0;
		 }
		 else
		 {
			 *pUlSize = sizeof(pBandSteering->BSOption.APGroup);
 
			 return 1;
		 }

		 return 0;
	 }

	 if (strcmp(ParamName, "History") == 0)
	 {
		 /* collect value */
		 if ( ( sizeof(pBandSteering->BSOption.BandHistory ) - 1 ) < *pUlSize)
		 {
			 CosaDmlWiFi_GetBandSteeringLog( pBandSteering->BSOption.BandHistory, 
			 								 sizeof(pBandSteering->BSOption.BandHistory) );

			 AnscCopyString(pValue, pBandSteering->BSOption.BandHistory);

			 return 0;
		 }
		 else
		 {
			 *pUlSize = sizeof(pBandSteering->BSOption.BandHistory);
			 
			 return 1;
		 }
		 
		 return 0;
	 }

	 /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
	 return -1;
 }

 /**********************************************************************  
 
	 caller:	 owner of this object 
 
	 prototype: 
 
		 ULONG
		 BandSteering_SetParamStringValue
			 (
				 ANSC_HANDLE				 hInsContext,
				 char*				         ParamName,
				 char*					 pString,
			 );
 
	 description:
 
		 This function is called to retrieve string parameter value; 
 
	 argument:	 ANSC_HANDLE				 hInsContext,
			 The instance handle;
 
			 char*					 ParamName,
			 The parameter name;
 
			 char*					 pString,
			 The string value buffer;
 
 
	 return:	 TRUE if succeeded.
 
 **********************************************************************/
 BOOL
 BandSteering_SetParamStringValue
	 (
		 ANSC_HANDLE				hInsContext,
		 char*					ParamName,
		 char*					pString
	 )
 {
	 UNREFERENCED_PARAMETER(hInsContext);
	 PCOSA_DATAMODEL_WIFI		 pMyObject	= (PCOSA_DATAMODEL_WIFI	 )g_pCosaBEManager->hWifi;
 	 PCOSA_DML_WIFI_BANDSTEERING	 pBandSteering  = pMyObject->pBandSteering;
 
	 /* check the parameter name and return the corresponding value */

	 if (strcmp(ParamName, "APGroup") == 0)
	 {
		/* save update to backup */
		//AnscCopyString(pBandSteering->BSOption.APGroup, pString);
                /*CID: 135597 BUFFER_SIZE_WARNING */
         strncpy(pBandSteering->BSOption.APGroup, pString, sizeof(pBandSteering->BSOption.APGroup)-1);
         pBandSteering->BSOption.APGroup[sizeof(pBandSteering->BSOption.APGroup)-1]='\0';
         pBandSteering->bBSOptionChanged = TRUE;
         return TRUE;
	 }

	 /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
	 return FALSE;
 }

 /**********************************************************************  
 
	 caller:	 owner of this object 
 
	 prototype: 
 
		 BOOL
		 BandSteering_Validate
			 (
				 ANSC_HANDLE				 hInsContext,
				 char*						 pReturnParamName,
				 ULONG* 					 puLength
			 );
 
	 description:
 
		 This function is called to finally commit all the update.
 
	 argument:	 ANSC_HANDLE				 hInsContext,
				 The instance handle;
 
				 char*						 pReturnParamName,
				 The buffer (128 bytes) of parameter name if there's a validation. 
 
				 ULONG* 					 puLength
				 The output length of the param name. 
 
	 return:	 TRUE if there's no validation.
 
 **********************************************************************/
 BOOL
 BandSteering_Validate
	 (
		 ANSC_HANDLE				 hInsContext,
		 char*						 pReturnParamName,
		 ULONG* 					 puLength
	 )
 {
     UNREFERENCED_PARAMETER(hInsContext);
     UNREFERENCED_PARAMETER(pReturnParamName);
     UNREFERENCED_PARAMETER(puLength);
	 return TRUE;
 }
 
 /**********************************************************************  
 
	 caller:	 owner of this object 
 
	 prototype: 
 
		 ULONG
		 BandSteering_Commit
			 (
				 ANSC_HANDLE				 hInsContext
			 );
 
	 description:
 
		 This function is called to finally commit all the update.
 
	 argument:	 ANSC_HANDLE				 hInsContext,
				 The instance handle;
 
	 return:	 The status of the operation.
 
 **********************************************************************/
 ULONG
 BandSteering_Commit
	 (
		 ANSC_HANDLE				 hInsContext
	 )
 {
	 UNREFERENCED_PARAMETER(hInsContext);
	 PCOSA_DATAMODEL_WIFI			 pMyObject	   = (PCOSA_DATAMODEL_WIFI	   )g_pCosaBEManager->hWifi;
     PCOSA_DML_WIFI_BANDSTEERING	 pBandSteering = pMyObject->pBandSteering;
 
	 /* Set the Band Steering Current Options */
 	 if ( TRUE == pBandSteering->bBSOptionChanged )
 	 {
		 CosaDmlWiFi_SetBandSteeringOptions( &pBandSteering->BSOption );
		 pBandSteering->bBSOptionChanged = FALSE;


 	 }

	 return ANSC_STATUS_SUCCESS;
 }
 
 /**********************************************************************  
 
	 caller:	 owner of this object 
 
	 prototype: 
 
		 ULONG
		 BandSteering_Rollback
			 (
				 ANSC_HANDLE				 hInsContext
			 );
 
	 description:
 
		 This function is called to roll back the update whenever there's a 
		 validation found.
 
	 argument:	 ANSC_HANDLE				 hInsContext,
				 The instance handle;
 
	 return:	 The status of the operation.
 
 **********************************************************************/
 ULONG
 BandSteering_Rollback
	 (
		 ANSC_HANDLE				 hInsContext
	 )
 {  
	 UNREFERENCED_PARAMETER(hInsContext);
	 PCOSA_DATAMODEL_WIFI			 pMyObject		 = (PCOSA_DATAMODEL_WIFI	 )g_pCosaBEManager->hWifi;
 	 PCOSA_DML_WIFI_BANDSTEERING	 pBandSteering   = pMyObject->pBandSteering;

	 CosaDmlWiFi_GetBandSteeringOptions( &pBandSteering->BSOption );
	 pBandSteering->bBSOptionChanged = FALSE;

	 return ANSC_STATUS_SUCCESS;
 }

 /***********************************************************************
 
  APIs for Object:
 
	 WiFi.X_RDKCENTRAL-COM_BandSteering.BandSetting.{i}.
 
	 *    BandSetting_GetEntryCount
	 *    BandSetting_GetEntry
	 *	BandSetting_GetParamIntValue
	 *	BandSetting_SetParamIntValue
	 *	BandSteering_Validate
	 *	BandSteering_Commit
	 *	BandSteering_Rollback
 
 ***********************************************************************/
 /**********************************************************************  
 
	 caller:	 owner of this object 
 
	 prototype: 
 
		 ULONG
		 BandSetting_GetEntryCount
			 (
				 ANSC_HANDLE				 hInsContext
			 );
 
	 description:
 
		 This function is called to retrieve the count of the table.
 
	 argument:	 ANSC_HANDLE				 hInsContext,
				 The instance handle;
 
	 return:	 The count of the table
 
 **********************************************************************/
 ULONG
 BandSetting_GetEntryCount
	 (
		 ANSC_HANDLE				 hInsContext
	 )
 {
	 UNREFERENCED_PARAMETER(hInsContext);
	 PCOSA_DATAMODEL_WIFI			 pMyObject		 = (PCOSA_DATAMODEL_WIFI	 )g_pCosaBEManager->hWifi;
	 PCOSA_DML_WIFI_BANDSTEERING	 pBandSteering	 = pMyObject->pBandSteering;
	 
	 return pBandSteering->RadioCount;
 }

/**********************************************************************  

	caller: 	owner of this object 

	prototype: 

		ANSC_HANDLE
		BandSetting_GetEntry
			(
				ANSC_HANDLE 				hInsContext,
				ULONG						nIndex,
				ULONG*						pInsNumber
			);

	description:

		This function is called to retrieve the entry specified by the index.

	argument:	ANSC_HANDLE 				hInsContext,
				The instance handle;

				ULONG						nIndex,
				The index of this entry;

				ULONG*						pInsNumber
				The output instance number;

	return: 	The handle to identify the entry

**********************************************************************/
ANSC_HANDLE
BandSetting_GetEntry
	(
		ANSC_HANDLE 				hInsContext,
		ULONG						nIndex,
		ULONG*						pInsNumber
	)
{
	UNREFERENCED_PARAMETER(hInsContext);
	PCOSA_DATAMODEL_WIFI			pMyObject		= (PCOSA_DATAMODEL_WIFI 	)g_pCosaBEManager->hWifi;
	PCOSA_DML_WIFI_BANDSTEERING 	pBandSteering	= pMyObject->pBandSteering;
	if( ( NULL != pBandSteering ) && \
		( nIndex < (ULONG)pBandSteering->RadioCount )
	  )
	{
		*pInsNumber = pBandSteering->pBSSettings[ nIndex ].InstanceNumber;
		
		return ( &pBandSteering->pBSSettings[ nIndex ] ); /* return the handle */
	}
	
	return NULL; /* return the NULL for invalid index */
}

/**********************************************************************  
 
	 caller:	 owner of this object 
 
	 prototype: 
 
		 BOOL
		 BandSetting_GetParamIntValue
			 (
				 ANSC_HANDLE				 hInsContext,
				 char*						 ParamName,
				 int*						 pInt
			 );
 
	 description:
 
		 This function is called to retrieve integer parameter value; 
 
	 argument:	 ANSC_HANDLE				 hInsContext,
				 The instance handle;
 
				 char*						 ParamName,
				 The parameter name;
 
				 int*						 pInt
				 The buffer of returned integer value;
 
	 return:	 TRUE if succeeded.
 
 **********************************************************************/
 BOOL
 BandSetting_GetParamIntValue
	 (
		 ANSC_HANDLE				 hInsContext,
		 char*						 ParamName,
		 int*						 pInt
	 )
 {
	PCOSA_DML_WIFI_BANDSTEERING_SETTINGS pBandSteeringSettings	= ( PCOSA_DML_WIFI_BANDSTEERING_SETTINGS )hInsContext;
	if (strcmp(ParamName, "UtilizationThreshold") == 0)
	{
		 /* collect value */
		 *pInt = pBandSteeringSettings->UtilizationThreshold;
		 
		 return TRUE;
	}
	
	if (strcmp(ParamName, "RSSIThreshold") == 0)
	{
		 /* collect value */
		 *pInt = pBandSteeringSettings->RSSIThreshold;
		 
		 return TRUE;
	}

	 if (strcmp(ParamName, "PhyRateThreshold") == 0)
	 {
		  /* collect value */
		  *pInt = pBandSteeringSettings->PhyRateThreshold;
		  
		  return TRUE;
	 }

	 if (strcmp(ParamName, "OverloadInactiveTime") == 0)
	 {
		  /* collect value */
		  *pInt = pBandSteeringSettings->OverloadInactiveTime;

		  return TRUE;
	 }

	 if (strcmp(ParamName, "IdleInactiveTime") == 0)
	 {
		  /* collect value */
		  *pInt = pBandSteeringSettings->IdleInactiveTime;

		  return TRUE;
	 }

 	 /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
	 return FALSE;
 }

 /**********************************************************************  
 
	 caller:	 owner of this object 
 
	 prototype: 
 
		 BOOL
		 BandSetting_SetParamIntValue
			 (
				 ANSC_HANDLE				 hInsContext,
				 char*						 ParamName,
				 int						 iValue
			 );
 
	 description:
 
		 This function is called to set integer parameter value; 
 
	 argument:	 ANSC_HANDLE				 hInsContext,
				 The instance handle;
 
				 char*						 ParamName,
				 The parameter name;
 
				 int						 iValue
				 The updated integer value;
 
	 return:	 TRUE if succeeded.
 
 **********************************************************************/
 BOOL
 BandSetting_SetParamIntValue
	 (
		 ANSC_HANDLE				 hInsContext,
		 char*						 ParamName,
		 int						 iValue
	 )
 {
	 PCOSA_DATAMODEL_WIFI			 	  pMyObject		 		 = (PCOSA_DATAMODEL_WIFI	 )g_pCosaBEManager->hWifi;
	 PCOSA_DML_WIFI_BANDSTEERING	 	  pBandSteering	 		 = pMyObject->pBandSteering;
	 PCOSA_DML_WIFI_BANDSTEERING_SETTINGS pBandSteeringSettings  = ( PCOSA_DML_WIFI_BANDSTEERING_SETTINGS )hInsContext;

	 /* check the parameter name and set the corresponding value */
	 if (strcmp(ParamName, "UtilizationThreshold") == 0)
	 {
		 /* save update to backup */
		 pBandSteeringSettings->UtilizationThreshold = iValue;
		 pBandSteering->bBSSettingsChanged			 = TRUE;
		 
		 return TRUE;
	 }
 
	 if (strcmp(ParamName, "RSSIThreshold") == 0)
	 {
		 /* save update to backup */
		 pBandSteeringSettings->RSSIThreshold = iValue;
		 pBandSteering->bBSSettingsChanged	  = TRUE;
		 
		 return TRUE;
	 }

	 if (strcmp(ParamName, "PhyRateThreshold") == 0)
	 {
		 /* save update to backup */
		 pBandSteeringSettings->PhyRateThreshold = iValue;
		 pBandSteering->bBSSettingsChanged		 = TRUE;
		 
		 return TRUE;
	 }

	 if (strcmp(ParamName, "OverloadInactiveTime") == 0)
	 {
		 /* save update to backup */
		 pBandSteeringSettings->OverloadInactiveTime = iValue;
		 pBandSteering->bBSSettingsChanged	     = TRUE;

		 return TRUE;
	 }

	 if (strcmp(ParamName, "IdleInactiveTime") == 0)
	 {
		 /* save update to backup */
		 pBandSteeringSettings->IdleInactiveTime = iValue;
		 pBandSteering->bBSSettingsChanged	 = TRUE;

		 return TRUE;
	 }

	 /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
	 return FALSE;
 }

 /**********************************************************************  
 
	 caller:	 owner of this object 
 
	 prototype: 
 
		 BOOL
		 BandSetting_Validate
			 (
				 ANSC_HANDLE				 hInsContext,
				 char*						 pReturnParamName,
				 ULONG* 					 puLength
			 );
 
	 description:
 
		 This function is called to finally commit all the update.
 
	 argument:	 ANSC_HANDLE				 hInsContext,
				 The instance handle;
 
				 char*						 pReturnParamName,
				 The buffer (128 bytes) of parameter name if there's a validation. 
 
				 ULONG* 					 puLength
				 The output length of the param name. 
 
	 return:	 TRUE if there's no validation.
 
 **********************************************************************/
 BOOL
 BandSetting_Validate
	 (
		 ANSC_HANDLE				 hInsContext,
		 char*						 pReturnParamName,
		 ULONG* 					 puLength
	 )
 {
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(pReturnParamName);
    UNREFERENCED_PARAMETER(puLength);
    return TRUE;
 }
 
 /**********************************************************************  
 
	 caller:	 owner of this object 
 
	 prototype: 
 
		 ULONG
		 BandSetting_Commit
			 (
				 ANSC_HANDLE				 hInsContext
			 );
 
	 description:
 
		 This function is called to finally commit all the update.
 
	 argument:	 ANSC_HANDLE				 hInsContext,
				 The instance handle;
 
	 return:	 The status of the operation.
 
 **********************************************************************/
 ULONG
 BandSetting_Commit
	 (
		 ANSC_HANDLE				 hInsContext
	 )
 {
	 PCOSA_DATAMODEL_WIFI			 	  pMyObject		 		= (PCOSA_DATAMODEL_WIFI	 )g_pCosaBEManager->hWifi;
 	 PCOSA_DML_WIFI_BANDSTEERING	 	  pBandSteering   		= pMyObject->pBandSteering;
	 PCOSA_DML_WIFI_BANDSTEERING_SETTINGS pBandSteeringSettings = ( PCOSA_DML_WIFI_BANDSTEERING_SETTINGS )hInsContext;
	 /* Set the Band Steering Current Options */
 	 if ( TRUE == pBandSteering->bBSSettingsChanged )
 	 {
		 CosaDmlWiFi_SetBandSteeringSettings( pBandSteeringSettings->InstanceNumber - 1,
		 									  pBandSteeringSettings );
		 
		 pBandSteering->bBSSettingsChanged = FALSE;
 	 }

	 return ANSC_STATUS_SUCCESS;
 }
 
 /**********************************************************************  
 
	 caller:	 owner of this object 
 
	 prototype: 
 
		 ULONG
		 BandSetting_Rollback
			 (
				 ANSC_HANDLE				 hInsContext
			 );
 
	 description:
 
		 This function is called to roll back the update whenever there's a 
		 validation found.
 
	 argument:	 ANSC_HANDLE				 hInsContext,
				 The instance handle;
 
	 return:	 The status of the operation.
 
 **********************************************************************/
 ULONG
 BandSetting_Rollback
	 (
		 ANSC_HANDLE				 hInsContext
	 )
 {
	 UNREFERENCED_PARAMETER(hInsContext);
	 PCOSA_DATAMODEL_WIFI			 pMyObject		 = (PCOSA_DATAMODEL_WIFI	 )g_pCosaBEManager->hWifi;
 	 PCOSA_DML_WIFI_BANDSTEERING	 pBandSteering   = pMyObject->pBandSteering;
	 int 							 iLoopCount;
	 /* Load Previous Values for Band Steering Settings */
	 for ( iLoopCount = 0; iLoopCount < pBandSteering->RadioCount ; ++iLoopCount )
	 {
		 memset( &pBandSteering->pBSSettings[ iLoopCount ], 0, sizeof( COSA_DML_WIFI_BANDSTEERING_SETTINGS ) );

		 /* Instance Number Always from 1 */
		 pBandSteering->pBSSettings[ iLoopCount ].InstanceNumber = iLoopCount + 1;
	 
		 CosaDmlWiFi_GetBandSteeringSettings( iLoopCount, 
											   &pBandSteering->pBSSettings[ iLoopCount ] );
	 }

	 pBandSteering->bBSSettingsChanged = FALSE;

	 return ANSC_STATUS_SUCCESS;
 }
 

 /***********************************************************************

 APIs for Object:

    WiFi.X_RDKCENTRAL-COM_ATM

    *  ATM_GetParamBoolValue
    *  ATM_GetParamUlongValue
	*  ATM_SetParamBoolValue
	
***********************************************************************/
/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        ATM_GetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:

        This function is called to retrieve Boolean parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL*                       pBool
                The buffer of returned boolean value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
ATM_GetParamBoolValue
(
	ANSC_HANDLE                 hInsContext,
	char*                       ParamName,
	BOOL*                       pBool
)
{
	UNREFERENCED_PARAMETER(hInsContext);
	PCOSA_DATAMODEL_WIFI	pMyObject	= (PCOSA_DATAMODEL_WIFI	 )g_pCosaBEManager->hWifi;
	PCOSA_DML_WIFI_ATM	 	pATM   = pMyObject->pATM;
	if (strcmp(ParamName, "Capable") == 0) {
		*pBool = pATM->Capable;		
		return TRUE;
	}

    if (strcmp(ParamName, "Enable") == 0) {
		*pBool = pATM->Enable;		
		return TRUE;
	}

    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        ATM_SetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL                        bValue
            );

    description:

        This function is called to set BOOL parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL                        bValue
                The updated BOOL value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
ATM_SetParamBoolValue
(
	ANSC_HANDLE                 hInsContext,
	char*                       ParamName,
	BOOL                        bValue
)
{
	UNREFERENCED_PARAMETER(hInsContext);
	PCOSA_DATAMODEL_WIFI	pMyObject	= (PCOSA_DATAMODEL_WIFI	 )g_pCosaBEManager->hWifi;
	PCOSA_DML_WIFI_ATM	 	pATM   = pMyObject->pATM;
    if (strcmp(ParamName, "Enable") == 0) {
		CosaDmlWiFi_SetATMEnable(pATM, bValue);
		return TRUE;
	}

	return FALSE;
}

BOOL
ATM_Validate
(
	ANSC_HANDLE				hInsContext,
	char*					pReturnParamName,
	ULONG* 					puLength
)
{
    UNREFERENCED_PARAMETER(hInsContext);
    UNREFERENCED_PARAMETER(pReturnParamName);
    UNREFERENCED_PARAMETER(puLength);
    return TRUE;
}


ULONG
ATM_Commit
(
	ANSC_HANDLE				 hInsContext
)
{
    UNREFERENCED_PARAMETER(hInsContext);
	//CosaDmlWiFi_SetATMEnable(pATM, bValue);
	return ANSC_STATUS_SUCCESS;
}

ULONG
ATM_Rollback
(
	ANSC_HANDLE				 hInsContext
)
{
    UNREFERENCED_PARAMETER(hInsContext);
	//CosaDmlWiFi_GetATMEnable(pATM, &bValue);
	return ANSC_STATUS_SUCCESS;
}
 
/***********************************************************************

 APIs for Object:

    WiFi.APGroup.{i}.

    *  APGroup_GetEntryCount
    *  APGroup_GetEntry
    *  APGroup_AddEntry
    *  APGroup_DelEntry
    *  APGroup_GetParamUlongValue
    *  APGroup_GetParamStringValue
    *  APGroup_Validate
    *  APGroup_Commit
    *  APGroup_Rollback

***********************************************************************/

ULONG
APGroup_GetEntryCount
(
	ANSC_HANDLE                 hInsContext
)
{
	CcspTraceDebug(("APGroup_GetEntryCount \n"));
	UNREFERENCED_PARAMETER(hInsContext);
	PCOSA_DATAMODEL_WIFI            pWiFi     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_WIFI_ATM				pATM = pWiFi->pATM;
	
    return pATM->grpCount;
	
	
}


ANSC_HANDLE
APGroup_GetEntry
(
	ANSC_HANDLE                 hInsContext,
	ULONG                       nIndex,
	ULONG*                      pInsNumber
)
{
	UNREFERENCED_PARAMETER(hInsContext);
	CcspTraceDebug(("APGroup_GetEntry '%lu'\n", nIndex));
	PCOSA_DATAMODEL_WIFI            pWiFi     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_WIFI_ATM				pATM = pWiFi->pATM;
	//PCOSA_DML_WIFI_ATM_APGROUP		pATMApGroup=&pATM->APGroup;
	
	if(nIndex < pATM->grpCount) {
		*pInsNumber=nIndex+1;
		return pATM->APGroup+nIndex;
	}
	return NULL;
}


/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        APGroup_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
APGroup_GetParamStringValue

    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
	UNREFERENCED_PARAMETER(pUlSize);
	CcspTraceDebug(("APGroup_GetParamStringValue parameter '%s'\n", ParamName));
	PCOSA_DML_WIFI_ATM_APGROUP      pWifiApGrp    = (PCOSA_DML_WIFI_ATM_APGROUP)hInsContext;
	
	if (strcmp(ParamName, "APList") == 0) {
    	AnscCopyString(pValue, pWifiApGrp->APList);
        return 0;
    }

    return -1;
}


/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        APGroup_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
APGroup_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    CcspTraceDebug(("APGroup_GetParamUlongValue parameter '%s'\n", ParamName));
	PCOSA_DML_WIFI_ATM_APGROUP      pWifiApGrp    = (PCOSA_DML_WIFI_ATM_APGROUP)hInsContext;
	
	if (strcmp(ParamName, "AirTimePercent") == 0) {
        *puLong = pWifiApGrp->AirTimePercent; // collect from corresponding AP object
		return TRUE;
    }

    return FALSE;
}

BOOL
APGroup_SetParamUlongValue (
	ANSC_HANDLE                 hInsContext,
	char*                       ParamName,
	ULONG                       uValue
)
{
    CcspTraceDebug(("APGroup_SetParamUlongValue parameter '%s'\n", ParamName));
CcspTraceDebug(("---- %s %s \n", __func__, 	ParamName));
	PCOSA_DML_WIFI_ATM_APGROUP      pWifiApGrp    = (PCOSA_DML_WIFI_ATM_APGROUP)hInsContext;
	
	if (strcmp(ParamName, "AirTimePercent") == 0)   {
		if( uValue > 100)
		{
			return FALSE;
		}
		pWifiApGrp->AirTimePercent= uValue;
		CosaDmlWiFi_SetATMAirTimePercent(pWifiApGrp->APList, pWifiApGrp->AirTimePercent);		
        return TRUE;
    }
	
    return FALSE;
}


/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        APGroup_Validate
            (
                ANSC_HANDLE                 hInsContext,
                char*                       pReturnParamName,
                ULONG*                      puLength
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       pReturnParamName,
                The buffer (128 bytes) of parameter name if there's a validation. 

                ULONG*                      puLength
                The output length of the param name. 

    return:     TRUE if there's no validation.

**********************************************************************/
BOOL
APGroup_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
	UNREFERENCED_PARAMETER(hInsContext);
	UNREFERENCED_PARAMETER(puLength);
	CcspTraceDebug(("APGroup_Validate parameter '%s'\n", pReturnParamName));
    return TRUE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        APGroup_Commit
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
APGroup_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
	UNREFERENCED_PARAMETER(hInsContext);
	CcspTraceDebug(("APGroup_Commit parameter \n"));
    return 0;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        APGroup_Rollback
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to roll back the update whenever there's a 
        validation found.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
APGroup_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
	UNREFERENCED_PARAMETER(hInsContext);
	CcspTraceInfo(("APGroup_Rollback parameter \n"));
    return ANSC_STATUS_SUCCESS;
}

/***********************************************************************

 APIs for Object:

    WiFi.X_RDKCENTRAL-COM_ATM.APGroup.{i}.Sta.{j}.

    *  Sta_GetEntryCount
    *  Sta_GetEntry
    *  Sta_AddEntry
    *  Sta_DelEntry
    *  Sta_GetParamUlongValue
    *  Sta_GetParamStringValue
    *  Sta_SetParamUlongValue
    *  Sta_SetParamStringValue
    *  Sta_Validate
    *  Sta_Commit
    *  Sta_Rollback

***********************************************************************/
/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        Sta_GetEntryCount
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to retrieve the count of the table.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The count of the table

**********************************************************************/
ULONG
Sta_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
	PCOSA_DML_WIFI_ATM_APGROUP	pATMApGroup=(PCOSA_DML_WIFI_ATM_APGROUP)hInsContext;
	return pATMApGroup->NumberSta; 
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ANSC_HANDLE
        Sta_GetEntry
            (
                ANSC_HANDLE                 hInsContext,
                ULONG                       nIndex,
                ULONG*                      pInsNumber
            );

    description:

        This function is called to retrieve the entry specified by the index.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                ULONG                       nIndex,
                The index of this entry;

                ULONG*                      pInsNumber
                The output instance number;

    return:     The handle to identify the entry

**********************************************************************/
ANSC_HANDLE
Sta_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
	CcspTraceInfo(("Sta_GetEntry parameter '%lu'\n", nIndex));
	PCOSA_DML_WIFI_ATM_APGROUP	pATMApGroup=(PCOSA_DML_WIFI_ATM_APGROUP)hInsContext;
	if(nIndex<COSA_DML_WIFI_ATM_MAX_STA_NUM) {
		*pInsNumber=nIndex+1;
		return (ANSC_HANDLE)&pATMApGroup->StaList[nIndex]; 
	}
	return NULL;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ANSC_HANDLE
        Sta_AddEntry
            (
                ANSC_HANDLE                 hInsContext,
                ULONG*                      pInsNumber
            );

    description:

        This function is called to add a new entry.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                ULONG*                      pInsNumber
                The output instance number;

    return:     The handle of new added entry.

**********************************************************************/
ANSC_HANDLE
Sta_AddEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG*                      pInsNumber
    )
{
	CcspTraceInfo(("Sta_AddEntry parameter '%ln'\n", pInsNumber));
	PCOSA_DML_WIFI_ATM_APGROUP		pATMApGroup= (PCOSA_DML_WIFI_ATM_APGROUP)hInsContext;
	PCOSA_DML_WIFI_ATM_APGROUP_STA  pATMApSta=NULL;
	if(pATMApGroup->NumberSta < (COSA_DML_WIFI_ATM_MAX_STA_NUM-1)) {
		pATMApGroup->NumberSta+=1;
		*pInsNumber=pATMApGroup->NumberSta;
		pATMApSta=&pATMApGroup->StaList[pATMApGroup->NumberSta-1];
		memset(pATMApSta, 0, sizeof(COSA_DML_WIFI_ATM_APGROUP_STA));
		pATMApSta->pAPList=pATMApGroup->APList;
		return (ANSC_HANDLE)pATMApSta; 
	}
	return NULL;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        Sta_DelEntry
            (
                ANSC_HANDLE                 hInsContext,
                ANSC_HANDLE                 hInstance
            );

    description:

        This function is called to delete an exist entry.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                ANSC_HANDLE                 hInstance
                The exist entry handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
Sta_DelEntry
    (
        ANSC_HANDLE                 hInsContext,
        ANSC_HANDLE                 hInstance
    )
{
	CcspTraceInfo(("Sta_DelEntry  \n"));
	PCOSA_DML_WIFI_ATM_APGROUP		pATMApGroup= (PCOSA_DML_WIFI_ATM_APGROUP)hInsContext;
	PCOSA_DML_WIFI_ATM_APGROUP_STA  pATMApGroupSta=(PCOSA_DML_WIFI_ATM_APGROUP_STA)hInstance;
	int		uInstance=pATMApGroupSta-pATMApGroup->StaList;
	//MACAddress=pATMApGroup->StaList[hInstance-1].MACAddress; 
	//APList=pATMApGroup->APList;	
	CosaDmlWiFi_SetATMSta(pATMApGroup->APList, pATMApGroupSta->MACAddress, 0); //Delete sta

	//shift 
	int mvcount=pATMApGroup->NumberSta-uInstance;
	if(mvcount>0) 
		memmove(pATMApGroupSta, pATMApGroupSta+1, mvcount*sizeof(COSA_DML_WIFI_ATM_APGROUP_STA));
	pATMApGroup->NumberSta-=1;
	
	return ANSC_STATUS_SUCCESS;
	
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        Sta_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
Sta_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    CcspTraceInfo(("Sta_GetParamStringValue parameter '%s'\n", ParamName)); 
	PCOSA_DML_WIFI_ATM_APGROUP_STA pATMApSta = (PCOSA_DML_WIFI_ATM_APGROUP_STA)hInsContext;

	if (strcmp(ParamName, "MACAddress") == 0) {
        /* collect value */
		AnscCopyString(pValue, pATMApSta->MACAddress);
        return 0;
    }
	
    return FALSE;
}


/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Sta_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Sta_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    CcspTraceInfo(("Sta_GetParamUlongValue parameter '%s'\n", ParamName));
	PCOSA_DML_WIFI_ATM_APGROUP_STA pATMApSta   = (PCOSA_DML_WIFI_ATM_APGROUP_STA)hInsContext;
	if (strcmp(ParamName, "AirTimePercent") == 0)  {
        /* collect value */
        *puLong = pATMApSta->AirTimePercent; // collect from corresponding AP->Sta object
        return TRUE;
    }
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Sta_SetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pString
            );

    description:

        This function is called to set string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pString
                The updated string value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Sta_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
    CcspTraceInfo(("Sta_SetParamStringValue parameter '%s'\n", ParamName)); 
    PCOSA_DML_WIFI_ATM_APGROUP_STA pWifiApGrpSta   = (PCOSA_DML_WIFI_ATM_APGROUP_STA)hInsContext;
    if (strcmp(ParamName, "MACAddress") == 0) {
		if (AnscSizeOfString(pString) >= sizeof(pWifiApGrpSta->MACAddress))
			return FALSE;
		if(0==strcasecmp(pWifiApGrpSta->MACAddress, pString))
			return TRUE;
		
		AnscCopyString(pWifiApGrpSta->MACAddress, pString);		
		return TRUE;
    }
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Sta_SetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG                       uValue
            );

    description:

        This function is called to set ULONG parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG                       uValue
                The updated ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Sta_SetParamUlongValue
	(
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
    CcspTraceInfo(("Sta_SetParamIntValue parameter '%s'\n", ParamName));

	PCOSA_DML_WIFI_ATM_APGROUP_STA pWifiApGrpSta   = (PCOSA_DML_WIFI_ATM_APGROUP_STA)hInsContext;
	if (strcmp(ParamName, "AirTimePercent") == 0)	{
		
		pWifiApGrpSta->AirTimePercent = uValue;

		if(pWifiApGrpSta->MACAddress[0]!=0)
			CosaDmlWiFi_SetATMSta(pWifiApGrpSta->pAPList, pWifiApGrpSta->MACAddress, pWifiApGrpSta->AirTimePercent);

		return TRUE;
	}
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Sta_Validate
            (
                ANSC_HANDLE                 hInsContext,
                char*                       pReturnParamName,
                ULONG*                      puLength
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       pReturnParamName,
                The buffer (128 bytes) of parameter name if there's a validation. 

                ULONG*                      puLength
                The output length of the param name. 

    return:     TRUE if there's no validation.

**********************************************************************/
BOOL
Sta_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
	UNREFERENCED_PARAMETER(hInsContext);
	UNREFERENCED_PARAMETER(puLength);
	CcspTraceInfo(("Sta_Validate parameter '%s'\n",pReturnParamName));
    return TRUE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        Sta_Commit
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to finally commit all the update.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
Sta_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
	UNREFERENCED_PARAMETER(hInsContext);
	CcspTraceInfo(("Sta_Commit parameter \n"));
    return TRUE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        Sta_Rollback
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to roll back the update whenever there's a 
        validation found.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
Sta_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
	UNREFERENCED_PARAMETER(hInsContext);
	CcspTraceInfo(("Sta_Rollback parameter \n"));
    return ANSC_STATUS_SUCCESS;
}

/***********************************************************************

 APIs for Object:

    WiFi.AccessPoint.{i}.X_RDKCENTRAL-COM_InterworkingService.

    *  InterworkingService_GetParamStringValue
    *  InterworkingService_SetParamStringValue

***********************************************************************/

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        InterworkingService_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
InterworkingService_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;

        /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Parameters") == 0)
    {
        /* collect value */
        if(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.ANQPConfigParameters){
            if( AnscSizeOfString(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.ANQPConfigParameters) < *pUlSize)
            {
                AnscCopyString(pValue, pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.ANQPConfigParameters);
                return 0;
            }else{
                *pUlSize = AnscSizeOfString(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.ANQPConfigParameters)+1;
                return 1;
            }
        }
        return 0;
    }

    return -1;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        InterworkingService_SetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pString
            );

    description:

        This function is called to set string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pString
                The updated string value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
InterworkingService_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{ 
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;

    if (strcmp(ParamName, "Parameters") == 0)
    {
        if( AnscEqualString(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.ANQPConfigParameters, pString, TRUE)){
            return TRUE;
        }else if(ANSC_STATUS_SUCCESS == CosaDmlWiFi_SetANQPConfig(&pWifiAp->AP.Cfg,pString)){
            if(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.ANQPConfigParameters){
                free(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.ANQPConfigParameters);
            }
            pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.ANQPConfigParameters = malloc(AnscSizeOfString(pString)+1);

            AnscCopyString(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.ANQPConfigParameters,pString);
            if(ANSC_STATUS_FAILURE == (UINT)CosaDmlWiFi_SaveANQPCfg(&pWifiAp->AP.Cfg)){
                CcspTraceWarning(("Failed to Save ANQP Configuration\n"));
            }
            return TRUE;
        }else{
            CosaDmlWiFi_SetANQPConfig(&pWifiAp->AP.Cfg,pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.ANQPConfigParameters);
        }
    }
    return FALSE;
}

/***********************************************************************

 APIs for Object:

    WiFi.AccessPoint.{i}.X_RDKCENTRAL-COM_Passpoint.

    *  Passpoint_GetParamBoolValue
    *  Passpoint_GetParamStringValue 
    *  Passpoint_SetParamBoolValue
    *  Passpoint_SetParamStringValue

***********************************************************************/
/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        Passpoint_GetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:

        This function is called to retrieve Boolean parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pBool

    return:     TRUE if succeeded;
                FALSE if not supported.

**********************************************************************/
BOOL
Passpoint_GetParamBoolValue
(
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
)
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    int retPsmGet = CCSP_SUCCESS;
    char *strValue = NULL;
    
    //Check RFC value
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.WiFi-Passpoint.Enable", NULL, &strValue);

    if (strcmp(ParamName, "Capability") == 0) {
        pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Capability = false;
#if defined (FEATURE_SUPPORT_INTERWORKING)
        if ((retPsmGet == CCSP_SUCCESS) && (_ansc_atoi(strValue) == true)){
            if(true == pWifiAp->AP.Cfg.InterworkingEnable){ 
                pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Capability = true;
            }else{
                CcspTraceWarning(("Cannot Enable Passpoint. Interworking Disabled\n"));
            }
        } else{ 
            CcspTraceWarning(("Cannot Enable Passpoint. RFC Disabled\n"));
        }
#endif
        if (retPsmGet == CCSP_SUCCESS){
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        }
        *pBool = pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Capability;
        return TRUE;
    }

    if (strcmp(ParamName, "Enable") == 0) {
        *pBool = false;
        if ((retPsmGet != CCSP_SUCCESS) || (_ansc_atoi(strValue) == false)){
            if(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Status){
                CosaDmlWiFi_SetHS2Status(&pWifiAp->AP.Cfg,false,true);
            }
        }
        if (retPsmGet == CCSP_SUCCESS){
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        }
        *pBool = pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Status; 
        return TRUE;
    }
    return FALSE;
}

/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        ULONG
        Passpoint_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
Passpoint_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;

        /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Parameters") == 0)
    {
        /* collect value */
        if(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.HS2Parameters){
            if( AnscSizeOfString(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.HS2Parameters) < *pUlSize)
            {
                AnscCopyString(pValue, pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.HS2Parameters);
                return 0;
            }else{
                *pUlSize = AnscSizeOfString(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.HS2Parameters)+1;
                return 1;
            }
        }
        return 0;
    }

    if (strcmp(ParamName, "WANMetrics") == 0)
    {
        CosaDmlWiFi_GetWANMetrics((ANSC_HANDLE)pWifiAp);
        /* collect value */
        if( AnscSizeOfString(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.WANMetrics) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.WANMetrics);
            return 0;
        }else{
            *pUlSize = AnscSizeOfString(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.WANMetrics)+1;
            return 1;
        }
        return 0;
    }

    if (strcmp(ParamName, "Stats") == 0)
    {
        CosaDmlWiFi_GetHS2Stats(&pWifiAp->AP.Cfg);
        /* collect value */
        if( AnscSizeOfString(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Stats) < *pUlSize)
        {
            AnscCopyString(pValue, pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Stats);
            return 0;
        }else{
            *pUlSize = AnscSizeOfString(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Stats)+1;
            return 1;
        }
        return 0;
    }

    return -1;
}
/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Passpoint_SetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL                        bValue
            );

    description:

        This function is called to set BOOL parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL                        bValue
                The updated BOOL value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Passpoint_SetParamBoolValue
(
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
)
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;

    //Check RFC value. Return FALSE if not enabled
    char *strValue = NULL;
    int retPsmGet = CCSP_SUCCESS;

    if (strcmp(ParamName, "Enable") == 0) {
        if(bValue == pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Status){
            CcspTraceDebug(("Passpoint value Already configured. Return Success\n"));
            return TRUE;
        }

        retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.WiFi-Passpoint.Enable", NULL, &strValue);
        if ((retPsmGet != CCSP_SUCCESS) || (false == _ansc_atoi(strValue)) || (FALSE == _ansc_atoi(strValue))){
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
            CcspTraceWarning(("Cannot Enable Passpoint. RFC Disabled\n"));
            return FALSE;
        }

        if(false == pWifiAp->AP.Cfg.InterworkingEnable){
            CcspTraceWarning(("Cannot Enable Passpoint. Interworking Disabled\n"));
            return FALSE;
        }

        if(ANSC_STATUS_SUCCESS == CosaDmlWiFi_SetHS2Status(&pWifiAp->AP.Cfg,bValue,true)){
            CcspTraceInfo(("Successfully set Passpoint Status set to %d on VAP : %lu\n",bValue,pWifiAp->AP.Cfg.InstanceNumber));
            return TRUE;
        } else {
            CcspTraceWarning(("Failed to set Passpoint Status set to %d on VAP : %lu\n",bValue,pWifiAp->AP.Cfg.InstanceNumber));
            return FALSE;
        }
	if (strValue)
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    return FALSE;
}
/**********************************************************************  

    caller:     owner of this object 

    prototype: 

        BOOL
        Passpoint_SetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pString
            );

    description:

        This function is called to set string parameter value; 

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pString
                The updated string value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
Passpoint_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{ 
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;

    if (strcmp(ParamName, "Parameters") == 0)
    {
        if( AnscEqualString(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.HS2Parameters, pString, TRUE)){
            return TRUE;
        }else if(ANSC_STATUS_SUCCESS == CosaDmlWiFi_SetHS2Config(&pWifiAp->AP.Cfg,pString)){
            if(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.HS2Parameters){
                free(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.HS2Parameters);
            }
            pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.HS2Parameters = malloc(AnscSizeOfString(pString)+1);

            AnscCopyString(pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.HS2Parameters,pString);
            if(ANSC_STATUS_FAILURE == (UINT)CosaDmlWiFi_SaveHS2Cfg(&pWifiAp->AP.Cfg)){
                CcspTraceWarning(("Failed to Save Passpoint Configuration\n"));
            }
            return TRUE;
        }
    }
    return FALSE;
}

