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

        *  CosaWifiCreate
        *  CosaWifiInitialize
        *  CosaWifiRemove
        *  CosaDmlWifiGetPortMappingNumber
    -------------------------------------------------------------------

    environment:

        platform independent

    -------------------------------------------------------------------

    author:

        COSA XML TOOL CODE GENERATOR 1.0

    -------------------------------------------------------------------

    revision:

        01/11/2011    initial revision.

**************************************************************************/
#define _XOPEN_SOURCE 700
#include <telemetry_busmessage_sender.h>
#include "cosa_apis.h"
#include "cosa_dbus_api.h"
#include "cosa_wifi_apis.h"
#include "cosa_wifi_sbapi_custom.h"
#include "cosa_wifi_internal.h"
#include "plugin_main_apis.h"
#include "collection.h"
#include "wifi_hal.h"
#include "wifi_monitor.h"
#include "wifi_easy_connect.h"
#include "ccsp_psm_helper.h"
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>
#include <time.h>
#include "ansc_platform.h"
#include "pack_file.h"
#include "ccsp_WifiLog_wrapper.h"
#include <sysevent/sysevent.h>
#include <sys/sysinfo.h>
#include "print_uptime.h"
#include "cosa_wifi_passpoint.h"
#include "cosa_wifi_dml.h"
#include "secure_wrapper.h"

#if defined (FEATURE_SUPPORT_WEBCONFIG)
#include "wifi_webconfig.h"
#endif
#include "cosa_wifi_passpoint.h"
#include "msgpack.h"
#include "ovsdb_table.h"

#if defined(_COSA_BCM_MIPS_) || defined(_XB6_PRODUCT_REQ_) || defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_)
#include "cJSON.h"
#include <ctype.h>
#endif

#ifdef USE_NOTIFY_COMPONENT
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <sys/un.h>
#endif
#define WLAN_MAX_LINE_SIZE 1024
#define RADIO_BROADCAST_FILE "/tmp/.advertise_ssids"
#if defined(_COSA_BCM_MIPS)
#define WLAN_WAIT_LIMIT 3
#endif

#if defined(_COSA_BCM_MIPS_) || defined(_XB6_PRODUCT_REQ_) || defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_)
#define PARTNERS_INFO_FILE              "/nvram/partners_defaults.json"
#define BOOTSTRAP_INFO_FILE             "/nvram/bootstrap.json"
#endif

#ifdef FEATURE_SUPPORT_ONBOARD_LOGGING
#define OnboardLog(...)                     rdk_log_onboard("WIFI", __VA_ARGS__)
#else
#define OnboardLog(...)
#endif

#if defined (_PLATFORM_RASPBERRYPI_) || defined(_PLATFORM_TURRIS_)
#define MAX_BUF_SIZE 128
#endif

/**************************************************************************
*
*	Function Declarations
*
**************************************************************************/
ANSC_STATUS CosaDmlWiFiApMfPushCfg(PCOSA_DML_WIFI_AP_MF_CFG pCfg, ULONG wlanIndex);
ANSC_STATUS CosaDmlWiFiApWpsApplyCfg(PCOSA_DML_WIFI_APWPS_CFG pCfg, ULONG index);
ANSC_STATUS CosaDmlWiFiApSecPushCfg(PCOSA_DML_WIFI_APSEC_CFG pCfg, ULONG instanceNumber);
ANSC_STATUS CosaDmlWiFiApSecApplyCfg(PCOSA_DML_WIFI_APSEC_CFG pCfg, ULONG instanceNumber);
ANSC_STATUS CosaDmlWiFiApAcctApplyCfg(PCOSA_DML_WIFI_APACCT_CFG pCfg, ULONG instanceNumber);
ANSC_STATUS CosaDmlWiFiApSecApplyWepCfg(PCOSA_DML_WIFI_APSEC_CFG pCfg, ULONG instanceNumber);
ANSC_STATUS CosaDmlWiFiApPushCfg (PCOSA_DML_WIFI_AP_CFG pCfg);
ANSC_STATUS CosaDmlWiFiApPushMacFilter(QUEUE_HEADER *pMfQueue, ULONG wlanIndex);
ANSC_STATUS CosaDmlWiFiSsidApplyCfg(PCOSA_DML_WIFI_SSID_CFG pCfg);
ANSC_STATUS CosaDmlWiFiApApplyCfg(PCOSA_DML_WIFI_AP_CFG pCfg);
#if defined (FEATURE_SUPPORT_INTERWORKING)
ANSC_STATUS CosaDmlWiFi_setInterworkingElement(PCOSA_DML_WIFI_AP_CFG pCfg);
ANSC_STATUS CosaDmlWiFi_getInterworkingElement(PCOSA_DML_WIFI_AP_CFG pCfg, ULONG apIns);
void CosaDmlWiFiPsmDelInterworkingEntry();
#endif
ANSC_STATUS CosaDmlWiFi_ApplyRoamingConsortiumElement(PCOSA_DML_WIFI_AP_CFG pCfg);
ANSC_STATUS CosaDmlWifi_setBSSTransitionActivated(PCOSA_DML_WIFI_AP_CFG pCfg, ULONG apIns);
BOOL gRadioRestartRequest[3]={FALSE,FALSE,FALSE};
BOOL g_newXH5Gpass=FALSE;

#if defined(_ENABLE_BAND_STEERING_)
ANSC_STATUS CosaDmlWiFi_GetBandSteeringLog_2();
ANSC_STATUS CosaDmlWiFi_GetBandSteeringLog_3();
ULONG BandsteerLoggingInterval = 3600;
#endif

#ifndef __user
#define __user
#endif

extern BOOL client_fast_reconnect(unsigned int apIndex, char *mac);
extern BOOL client_fast_redeauth(unsigned int apIndex, char *mac);
extern pthread_mutex_t g_apRegister_lock;
INT CosaDmlWiFi_AssociatedDevice_callback(INT apIndex, wifi_associated_dev_t *associated_dev);
INT CosaDmlWiFi_DisAssociatedDevice_callback(INT apIndex, char* mac, int reason);
void *Wifi_Hosts_Sync_Func(void *pt, int index, wifi_associated_dev_t *associated_dev, BOOL bCallForFullSync, BOOL bCallFromDisConnCB);
int sMac_to_cMac(char *sMac, unsigned char *cMac);
INT m_wifi_init();
ANSC_STATUS CosaDmlWiFi_startHealthMonitorThread(void);
static ANSC_STATUS CosaDmlWiFi_SetRegionCode(char *code);
void *updateBootLogTime();
static BOOL updateBootTimeRunning = FALSE;


extern ovsdb_table_t table_Wifi_Global_Config;
extern ovsdb_table_t table_Wifi_Radio_Config;
extern ovsdb_table_t table_Wifi_VAP_Config;
char *vap_names[] = {"private_ssid_2g", "private_ssid_5g", "iot_ssid_2g", "iot_ssid_5g", "hotspot_open_2g", "hotspot_open_5g", "lnf_psk_2g", "lnf_psk_5g", "hotspot_secure_2g", "hotspot_secure_5g","lnf_radius_2g","lnf_radius_5g","mesh_backhaul_2g","mesh_backhaul_5g","guest_ssid_2g","guest_ssid_5g"};
char *filter_vaps[] = {"-",SCHEMA_COLUMN(Wifi_VAP_Config,security),SCHEMA_COLUMN(Wifi_VAP_Config,interworking),NULL};
char *filter_global[] = {"-",SCHEMA_COLUMN(Wifi_Global_Config,gas_config),NULL};
char *filter_radio[] = {"-",SCHEMA_COLUMN(Wifi_Radio_Config,vap_configs),NULL};

#if defined(_XF3_PRODUCT_REQ_) && defined(ENABLE_FEATURE_MESHWIFI) 
static BOOL g_mesh_script_executed = FALSE;
#endif
BOOL g_wifidb_rfc = FALSE;
#if defined (FEATURE_HOSTAP_AUTHENTICATOR) && defined(_XB7_PRODUCT_REQ_)
BOOL isWifiApplyLibHostapRunning = FALSE;
#endif

void CosaDmlWiFi_RemoveSpacesFromString( char *string );
void Update_Hotspot_MacFilt_Entries(BOOL signal_thread);
#if defined(ENABLE_FEATURE_MESHWIFI)
static void wifi_handle_sysevent_async(void);
#endif

#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
static void CosaDmlWiFi_StringToChannelsList(char *psmString, PCOSA_DML_WIFI_DPP_STA_CFG pWifiDppSta);
#endif
void Load_Hotspot_APIsolation_Settings(void);
INT wifi_initRadio(INT radioIndex);
INT wifi_getAssociatedDeviceDetail(INT apIndex, INT devIndex, wifi_device_t *output_struct);
INT wifi_kickAssociatedDevice(INT apIndex, wifi_device_t *device);
void bzero(void *s, size_t n);

static void MeshNotifySecurityChange(INT apIndex, PCOSA_DML_WIFI_APSEC_CFG pStoredApSecCfg)
{
    char *secMode;
    char *encryptMode;

    CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh of AP config changes\n",__FUNCTION__));

    // Grab security Mode
    switch (pStoredApSecCfg->ModeEnabled)
    {
        case COSA_DML_WIFI_SECURITY_WEP_64:
            secMode = "WEP-64";
            break;
        case COSA_DML_WIFI_SECURITY_WEP_128:
            secMode = "WEP-128";
            break;
        case COSA_DML_WIFI_SECURITY_WPA_Personal:
            secMode = "WPA-Personal";
            break;
        case COSA_DML_WIFI_SECURITY_WPA2_Personal:
            secMode = "WPA2-Personal";
            break;
        case COSA_DML_WIFI_SECURITY_WPA_WPA2_Personal:
            secMode = "WPA-WPA2-Personal";
            break;
        case COSA_DML_WIFI_SECURITY_WPA_Enterprise:
            secMode = "WPA-Enterprise";
            break;
        case COSA_DML_WIFI_SECURITY_WPA2_Enterprise:
            secMode = "WPA2-Enterprise";
            break;
        case COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise:
            secMode = "WPA-WPA2-Enterprise";
            break;
        case COSA_DML_WIFI_SECURITY_None:
        default:
            secMode = "None";
            break;
    }

    // Grab encryption method
    switch (pStoredApSecCfg->EncryptionMethod)
    {
        case COSA_DML_WIFI_AP_SEC_TKIP:
            encryptMode = "TKIPEncryption";
            break;
        case COSA_DML_WIFI_AP_SEC_AES:
            encryptMode = "AESEncryption";
            break;
        case COSA_DML_WIFI_AP_SEC_AES_TKIP:
            encryptMode = "TKIPandAESEncryption";
            break;
        default:
            encryptMode = "None";
            break;
    }

    // notify mesh components that wifi ap settings changed
    // index|ssid|passphrase|secMode|encryptMode
    v_secure_system("/usr/bin/sysevent set wifi_ApSecurity 'RDK|%d|%s|%s|%s'", apIndex, pStoredApSecCfg->KeyPassphrase, secMode, encryptMode);
}

void CosaDmlWiFi_RemoveSpacesFromString( char *string )
{
    int count = 0, i = 0; 

    for ( i = 0; string[ i ] != '\0'; i++ )
    {
        if ( string[i] != ' ' )
        {
            string[count++] = string[i];
        }
    }

    string[count] = '\0';
}

ANSC_STATUS
CosaDmlWiFiRadiogetSupportedStandards
(
 int wlanIndex,
 ULONG *pulsupportedStandards
 )
{
    char supportedStandards[64] = { 0 };
    memset(supportedStandards, 0 ,sizeof(supportedStandards));
    *pulsupportedStandards = 0;
    /*CID: 142982 Out-of-bounds access*/
    if(( wifi_getRadioSupportedStandards(wlanIndex, supportedStandards)) == RETURN_OK)
    {
        CcspWifiTrace(("RDK_LOG_WARN, %s:supportedstandards = %s\n",__FUNCTION__,supportedStandards));
        char *p = NULL, *save_str = NULL;
        p = strtok_r( supportedStandards, ",", &save_str);
        char tmpStringBuffer[16] = { 0 };
        while ( p!= NULL )
        {
            memset(tmpStringBuffer, 0 ,sizeof(tmpStringBuffer));
            snprintf(tmpStringBuffer, strlen(p) + 1, "%s", p);
            CosaDmlWiFi_RemoveSpacesFromString( tmpStringBuffer );
            if( 0 == strcmp( tmpStringBuffer, "a") )
            {
                *pulsupportedStandards |= COSA_DML_WIFI_STD_a;

            }
            else if( 0 == strcmp( tmpStringBuffer, "b") )
            {
                *pulsupportedStandards |= COSA_DML_WIFI_STD_b;

            }
            else if( 0 == strcmp( tmpStringBuffer, "g") )
            {
                *pulsupportedStandards |= COSA_DML_WIFI_STD_g;

            }
            else if( 0 == strcmp( tmpStringBuffer, "n") )
            {
                *pulsupportedStandards |= COSA_DML_WIFI_STD_n;

            }
            else if( 0 == strcmp( tmpStringBuffer, "ac") )
            {
                *pulsupportedStandards |= COSA_DML_WIFI_STD_ac;
            }
#if defined (_WIFI_AX_SUPPORT_)
            if( 0 == strcmp( tmpStringBuffer, "ax") )
            {
                *pulsupportedStandards |= COSA_DML_WIFI_STD_ax;
            }
#endif
            p = strtok_r (NULL, ",", &save_str);
        }
    }
    else
    {
        CcspWifiTrace(("RDK_LOG_WARN, %s wifi_getRadioSupportedStandards is failing \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;

    }
    return ANSC_STATUS_SUCCESS;
}

#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
ANSC_STATUS CosaDmlWiFi_initEasyConnect(PCOSA_DATAMODEL_WIFI pWifiDataModel);
ANSC_STATUS CosaDmlWiFi_startDPP(PCOSA_DML_WIFI_AP pWiFiAP, ULONG staIndex);
#endif // !defined(_HUB4_PRODUCT_REQ_)

#if defined(_COSA_BCM_MIPS_) || defined(_XB6_PRODUCT_REQ_) || defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_)
ANSC_STATUS CosaWiFiInitializeParmUpdateSource(PCOSA_DATAMODEL_RDKB_WIFIREGION  pwifiregion);
#endif

#if defined(_ENABLE_BAND_STEERING_)
#if defined(_PLATFORM_RASPBERRYPI_) || defined(_PLATFORM_TURRIS_)
void *_Band_Switch(void *arg);
void _wifi_eventCapture(void);
#endif
#endif

pthread_cond_t reset_done = PTHREAD_COND_INITIALIZER; 
pthread_mutex_t macfilter = PTHREAD_MUTEX_INITIALIZER;

#define  ARRAY_SZ(x)    (sizeof(x) / sizeof((x)[0]))
ULONG INSTClientReprotingPeriods[] = {0,1,5,15,30,60,300,900,1800,3600,10800,21600,43200,86400};

/**************************************************************************
*
*	Function Definitions
*
**************************************************************************/


int syscfg_executecmd(const char *caller, char *cmd, char **retBuf) 
{
  FILE *f;
  char *ptr = NULL;
  size_t buff_size = 0;  // current memory in-use size
  *retBuf = NULL;


  if((f = popen(cmd, "r")) == NULL) {
    printf("%s: popen %s error\n",caller, cmd);
    return -1;
  }

  while(!feof(f))
  {
    // allocate memory to allow for one more line:
    if((ptr = realloc(*retBuf, buff_size + WLAN_MAX_LINE_SIZE)) == NULL)
    {
      printf("%s: realloc %s error\n",caller, cmd);
      // Note: caller still needs to free retBuf
      pclose(f);
      return -1;
    }

    *retBuf=ptr;        // update retBuf
    ptr+=buff_size;     // ptr points to current end of string

    *ptr = 0;
    fgets(ptr,WLAN_MAX_LINE_SIZE,f);

    if(strlen(ptr) == 0)
    {
      break;
    }
    buff_size += strlen(ptr);  // update current memory in-use
  }
  pclose(f);

  return 0;
}

//Temporary wrapper replace for system()
int execvp_wrapper(char * const argv[]) {
   int child_pid = fork();
   int ret = -1;
   if (child_pid == -1) {
      perror("fork failed");
   } else if (!child_pid) {
          execvp(argv[0], argv);
          // should not be reached
          perror("exec failed");
          _exit(-1);
   } else {
          waitpid(child_pid, &ret, 0);
          ret = WEXITSTATUS(ret);
   }

  return ret;
}

/* TXB7-3224 Changing the Function as static to avoid duplicate entries during linking */
static void get_uptime(int *uptime)
{
    struct 	sysinfo info;
    sysinfo( &info );
    *uptime= info.uptime;
}

/*This function is for LG Celeno MV1 platform*/
void enable_reset_radio_flag(int wlanIndex)
{
#ifdef _LG_MV1_CELENO_
	gRadioRestartRequest[wlanIndex%2]=TRUE;
#endif
}

void enable_reset_both_radio_flag()
{
#ifdef _LG_MV1_CELENO_
	gRadioRestartRequest[2]=TRUE;
#endif
}

#ifdef _COSA_SIM_
ANSC_STATUS
CosaDmlWiFiInit
    (
        ANSC_HANDLE                 hDml,
        PANSC_HANDLE                phContext
    )
{
    UNREFERENCED_PARAMETER(hDml);
    UNREFERENCED_PARAMETER(phContext);
    return ANSC_STATUS_SUCCESS;
}
static int gRadioCount = 1;

/*
 *  Description:
 *     The API retrieves the number of WiFi radios in the system.
 */
ULONG
CosaDmlWiFiRadioGetNumberOfEntries
    (
        ANSC_HANDLE                 hContext
    )
{
    ANSC_STATUS                     returnStatus = ANSC_STATUS_SUCCESS;
    ULONG                           ulCount      = 0;
    UNREFERENCED_PARAMETER(hContext);
#if 1
    if (!g_pCosaBEManager->has_wifi_slap)
    {
        /*wifi is in remote CPU*/
        return 0;
    }
    else
    {
        CcspTraceWarning(("CosaDmlWiFiRadioGetNumberOfEntries - This is a local call ...\n"));
        return gRadioCount;
    }
#endif
}    
    
    
/* Description:
 *	The API retrieves the complete info of the WiFi radio designated by index. 
 *	The usual process is the caller gets the total number of entries, 
 *	then iterate through those by calling this API.
 * Arguments:
 * 	ulIndex		Indicates the index number of the entry.
 * 	pEntry		To receive the complete info of the entry.
 */
ANSC_STATUS
CosaDmlWiFiRadioGetEntry
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        PCOSA_DML_WIFI_RADIO_FULL   pEntry
    )
{
    if (!pEntry)
    {
        return 0;
    }
    
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadio      = pEntry;
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg   = &pWifiRadio->Cfg;
    PCOSA_DML_WIFI_RADIO_SINFO      pWifiRadioSinfo = &pWifiRadio->StaticInfo;
    PCOSA_DML_WIFI_RADIO_DINFO      pWifiRadioDinfo = &pWifiRadio->DynamicInfo;
    /*PPOAM_COSAWIFIDM_OBJECT*/ANSC_HANDLE         pPoamWiFiDm     = (/*PPOAM_COSAWIFIDM_OBJECT*/ANSC_HANDLE)hContext;

    if ( pPoamWiFiDm )
    {
        return 0;
    }
    else
    {
        /*dummy data*/
        pWifiRadio->Cfg.InstanceNumber = ulIndex + 1;

        sprintf(pWifiRadio->StaticInfo.Name, "eth%d", ulIndex);
        sprintf(pWifiRadio->Cfg.Alias, "Radio%d", ulIndex);
        
        pWifiRadio->StaticInfo.bUpstream               = TRUE;
        pWifiRadio->StaticInfo.MaxBitRate              = 128+ulIndex;
        pWifiRadio->StaticInfo.SupportedFrequencyBands = COSA_DML_WIFI_FREQ_BAND_2_4G;                   /* Bitmask of COSA_DML_WIFI_FREQ_BAND */
#ifdef _WIFI_AX_SUPPORT_
        pWifiRadio->StaticInfo.SupportedStandards      = COSA_DML_WIFI_STD_g | COSA_DML_WIFI_STD_n | COSA_DML_WIFI_STD_ax;      /* Bitmask of COSA_DML_WIFI_STD */
#else
        pWifiRadio->StaticInfo.SupportedStandards      = COSA_DML_WIFI_STD_b | COSA_DML_WIFI_STD_g;      /* Bitmask of COSA_DML_WIFI_STD */
#endif
        AnscCopyString(pWifiRadio->StaticInfo.PossibleChannels, "1,2,3,4,5,6,7,8,9,10,11");
        pWifiRadio->StaticInfo.AutoChannelSupported    = TRUE;
        AnscCopyString(pWifiRadio->StaticInfo.TransmitPowerSupported, "10,20,50,100");
        pWifiRadio->StaticInfo.IEEE80211hSupported     = TRUE;

        CosaDmlWiFiRadioGetCfg(hContext, pWifiRadioCfg);
        CosaDmlWiFiRadioGetDinfo(hContext, pWifiRadioCfg->InstanceNumber, pWifiRadioDinfo);    

        return ANSC_STATUS_SUCCESS;
    }

}

ANSC_STATUS
CosaDmlWiFiRadioSetDefaultCfgValues
    (
        ANSC_HANDLE                 hContext,
        unsigned long               ulIndex,
        PCOSA_DML_WIFI_RADIO_CFG    pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulIndex);
    UNREFERENCED_PARAMETER(pCfg);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiRadioSetValues
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        ULONG                       ulInstanceNumber,
        char*                       pAlias
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulIndex);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    UNREFERENCED_PARAMETER(pAlias);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiRadioSetCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_RADIO_CFG    pCfg        /* Identified by InstanceNumber */
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pCfg);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiRadioGetCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_RADIO_CFG    pCfg        /* Identified by InstanceNumber */
    )
{
    UNREFERENCED_PARAMETER(hContext);
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;

    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }
    
        pCfg->bEnabled                       = TRUE;
        pCfg->LastChange                   = 123456;
        pCfg->OperatingFrequencyBand         = COSA_DML_WIFI_FREQ_BAND_2_4G;
        pCfg->OperatingStandards             = COSA_DML_WIFI_STD_b | COSA_DML_WIFI_STD_g;         /* Bitmask of COSA_DML_WIFI_STD */
        pCfg->Channel                        = 1;
        pCfg->AutoChannelEnable              = TRUE;
        pCfg->AutoChannelRefreshPeriod       = 3600;
        pCfg->OperatingChannelBandwidth      = COSA_DML_WIFI_CHAN_BW_20M;
        pCfg->ExtensionChannel               = COSA_DML_WIFI_EXT_CHAN_Above;
        pCfg->GuardInterval                  = COSA_DML_WIFI_GUARD_INTVL_400ns;
        pCfg->MCS                            = 1;
        pCfg->TransmitPower                  = 100;
        pCfg->IEEE80211hEnabled              = TRUE;
        AnscCopyString(pCfg->RegulatoryDomain, "COM");
        /* Below is Cisco Extensions */
        pCfg->APIsolation                    = TRUE;
        pCfg->FrameBurst                     = TRUE;
        pCfg->TxRate                         = COSA_DML_WIFI_TXRATE_54M;
        pCfg->CTSProtectionMode              = TRUE;
        pCfg->BeaconInterval                 = 3600;
        pCfg->DTIMInterval                   = 100;
        pCfg->FragmentationThreshold         = 1024;
        pCfg->RTSThreshold                   = 1024;
        /* USGv2 Extensions */

        pCfg->LongRetryLimit                = 5;
        pCfg->MbssUserControl               = 1;
        pCfg->AdminControl                  = 12;
        pCfg->OnOffPushButtonTime           = 23;
        pCfg->ObssCoex                      = 34;
        pCfg->MulticastRate                 = 45;
        pCfg->ApplySetting                  = TRUE;
        pCfg->ApplySettingSSID = 0;

		gChannelSwitchingCount = 0;
        return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiRadioGetDinfo
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_DINFO  pInfo
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    if (!pInfo)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
        pInfo->Status                 = COSA_DML_IF_STATUS_Up;
        AnscCopyString(pInfo->ChannelsInUse, "1");
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiRadioGetChannelsInUse
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_DINFO  pInfo
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    if (!pInfo)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
        AnscCopyString(pInfo->ChannelsInUse,"1");

        return ANSC_STATUS_SUCCESS;
    }
}
ANSC_STATUS
CosaDmlWiFiRadioGetApChannelScan
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_DINFO  pInfo
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    if (!pInfo)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if (FALSE)
    {
        return returnStatus;
    }
    else
    { 
        AnscCopyString(pInfo->ApChannelScan ,"HOME-10C4-2.4|WPA/WPA2-PSK AES-CCMP TKIP|802.11b/g/n|-63|6|70:54:D2:00:AA:50");
        return ANSC_STATUS_SUCCESS;
    }
}
ANSC_STATUS
CosaDmlWiFiRadioGetStats
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_STATS  pStats
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    if (!pStats)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if (FALSE)
    {
	return returnStatus;
    }
    else
    {
        pStats->BytesSent                          = 123456;
        pStats->BytesReceived                      = 234561;
        pStats->PacketsSent                        = 235;
        pStats->PacketsReceived                    = 321;
        pStats->ErrorsSent                         = 0;
        pStats->ErrorsReceived                     = 0;
        pStats->DiscardPacketsSent                 = 1;
        pStats->DiscardPacketsReceived             = 3;
    
        return ANSC_STATUS_SUCCESS;
    }
}

/* WiFi SSID */
static int gSsidCount = 1;
/* Description:
 *	The API retrieves the number of WiFi SSIDs in the system.
 */
ULONG
CosaDmlWiFiSsidGetNumberOfEntries
    (
        ANSC_HANDLE                 hContext
    )
{
    UNREFERENCED_PARAMETER(hContext);
    if (FALSE)
    {
        return 0;
    }
    else
    {
        return gSsidCount;
    }
}

/* Description:
 *	The API retrieves the complete info of the WiFi SSID designated by index. The usual process is the caller gets the total number of entries, then iterate through those by calling this API.
 * Arguments:
 * 	ulIndex		Indicates the index number of the entry.
 * 	pEntry		To receive the complete info of the entry.
 */
ANSC_STATUS
CosaDmlWiFiSsidGetEntry
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        PCOSA_DML_WIFI_SSID_FULL    pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulIndex);
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    if (!pEntry)
    {
        return ANSC_STATUS_FAILURE
    }
    
    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
        /*Set default Name & Alias*/
        sprintf(pEntry->StaticInfo.Name, "SSID%d", ulIndex);
    
        pEntry->Cfg.InstanceNumber    = ulIndex;
        _ansc_sprintf(pEntry->Cfg.WiFiRadioName, "eth0");
    
        /*indicated by InstanceNumber*/
        CosaDmlWiFiSsidGetCfg((ANSC_HANDLE)hContext,&pEntry->Cfg);
    
        CosaDmlWiFiSsidGetDinfo((ANSC_HANDLE)hContext,pEntry->Cfg.InstanceNumber,&pEntry->DynamicInfo);

        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiSsidSetValues
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        ULONG                       ulInstanceNumber,
        char*                       pAlias
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulIndex);
    UNREFERENCED_PARAMETER(pAlias);
    UNREFERENCED_PARAMETER(ulInstanceNumber);    
    if (/*pPoamWiFiDm*/FALSE)
    {
        return returnStatus;
    }
    else
    {
        return ANSC_STATUS_SUCCESS;
    }
}    

/* Description:
 *	The API adds a new WiFi SSID into the system. 
 * Arguments:
 *	hContext	reserved.
 *	pEntry		Caller pass in the configuration through pEntry->Cfg field and gets back the generated pEntry->StaticInfo.Name, MACAddress, etc.
 */
ANSC_STATUS
CosaDmlWiFiSsidAddEntry
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_SSID_FULL    pEntry
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pEntry);
    if (/*pPoamWiFiDm*/FALSE)
    {
        return returnStatus;
    }
    else
    {
        gSsidCount++;
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiSsidDelEntry
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulInstanceNumber);

    if (FALSE/*pPoamWiFiDm*/)
    {
        return returnStatus;
    }
    else
    {
        gSsidCount--;
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiSsidSetCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_SSID_CFG     pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pCfg);
    if (/*pPoamWiFiDm*/FALSE)
    {
        return returnStatus;
    }
    else
    {
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiSsidGetCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_SSID_CFG     pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if (/*pPoamWiFiDm*/FALSE)
    {
        return returnStatus;
    }
    else
    {
        pCfg->bEnabled                 = FALSE;
        pCfg->LastChange = 1923;
        _ansc_sprintf(pCfg->SSID, "test%d", pCfg->InstanceNumber);
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiSsidGetDinfo
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_SSID_DINFO   pInfo
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    if (!pInfo)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if (/*pPoamWiFiDm*/FALSE)
    {
        return returnStatus;
    }
    else
    {
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiSsidGetStats
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_SSID_STATS   pStats
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    if (!pStats)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
        pStats->ErrorsSent                  = 234;
        pStats->UnknownProtoPacketsReceived = 56;
        pStats->BytesSent                   = 100;
        pStats->BytesReceived               = 101;
        pStats->PacketsSent                 = 102;
        pStats->PacketsReceived             = 103;
        pStats->ErrorsReceived              = 104;
        pStats->UnicastPacketsSent          = 105;
        pStats->UnicastPacketsReceived      = 106;
        pStats->DiscardPacketsSent          = 107;
        pStats->DiscardPacketsReceived      = 108;
    
        pStats->MulticastPacketsSent        = 109;
        pStats->MulticastPacketsReceived    = 110;
        pStats->BroadcastPacketsSent        = 111;
        pStats->BroadcastPacketsReceived    = 112;
    
        return ANSC_STATUS_SUCCESS;
    }
}


/* WiFi AP is always associated with a SSID in the system */
static int gApCount = 1;
ULONG
CosaDmlWiFiAPGetNumberOfEntries
    (
        ANSC_HANDLE                 hContext
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);

    if (FALSE)
    {
        return 0; 
    }
    else
    {
        return gApCount;
    }
}

ANSC_STATUS
CosaDmlWiFiApGetEntry
    (
        ANSC_HANDLE                 hContext,
        char                        *pSsid,
        PCOSA_DML_WIFI_AP_FULL      pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj      = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;

    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
        CosaDmlWiFiApGetCfg(NULL, pSsid, &pEntry->Cfg);
        CosaDmlWiFiApGetInfo(NULL, pSsid, &pEntry->Info);
        CosaDmlGetApRadiusSettings(NULL,pSsid,&pEntry->RadiusSetting);
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
#if defined (MULTILAN_FEATURE)
CosaDmlWiFiApAddEntry
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_FULL      pEntry
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);
    UNREFERENCED_PARAMETER(pEntry);
    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
#endif
CosaDmlWiFiApSetValues
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        ULONG                       ulInstanceNumber,
        char*                       pAlias
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    
    if (FALSE)
    {
        return returnStatus;        
    }
    else
    {
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiApSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_CFG       pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;

    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiApGetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_CFG       pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;

    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }
        
    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
        pCfg->bEnabled      = TRUE;
        pCfg->RetryLimit    = 1;
        AnscCopyString(pCfg->SSID, "Device.WiFi.SSID.1.");

        pCfg->WmmNoAck      = 123;
        pCfg->MulticastRate = 123;
        pCfg->BssMaxNumSta  = 128;
        pCfg->BssCountStaAsCpe  = TRUE;
        pCfg->BssHotSpot    = TRUE;
    
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiApGetInfo
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_INFO      pInfo
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;

    if (!pInfo)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
        pInfo->WMMCapability = TRUE;
    
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiApSecGetEntry
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APSEC_FULL   pEntry
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;

    if (!pEntry)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if (FALSE)
    {
        return returnStatus;
    }
    else
    {

        pEntry->Info.ModesSupported = COSA_DML_WIFI_SECURITY_WEP_64 | COSA_DML_WIFI_SECURITY_WEP_128;
        
        CosaDmlWiFiApSecGetCfg((ANSC_HANDLE)hContext, NULL, &pEntry->Cfg);

        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiApSecGetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APSEC_CFG    pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;

    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
        AnscCopyString(pCfg->WEPKeyp, "1234");
    
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiApSecSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APSEC_CFG    pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;

    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
    
        return ANSC_STATUS_SUCCESS;
    }
}

/*not called*/
ANSC_STATUS
CosaDmlWiFiApWpsGetEntry
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APWPS_FULL   pEntry
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;

    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
    
        return ANSC_STATUS_SUCCESS;
    }

}

ANSC_STATUS
CosaDmlWiFiApWpsSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APWPS_CFG    pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;

    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
    
        return ANSC_STATUS_SUCCESS;
    }

}

ANSC_STATUS
CosaDmlWiFiApWpsGetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APWPS_CFG    pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    
    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }

    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
        pCfg->ConfigMethodsEnabled = COSA_DML_WIFI_WPS_METHOD_Ethernet;
    
        return ANSC_STATUS_SUCCESS;
    }

    
}

/* Description:
 *	This routine is to retrieve the complete list of currently associated WiFi devices, 
 *	which is a dynamic table.
 * Arguments:
 *	pName   		Indicate which SSID to operate on.
 *	pulCount		To receive the actual number of entries.
 * Return:
 * The pointer to the array of WiFi associated devices, allocated by callee. 
 * If no entry is found, NULL is returned.
 */
PCOSA_DML_WIFI_AP_ASSOC_DEVICE
CosaDmlWiFiApGetAssocDevices
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PULONG                      pulCount
    )
{
    PCOSA_DML_WIFI_AP_ASSOC_DEVICE  AssocDeviceArray  = (PCOSA_DML_WIFI_AP_ASSOC_DEVICE)NULL;
    ULONG                           index             = 0;
    ULONG                           ulCount           = 0;
    
    if (FALSE)
    {
        *pulCount = ulCount;
        
        return AssocDeviceArray;
    }
    else
    {
        /*For example we have 5 AssocDevices*/
        *pulCount = 5;
        AssocDeviceArray = (PCOSA_DML_WIFI_AP_ASSOC_DEVICE)AnscAllocateMemory(sizeof(COSA_DML_WIFI_AP_ASSOC_DEVICE)*(*pulCount));
    
        if ( !AssocDeviceArray )
        {
            *pulCount = 0;
            return NULL;
        }
    
        for(index = 0; index < *pulCount; index++)
        {
            AssocDeviceArray[index].AuthenticationState  = TRUE;
            AssocDeviceArray[index].LastDataDownlinkRate = 200+index;
            AssocDeviceArray[index].LastDataUplinkRate   = 100+index;
            AssocDeviceArray[index].SignalStrength       = 50+index;
            AssocDeviceArray[index].Retransmissions      = 20+index;
            AssocDeviceArray[index].Active               = TRUE;
        }
    
        return AssocDeviceArray;
    }
}

ANSC_STATUS
CosaDmlWiFiApMfGetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_MF_CFG    pCfg
    )
{
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApMfSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_MF_CFG    pCfg
    )
{
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_GetWEPKey64ByIndex(ULONG apIns, ULONG keyIdx, PCOSA_DML_WEPKEY_64BIT pWepKey)
{
    /* dummy data for simu */
    static char wepKey64[COSA_DML_WEP_KEY_NUM][64 / 8 * 2 + 1];
    if (apIns != 1 || !pWepKey) /* only support 1 ap in simu */
        return ANSC_STATUS_FAILURE;

    snprintf((char*)pWepKey->WEPKey, sizeof(pWepKey->WEPKey), "%s", wepKey64[keyIdx]);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetWEPKey64ByIndex(ULONG apIns, ULONG keyIdx, PCOSA_DML_WEPKEY_64BIT pWepKey)
{
    /* dummy data for simu */
    static char wepKey64[COSA_DML_WEP_KEY_NUM][64 / 8 * 2 + 1];

    if (apIns != 1 || !pWepKey) /* only support 1 ap in simu */
        return ANSC_STATUS_FAILURE;

    snprintf(wepKey64[keyIdx], sizeof(wepKey64[keyIdx]), "%s", pWepKey->WEPKey);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_GetWEPKey128ByIndex(ULONG apIns, ULONG keyIdx, PCOSA_DML_WEPKEY_128BIT pWepKey)
{
    /* dummy data for simu */
    static char wepKey128[COSA_DML_WEP_KEY_NUM][128 / 8 * 2 + 1];

    if (apIns != 1 || !pWepKey) /* only support 1 ap in simu */
        return ANSC_STATUS_FAILURE;

    snprintf((char *)pWepKey->WEPKey, sizeof(pWepKey->WEPKey), "%s", wepKey128[keyIdx]);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetWEPKey128ByIndex(ULONG apIns, ULONG keyIdx, PCOSA_DML_WEPKEY_128BIT pWepKey)
{
    /* dummy data for simu */
    static char wepKey128[COSA_DML_WEP_KEY_NUM][128 / 8 * 2 + 1];

    if (apIns != 1 || !pWepKey) /* only support 1 ap in simu */
        return ANSC_STATUS_FAILURE;

    snprintf(wepKey128[keyIdx], sizeof(wepKey128[keyIdx]), "%s", pWepKey->WEPKey);
    return ANSC_STATUS_SUCCESS;
}

#define MAX_MAC_FILT                16

static int                          g_macFiltCnt = 1;
static COSA_DML_WIFI_AP_MAC_FILTER  g_macFiltTab[MAX_MAC_FILT] = {
    { 1, "MacFilterTable1", "00:1a:2b:aa:bb:cc" },
};

ULONG
CosaDmlMacFilt_GetNumberOfEntries(ULONG apIns)
{
    if (apIns != 1) /* only support 1 ap in simu */
        return ANSC_STATUS_FAILURE;

    return g_macFiltCnt;
}

ANSC_STATUS
CosaDmlMacFilt_GetEntryByIndex(ULONG apIns, ULONG index, PCOSA_DML_WIFI_AP_MAC_FILTER pMacFilt)
{
    if (apIns != 1 || !pMacFilt) /* only support 1 ap in simu */
        return ANSC_STATUS_FAILURE;

    if (index >= g_macFiltCnt)
        return ANSC_STATUS_FAILURE;

    memcpy(pMacFilt, &g_macFiltTab[index], sizeof(COSA_DML_WIFI_AP_MAC_FILTER));

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlMacFilt_SetValues(ULONG apIns, ULONG index, ULONG ins, char *Alias)
{
    int i;

    if (apIns != 1 || !Alias) /* only support 1 ap in simu */
        return ANSC_STATUS_FAILURE;

    if (index >= g_macFiltCnt)
        return ANSC_STATUS_FAILURE;


    g_macFiltTab[index].InstanceNumber = ins;
    snprintf(g_macFiltTab[index].Alias, sizeof(g_macFiltTab[index].Alias),
            "%s", Alias);

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlMacFilt_AddEntry(ULONG apIns, PCOSA_DML_WIFI_AP_MAC_FILTER pMacFilt)
{
    if (apIns != 1 || !pMacFilt) /* only support 1 ap in simu */
        return ANSC_STATUS_FAILURE;

    if (g_macFiltCnt >= MAX_MAC_FILT)
        return ANSC_STATUS_FAILURE;

    memcpy(&g_macFiltTab[g_macFiltCnt], pMacFilt, sizeof(COSA_DML_WIFI_AP_MAC_FILTER));
    g_macFiltCnt++;
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlMacFilt_DelEntry(ULONG apIns, ULONG macFiltIns)
{
    int i;

    if (apIns != 1) /* only support 1 ap in simu */
        return ANSC_STATUS_FAILURE;

    for (i = 0; i < g_macFiltCnt; i++)
    {
        if (g_macFiltTab[i].InstanceNumber == macFiltIns)
            break;
    }
    if (i == g_macFiltCnt)
        return ANSC_STATUS_FAILURE;

    memmove(&g_macFiltTab[i], &g_macFiltTab[i+1], 
            (g_macFiltCnt - i - 1) * sizeof(COSA_DML_WIFI_AP_MAC_FILTER));
    g_macFiltCnt--;

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlMacFilt_GetConf(ULONG apIns, ULONG macFiltIns, PCOSA_DML_WIFI_AP_MAC_FILTER pMacFilt)
{
    int i;

    if (apIns != 1 || !pMacFilt) /* only support 1 ap in simu */
        return ANSC_STATUS_FAILURE;

    for (i = 0; i < g_macFiltCnt; i++)
    {
        if (g_macFiltTab[i].InstanceNumber == macFiltIns)
            break;
    }
    if (i == g_macFiltCnt)
        return ANSC_STATUS_FAILURE;

    memcpy(pMacFilt, &g_macFiltTab[i], sizeof(COSA_DML_WIFI_AP_MAC_FILTER));
    pMacFilt->InstanceNumber = macFiltIns; /* just in case */

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlMacFilt_SetConf(ULONG apIns, ULONG macFiltIns, PCOSA_DML_WIFI_AP_MAC_FILTER pMacFilt)
{
    int i;

    if (apIns != 1 || !pMacFilt) /* only support 1 ap in simu */
        return ANSC_STATUS_FAILURE;

    for (i = 0; i < g_macFiltCnt; i++)
    {
        if (g_macFiltTab[i].InstanceNumber == macFiltIns)
            break;
    }
    if (i == g_macFiltCnt)
        return ANSC_STATUS_FAILURE;

    memcpy(&g_macFiltTab[i], pMacFilt, sizeof(COSA_DML_WIFI_AP_MAC_FILTER));
    g_macFiltTab[i].InstanceNumber = macFiltIns; /* just in case */

    return ANSC_STATUS_SUCCESS;
}

#elif (_COSA_DRG_CNS_)

#include <utctx/utctx.h>
#include <utctx/utctx_api.h>
#include <utapi/utapi.h>
#include <utapi/utapi_util.h>

ANSC_STATUS
CosaDmlWiFiInit
    (
        ANSC_HANDLE                 hDml,
        PANSC_HANDLE                phContext
    )
{
    UNREFERENCED_PARAMETER(hDml);
    UNREFERENCED_PARAMETER(phContext);
    return ANSC_STATUS_SUCCESS;
}

/*
 *  Description:
 *     The API retrieves the number of WiFi radios in the system.
 */
ULONG
CosaDmlWiFiRadioGetNumberOfEntries
    (
        ANSC_HANDLE                 hContext
    )
{
    return Utopia_GetWifiRadioInstances();
}    
    
    
/* Description:
 *	The API retrieves the complete info of the WiFi radio designated by index. 
 *	The usual process is the caller gets the total number of entries, 
 *	then iterate through those by calling this API.
 * Arguments:
 * 	ulIndex		Indicates the index number of the entry.
 * 	pEntry		To receive the complete info of the entry.
 */
ANSC_STATUS
CosaDmlWiFiRadioGetEntry
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        PCOSA_DML_WIFI_RADIO_FULL   pEntry
    )
{
    if (!pEntry)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadio      = pEntry;
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg   = &pWifiRadio->Cfg;
    PCOSA_DML_WIFI_RADIO_SINFO      pWifiRadioSinfo = &pWifiRadio->StaticInfo;
    PCOSA_DML_WIFI_RADIO_DINFO      pWifiRadioDinfo = &pWifiRadio->DynamicInfo;

    int rc = -1;
    UtopiaContext ctx;

    CosaDmlWiFiRadioSetDefaultCfgValues(NULL,ulIndex,pWifiRadioCfg); /* Fill the default values for Cfg */
    if(pEntry)
    {
        /* Initialize a Utopia Context */
        if(!Utopia_Init(&ctx))
           return ANSC_STATUS_FAILURE;

        rc = Utopia_GetWifiRadioEntry(&ctx, ulIndex, pEntry);

        /* Free Utopia Context */
        Utopia_Free(&ctx,0);
    }
    if (rc != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}


ANSC_STATUS
CosaDmlWiFiRadioSetDefaultCfgValues
    (
        ANSC_HANDLE                 hContext,
        unsigned long               ulIndex,
	PCOSA_DML_WIFI_RADIO_CFG    pCfg
    )
{
    int rc = -1;
    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if(0 == ulIndex)
    {
	rc = strcpy_s(pCfg->Alias, sizeof(pCfg->Alias), "wl0");
        if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
	pCfg->OperatingFrequencyBand = COSA_DML_WIFI_FREQ_BAND_2_4G;
	pCfg->OperatingStandards = COSA_DML_WIFI_STD_b | COSA_DML_WIFI_STD_g | COSA_DML_WIFI_STD_n;
        pCfg->OperatingChannelBandwidth = COSA_DML_WIFI_CHAN_BW_20M;
     }else
     {
        rc = strcpy_s(pCfg->Alias,sizeof(pCfg->Alias), "wl1");
        if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
        pCfg->OperatingFrequencyBand = COSA_DML_WIFI_FREQ_BAND_5G;
        pCfg->OperatingStandards = COSA_DML_WIFI_STD_a | COSA_DML_WIFI_STD_n;
        pCfg->OperatingChannelBandwidth = COSA_DML_WIFI_CHAN_BW_40M;
     }
    pCfg->bEnabled = TRUE;
    pCfg->Channel = 0;
    pCfg->AutoChannelEnable = TRUE;
    pCfg->AutoChannelRefreshPeriod = (15*60);
    pCfg->ExtensionChannel = COSA_DML_WIFI_EXT_CHAN_Above;
    pCfg->GuardInterval = COSA_DML_WIFI_GUARD_INTVL_Auto;
    pCfg->MCS = -1;
    pCfg->TransmitPower = 100; /* High */
    pCfg->IEEE80211hEnabled = FALSE;
    strcpy(pCfg->RegulatoryDomain,"US");
    pCfg->APIsolation = FALSE;
    pCfg->FrameBurst = TRUE;
    pCfg->TxRate = COSA_DML_WIFI_TXRATE_Auto;
    pCfg->CTSProtectionMode = TRUE;
    pCfg->BeaconInterval = 100;
    pCfg->DTIMInterval = 1;
    pCfg->FragmentationThreshold = 2346;
    pCfg->RTSThreshold = 2347;  
   
    return ANSC_STATUS_SUCCESS;
}


ANSC_STATUS
CosaDmlWiFiRadioSetValues
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        ULONG                       ulInstanceNumber,
        char*                       pAlias
    )
{
    UtopiaContext ctx;
    int rc = -1;

    if (!pAlias)
    {
        return ANSC_STATUS_FAILURE;
    }

    /* Initialize a Utopia Context */
    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;
    rc = Utopia_WifiRadioSetValues(&ctx,ulIndex,ulInstanceNumber,pAlias);

    /* Free Utopia Context */
    Utopia_Free(&ctx,!rc);

    if (rc != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiRadioSetCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_RADIO_CFG    pCfg        /* Identified by InstanceNumber */
    )
{
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg  = pCfg;
    /*PPOAM_COSAWIFIDM_OBJECT         pPoamWiFiDm    = (PPOAM_COSAWIFIDM_OBJECT )hContext;*/
    ULONG ulAutoChannelCycle = 0;

    UtopiaContext ctx;
    int rc = -1;

    if(pCfg)
    {
        /* Initialize a Utopia Context */
        if(!Utopia_Init(&ctx))
           return ANSC_STATUS_FAILURE;

        rc = Utopia_SetWifiRadioCfg(&ctx,pCfg);

        /* Set WLAN Restart event */
        Utopia_SetEvent(&ctx,Utopia_Event_WLAN_Restart);

        /* Free Utopia Context */
        Utopia_Free(&ctx,!rc);
    }

    if (rc != 0)
        return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiRadioGetCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_RADIO_CFG    pCfg        /* Identified by InstanceNumber */
    )
{
    UtopiaContext ctx;
    int rc = -1;

    if(pCfg)
    {
        /* Initialize a Utopia Context */
        if(!Utopia_Init(&ctx))
           return ANSC_STATUS_FAILURE;

        rc = Utopia_GetWifiRadioCfg(&ctx,0,pCfg);

        /* Free Utopia Context */
        Utopia_Free(&ctx,0);
    }

    if (rc != 0)
        return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiRadioGetDinfo
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_DINFO  pInfo
    )
{
    int rc = -1;
    if(pInfo)
    {
        rc = Utopia_GetWifiRadioDinfo(ulInstanceNumber,pInfo);
    }

    if (rc != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiRadioGetChannelsInUse
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_DINFO  pInfo
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;

    if (!pInfo)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
        AnscCopyString(pInfo->ChannelsInUse,"1");

        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiRadioGetApChannelScan
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_DINFO  pInfo
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    UNREFERENCED_PARAMETER(hContext);
    if (!pInfo)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if (FALSE)
    {
        return returnStatus;
    }
    else
    { 
        AnscCopyString(pInfo->ApChannelScan ,"HOME-10C4-2.4|WPA/WPA2-PSK AES-CCMP TKIP|802.11b/g/n|-63|6|70:54:D2:00:AA:50");
        return ANSC_STATUS_SUCCESS;
    }
}
ANSC_STATUS
CosaDmlWiFiRadioGetStats
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_STATS  pStats
    )
{
    UNREFERENCED_PARAMETER(hContext);  
    int rc = -1;
    if(pStats)
    {
        rc = Utopia_GetWifiRadioStats(ulInstanceNumber,pStats);
    }

    if (rc != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

/* WiFi SSID */
/* Description:
 *	The API retrieves the number of WiFi SSIDs in the system.
 */
ULONG
CosaDmlWiFiSsidGetNumberOfEntries
    (
        ANSC_HANDLE                 hContext
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    int count = 0;
    UNREFERENCED_PARAMETER(hContext);
    UtopiaContext ctx;

    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;
    count =  Utopia_GetWifiSSIDInstances(&ctx);

    /* Free Utopia Context */
    Utopia_Free(&ctx,0);
    if(count < 2)
        count = 2;
    return count;
}

/* Description:
 *	The API retrieves the complete info of the WiFi SSID designated by index. The usual process is the caller gets the total number of entries, then iterate through those by calling this API.
 * Arguments:
 * 	ulIndex		Indicates the index number of the entry.
 * 	pEntry		To receive the complete info of the entry.
 */
ANSC_STATUS
CosaDmlWiFiSsidGetEntry
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        PCOSA_DML_WIFI_SSID_FULL    pEntry
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_FAILURE;
    UNREFERENCED_PARAMETER(hContext);
    UtopiaContext ctx;

    if(pEntry)
    {
        /* Initialize a Utopia Context */
        if(!Utopia_Init(&ctx))
           return ANSC_STATUS_FAILURE;

        returnStatus = Utopia_GetWifiSSIDEntry(&ctx, ulIndex, pEntry);

        /* Free Utopia Context */
        Utopia_Free(&ctx,0);
    }

    if (returnStatus != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiSsidSetValues
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        ULONG                       ulInstanceNumber,
        char*                       pAlias
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UtopiaContext ctx;
    int rc = -1;

    /* Initialize a Utopia Context */
    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;
    returnStatus = Utopia_WifiSSIDSetValues(&ctx,ulIndex,ulInstanceNumber,pAlias); 
   
    /* Free Utopia Context */
    Utopia_Free(&ctx,!returnStatus);
 
    if (returnStatus != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}    

/* Description:
 *	The API adds a new WiFi SSID into the system. 
 * Arguments:
 *	hContext	reserved.
 *	pEntry		Caller pass in the configuration through pEntry->Cfg field and gets back the generated pEntry->StaticInfo.Name, MACAddress, etc.
 */
ANSC_STATUS
CosaDmlWiFiSsidAddEntry
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_SSID_FULL    pEntry
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);

    UtopiaContext ctx;

    if (!pEntry)
    {
        return ANSC_STATUS_FAILURE;
    }

    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;
    returnStatus =  Utopia_AddWifiSSID(&ctx,pEntry);

    /* Free Utopia Context */
    Utopia_Free(&ctx,!returnStatus);
    
    if (returnStatus != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiSsidDelEntry
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);    
    UtopiaContext ctx;

    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;
    returnStatus =  Utopia_DelWifiSSID(&ctx,ulInstanceNumber);
        
    /* Free Utopia Context */
    Utopia_Free(&ctx,!returnStatus);
    
    if (returnStatus != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiSsidSetCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_SSID_CFG     pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UtopiaContext ctx;
    int rc = -1;

    if(pCfg)
    {
        /* Initialize a Utopia Context */
        if(!Utopia_Init(&ctx))
           return ANSC_STATUS_FAILURE;

        rc = Utopia_SetWifiSSIDCfg(&ctx,pCfg);

        /* Set WLAN Restart event */
        Utopia_SetEvent(&ctx,Utopia_Event_WLAN_Restart);

        /* Free Utopia Context */
        Utopia_Free(&ctx,!rc);
    }

    if (rc != 0)
        return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiSsidGetCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_SSID_CFG     pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UtopiaContext ctx;
    UNREFERENCED_PARAMETER(hContext);
    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }

    /* Initialize a Utopia Context */
    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;

    returnStatus = Utopia_GetWifiSSIDCfg(&ctx,0,pCfg);

    /* Free Utopia Context */
    Utopia_Free(&ctx,0);

    if (returnStatus != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiSsidGetDinfo
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_SSID_DINFO   pInfo
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    if (!pInfo)
    {
        return ANSC_STATUS_FAILURE;
    }
    returnStatus = Utopia_GetWifiSSIDDInfo(ulInstanceNumber,pInfo);

    if (returnStatus != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiSsidGetStats
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_SSID_STATS   pStats
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    if (!pStats)
    {
        return ANSC_STATUS_FAILURE;
    }
    returnStatus = Utopia_GetWifiSSIDStats(ulInstanceNumber,pStats);
   
    if (returnStatus != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

/* WiFi AP is always associated with a SSID in the system */
ULONG
CosaDmlWiFiAPGetNumberOfEntries
    (
        ANSC_HANDLE                 hContext
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    int count = 0;
    UNREFERENCED_PARAMETER(hContext);
    UtopiaContext ctx;

    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;
    count =  Utopia_GetWifiAPInstances(&ctx);

    /* Free Utopia Context */
    Utopia_Free(&ctx,0);
    if(count < 2)
        count = 2;
    return count;
}

ANSC_STATUS
CosaDmlWiFiApGetEntry
    (
        ANSC_HANDLE                 hContext,
        char                        *pSsid,
        PCOSA_DML_WIFI_AP_FULL      pEntry
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj      = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_FAILURE;

    UtopiaContext ctx;
    int rc = -1;
    UNREFERENCED_PARAMETER(hContext);
    if((pEntry) && (pSsid))
    {
        /* Initialize a Utopia Context */
        if(!Utopia_Init(&ctx))
           return ANSC_STATUS_FAILURE;

        returnStatus = Utopia_GetWifiAPEntry(&ctx,pSsid,pEntry);

        /* Free Utopia Context */
        Utopia_Free(&ctx,0);
    }

    if (returnStatus != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
#if defined (MULTILAN_FEATURE)
CosaDmlWiFiApAddEntry
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_FULL      pEntry
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);
    UNREFERENCED_PARAMETER(pEntry);
    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
#endif
CosaDmlWiFiApSetValues
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        ULONG                       ulInstanceNumber,
        char*                       pAlias
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UtopiaContext ctx;

    if (!pAlias)
    {
        return ANSC_STATUS_FAILURE;
    }

    /* Initialize a Utopia Context */
    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;
    returnStatus = Utopia_WifiAPSetValues(&ctx,ulIndex,ulInstanceNumber,pAlias);

    /* Free Utopia Context */
    Utopia_Free(&ctx,!returnStatus);

    if (returnStatus != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_CFG       pCfg
    )
{
    UtopiaContext ctx;
    int rc = -1;
    UNREFERENCED_PARAMETER(hContext);
    if( (pCfg) && (pSsid) )
    {
        /* Initialize a Utopia Context */
        if(!Utopia_Init(&ctx))
           return ANSC_STATUS_FAILURE;

        rc = Utopia_SetWifiAPCfg(&ctx,pCfg);

        /* Set WLAN Restart event */
        Utopia_SetEvent(&ctx,Utopia_Event_WLAN_Restart);

        /* Free Utopia Context */
        Utopia_Free(&ctx,!rc);
    }

    if (rc != 0)
        return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;

}

ANSC_STATUS
CosaDmlWiFiApGetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_CFG       pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UtopiaContext ctx;
    UNREFERENCED_PARAMETER(hContext);
    if ( (!pCfg) || (!pSsid) )
    {
        return ANSC_STATUS_FAILURE;
    }
    /* Initialize a Utopia Context */
    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;

    returnStatus = Utopia_GetWifiAPCfg(&ctx,0,pCfg);

    /* Free Utopia Context */
    Utopia_Free(&ctx,0);

    if (returnStatus != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApGetInfo
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_INFO      pInfo
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UtopiaContext ctx;
    UNREFERENCED_PARAMETER(hContext);
    if ( (!pInfo) || (!pSsid) )
    {
        return ANSC_STATUS_FAILURE;
    }
    /* Initialize a Utopia Context */
    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;

    returnStatus = Utopia_GetWifiAPInfo(&ctx,pSsid, pInfo);

    /* Free Utopia Context */
    Utopia_Free(&ctx,0);

    if (returnStatus != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApSecGetEntry
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APSEC_FULL   pEntry
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UtopiaContext ctx;
    UNREFERENCED_PARAMETER(hContext);
    if ( (!pEntry) || (!pSsid) )
    {
        return ANSC_STATUS_FAILURE;
    }

    /* Initialize a Utopia Context */
    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;

    returnStatus = Utopia_GetWifiAPSecEntry(&ctx,pSsid,pEntry);

    /* Free Utopia Context */
    Utopia_Free(&ctx,0);

    if (returnStatus != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApSecGetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APSEC_CFG    pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UtopiaContext ctx;
    UNREFERENCED_PARAMETER(hContext);
    if ( (!pCfg) || (!pSsid) )
    {
        return ANSC_STATUS_FAILURE;
    }

    /* Initialize a Utopia Context */
    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;

    returnStatus = Utopia_GetWifiAPSecCfg(&ctx,pSsid,pCfg);

    /* Free Utopia Context */
    Utopia_Free(&ctx,0);
        
    if (returnStatus != 0)          
       return ANSC_STATUS_FAILURE;  
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApSecSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APSEC_CFG    pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UtopiaContext ctx;
    UNREFERENCED_PARAMETER(hContext);
    if ( (!pCfg) || (!pSsid) )
    {
        return ANSC_STATUS_FAILURE;
    }
      
    /* Initialize a Utopia Context */
    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;

    returnStatus = Utopia_SetWifiAPSecCfg(&ctx,pSsid,pCfg);

    /* Set WLAN Restart event */
    Utopia_SetEvent(&ctx,Utopia_Event_WLAN_Restart);

    /* Free Utopia Context */
    Utopia_Free(&ctx,!returnStatus);            
        
    if (returnStatus != 0)          
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApWpsGetEntry
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APWPS_FULL   pEntry
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;

    UtopiaContext ctx;
    UNREFERENCED_PARAMETER(hContext);
    if ( (!pEntry) || (!pSsid) )
    {
        return ANSC_STATUS_FAILURE;
    }

    /* Initialize a Utopia Context */
    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;

    returnStatus = Utopia_GetWifiAPWPSEntry(&ctx,pSsid,pEntry);

    /* Free Utopia Context */
    Utopia_Free(&ctx,0);

    if (returnStatus != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
    
}

ANSC_STATUS
CosaDmlWiFiApWpsSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APWPS_CFG    pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UtopiaContext ctx;
    UNREFERENCED_PARAMETER(hContext);
    if ( (!pCfg) || (!pSsid) )
    {
        return ANSC_STATUS_FAILURE;
    }

    /* Initialize a Utopia Context */
    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;

    returnStatus = Utopia_SetWifiAPWPSCfg(&ctx,pSsid,pCfg);

    /* Free Utopia Context */
    Utopia_Free(&ctx,!returnStatus);

    if (returnStatus != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApWpsGetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APWPS_CFG    pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UtopiaContext ctx;
    UNREFERENCED_PARAMETER(hContext);
    if ( (!pCfg) || (!pSsid) )
    {
        return ANSC_STATUS_FAILURE;
    }

    /* Initialize a Utopia Context */
    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;

    returnStatus = Utopia_GetWifiAPWPSCfg(&ctx,pSsid,pCfg);

    /* Free Utopia Context */
    Utopia_Free(&ctx,0);

    if (returnStatus != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

/* Description:
 *	This routine is to retrieve the complete list of currently associated WiFi devices, 
 *	which is a dynamic table.
 * Arguments:
 *	pName   		Indicate which SSID to operate on.
 *	pulCount		To receive the actual number of entries.
 * Return:
 * The pointer to the array of WiFi associated devices, allocated by callee. 
 * If no entry is found, NULL is returned.
 */
PCOSA_DML_WIFI_AP_ASSOC_DEVICE
CosaDmlWiFiApGetAssocDevices
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PULONG                      pulCount
    )
{
    PCOSA_DML_WIFI_AP_ASSOC_DEVICE  AssocDeviceArray  = (PCOSA_DML_WIFI_AP_ASSOC_DEVICE)NULL;
    ANSC_STATUS                     returnStatus      = ANSC_STATUS_SUCCESS;
    ULONG                           index             = 0;
    ULONG                           ulCount           = 0;
    UtopiaContext ctx;
    UNREFERENCED_PARAMETER(hContext);
    if (!pSsid)
    {
        return ANSC_STATUS_FAILURE;
    }

    /* Initialize a Utopia Context */
    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;

    *pulCount = Utopia_GetAssociatedDevicesCount(&ctx,pSsid);
    if(0 == *pulCount)
    {
       Utopia_Free(&ctx,0);
       return NULL;
    }

    AssocDeviceArray = (PCOSA_DML_WIFI_AP_ASSOC_DEVICE)AnscAllocateMemory(sizeof(COSA_DML_WIFI_AP_ASSOC_DEVICE)*(*pulCount));

    if ( !AssocDeviceArray )
    {
        *pulCount = 0;
        Utopia_Free(&ctx,0);
        return NULL;
    }

    for(index = 0; index < *pulCount; index++)
    {
        returnStatus = Utopia_GetAssocDevice(&ctx,pSsid,index,&AssocDeviceArray[index]);
    }

    /* Free Utopia Context */
    Utopia_Free(&ctx,0);

    return AssocDeviceArray;
}

ANSC_STATUS
CosaDmlWiFiApMfGetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_MF_CFG    pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UtopiaContext ctx;
    UNREFERENCED_PARAMETER(hContext);
    if ( (!pCfg) || (!pSsid) )
    {
        return ANSC_STATUS_FAILURE;
    }
    /* Initialize a Utopia Context */
    if(!Utopia_Init(&ctx))
        return ANSC_STATUS_FAILURE;

    returnStatus = Utopia_GetWifiAPMFCfg(&ctx, pSsid, pCfg);

    /* Free Utopia Context */
    Utopia_Free(&ctx,0);

    if (returnStatus != 0)
       return ANSC_STATUS_FAILURE;
    else
       return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApMfSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_MF_CFG    pCfg
    )
{
    UtopiaContext ctx;
    int rc = -1;
    UNREFERENCED_PARAMETER(hContext);
    if( (pCfg) && (pSsid) )
    {
        /* Initialize a Utopia Context */
        if(!Utopia_Init(&ctx))
           return ANSC_STATUS_FAILURE;

        rc = Utopia_SetWifiAPMFCfg(&ctx, pSsid, pCfg);

        /* Free Utopia Context */
        Utopia_Free(&ctx,!rc);
    }

    if (rc != 0)
        return ANSC_STATUS_FAILURE;
    else
        return ANSC_STATUS_SUCCESS;
}

/**
 * stub funtions for USGv2 extension
 */

ANSC_STATUS
CosaDmlWiFi_GetWEPKey64ByIndex(ULONG apIns, ULONG keyIdx, PCOSA_DML_WEPKEY_64BIT pWepKey)
{
    UNREFERENCED_PARAMETER(apIns);
    UNREFERENCED_PARAMETER(keyIdx);
    UNREFERENCED_PARAMETER(pWepKey);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetWEPKey64ByIndex(ULONG apIns, ULONG keyIdx, PCOSA_DML_WEPKEY_64BIT pWepKey)
{
    UNREFERENCED_PARAMETER(apIns);
    UNREFERENCED_PARAMETER(keyIdx);
    UNREFERENCED_PARAMETER(pWepKey);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_GetWEPKey128ByIndex(ULONG apIns, ULONG keyIdx, PCOSA_DML_WEPKEY_128BIT pWepKey)
{
    UNREFERENCED_PARAMETER(apIns);
    UNREFERENCED_PARAMETER(keyIdx);
    UNREFERENCED_PARAMETER(pWepKey);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetWEPKey128ByIndex(ULONG apIns, ULONG keyIdx, PCOSA_DML_WEPKEY_128BIT pWepKey)
{
    UNREFERENCED_PARAMETER(apIns);
    UNREFERENCED_PARAMETER(keyIdx);
    UNREFERENCED_PARAMETER(pWepKey);
    return ANSC_STATUS_SUCCESS;
}

ULONG
CosaDmlMacFilt_GetNumberOfEntries(ULONG apIns)
{
    UNREFERENCED_PARAMETER(apIns);
    return 0;
}

ANSC_STATUS
CosaDmlMacFilt_GetEntryByIndex(ULONG apIns, ULONG index, PCOSA_DML_WIFI_AP_MAC_FILTER pMacFilt)
{
    UNREFERENCED_PARAMETER(apIns);
    UNREFERENCED_PARAMETER(index);
    UNREFERENCED_PARAMETER(pMacFilt);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlMacFilt_SetValues(ULONG apIns, ULONG index, ULONG ins, char *Alias)
{
    UNREFERENCED_PARAMETER(apIns);
    UNREFERENCED_PARAMETER(index);
    UNREFERENCED_PARAMETER(ins);
    UNREFERENCED_PARAMETER(Alias);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlMacFilt_AddEntry(ULONG apIns, PCOSA_DML_WIFI_AP_MAC_FILTER pMacFilt)
{
    UNREFERENCED_PARAMETER(apIns);
    UNREFERENCED_PARAMETER(pMacFilt);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlMacFilt_DelEntry(ULONG apIns, ULONG macFiltIns)
{
    UNREFERENCED_PARAMETER(apIns);
    UNREFERENCED_PARAMETER(macFiltIns);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlMacFilt_GetConf(ULONG apIns, ULONG macFiltIns, PCOSA_DML_WIFI_AP_MAC_FILTER pMacFilt)
{
    UNREFERENCED_PARAMETER(apIns);
    UNREFERENCED_PARAMETER(macFiltIns);
    UNREFERENCED_PARAMETER(pMacFilt);
    return ANSC_STATUS_SUCCESS;
}
 
ANSC_STATUS
CosaDmlMacFilt_SetConf(ULONG apIns, ULONG macFiltIns, PCOSA_DML_WIFI_AP_MAC_FILTER pMacFilt)
{
    UNREFERENCED_PARAMETER(apIns);
    UNREFERENCED_PARAMETER(macFiltIns);
    UNREFERENCED_PARAMETER(pMacFilt);
    return ANSC_STATUS_SUCCESS;
}

#elif (_COSA_DRG_TPG_)

#include <utctx.h>
#include <utctx_api.h>
#include <utapi.h>
#include <utapi_util.h>

ANSC_STATUS
CosaDmlWiFiInit
    (
        ANSC_HANDLE                 hDml,
        PANSC_HANDLE                phContext
    )
{
    UNREFERENCED_PARAMETER(hDml);
    UNREFERENCED_PARAMETER(phContext);
    return ANSC_STATUS_FAILURE;
}

/*
 *  Description:
 *     The API retrieves the number of WiFi radios in the system.
 */
ULONG
CosaDmlWiFiRadioGetNumberOfEntries
    (
        ANSC_HANDLE                 hContext
    )
{
    UNREFERENCED_PARAMETER(hContext);
    /*wifi is in remote CPU*/
    CcspTraceWarning(("CosaDmlWiFiRadioGetNumberOfEntries - This is a local call ...\n"));
    return 0;
}    
    
    
/* Description:
 *	The API retrieves the complete info of the WiFi radio designated by index. 
 *	The usual process is the caller gets the total number of entries, 
 *	then iterate through those by calling this API.
 * Arguments:
 * 	ulIndex		Indicates the index number of the entry.
 * 	pEntry		To receive the complete info of the entry.
 */
ANSC_STATUS
CosaDmlWiFiRadioGetEntry
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        PCOSA_DML_WIFI_RADIO_FULL   pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulIndex);
    if (!pEntry)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadio      = pEntry;
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg   = &pWifiRadio->Cfg;
    PCOSA_DML_WIFI_RADIO_SINFO      pWifiRadioSinfo = &pWifiRadio->StaticInfo;
    PCOSA_DML_WIFI_RADIO_DINFO      pWifiRadioDinfo = &pWifiRadio->DynamicInfo;

        return ANSC_STATUS_FAILURE;

}

ANSC_STATUS
CosaDmlWiFiRadioSetDefaultCfgValues
    (
        ANSC_HANDLE                 hContext,
        unsigned long               ulIndex,
        PCOSA_DML_WIFI_RADIO_CFG    pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulIndex);
    UNREFERENCED_PARAMETER(pCfg);
    return ANSC_STATUS_SUCCESS;
}
ANSC_STATUS
CosaDmlWiFiRadioSetValues
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        ULONG                       ulInstanceNumber,
        char*                       pAlias
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulIndex);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    UNREFERENCED_PARAMETER(pAlias);
    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
CosaDmlWiFiRadioSetCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_RADIO_CFG    pCfg        /* Identified by InstanceNumber */
    )
{
    UNREFERENCED_PARAMETER(hContext);
    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg  = pCfg;
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    
        return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
CosaDmlWiFiRadioGetCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_RADIO_CFG    pCfg        /* Identified by InstanceNumber */
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }
    
        return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
CosaDmlWiFiRadioGetDinfo
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_DINFO  pInfo
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulInstanceNumber);

    if (!pInfo)
    {
        return ANSC_STATUS_FAILURE;
    }
    
        return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
CosaDmlWiFiRadioGetChannelsInUse
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_DINFO  pInfo
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    if (!pInfo)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
        AnscCopyString(pInfo->ChannelsInUse,"1");

        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiRadioGetApChannelScan
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_DINFO  pInfo
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    if (!pInfo)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if (FALSE)
    {
        return returnStatus;
    }
    else
    { 
        AnscCopyString(pInfo->ApChannelScan ,"HOME-10C4-2.4|WPA/WPA2-PSK AES-CCMP TKIP|802.11b/g/n|-63|6|70:54:D2:00:AA:50");
        return ANSC_STATUS_SUCCESS;
    }
}
ANSC_STATUS
CosaDmlWiFiRadioGetStats
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_STATS  pStats
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    if (!pStats)
    {
        return ANSC_STATUS_FAILURE;
    }
    
        return ANSC_STATUS_FAILURE;
}

/* WiFi SSID */
static int gSsidCount = 1;
/* Description:
 *	The API retrieves the number of WiFi SSIDs in the system.
 */
ULONG
CosaDmlWiFiSsidGetNumberOfEntries
    (
        ANSC_HANDLE                 hContext
    )
{
    UNREFERENCED_PARAMETER(hContext);
    return 0;
}

/* Description:
 *	The API retrieves the complete info of the WiFi SSID designated by index. The usual process is the caller gets the total number of entries, then iterate through those by calling this API.
 * Arguments:
 * 	ulIndex		Indicates the index number of the entry.
 * 	pEntry		To receive the complete info of the entry.
 */
ANSC_STATUS
CosaDmlWiFiSsidGetEntry
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        PCOSA_DML_WIFI_SSID_FULL    pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulIndex);
    UNREFERENCED_PARAMETER(pEntry);
    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
CosaDmlWiFiSsidSetValues
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        ULONG                       ulInstanceNumber,
        char*                       pAlias
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulIndex);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    UNREFERENCED_PARAMETER(pAlias);
    return ANSC_STATUS_FAILURE;
}    

/* Description:
 *	The API adds a new WiFi SSID into the system. 
 * Arguments:
 *	hContext	reserved.
 *	pEntry		Caller pass in the configuration through pEntry->Cfg field and gets back the generated pEntry->StaticInfo.Name, MACAddress, etc.
 */
ANSC_STATUS
CosaDmlWiFiSsidAddEntry
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_SSID_FULL    pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pEntry);
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;

        return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
CosaDmlWiFiSsidDelEntry
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
        return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
CosaDmlWiFiSsidSetCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_SSID_CFG     pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pCfg);
    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
CosaDmlWiFiSsidGetCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_SSID_CFG     pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pCfg);
    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }
    
        return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
CosaDmlWiFiSsidGetDinfo
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_SSID_DINFO   pInfo
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    if (!pInfo)
    {
        return ANSC_STATUS_FAILURE;
    }
    
        return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
CosaDmlWiFiSsidGetStats
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_SSID_STATS   pStats
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    if (!pStats)
    {
        return ANSC_STATUS_FAILURE;
    }
    
        return ANSC_STATUS_FAILURE;
}


/* WiFi AP is always associated with a SSID in the system */
static int gApCount = 1;
ULONG
CosaDmlWiFiAPGetNumberOfEntries
    (
        ANSC_HANDLE                 hContext
    )
{
    UNREFERENCED_PARAMETER(hContext);
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;

        return 0;
}

ANSC_STATUS
CosaDmlWiFiApGetEntry
    (
        ANSC_HANDLE                 hContext,
        char                        *pSsid,
        PCOSA_DML_WIFI_AP_FULL      pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);
    UNREFERENCED_PARAMETER(pEntry);
    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
#if defined (MULTILAN_FEATURE)
CosaDmlWiFiApAddEntry
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_FULL      pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);
    UNREFERENCED_PARAMETER(pEntry);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
#endif
CosaDmlWiFiApSetValues
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        ULONG                       ulInstanceNumber,
        char*                       pAlias
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulIndex);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    UNREFERENCED_PARAMETER(pAlias);
    return ANSC_STATUS_SUCCESS;
  
}

ANSC_STATUS
CosaDmlWiFiApSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_CFG       pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);
    UNREFERENCED_PARAMETER(pCfg);
    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
CosaDmlWiFiApGetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_CFG       pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);
    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }
        
        return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
CosaDmlWiFiApGetInfo
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_INFO      pInfo
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);
    if (!pInfo)
    {
        return ANSC_STATUS_FAILURE;
    }
    
        return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
CosaDmlWiFiApSecGetEntry
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APSEC_FULL   pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);
    if (!pEntry)
    {
        return ANSC_STATUS_FAILURE;
    }
    
        return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
CosaDmlWiFiApSecGetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APSEC_CFG    pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);
    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }
    
        return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
CosaDmlWiFiApSecSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APSEC_CFG    pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);
    UNREFERENCED_PARAMETER(pCfg);
    return ANSC_STATUS_FAILURE;
}

/*not called*/
ANSC_STATUS
CosaDmlWiFiApWpsGetEntry
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APWPS_FULL   pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);
    UNREFERENCED_PARAMETER(pEntry);
    return ANSC_STATUS_FAILURE;

}

ANSC_STATUS
CosaDmlWiFiApWpsSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APWPS_CFG    pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);
    UNREFERENCED_PARAMETER(pCfg);
    return ANSC_STATUS_FAILURE;

}

ANSC_STATUS
CosaDmlWiFiApWpsGetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APWPS_CFG    pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);
    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }

        return ANSC_STATUS_FAILURE;
    
}

/* Description:
 *	This routine is to retrieve the complete list of currently associated WiFi devices, 
 *	which is a dynamic table.
 * Arguments:
 *	pName   		Indicate which SSID to operate on.
 *	pulCount		To receive the actual number of entries.
 * Return:
 * The pointer to the array of WiFi associated devices, allocated by callee. 
 * If no entry is found, NULL is returned.
 */
PCOSA_DML_WIFI_AP_ASSOC_DEVICE
CosaDmlWiFiApGetAssocDevices
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PULONG                      pulCount
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);    
    PCOSA_DML_WIFI_AP_ASSOC_DEVICE  AssocDeviceArray  = (PCOSA_DML_WIFI_AP_ASSOC_DEVICE)NULL;
    ULONG                           index             = 0;
    ULONG                           ulCount           = 0;
    
        
        *pulCount = ulCount;
        
        return AssocDeviceArray;
}

ANSC_STATUS
CosaDmlWiFiApMfGetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_MF_CFG    pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);
    UNREFERENCED_PARAMETER(pCfg);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApMfSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_MF_CFG    pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);
    UNREFERENCED_PARAMETER(pCfg);
    return ANSC_STATUS_SUCCESS;
}

#elif defined(_COSA_INTEL_USG_ATOM_) || defined(_COSA_BCM_MIPS_) || defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_)

#include <pthread.h>
pthread_mutex_t sWiFiThreadMutex = PTHREAD_MUTEX_INITIALIZER;

// #define wifiDbgPrintf 
#define wifiDbgPrintf printf

#define RADIO_INDEX_MAX 2


static int gRadioCount = 2;
/* WiFi SSID */
#if defined (MULTILAN_FEATURE)
static int gSsidCount = 0;
#else
static int gSsidCount = 16;
#endif
static int gApCount = 16;

static  COSA_DML_WIFI_RADIO_CFG sWiFiDmlRadioStoredCfg[2];
static  COSA_DML_WIFI_RADIO_CFG sWiFiDmlRadioRunningCfg[2];
COSA_DML_WIFI_SSID_CFG sWiFiDmlSsidStoredCfg[WIFI_INDEX_MAX];
COSA_DML_WIFI_SSID_CFG sWiFiDmlSsidRunningCfg[WIFI_INDEX_MAX];
COSA_DML_WIFI_AP_FULL sWiFiDmlApStoredCfg[WIFI_INDEX_MAX];
COSA_DML_WIFI_AP_FULL sWiFiDmlApRunningCfg[WIFI_INDEX_MAX];
COSA_DML_WIFI_APSEC_FULL  sWiFiDmlApSecurityStored[WIFI_INDEX_MAX];
COSA_DML_WIFI_APSEC_FULL  sWiFiDmlApSecurityRunning[WIFI_INDEX_MAX];
static COSA_DML_WIFI_APACCT_FULL  sWiFiDmlApAcctStored[WIFI_INDEX_MAX];
static COSA_DML_WIFI_APACCT_FULL  sWiFiDmlApAcctRunning[WIFI_INDEX_MAX];
static COSA_DML_WIFI_APWPS_FULL sWiFiDmlApWpsStored[WIFI_INDEX_MAX];
static COSA_DML_WIFI_APWPS_FULL sWiFiDmlApWpsRunning[WIFI_INDEX_MAX];
PCOSA_DML_WIFI_AP_MF_CFG  sWiFiDmlApMfCfg[WIFI_INDEX_MAX];
static BOOLEAN sWiFiDmlApStatsEnableCfg[WIFI_INDEX_MAX];
static BOOLEAN sWiFiDmlRestartHostapd = FALSE;
static BOOLEAN sWiFiDmlRestartVap[WIFI_INDEX_MAX] = { FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE };
BOOLEAN sWiFiDmlvApStatsFeatureEnableCfg = TRUE;
QUEUE_HEADER *sWiFiDmlApMfQueue[WIFI_INDEX_MAX];
static BOOLEAN sWiFiDmlWepChg[WIFI_INDEX_MAX] = { FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE };
static BOOLEAN sWiFiDmlAffectedVap[WIFI_INDEX_MAX] = { FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE };
static BOOLEAN sWiFiDmlPushWepKeys[WIFI_INDEX_MAX] = { FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE };
static BOOLEAN sWiFiDmlUpdateVlanCfg[WIFI_INDEX_MAX] = { FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE };
static BOOLEAN sWiFiDmlUpdatedAdvertisement[WIFI_INDEX_MAX] = { FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE };
//static ULONG sWiFiDmlRadioLastStatPoll[WIFI_INDEX_MAX] = { 0, 0 };
static ULONG sWiFiDmlSsidLastStatPoll[WIFI_INDEX_MAX] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; 
static COSA_DML_WIFI_BANDSTEERING_SETTINGS sWiFiDmlBandSteeringStoredSettinngs[2];


INT assocCountThreshold = 0;
INT assocMonitorDuration = 0;
INT assocGateTime = 0;

INT deauthCountThreshold = 0;
INT deauthMonitorDuration = 0;
INT deauthGateTime = 0;

extern ANSC_HANDLE bus_handle;
extern char        g_Subsystem[32];
extern PCOSA_BACKEND_MANAGER_OBJECT g_pCosaBEManager;
static PCOSA_DATAMODEL_WIFI            pMyObject = NULL;
//static COSA_DML_WIFI_SSID_SINFO   gCachedSsidInfo[16];

static PCOSA_DML_WIFI_SSID_BRIDGE  pBridgeVlanCfg = NULL;

static char *FactoryReset    	= "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.FactoryReset";
static char *ReservedSSIDNames       = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.ReservedSSIDNames";
static char *FactoryResetSSID    	= "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.FactoryResetSSID";
static char *ValidateSSIDName        = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.ValidateSSIDName";
static char *FixedWmmParams        = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.FixedWmmParamsValues";
//static char *SsidUpgradeRequired = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.SsidUpgradeRequired";
static char *GoodRssiThreshold	 = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.X_RDKCENTRAL-COM_GoodRssiThreshold";
static char *AssocCountThreshold = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.X_RDKCENTRAL-COM_AssocCountThreshold";
static char *AssocMonitorDuration = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.X_RDKCENTRAL-COM_AssocMonitorDuration";
static char *AssocGateTime = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.X_RDKCENTRAL-COM_AssocGateTime";
static char *RapidReconnectIndicationEnable     = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.X_RDKCENTRAL-COM_RapidReconnectIndicationEnable";
static char *WiFivAPStatsFeatureEnable = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.vAPStatsEnable";
static char *FeatureMFPConfig	 = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.FeatureMFPConfig";
static char *WiFiTxOverflowSelfheal = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.TxOverflowSelfheal";
static char *WiFiForceDisableWiFiRadio = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.X_RDK-CENTRAL_COM_ForceDisable";
static char *WiFiForceDisableRadioStatus = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.X_RDK-CENTRAL_COM_ForceDisable_RadioStatus";
#if defined (FEATURE_HOSTAP_AUTHENTICATOR)
static char *WiFiEnableHostapdAuthenticator = "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Control.DisableNativeHostapd";
#endif

static char *MeasuringRateRd        = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.Stats.X_COMCAST-COM_RadioStatisticsMeasuringRate";
static char *MeasuringIntervalRd = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.Stats.X_COMCAST-COM_RadioStatisticsMeasuringInterval";

// Not being set for 1st GA
// static char *RegulatoryDomain 	= "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.RegulatoryDomain";
static char *CTSProtection    	= "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.CTSProtection";
static char *BeaconInterval   	= "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.BeaconInterval";
static char *DTIMInterval   	= "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.DTIMInterval";
static char *FragThreshold   	= "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.FragThreshold";
static char *RTSThreshold   	= "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.RTSThreshold";
static char *ObssCoex   	= "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.ObssCoex";
static char *STBCEnable         = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.STBCEnable";
static char *GuardInterval      = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.GuardInterval";
static char *GreenField         = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.GreenField";
static char *TransmitPower      = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.TransmitPower";
static char *UserControl       = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.UserControl";
static char *AdminControl    = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.AdminControl";

static char *WmmEnable   	= "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.WmmEnable";
static char *UAPSDEnable   	= "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.UAPSDEnable";
static char *WmmNoAck   	= "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.WmmNoAck";
static char *BssMaxNumSta   	= "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.BssMaxNumSta";
//static char *AssocDevNextInstance = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.AssocDevNextInstance";
//static char *MacFilterNextInstance = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.MacFilterNextInstance";
// comma separated list of valid mac filter instances, format numInst:i1,i2,...,in where there are n instances. "4:1,5,6,10"
static char *MacFilterMode      = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.MacFilterMode";
static char *MacFilterList      = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.MacFilterList";
static char *MacFilter          = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.MacFilter.%d";
static char *MacFilterDevice    = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.MacFilterDevice.%d";
#if defined(CISCO_XB3_PLATFORM_CHANGES) || !defined(_XB6_PRODUCT_REQ_) 
static char *WepKeyLength    = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.WepKeyLength";
#endif
static char *ApIsolationEnable    = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.ApIsolationEnable";
static char *BSSTransitionActivated    = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.BSSTransitionActivated";
static char *BssHotSpot        = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.HotSpot";
static char *WpsPushButton = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.WpsPushButton";
static char *RapidReconnThreshold	 = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.RapidReconnThreshold";
static char *RapidReconnCountEnable	 = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.RapidReconnCountEnable";
static char *ApMFPConfig	 = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.Security.MFPConfig";
static char *vAPStatsEnable = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.vAPStatsEnable";

static char *BeaconRateCtl   = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.AccessPoint.%d.BeaconRateCtl";
static char *NeighborReportActivated	 = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_NeighborReportActivated";

#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
// DPP Sta Parameters
static char *DppVersion   = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_DPP.Version";
static char *DppPrivateSigningKey   = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_DPP.PrivateSigningKey";
static char *DppPrivateReconfigAccessKey   = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_DPP.PrivateReconfigAccessKey";
static char *DppClientMac   = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_DPP.STA.%d.ClientMac";
static char *DppInitPubKeyInfo   = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_DPP.STA.%d.InitiatorBootstrapSubjectPublicKeyInfo";
static char *DppRespPubKeyInfo   = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_DPP.STA.%d.ResponderBootstrapSubjectPublicKeyInfo";
static char *DppChannels   = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_DPP.STA.%d.Channels";
static char *DppMaxRetryCnt   = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_DPP.STA.%d.MaxRetryCount";
static char *DppActivate   = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_DPP.STA.%d.Activate";
static char *DppActivationStatus   = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_DPP.STA.%d.ActivationStatus";
static char *DppEnrolleeRespStatus   = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_DPP.STA.%d.EnrolleeResponderStatus";
static char *DppEnrolleeKeyManagement   = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_DPP.STA.%d.KeyManagement";
#endif
// Currently these are statically set during initialization
// static char *WmmCapabilities   	= "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.WmmCapabilities";
// static char *UAPSDCapabilities  = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.UAPSDCapabilities";

// Platform specific data that is stored in the ARM Intel DB and converted to PSM entries
// They will be read only on Factory Reset command and override the current Wifi configuration
static char *RadioIndex ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.Radio.SSID.%d.RadioIndex";
static char *WlanEnable ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.Radio.SSID.%d.WLANEnable";
static char *BssSsid ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.Radio.SSID.%d.SSID";
static char *HideSsid ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.Radio.SSID.%d.HideSSID";
static char *SecurityMode ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.Radio.SSID.%d.Security";
static char *EncryptionMethod ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.Radio.SSID.%d.Encryption";
static char *Passphrase ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.Radio.SSID.%d.Passphrase";
static char *WmmRadioEnable ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.Radio.SSID.%d.WMMEnable";
static char *WpsEnable ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.Radio.SSID.%d.WPSEnable";
static char *WpsPin ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.WPSPin";
static char *Vlan ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.Radio.SSID.%d.Vlan";
static char *ReloadConfig = "com.cisco.spvtg.ccsp.psm.ReloadConfig";
#if defined(_PLATFORM_RASPBERRYPI_) || defined(_PLATFORM_TURRIS_)
//Band Steering on Factory Reset
static char *bsEnable ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.X_RDKCENTRAL-COM_BandSteering.Enable";
static char *bsRssi ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.X_RDKCENTRAL-COM_BandSteering.BandSetting.%d.RSSIThreshold";
#endif
#if defined (MULTILAN_FEATURE)
//TODO: Update static array with CCSP Macros.
static char *SsidLowerLayers ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.SSID.%d.LowerLayers";
#endif
static char *WifiVlanCfgVersion ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.VlanCfgVerion";
static char *l2netBridgeInstances = "dmsb.l2net.";
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
static char *l2netBridgeName = "dmsb.l2net.%d.Name";
#endif
static char *l2netBridge = "dmsb.l2net.%d.Members.WiFi";
static char *l2netVlan   = "dmsb.l2net.%d.Vid";
#if defined (_BWG_PRODUCT_REQ_)
static char *XfinityNewl2netVlan   = "dmsb.l2net.%d.XfinityNewVid";
#endif
static char *l2netl3InstanceNum = "dmsb.atom.l2net.%d.l3net";
static char *l3netIpAddr = "dmsb.atom.l3net.%d.V4Addr";
static char *l3netIpSubNet = "dmsb.atom.l3net.%d.V4SubnetMask";

static char *PreferPrivate    	= "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.PreferPrivate";
static char *PreferPrivate_configured    	= "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.PreferPrivateConfigure";

static char *SetChanUtilThreshold ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.Radio.%d.SetChanUtilThreshold";
static char *SetChanUtilSelfHealEnable ="eRT.com.cisco.spvtg.ccsp.Device.WiFi.Radio.%d.ChanUtilSelfHealEnable";
#if defined (FEATURE_SUPPORT_INTERWORKING)
static char *InterworkingRFCEnable = "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.WiFi-Interworking.Enable";
static char *InterworkingServiceCapability      = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_InterworkingServiceCapability";
static char *InterworkingServiceEnable   = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_InterworkingServiceEnable";
static char *InterworkingASRAEnable      = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_InterworkingElement.ASRA";
static char *InterworkingESREnable       = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_InterworkingElement.ESR";
static char *SetInterworkingVenueGroup   = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_InterworkingElement.VenueInfo.VenueGroup";
static char *SetInterworkingVenueType    = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_InterworkingElement.VenueInfo.VenueType";
static char *InterworkingUESAEnable      = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_InterworkingElement.UESA";
static char *SetInterworkingHESSID       = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_InterworkingElement.HESSID";
static char *SetInterworkingInternetAvailable    = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_InterworkingElement.Internet";
static char *SetInterworkingVenueOptionPresent   = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_InterworkingElement.VenueOptionPresent";
static char *InterworkingAccessNetworkType       = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_InterworkingElement.AccessNetworkType";
static char *InterworkingHESSOptionPresentEnable         = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_InterworkingElement.HESSOptionPresent";
#endif
#define TR181_WIFIREGION_Code    "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.X_RDKCENTRAL-COM_Syndication.WiFiRegion.Code"
#define WIFIEXT_DM_OBJ           ""
#define WIFIEXT_DM_RADIO_UPDATE  ""
#define WIFIEXT_DM_WPS_UPDATE    ""
#define WIFIEXT_DM_SSID_UPDATE   ""
#define INTERVAL 50000

#define WIFI_COMP				"eRT.com.cisco.spvtg.ccsp.wifi"
#define WIFI_BUS					"/com/cisco/spvtg/ccsp/wifi"
#define HOTSPOT_DEVICE_NAME	"AP_steering"
#if defined(_CBR_PRODUCT_REQ_) || defined (_BWG_PRODUCT_REQ_)
#define HOTSPOT_NO_OF_INDEX			5
static const int Hotspot_Index[HOTSPOT_NO_OF_INDEX]={5,6,9,10,16};
#else
#define HOTSPOT_NO_OF_INDEX			4
static const int Hotspot_Index[HOTSPOT_NO_OF_INDEX]={5,6,9,10};
#endif
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
#define ATH_NAME "ath"

#define GET_SSID_INDEX(ssidList, uInstanceNumber, idx) do { \
    for ( idx = 0; idx < (ULONG)gSsidCount; idx++ ) \
        if ( ssidList[idx].InstanceNumber == uInstanceNumber ) \
            break; \
    if ( idx >= (ULONG)gSsidCount ) { \
        wifiDbgPrintf("%s: SSID entry %lu not found in dB \n", __FUNCTION__, uInstanceNumber); \
        return ANSC_STATUS_FAILURE; \
    } \
}while(0)

#define GET_AP_INDEX(apList, uInstanceNumber, idx) do { \
    for ( idx = 0; idx < (ULONG)gSsidCount; idx++ ) \
        if ( apList[idx].Cfg.InstanceNumber == (ULONG)uInstanceNumber ) \
            break; \
    if ( idx >= (ULONG)gSsidCount ) { \
        wifiDbgPrintf("%s: AP entry %lu not found in dB \n", __FUNCTION__, (ULONG)uInstanceNumber); \
        return ANSC_STATUS_FAILURE; \
    } \
}while(0)
#endif


static BOOLEAN SSID1_Changed = FALSE ;
static BOOLEAN SSID2_Changed = FALSE ;
static BOOLEAN PASSPHRASE1_Changed = FALSE ;
static BOOLEAN PASSPHRASE2_Changed = FALSE ;
static BOOLEAN AdvEnable24 = TRUE ;
static BOOLEAN AdvEnable5 = TRUE ;

static char *SSID1 = "Device.WiFi.SSID.1.SSID" ;
static char *SSID2 = "Device.WiFi.SSID.2.SSID" ;

static char *PASSPHRASE1 = "Device.WiFi.AccessPoint.1.Security.X_COMCAST-COM_KeyPassphrase" ;
static char *PASSPHRASE2 = "Device.WiFi.AccessPoint.2.Security.X_COMCAST-COM_KeyPassphrase" ;
static char *NotifyWiFiChanges = "eRT.com.cisco.spvtg.ccsp.Device.WiFi.NotifyWiFiChanges" ;

char notifyWiFiChangesVal[16] = {0};

#ifdef CISCO_XB3_PLATFORM_CHANGES
static char *WiFiRestored_AfterMigration = "eRT.com.cisco.spvtg.ccsp.Device.WiFi.WiFiRestored_AfterMigration" ;
#endif
static char *DiagnosticEnable = "eRT.com.cisco.spvtg.ccsp.Device.WiFi.NeighbouringDiagnosticEnable" ;

static ANSC_STATUS CosaDmlWiFiRadioSetTransmitPowerPercent ( int wlanIndex, int transmitPowerPercent);

static const int gWifiVlanCfgVersion = 2;
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
static int gWifi_sysevent_fd = 0;
static token_t gWifi_sysEtoken = TOKEN_NULL;
#endif

struct wifiDataTxRateHalMap wifiDataTxRateMap[] =
{
    {WIFI_BITRATE_DEFAULT, "Default"}, //Used in Set
    {WIFI_BITRATE_1MBPS,   "1"},
    {WIFI_BITRATE_2MBPS,   "2"},
    {WIFI_BITRATE_5_5MBPS, "5.5"},
    {WIFI_BITRATE_6MBPS,   "6"},
    {WIFI_BITRATE_9MBPS,   "9"},
    {WIFI_BITRATE_11MBPS,  "11"},
    {WIFI_BITRATE_12MBPS,  "12"},
    {WIFI_BITRATE_18MBPS,  "18"},
    {WIFI_BITRATE_24MBPS,  "24"},
    {WIFI_BITRATE_36MBPS,  "36"},
    {WIFI_BITRATE_48MBPS,  "48"},
    {WIFI_BITRATE_54MBPS,  "54"}
};

int isReservedSSID(char *ReservedNames, char *ssid)
{
        char *tmp = NULL;
        char name[512] = {0};
        char *save_str = NULL;

        strncpy(name, ReservedNames, sizeof(name)-1);
        tmp=strtok_r(name, ",", &save_str);
        while (tmp != NULL)
        {
            if(strcasecmp(ssid,tmp) == 0 )
            {
                //Already this string is in the reservered names.
                return 1;
            }
            tmp = strtok_r(NULL, ",", &save_str);
        }

        return 0;
}

void checkforbiddenSSID(int index)
{
#if defined(_COSA_INTEL_USG_ATOM_) && defined(_LG_MV1_CELENO_)
    int retPsmGet = CCSP_SUCCESS;
    char SSID[33] = {0};
    char *strValue  = NULL;

    wifi_getSSIDName(index, SSID);
    retPsmGet = PSM_Get_Record_Value2( bus_handle, g_Subsystem, ReservedSSIDNames, NULL, &strValue );
    if (retPsmGet == CCSP_SUCCESS)
    {
        if(isReservedSSID(strValue, SSID))
        {
            memset(SSID,0,sizeof(SSID));
            if((!wifi_getDefaultSsid(index, SSID)) && (strlen(SSID) > 0)){
                wifi_setSSIDName(index, SSID);
            } else {
                AnscTraceError(("%s Failed to revert SSID to default\n", __FUNCTION__));
            }
        }
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
    } else {
        AnscTraceError(("%spsm set failed for Reserved name\n", __FUNCTION__));
    }
#endif //Applicable only for MV1
}

void configWifi(BOOLEAN redirect)
{
	char   dst_pathname_cr[64]  =  {0};
//	sleep(2);
        CCSP_MESSAGE_BUS_INFO *bus_info = (CCSP_MESSAGE_BUS_INFO *)bus_handle;
        componentStruct_t **        ppComponents = NULL;
	char* faultParam = NULL;
        int size =0;
	int ret;
	snprintf(dst_pathname_cr, sizeof(dst_pathname_cr), "%s%s", g_Subsystem, CCSP_DBUS_INTERFACE_CR);
	ret = CcspBaseIf_discComponentSupportingNamespace(bus_handle,
               dst_pathname_cr,
               "Device.DeviceInfo.X_RDKCENTRAL-COM_ConfigureWiFi",
                g_Subsystem,        /* prefix */
                &ppComponents,
                &size);
	if ( ret != CCSP_SUCCESS )	
		return ;

	parameterValStruct_t    value = { "Device.DeviceInfo.X_RDKCENTRAL-COM_ConfigureWiFi", "true", ccsp_boolean};

	if ( !redirect )
	{
		value.parameterValue = "false";
	}
        ret = CcspBaseIf_setParameterValues
               (                        bus_handle,
                                        ppComponents[0]->componentName,
                                        ppComponents[0]->dbusPath,
                                        0, 0x0,   /* session id and write id */
                                        &value,
                                        1,
                                        TRUE,   /* no commit */
                                        &faultParam
                );

        if (ret != CCSP_SUCCESS && faultParam)
        {
	     CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s Failed to SetValue for param '%s' and ret val is %d\n",__FUNCTION__,faultParam,ret));
             printf("Error:Failed to SetValue for param '%s'\n", faultParam);
             bus_info->freefunc(faultParam);
        }
	 free_componentStruct_t(bus_handle, 1, ppComponents);

}

#define MAX_PASSPHRASE_SIZE 65
char SSID1_DEF[COSA_DML_WIFI_MAX_SSID_NAME_LEN],SSID2_DEF[COSA_DML_WIFI_MAX_SSID_NAME_LEN];
char PASSPHRASE1_DEF[MAX_PASSPHRASE_SIZE],PASSPHRASE2_DEF[MAX_PASSPHRASE_SIZE];
// Function gets the initial values of NeighbouringDiagnostic
ANSC_STATUS
CosaDmlWiFiNeighbouringGetEntry
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_NEIGHTBOURING_WIFI_DIAG_CFG   pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    if (!pEntry) return ANSC_STATUS_FAILURE;
	wifiDbgPrintf("%s\n",__FUNCTION__);
	CosaDmlGetNeighbouringDiagnosticEnable(&pEntry->bEnable);
	return ANSC_STATUS_SUCCESS;
}

// Function reads NeighbouringDiagnosticEnable value from PSM
void CosaDmlGetNeighbouringDiagnosticEnable(BOOLEAN *DiagEnable)
{
	wifiDbgPrintf("%s\n",__FUNCTION__);

    if (!g_wifidb_rfc) {
	char* strValue = NULL;
        /*CID: 71006 Unchecked return value*/
	if(PSM_Get_Record_Value2(bus_handle,g_Subsystem, DiagnosticEnable, NULL, &strValue) != CCSP_SUCCESS) {
           CcspTraceInfo(("PSM DiagnosticEnable read error !!!\n"));
        }
  	
	if(strValue)
	{
		*DiagEnable = (atoi(strValue) == 1) ? TRUE : FALSE;
        	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
	}
	else 
	{
		*DiagEnable =FALSE;
	}
    }else {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            *DiagEnable = pcfg->diagnostic_enable;
            free(pcfg);
        } else {
            *DiagEnable = FALSE;
            CcspWifiTrace(("RDK_LOG_ERROR,%s WIFI DB DiagnosticEnable read error !!!\n",__func__));
        }
    }
}

// Function sets NeighbouringDiagnosticEnable value to PSM
void CosaDmlSetNeighbouringDiagnosticEnable(BOOLEAN DiagEnableVal)
{
	char strValue[10];
	wifiDbgPrintf("%s\n",__FUNCTION__);
	memset(strValue,0,sizeof(strValue));
   	sprintf(strValue,"%d",DiagEnableVal);
        /*CID: 62214 Unchecked return value*/
        if(PSM_Set_Record_Value2(bus_handle,g_Subsystem, DiagnosticEnable, ccsp_string, strValue) != CCSP_SUCCESS)
           CcspTraceInfo(("CosaDmlSetNeighbouringDiagnosticEnable:PSM Read Error !!!\n"));
        
        if (g_wifidb_rfc) {
            struct schema_Wifi_Global_Config *pcfg = NULL;
            pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
            if (pcfg != NULL) {
                pcfg->diagnostic_enable = DiagEnableVal;
                if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) > 0) {
                    CcspTraceInfo(("%s Updated WIFI DB\n",__func__));
                    free(pcfg);
                } else {
                    CcspTraceInfo(("%s: WIFI DB update error !!!\n",__func__));
                }
            }
        }

}

void getDefaultSSID(int wlanIndex, char *DefaultSSID)
{
	char recName[256];
	char* strValue = NULL;
	int rc = -1;
#if defined(_COSA_BCM_MIPS)
	int wlanWaitLimit = WLAN_WAIT_LIMIT;
#endif
    if (!DefaultSSID) return;
        memset(recName, 0, sizeof(recName));
        snprintf(recName, sizeof(recName), BssSsid, wlanIndex+1);
        printf("getDefaultSSID fetching %s\n", recName);
#if defined(_COSA_BCM_MIPS)
	// There seemed to be problem getting the SSID and passphrase.  Give it a multiple tries.
	while ( wlanWaitLimit-- && !strValue )
	{
		PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
		if (strValue != NULL)
		{
                    rc = strcpy_s(DefaultSSID,strlen(strValue)+1,strValue);
		    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
                    if (rc != 0) {
                    ERR_CHK(rc);
                    return;
                    }
		}
		else
		{
			sleep(1);
		}
	}
#else
#if _LG_MV1_CELENO_
	if(wifi_getDefaultSsid(wlanIndex, DefaultSSID))
	{
		printf("Error in getting wifi default SSID name in:%s\n",__FUNCTION__);
	}
#else
	PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
	if (strValue != NULL)
	{
		rc = strcpy_s(DefaultSSID,strlen(strValue)+1,strValue);
		((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
		if (rc != 0) {
			ERR_CHK(rc);
			return;
		}
	}
#endif
#endif
}

void getDefaultPassphase(int wlanIndex, char *DefaultPassphrase)
{
    char recName[256];
    char* strValue = NULL;
        int rc = -1;
#if defined(_COSA_BCM_MIPS)
    int wlanWaitLimit = WLAN_WAIT_LIMIT;
#endif
    if (!DefaultPassphrase) return;
        memset(recName, 0, sizeof(recName));
        snprintf(recName, sizeof(recName), Passphrase, wlanIndex+1);
        printf("getDefaultPassphrase fetching %s\n", recName);
#if defined(_COSA_BCM_MIPS)
    // There seemed to be problem getting the SSID and passphrase.  Give it a multiple tries.
    while ( wlanWaitLimit-- && !strValue )  
    {
        PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
        if (strValue != NULL)
        {
            rc = strcpy_s(DefaultPassphrase, MAX_PASSPHRASE_SIZE, strValue);
           ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
                    if (rc != 0) {
                        ERR_CHK(rc);
                        return;
                    }
        }
        else
        {
            sleep(1);
        }
    }
#else
#if _LG_MV1_CELENO_
        if(wifi_getDefaultPassword(wlanIndex, DefaultPassphrase))
	{
		printf("Error in getting wifi default password in:%s\n",__FUNCTION__);
	}
#else
        /*CID: 64691 Unchecked return value*/
    if(PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue)!= CCSP_SUCCESS)
           CcspTraceInfo(("getDefaultPassphase:PSM read error !!!\n"));   
    if (strValue != NULL)
    {
        rc = strcpy_s(DefaultPassphrase, MAX_PASSPHRASE_SIZE, strValue);
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    if (rc != 0) {
            ERR_CHK(rc);
            return;
        }
    }
#endif
#endif
}

void WriteWiFiLog(char *msg)
{
    char LogMsg_arr[512] = {0};
    char *LogMsg = LogMsg_arr;
    char LogLevel[512] = {0};
    int rc = -1;

    if( !msg)
    {
        return;
    }
    /*CID: 144444 :BUFFER_SIZE_WARNING*/
    rc = strcpy_s(LogLevel, sizeof(LogLevel), msg);
    if (rc != 0) {
        ERR_CHK(rc);
        return;
    }
    LogLevel[sizeof(LogLevel)-1] = '\0';
    strtok_r (LogLevel, ",",&LogMsg);
    if( AnscEqualString(LogLevel, "RDK_LOG_ERROR", TRUE))
    {
        CcspTraceError(("%s", LogMsg));
    }
    else if( AnscEqualString(LogLevel, "RDK_LOG_WARN", TRUE))
    {
        CcspTraceWarning(("%s", LogMsg));
    }
    else if( AnscEqualString(LogLevel, "RDK_LOG_NOTICE", TRUE))
    {
        CcspTraceNotice(("%s", LogMsg));
    }
    else if( AnscEqualString(LogLevel, "RDK_LOG_INFO", TRUE))
    {
        CcspTraceInfo(("%s", LogMsg));
    }
    else if( AnscEqualString(LogLevel, "RDK_LOG_DEBUG", TRUE))
    {
        CcspTraceDebug(("%s", LogMsg));
    }
    else if( AnscEqualString(LogLevel, "RDK_LOG_FATAL", TRUE))
    {
        CcspTraceCritical(("%s", LogMsg));
    }
    else
    {
        CcspTraceInfo(("%s", LogMsg));
    }
}

void Captive_Portal_Check(void)
{
    int rc = -1;
	if ( (SSID1_Changed) && (SSID2_Changed) && (PASSPHRASE1_Changed) && (PASSPHRASE2_Changed))
	{
		BOOLEAN redirect;
		CcspWifiTrace(("RDK_LOG_INFO,CaptivePortal:%s - setting the CaptivePortalCheck event \n",__FUNCTION__));
		v_secure_system("/usr/bin/sysevent set CaptivePortalCheck false");
		redirect = FALSE;
  	    CcspWifiTrace(("RDK_LOG_WARN,CaptivePortal:%s - All four notification's received, Now start reverting redirection changes...\n",__FUNCTION__));
		printf("%s - All four notification's received, Now start reverting redirection changes...\n",__FUNCTION__);
		int retPsmSet;
#ifdef CISCO_XB3_PLATFORM_CHANGES
                int retPsmMigSet;
#endif
	       	retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, NotifyWiFiChanges, ccsp_string,"false");
                if (retPsmSet == CCSP_SUCCESS) {
			CcspWifiTrace(("RDK_LOG_INFO,CaptivePortal:%s - PSM set of NotifyWiFiChanges success ...\n",__FUNCTION__));
		}
		else
		{
			CcspWifiTrace(("RDK_LOG_ERROR,CaptivePortal:%s - PSM set of NotifyWiFiChanges failed and ret value is %d...\n",__FUNCTION__,retPsmSet));
		}
                if (g_wifidb_rfc) {
                   struct schema_Wifi_Global_Config *pcfg = NULL;
                   pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
                   if (pcfg != NULL) {
                       pcfg->notify_wifi_changes = FALSE;
                       if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) > 0) {
                           CcspWifiTrace(("RDK_LOG_INFO,CaptivePortal:%s Updated WIFI DB Notifywifichanges \n",__FUNCTION__));
                           free(pcfg);
                       } else {
                           CcspWifiTrace(("RDK_LOG_ERROR,CaptivePortal:%s Failed to update WIFI DB Notifywifichanges\n",__FUNCTION__));
                       }
                   }
                }
                
#ifdef CISCO_XB3_PLATFORM_CHANGES
                retPsmMigSet=PSM_Set_Record_Value2(bus_handle,g_Subsystem, WiFiRestored_AfterMigration, ccsp_string,"false");
                if (retPsmMigSet == CCSP_SUCCESS) {
                        CcspWifiTrace(("RDK_LOG_INFO,CaptivePortal:%s - PSM set of WiFiRestored_AfterMigration success ...\n",__FUNCTION__));
                }
                else
                {
                        CcspWifiTrace(("RDK_LOG_ERROR,CaptivePortal:%s - PSM set of WiFiRestored_AfterMigration failed and ret value is %d...\n",__FUNCTION__,retPsmMigSet));
                }

#endif

        rc = strcpy_s(notifyWiFiChangesVal,sizeof(notifyWiFiChangesVal),"false");
	if (rc != 0) {
            ERR_CHK(rc);
            return;
        }

		configWifi(redirect);	
#ifdef CISCO_XB3_PLATFORM_CHANGES
		PSM_Set_Record_Value2(bus_handle,g_Subsystem, FactoryReset, ccsp_string, "0");
		CcspWifiTrace(("RDK_LOG_WARN, %s:%d Reset FactoryReset to 0\n",__FUNCTION__,__LINE__));
#endif
		SSID1_Changed = FALSE;	
		SSID2_Changed = FALSE;
		PASSPHRASE1_Changed = FALSE;
		PASSPHRASE2_Changed = FALSE;
	}
}

void *RegisterWiFiConfigureCallBack(void *par)
{
    UNREFERENCED_PARAMETER(par);
    char *stringValue = NULL;
    int retPsmGet = CCSP_SUCCESS;
    int notify;
    notify = 1;
    int rc = -1;
    if (!g_wifidb_rfc) {
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, NotifyWiFiChanges, NULL, &stringValue);

    CcspWifiTrace(("RDK_LOG_WARN,%s CaptivePortal: PSM get of NotifyChanges value is %s PSM get returned %d...\n",__FUNCTION__,stringValue,retPsmGet));
    if ((retPsmGet != CCSP_SUCCESS) || (stringValue == NULL))
    {
        wifiDbgPrintf("%s %s not found in PSM and returned %d \n", __FUNCTION__, NotifyWiFiChanges, retPsmGet);
        CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : %s not found in PSM and returned %d \n", __FUNCTION__, NotifyWiFiChanges, retPsmGet));
        return NULL;
    }
    } else {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            stringValue = pcfg->notify_wifi_changes ? strdup("true") : strdup("false");
            free(pcfg);
        } else {
            stringValue = strdup("false"); 
            CcspWifiTrace(("RDK_LOG_ERROR,%s: WIFI DB Notify wifi changes read error !!!\n",__func__));
        }
    }

    if (AnscEqualString(stringValue, "true", TRUE))
    {
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(stringValue);
        rc = strcpy_s(notifyWiFiChangesVal,sizeof(notifyWiFiChangesVal),"true");
	if (rc != 0) {
            ERR_CHK(rc);
            return NULL;
        }
	sleep (15);

	char SSID1_CUR[COSA_DML_WIFI_MAX_SSID_NAME_LEN],SSID2_CUR[COSA_DML_WIFI_MAX_SSID_NAME_LEN];
	
	char PASSPHRASE1_CUR[65],PASSPHRASE2_CUR[65];

	memset(SSID1_DEF,0,sizeof(SSID1_DEF));
	memset(SSID1_CUR,0,sizeof(SSID1_CUR));

	memset(SSID2_DEF,0,sizeof(SSID2_DEF));
	memset(SSID2_CUR,0,sizeof(SSID2_CUR));

	memset(PASSPHRASE1_DEF,0,sizeof(PASSPHRASE1_DEF));
	memset(PASSPHRASE1_CUR,0,sizeof(PASSPHRASE1_CUR));

	memset(PASSPHRASE2_DEF,0,sizeof(PASSPHRASE2_DEF));
	memset(PASSPHRASE2_CUR,0,sizeof(PASSPHRASE2_CUR));

	int wlanIndex=0;
#if defined(_COSA_BCM_MIPS)
	int wlanWaitLimit = WLAN_WAIT_LIMIT;

	// There seemed to be problem getting the SSID and passphrase.  Give it a multiple tries.
	getDefaultSSID(wlanIndex,&SSID1_DEF);

	while ( wlanWaitLimit-- && !SSID1_CUR[0] )
	{
		wifi_getSSIDName(wlanIndex,&SSID1_CUR);
		if ( !SSID1_CUR[0] )
		{
			sleep(2);
		}
	}

	getDefaultPassphase(wlanIndex,&PASSPHRASE1_DEF);

	wlanWaitLimit = WLAN_WAIT_LIMIT;
	while ( wlanWaitLimit-- && !PASSPHRASE1_CUR[0] )
	{
		wifi_getApSecurityKeyPassphrase(wlanIndex,&PASSPHRASE1_CUR);
		if ( !PASSPHRASE1_CUR[0] )
		{
			sleep(2);
		}
	}

	wlanIndex=1;
	getDefaultSSID(wlanIndex,&SSID2_DEF);

	wlanWaitLimit = WLAN_WAIT_LIMIT;
	while ( wlanWaitLimit-- && !SSID2_CUR[0] )
	{
		wifi_getSSIDName(wlanIndex,&SSID2_CUR);
		if ( !SSID2_CUR[0] )
		{
			sleep(2);
		}
	}

	getDefaultPassphase(wlanIndex,&PASSPHRASE2_DEF);

	wlanWaitLimit = WLAN_WAIT_LIMIT;
	while ( wlanWaitLimit-- && !PASSPHRASE2_CUR[0] )
	{
		wifi_getApSecurityKeyPassphrase(wlanIndex,&PASSPHRASE2_CUR);
		if ( !PASSPHRASE2_CUR[0] )
		{
			sleep(2);
		}
	}
#else
	getDefaultSSID(wlanIndex,SSID1_DEF);
         /*TODO CID: 60832  Out-of-bounds access - Fix in QTN code*/
	wifi_getSSIDName(wlanIndex,SSID1_CUR);
	getDefaultPassphase(wlanIndex,PASSPHRASE1_DEF);
	wifi_getApSecurityKeyPassphrase(wlanIndex,PASSPHRASE1_CUR);

	wlanIndex=1;
         /*TODO CID: 67939  Out-of-bounds access - Fix in QTN code*/
	getDefaultSSID(wlanIndex,SSID2_DEF);
	wifi_getSSIDName(wlanIndex,SSID2_CUR);
	getDefaultPassphase(wlanIndex,PASSPHRASE2_DEF);
	wifi_getApSecurityKeyPassphrase(wlanIndex,PASSPHRASE2_CUR);
#endif

	while( access( "/tmp/wifi_initialized" , F_OK ) != 0 )
	{
		CcspWifiTrace(("RDK_LOG_WARN,CaptivePortal:%s - Waiting for wifi init ...\n",__FUNCTION__));
		sleep(2);
	}

	if (AnscEqualString(SSID1_DEF, SSID1_CUR , TRUE))
	{
		CcspWifiTrace(("RDK_LOG_WARN,CaptivePortal:%s - Registering for 2.4GHz SSID value change notification ...\n",__FUNCTION__));
  		SetParamAttr("Device.WiFi.SSID.1.SSID",notify);

	}	
	else
	{
		CcspWifiTrace(("RDK_LOG_WARN, Inside SSID1 is changed already\n"));
		SSID1_Changed = TRUE;
	}

	if (AnscEqualString(PASSPHRASE1_DEF, PASSPHRASE1_CUR , TRUE))
	{
		CcspWifiTrace(("RDK_LOG_WARN,CaptivePortal:%s - Registering for 2.4GHz Passphrase value change notification ...\n",__FUNCTION__));
        	SetParamAttr("Device.WiFi.AccessPoint.1.Security.X_COMCAST-COM_KeyPassphrase",notify);
	}
	else
	{
		CcspWifiTrace(("RDK_LOG_WARN, Inside KeyPassphrase1 is changed already\n"));
		PASSPHRASE1_Changed = TRUE;
	}
	if (AnscEqualString(SSID2_DEF, SSID2_CUR , TRUE))
	{
		CcspWifiTrace(("RDK_LOG_WARN,CaptivePortal:%s - Registering for 5GHz SSID value change notification ...\n",__FUNCTION__));
		SetParamAttr("Device.WiFi.SSID.2.SSID",notify);
	}
	else
	{
		CcspWifiTrace(("RDK_LOG_WARN, Inside SSID2 is changed already\n"));
		SSID2_Changed = TRUE;
	}

	if (AnscEqualString(PASSPHRASE2_DEF, PASSPHRASE2_CUR , TRUE))
	{
		CcspWifiTrace(("RDK_LOG_WARN,CaptivePortal:%s - Registering for 5GHz Passphrase value change notification ...\n",__FUNCTION__));
        SetParamAttr("Device.WiFi.AccessPoint.2.Security.X_COMCAST-COM_KeyPassphrase",notify);
	}
	else
	{
		CcspWifiTrace(("RDK_LOG_WARN, Inside KeyPassphrase2 is changed already\n"));
		PASSPHRASE2_Changed = TRUE;
	}
     	Captive_Portal_Check();
   }
   else
   {
         if (stringValue)
             ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(stringValue);
         rc = strcpy_s(notifyWiFiChangesVal,sizeof(notifyWiFiChangesVal),"false");
	 if (rc != 0) {
            ERR_CHK(rc);
            return NULL;
        }
	 CcspWifiTrace(("RDK_LOG_WARN, CaptivePortal: Inside else check for NotifyChanges\n"));
   }
    return NULL;
}

void
WiFiPramValueChangedCB
    (
        parameterSigStruct_t*       val,
        int                         size,
        void*                       user_data
    )
{
    UNREFERENCED_PARAMETER(size);
    UNREFERENCED_PARAMETER(user_data);
    int uptime = 0;
    char *stringValue = NULL;
    int retPsmGet = CCSP_SUCCESS;

    if (!val) return;
    printf(" value change received for parameter = %s\n",val->parameterName);

    CcspWifiTrace(("RDK_LOG_WARN,CaptivePortal:%s - value change received for parameter %s...\n",__FUNCTION__,val->parameterName));

    if (!g_wifidb_rfc) {
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, NotifyWiFiChanges, NULL, &stringValue);
    if ((retPsmGet != CCSP_SUCCESS) || (stringValue == NULL))
    {
        wifiDbgPrintf("%s %s not found in PSM and returned %d \n", __FUNCTION__, NotifyWiFiChanges, retPsmGet);
        CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : %s not found in PSM and returned %d \n", __FUNCTION__, NotifyWiFiChanges, retPsmGet));
        return;
    }
    } else {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            stringValue = pcfg->notify_wifi_changes ? strdup("true") : strdup("false");
            free(pcfg);
        } else {
            stringValue = strdup("false");
            CcspWifiTrace(("RDK_LOG_ERROR,%s: WIFI DB Notify wifi changes read error !!!\n",__func__));
        }
    }
 
    CcspWifiTrace(("RDK_LOG_WARN,%s CaptivePortal: PSM get of NotifyChanges value is %s \n PSM get returned %d...\n",__FUNCTION__,stringValue,retPsmGet));

    if (AnscEqualString(stringValue, "true", TRUE))
    {
	if (AnscEqualString((char *)val->parameterName, SSID1, TRUE) && strcmp(val->newValue,SSID1_DEF))
	{
		get_uptime(&uptime);
	  	CcspWifiTrace(("RDK_LOG_WARN,SSID_name_changed:%d\n",uptime));
		OnboardLog("SSID_name_changed:%d\n",uptime);
                t2_event_d("bootuptime_SSIDchanged_split", uptime);
		OnboardLog("SSID_name:%s\n",val->newValue);
		SSID1_Changed = TRUE;	
	}
	else if (AnscEqualString((char *)val->parameterName, SSID2, TRUE) && strcmp(val->newValue,SSID2_DEF)) 
	{
        get_uptime(&uptime);
        CcspWifiTrace(("RDK_LOG_WARN,SSID_name_changed:%d\n",uptime));
	    OnboardLog("SSID_name_changed:%d\n",uptime);
            t2_event_d("bootuptime_SSIDchanged_split", uptime);
	    OnboardLog("SSID_name:%s\n",val->newValue);        
        CcspTraceInfo(("SSID_name:%s\n",val->newValue));
		SSID2_Changed = TRUE;	
	}
	else if (AnscEqualString((char *)val->parameterName, PASSPHRASE1, TRUE) && strcmp(val->newValue,PASSPHRASE1_DEF)) 
	{
		CcspWifiTrace(("RDK_LOG_WARN,CaptivePortal:%s - Received notification for changing 2.4GHz passphrase of private WiFi...\n",__FUNCTION__));
		PASSPHRASE1_Changed = TRUE;	
	}
	else if (AnscEqualString((char *)val->parameterName, PASSPHRASE2, TRUE) && strcmp(val->newValue,PASSPHRASE2_DEF) ) 
	{
		CcspWifiTrace(("RDK_LOG_WARN,CaptivePortal:%s - Received notification for changing 5 GHz passphrase of private WiFi...\n",__FUNCTION__));
		PASSPHRASE2_Changed = TRUE;	
	} else {
	
	        printf("This is Factory reset case Ignore \n");
	    	CcspWifiTrace(("RDK_LOG_WARN,CaptivePortal:%s - This is Factory reset case Ignore ...\n",__FUNCTION__));
	        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(stringValue);
            return;
	    
	}
	Captive_Portal_Check();
    }
	
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(stringValue);

}

int SetParamAttr(char *pParameterName, int NotificationType) 
{
	char dst_pathname_cr[64] = { 0 };
	extern ANSC_HANDLE bus_handle;
	char l_Subsystem[32] = { 0 };
	int ret;
	int size = 0;
        int rc = -1;
	componentStruct_t ** ppComponents = NULL;
	char paramName[100] = { 0 };
    if (!pParameterName) return CCSP_CR_ERR_INVALID_PARAM;
	parameterAttributeStruct_t attriStruct;
    attriStruct.parameterName = NULL;
    attriStruct.notificationChanged = 1;
    attriStruct.accessControlChanged = 0;
	strcpy(l_Subsystem, "eRT.");
	rc = strcpy_s(paramName,sizeof(paramName),pParameterName);
	if (rc != 0) {
            ERR_CHK(rc);
            return 0;
        }
	snprintf(dst_pathname_cr, sizeof(dst_pathname_cr), "%s%s", l_Subsystem, CCSP_DBUS_INTERFACE_CR);

	ret = CcspBaseIf_discComponentSupportingNamespace(bus_handle,
			dst_pathname_cr, paramName, l_Subsystem, /* prefix */
			&ppComponents, &size);

	if (ret == CCSP_SUCCESS && size == 1)
		{
#ifndef USE_NOTIFY_COMPONENT
			    ret = CcspBaseIf_Register_Event(
                bus_handle, 
                ppComponents[0]->componentName, 
                "parameterValueChangeSignal"
            );      
        
        if ( CCSP_SUCCESS != ret) 
        {
            CcspWifiTrace(("RDK_LOG_ERROR,CaptivePortal:WiFi Parameter failed to Register for notification event ...\n"));

        } 

        CcspBaseIf_SetCallback2
            (
                bus_handle,
                "parameterValueChangeSignal",
                WiFiPramValueChangedCB,
                NULL
            );
#endif
		attriStruct.parameterName = paramName;
		attriStruct.notification = NotificationType; 
		ret = CcspBaseIf_setParameterAttributes(
            bus_handle,
            ppComponents[0]->componentName,
            ppComponents[0]->dbusPath,
            0,
            &attriStruct,
            1
        );
		if ( CCSP_SUCCESS != ret) 
        {
            CcspWifiTrace(("RDK_LOG_ERROR,CaptivePortal:%s WiFi Parameter failed to turn notification on ...\n",__FUNCTION__));
        } 
		free_componentStruct_t(bus_handle, size, ppComponents);
	} else {
		CcspWifiTrace(("RDK_LOG_ERROR,CaptivePortal:%s WiFi Parameter failed to SetValue for SetParamAttr ret val is: %d and param is :%s ... \n",__FUNCTION__,ret,paramName));
	}
	return ret;
}
static COSA_DML_WIFI_RADIO_POWER gRadioPowerState[2] = { COSA_DML_WIFI_POWER_UP, COSA_DML_WIFI_POWER_UP};
// Are Global for whole WiFi
static COSA_DML_WIFI_RADIO_POWER gRadioPowerSetting = COSA_DML_WIFI_POWER_UP;
static COSA_DML_WIFI_RADIO_POWER gRadioNextPowerSetting = COSA_DML_WIFI_POWER_UP;
/* zqiu
ANSC_STATUS
CosaDmlWiFiFactoryResetSsidData(int start, int end)
{
    char recName[256];
    char recValue[256];
    char *strValue = NULL;
    int retPsmGet = CCSP_SUCCESS;
    int resetSSID[2] = {0,0};
    int i = 0;
printf("%s g_Subsytem = %s\n",__FUNCTION__,g_Subsystem);

    // Delete PSM entries for the specified range of Wifi SSIDs
    for (i = start; i <= end; i++)
	{
printf("%s: deleting records for index %d \n", __FUNCTION__, i);
	    sprintf(recName, WmmEnable, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    sprintf(recName, UAPSDEnable, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    sprintf(recName, WmmNoAck, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    sprintf(recName, BssMaxNumSta, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    sprintf(recName, BssHotSpot, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    // Platform specific data that is stored in the ARM Intel DB and converted to PSM entries
	    // They will be read only on Factory Reset command and override the current Wifi configuration
	    sprintf(recName, RadioIndex, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    sprintf(recName, WlanEnable, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    sprintf(recName, BssSsid, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    sprintf(recName, HideSsid, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    sprintf(recName, SecurityMode, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    sprintf(recName, EncryptionMethod, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    sprintf(recName, Passphrase, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    sprintf(recName, WmmRadioEnable, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    sprintf(recName, WpsEnable, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    sprintf(recName, Vlan, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);
    }

    PSM_Set_Record_Value2(bus_handle,g_Subsystem, ReloadConfig, ccsp_string, "true");

    return ANSC_STATUS_SUCCESS;
}
*/
//LGI add begin
ANSC_STATUS
CosaDmlWiFi_SetWiFiReservedSSIDNames
    (
        ANSC_HANDLE phContext,
        char *ReservedName
    )
{

    PCOSA_DATAMODEL_WIFI    pMyObject = ( PCOSA_DATAMODEL_WIFI )phContext;
    char tempBuf[512] = {0};
    int retPsmSet = CCSP_SUCCESS;
    
    snprintf(tempBuf, sizeof(tempBuf)-1,"%s,%s",pMyObject->ReservedSSIDNames, ReservedName);
    strncpy(pMyObject->ReservedSSIDNames,tempBuf,sizeof(pMyObject->ReservedSSIDNames)-1);
    
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, ReservedSSIDNames,ccsp_string,tempBuf);
    if (retPsmSet != CCSP_SUCCESS) {
        return ANSC_STATUS_FAILURE;
    }
    //Radio resart is required to check the current SSID
    gRadioRestartRequest[2] = TRUE;
    
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_GetWiFiReservedSSIDNames( char  *ReservedNames )
{
        int retPsmGet = CCSP_SUCCESS;
        char *strValue  = NULL;


        retPsmGet = PSM_Get_Record_Value2( bus_handle, g_Subsystem, ReservedSSIDNames, NULL, &strValue );
        if (retPsmGet == CCSP_SUCCESS)
        {
                snprintf(ReservedNames,511,"%s",strValue);
                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
        }
        else
        {
                CcspTraceInfo(("%s Failed to get PSM\n", __FUNCTION__ ));
                return ANSC_STATUS_FAILURE;
        }
        return ANSC_STATUS_SUCCESS;
}
//LGI add end

ANSC_STATUS
CosaDmlWiFiGetFactoryResetPsmData
    (
        BOOLEAN *factoryResetFlag
    )
{
    char *strValue = NULL;
    int retPsmGet = CCSP_SUCCESS;

    if (!factoryResetFlag) return ANSC_STATUS_FAILURE;

	printf("%s g_Subsytem = %s\n",__FUNCTION__, g_Subsystem);
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s \n",__FUNCTION__));
    // Get Non-vol parameters from ARM through PSM
    // PSM may not be available yet on arm so sleep if there is not connection
    int retry = 0;
    /* PSM came around 1sec after the 1st retry from wifi (sleep is 10secs)
     * So, to handle this case,  modified the sleep duration and no of iterations
     * as we can't be looping for a long time for PSM */

    while (retry++ < 10)
    {
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s :Calling PSM GET to get FactoryReset flag value\n",__FUNCTION__));
	retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, FactoryReset, NULL, &strValue);
	if (retPsmGet == CCSP_SUCCESS) {
	printf("%s %s = %s \n",__FUNCTION__, FactoryReset, strValue);
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s :PSM GET Success %s = %s \n",__FUNCTION__, FactoryReset, strValue));
	    *factoryResetFlag = _ansc_atoi(strValue);
	    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);

            // Set to FALSE after FactoryReset has been applied
	    // PSM_Set_Record_Value2(bus_handle,g_Subsystem, FactoryReset, ccsp_string, "0");

	} else if (retPsmGet == CCSP_CR_ERR_INVALID_PARAM) { 
            *factoryResetFlag = 0;
	    printf("%s PSM_Get_Record_Value2 (%s) returned error %d \n",__FUNCTION__, FactoryReset, retPsmGet); 
		CcspWifiTrace(("RDK_LOG_WARN,WIFI %s :PSM_Get_Record_Value2 (%s) returned error %d \n",__FUNCTION__, FactoryReset, retPsmGet));
            // Set to FALSE
	    PSM_Set_Record_Value2(bus_handle,g_Subsystem, FactoryReset, ccsp_string, "0");
	} else { 
	    printf("%s PSM_Get_Record_Value2 returned error %d retry in 10 seconds \n",__FUNCTION__, retPsmGet);
		CcspWifiTrace(("RDK_LOG_WARN,WIFI %s :returned error %d retry in 10 seconds\n",__FUNCTION__, retPsmGet));	
	    AnscSleep(2000); 
	    continue;
	} 
	break;
    }

    if (retPsmGet != CCSP_SUCCESS && retPsmGet != CCSP_CR_ERR_INVALID_PARAM) {
            printf("%s Could not connect to the server error %d\n",__FUNCTION__, retPsmGet);
			CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : Could not connect to the server error %d \n",__FUNCTION__, retPsmGet));
            *factoryResetFlag = 0;
            return ANSC_STATUS_FAILURE;
    }
/*zqiu:

    // Check to see if there is a required upgrad reset for the SSID
    // This is required for Comcast builds that were upgraded from 1.3.  This code should only be trigger 
    // in Comcast non-BWG builds.
    if (*factoryResetFlag == 0) {

        retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, SsidUpgradeRequired, NULL, &strValue);
        if (retPsmGet == CCSP_SUCCESS) {
            printf("%s %s = %s \n",__FUNCTION__, SsidUpgradeRequired, strValue); 

            int upgradeFlag = _ansc_atoi(strValue);
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);

            if (upgradeFlag == 1) {
                *factoryResetFlag = 1;
            }
        }

        {
            char secSsid1[32], secSsid2[32], hotSpot1[32], hotSpot2[32];
            wifi_getSSIDName(2,secSsid1);
            wifi_getSSIDName(3,secSsid2);
            wifi_getSSIDName(4,hotSpot1);
            wifi_getSSIDName(5,hotSpot2);
            wifiDbgPrintf("%s: secSsid1 = %s secSsid2 = %s hotSpot1 = %s hotSpot2 = %s \n", __func__, secSsid1,secSsid2,hotSpot1,hotSpot2);

            if ( (strcmp(secSsid1,"Security-2.4") == 0)  || (strcmp(secSsid2,"Security-5") == 0)  ||
                 (strcmp(hotSpot1,"Hotspot-2.4") == 0)  || (strcmp(hotSpot2,"Hotspot-5") == 0) ) {
                wifiDbgPrintf("%s: Factory Reset required for all but the primary SSIDs \n", __func__);
                *factoryResetFlag = 1;
            }
        }

        if (*factoryResetFlag == 1) {
            char recName[256];

            // Set FactoryReset flags 
            sprintf(recName, FactoryResetSSID, 1);
            PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "254");
            sprintf(recName, FactoryResetSSID, 2);
            PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "254");
            PSM_Set_Record_Value2(bus_handle,g_Subsystem, FactoryReset, ccsp_string, "1");
            PSM_Set_Record_Value2(bus_handle,g_Subsystem, SsidUpgradeRequired, ccsp_string, "0"); 
            //  Force new PSM values from default config and wifidb in Intel db for non-Primary SSIDs
            CosaDmlWiFiFactoryResetSsidData(3,16);
        }
    } else {
        // if the FactoryReset was set, we don't need to do the Upgrade reset as well.
        PSM_Set_Record_Value2(bus_handle,g_Subsystem, SsidUpgradeRequired, ccsp_string, "0");
    }
*/
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Returning Success \n",__FUNCTION__));
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiGetResetRequired
(
BOOLEAN *resetFlag
)
{
    char *strValue = NULL;
    int retPsmGet = CCSP_SUCCESS;
    int version;

    if (!resetFlag) return ANSC_STATUS_FAILURE;

    wifiDbgPrintf("%s g_Subsytem = %s\n",__FUNCTION__, g_Subsystem);  
    *resetFlag = FALSE;
    
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Calling PSM GET for %s \n",__FUNCTION__,WifiVlanCfgVersion));
    if (g_wifidb_rfc) {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg == NULL) {
            retPsmGet = CCSP_FAILURE;
            CcspWifiTrace(("RDK_LOG_ERROR,%s: WIFI DB Vlan config version read error !!!\n",__func__));
        } else {
            char vlan_version[4];
            sprintf(vlan_version,"%d",pcfg->vlan_cfg_version);
            strValue = strdup(vlan_version);
        }
    } else {
        retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, WifiVlanCfgVersion, NULL, &strValue);
    }
    if (retPsmGet == CCSP_SUCCESS) {
        wifiDbgPrintf("%s %s = %s \n",__FUNCTION__, WifiVlanCfgVersion, strValue); 
		CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : PSM GET Success %s = %s\n",__FUNCTION__, WifiVlanCfgVersion, strValue));
        version = _ansc_atoi(strValue);
        if (version != gWifiVlanCfgVersion) {
            wifiDbgPrintf("%s: Radio restart required:  %s value of %s was not the required cfg value %d \n",__FUNCTION__, WifiVlanCfgVersion, strValue, gWifiVlanCfgVersion);
			CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Radio restart required:  %s value of %s was not the required cfg value %d \n",__FUNCTION__, WifiVlanCfgVersion, strValue, gWifiVlanCfgVersion));
            *resetFlag = TRUE;
        } else {
            *resetFlag = FALSE;
        }
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    } else {
        wifiDbgPrintf("%s %s not found in PSM set to %d \n",__FUNCTION__, WifiVlanCfgVersion , gWifiVlanCfgVersion);
		CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : %s not found in PSM set to %d \n",__FUNCTION__,WifiVlanCfgVersion, gWifiVlanCfgVersion));
        *resetFlag = TRUE;
    }
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Returning Success \n",__FUNCTION__));
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiGetResetHotSpotRequired
(
BOOLEAN *resetFlag
)
{
    char *strValue = NULL;
	char recName[256];
    int retPsmGet = CCSP_SUCCESS;
    int i;

    if (!resetFlag) return ANSC_STATUS_FAILURE;

    wifiDbgPrintf("%s g_Subsytem = %s\n",__FUNCTION__, g_Subsystem);  
    *resetFlag = FALSE;

    // if either Primary SSID is set to TRUE, then HotSpot needs to be reset 
    // since these should not be HotSpot SSIDs
    for (i = 1; i <= 2; i++) {
        sprintf(recName, BssHotSpot, i);
        retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
        if (retPsmGet == CCSP_SUCCESS) {
            wifiDbgPrintf("%s: found BssHotSpot value = %s \n", __func__, strValue);
            CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : found BssHotSpot value = %s \n",__FUNCTION__, strValue));
            BOOL enable = _ansc_atoi(strValue);
            if (enable == TRUE) {
                *resetFlag = TRUE;
            }
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        }
    }
    CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Returning Success \n",__FUNCTION__));
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiGetRadioSetSecurityDataPsmData
    (
        ULONG                       wlanIndex,
        ULONG                       ulInstance,
        ULONG                       modeEnabled
    )
{
    char securityType[32];
    char authMode[32];
    int rc = -1;

#ifndef CISCO_XB3_PLATFORM_CHANGES
    UNREFERENCED_PARAMETER(ulInstance);
#endif

	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s \n",__FUNCTION__));
    memset(securityType,0,sizeof(securityType));
    if (modeEnabled == COSA_DML_WIFI_SECURITY_None)
    {
        rc = strcpy_s(securityType,sizeof(securityType), "None");
        if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
        rc = strcpy_s(authMode,sizeof(authMode),"None");
	if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
    }
#ifdef CISCO_XB3_PLATFORM_CHANGES
	else if (modeEnabled == COSA_DML_WIFI_SECURITY_WEP_64 ||
               modeEnabled == COSA_DML_WIFI_SECURITY_WEP_128)
        {
            char recName[256];

            rc = strcpy_s(securityType,sizeof(securityType),"Basic");
	    if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
            rc = strcpy_s(authMode,sizeof(authMode),"None");
	    if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }

            wifi_setApBasicEncryptionMode(wlanIndex, "WEPEncryption");
 
            wifi_setApWepKeyIndex(wlanIndex, 1);
 
            memset(recName, 0, sizeof(recName));
            snprintf(recName, sizeof(recName), WepKeyLength, ulInstance);
            if (modeEnabled == COSA_DML_WIFI_SECURITY_WEP_64)
            {
                    PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "64" );
            } else if (modeEnabled == COSA_DML_WIFI_SECURITY_WEP_128)
            {
                    PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "128" );
            }
            if (g_wifidb_rfc) {
                struct schema_Wifi_VAP_Config  *pcfg= NULL;
                pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[wlanIndex], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
                if (pcfg != NULL) {
                    if (modeEnabled == COSA_DML_WIFI_SECURITY_WEP_64) {
                        pcfg->wep_key_length = 64;
                    } else if (modeEnabled == COSA_DML_WIFI_SECURITY_WEP_128) {
                        pcfg->wep_key_length = 128;
                    }
                }
           }
        }
#endif
#ifndef _XB6_PRODUCT_REQ_
	else if (modeEnabled == COSA_DML_WIFI_SECURITY_WPA_WPA2_Personal)
    {
        rc = strcpy_s(securityType,sizeof(securityType),"WPAand11i");
	if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
        rc = strcpy_s(authMode,sizeof(authMode),"PSKAuthentication");
	if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
    }
	else if (modeEnabled == COSA_DML_WIFI_SECURITY_WPA_Personal)
    {
        rc = strcpy_s(securityType,sizeof(securityType),"WPA");
	if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
        rc = strcpy_s(authMode,sizeof(authMode),"PSKAuthentication");
	if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
    } else if (modeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Personal)
    {
        rc = strcpy_s(securityType,sizeof(securityType),"11i");
	if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
        rc = strcpy_s(authMode,sizeof(authMode),"PSKAuthentication");
	if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
    } else
    {
        rc = strcpy_s(securityType,sizeof(securityType),"None");
	if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
        rc = strcpy_s(authMode,sizeof(authMode),"None");
	if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
    }
#else
	else if (modeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Personal)
    {
        rc = strcpy_s(securityType,sizeof(securityType),"11i");
	if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
        rc = strcpy_s(authMode,sizeof(authMode),"PSKAuthentication");
	if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
    } else
    {
        rc = strcpy_s(securityType,sizeof(securityType),"None");
	if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
        rc = strcpy_s(authMode,sizeof(authMode),"None");
	if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
    }
#endif
    wifi_setApBeaconType(wlanIndex, securityType);
    wifi_setApBasicAuthenticationMode(wlanIndex, authMode);
    CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wlanIndex = %lu,securityType =%s,authMode = %s\n",__FUNCTION__, wlanIndex,securityType,authMode));
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Returning Success \n",__FUNCTION__));
    return ANSC_STATUS_SUCCESS;
}
ANSC_STATUS
CosaDmlWiFiGetRadioFactoryResetPsmData
    (
        ULONG                       wlanIndex,
        ULONG                       ulInstance
    )
{
    char *strValue = NULL;
    int retPsmGet = CCSP_SUCCESS;
    UNREFERENCED_PARAMETER(ulInstance);
    printf("%s g_Subsytem = %s\n",__FUNCTION__, g_Subsystem);

    unsigned int password = 0;
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Calling PSM GET for %s \n",__FUNCTION__, WpsPin));
    if (!g_wifidb_rfc) {
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, WpsPin, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        password = _ansc_atoi(strValue);
		CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : PSM GET Success password %d \n",__FUNCTION__, password));
        wifi_setApWpsDevicePIN(wlanIndex, password);
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    } else {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            password = _ansc_atoi(pcfg->wps_pin);
            free(pcfg);
            wifi_setApWpsDevicePIN(wlanIndex, password);
        }
    }
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Returning Success \n",__FUNCTION__));
    return ANSC_STATUS_SUCCESS;
}

#if defined(_PLATFORM_RASPBERRYPI_) || defined(_PLATFORM_TURRIS_)
ANSC_STATUS
CosaDmlWiFiGetBSFactoryResetPsmData
    (
        ULONG                       wlanIndex,
        ULONG                       ulInstance
    )
{
    char *strValue = NULL;
    char recName[256];
    int intValue;
    BOOL bsEn;
    int retPsmGet = CCSP_SUCCESS;

	printf("%s g_Subsytem = %s wlanIndex = %d \n",__FUNCTION__, g_Subsystem, wlanIndex);
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wlanIndex = %d \n",__FUNCTION__, wlanIndex));
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s Get Factory Reset PsmData & Apply to WIFI ",__FUNCTION__));

	retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, bsEnable, NULL, &strValue);
	if (retPsmGet == CCSP_SUCCESS) {
		bsEn = _ansc_atoi(strValue);
		int retStatus = wifi_setBandSteeringEnable(bsEn);
		if(retStatus == 0) {
			CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wifi_setBandSteeringEnable success index %d , %d",__FUNCTION__,wlanIndex,intValue));
		}
		else {
			CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wifi_setBandSteeringEnable failed  index %d , %d",__FUNCTION__,wlanIndex,intValue));
		}
		((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
	}

	memset(recName, 0, sizeof(recName));
	snprintf(recName, sizeof(recName), bsRssi, ulInstance);

	retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
	if (retPsmGet == CCSP_SUCCESS) {
		intValue = _ansc_atoi(strValue);
		int retStatus = wifi_setBandSteeringRSSIThreshold(wlanIndex, intValue);
		if(retStatus == 0) {
			CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wifi_setBandSteeringRSSIThreshold success index %d , %d",__FUNCTION__,wlanIndex,intValue));
		}
		else {
			CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wifi_setBandSteeringRSSIThreshold failed  index %d , %d",__FUNCTION__,wlanIndex,intValue));
		}
		((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
	}
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Returning Success \n",__FUNCTION__));
	return ANSC_STATUS_SUCCESS;
}
#endif


ANSC_STATUS
CosaDmlWiFiGetSSIDFactoryResetPsmData
    (
        ULONG                       wlanIndex,
        ULONG                       ulInstance
    )
{
    char *strValue = NULL;
    char recName[256];
    int intValue;
    int retPsmGet = CCSP_SUCCESS;

printf("%s g_Subsytem = %s wlanInex = %lu \n",__FUNCTION__, g_Subsystem, wlanIndex);
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wlanInex = %lu \n",__FUNCTION__, wlanIndex));
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), RadioIndex, ulInstance);
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s Get Factory Reset PsmData & Apply to WIFI ",__FUNCTION__));
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        intValue = _ansc_atoi(strValue);
fprintf(stderr, "-- %s %d wifi_setApRadioIndex  wlanIndex = %lu intValue=%d \n", __func__, __LINE__, wlanIndex, intValue);
	wifi_setApRadioIndex(wlanIndex, intValue);
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }

    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), WlanEnable, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        intValue = _ansc_atoi(strValue);
    int retStatus = wifi_setApEnable(wlanIndex, intValue);
	if(retStatus == 0) {
		CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wifi_setApEnable success index %lu , %d\n",__FUNCTION__,wlanIndex,intValue));
	}
	else {
		CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wifi_setApEnable failed  index %lu , %d\n",__FUNCTION__,wlanIndex,intValue));
	}
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }

    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), BssSsid, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
	wifi_setSSIDName(wlanIndex, strValue);
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }

    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), HideSsid, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        intValue = (_ansc_atoi(strValue) == 0) ? 1 : 0;
        wifi_setApSsidAdvertisementEnable(wlanIndex, intValue);
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }

    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), SecurityMode, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        intValue = _ansc_atoi(strValue);
        CosaDmlWiFiGetRadioSetSecurityDataPsmData(wlanIndex, ulInstance, intValue);
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }

    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), EncryptionMethod, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        intValue = _ansc_atoi(strValue);
	if (intValue == COSA_DML_WIFI_AP_SEC_TKIP)
	{ 
	    wifi_setApWpaEncryptionMode(wlanIndex, "TKIPEncryption");
	} else if ( intValue == COSA_DML_WIFI_AP_SEC_AES)
	{
	    wifi_setApWpaEncryptionMode(wlanIndex, "AESEncryption");
	} 
#ifndef _XB6_PRODUCT_REQ_	
	else if ( intValue == COSA_DML_WIFI_AP_SEC_AES_TKIP)
	{
	    wifi_setApWpaEncryptionMode(wlanIndex, "TKIPandAESEncryption");
	}
#endif
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }

    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), Passphrase, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        wifi_setApSecurityKeyPassphrase(wlanIndex, strValue);
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }

    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), WmmRadioEnable, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS)
    {
        intValue = _ansc_atoi(strValue);
        wifi_setApWmmEnable(wlanIndex, intValue);
        // This value is also written to PSM because it is not stored by Atheros
        // Want to override with Platform specific data on factory reset
        snprintf(recName, sizeof(recName),WmmEnable, ulInstance);
        PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
        snprintf(recName, sizeof(recName), UAPSDEnable, ulInstance);
        PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }

    unsigned int password = 0;
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, WpsPin, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        password = _ansc_atoi(strValue);
        wifi_setApWpsDevicePIN(wlanIndex, password);
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }

    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), WpsEnable, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        int intValue = _ansc_atoi(strValue);
#if !defined(_COSA_BCM_MIPS_)&& !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        if ( (intValue == TRUE) && (password != 0) ) {
           intValue = 2; // Configured 
        } 
#endif
#ifdef CISCO_XB3_PLATFORM_CHANGES
            wifi_setApWpsEnable(wlanIndex, intValue);
#else
            wifi_setApWpsEnable(wlanIndex, (intValue>0));
#endif	
          ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    

   memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), BssHotSpot, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        // if this is a HotSpot SSID, then set EnableOnline=TRUE and
        //  it should only be brought up once the RouterEnabled=TRUE
        wifiDbgPrintf("%s: found BssHotSpot value = %s \n", __func__, strValue);

#if !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        BOOL enable = _ansc_atoi(strValue);
        wifi_setApEnableOnLine(wlanIndex,enable);
#endif
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    } else {
        wifiDbgPrintf("%s: didn't find BssHotSpot setting EnableOnline to FALSE \n", __func__);
#if !defined(_COSA_BCM_MIPS_)&& !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        wifi_setApEnableOnLine(wlanIndex,0);
#endif
    }

	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Returning Success \n",__FUNCTION__));
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiGetRadioPsmData
    (
        PCOSA_DML_WIFI_RADIO_CFG    pCfg        /* Identified by InstanceNumber */
    )
{
    char *strValue = NULL;
    char recName[256];
    int intValue=0;
    int retPsmGet = CCSP_SUCCESS;
    ULONG                       wlanIndex;
    ULONG                       ulInstance;
    struct schema_Wifi_Radio_Config  *pcfg= NULL;
    if (pCfg != NULL) {
        ulInstance = pCfg->InstanceNumber;
        wlanIndex = pCfg->InstanceNumber - 1;
    } else {
        return ANSC_STATUS_FAILURE;
    }

printf("%s g_Subsytem = %s\n",__FUNCTION__, g_Subsystem);

    // All these values need to be set once the VAP is up
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wlanInex = %lu \n",__FUNCTION__, wlanIndex));
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s Get Factory Reset Radio PsmData & Apply to WIFI \n",__FUNCTION__));

    if (g_wifidb_rfc) {
        char radio_name[16] = {0};
        if (convert_radio_to_name(pCfg->InstanceNumber,radio_name) == 0) {
            pcfg = (struct schema_Wifi_Radio_Config  *) wifi_db_get_table_entry(radio_name, "radio_name",&table_Wifi_Radio_Config,OCLM_STR);
            if (pcfg == NULL) {
                CcspWifiTrace(("RDK_LOG_WARN,%s WIFI DB Failed to radio entry for index %d",__FUNCTION__,(int)wlanIndex));
                return ANSC_STATUS_FAILURE;
            }
        }
    }

    if (!g_wifidb_rfc) {
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), CTSProtection, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
	BOOL enable = _ansc_atoi(strValue);
        pCfg->CTSProtectionMode = (enable == TRUE) ? TRUE : FALSE;

        wifi_setRadioCtsProtectionEnable(wlanIndex, enable);
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    } else {
        pCfg->CTSProtectionMode = pcfg->cts_protection;
        wifi_setRadioCtsProtectionEnable(wlanIndex, pCfg->CTSProtectionMode);
    }

    if (!g_wifidb_rfc) {
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), BeaconInterval, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        intValue = _ansc_atoi(strValue);
        pCfg->BeaconInterval = intValue;
	wifi_setApBeaconInterval(wlanIndex, intValue);
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    } else {
        pCfg->BeaconInterval = pcfg->beacon_interval;
        wifi_setApBeaconInterval(wlanIndex, pCfg->BeaconInterval);
    }

    if (!g_wifidb_rfc) {
    memset(recName, 0, sizeof(recName));

    snprintf(recName, sizeof(recName), DTIMInterval, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        intValue = _ansc_atoi(strValue);
        pCfg->DTIMInterval = intValue;
        wifi_setApDTIMInterval(wlanIndex, intValue);
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    } else {
        pCfg->DTIMInterval = pcfg->dtim_period;
        wifi_setApDTIMInterval(wlanIndex, pCfg->DTIMInterval);
    }
    
    if (!g_wifidb_rfc) {
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), FragThreshold, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    } else {
        intValue = pcfg->fragmentation_threshold;
    }
    if (retPsmGet == CCSP_SUCCESS) {
        char opStandards[32];

#if defined (_WIFI_AX_SUPPORT_)
	UINT pureMode;
#else
	BOOL gOnly;
	BOOL nOnly;
    BOOL acOnly;
#endif
        if (!g_wifidb_rfc) {
        intValue = _ansc_atoi(strValue);
        }
        pCfg->FragmentationThreshold = intValue;

#if defined (_WIFI_AX_SUPPORT_)
		wifi_getRadioMode(wlanIndex, opStandards, &pureMode);
#else
		wifi_getRadioStandard(wlanIndex, opStandards, &gOnly, &nOnly, &acOnly);
#endif
		if (strncmp("n",opStandards,1)!=0 && strncmp("ac",opStandards,1)!=0) {
	    wifi_setRadioFragmentationThreshold(wlanIndex, intValue);
        }
        if (strValue) {
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        }
    }

    if (!g_wifidb_rfc) {
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), RTSThreshold, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        intValue = _ansc_atoi(strValue);
        pCfg->RTSThreshold = intValue;
	wifi_setApRtsThreshold(wlanIndex, intValue);
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    } else {
        pCfg->RTSThreshold = pcfg->rts_threshold;
        wifi_setApRtsThreshold(wlanIndex, pCfg->RTSThreshold);
    }

    if (!g_wifidb_rfc) {
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), ObssCoex, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
	BOOL enable = _ansc_atoi(strValue);
        pCfg->ObssCoex = (enable == TRUE) ? TRUE : FALSE;

        wifi_setRadioObssCoexistenceEnable(wlanIndex, enable);
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    } 
    } else {
        pCfg->ObssCoex = pcfg->obss_coex;
        wifi_setRadioObssCoexistenceEnable(wlanIndex, pCfg->ObssCoex);
    }

    if (!g_wifidb_rfc) {
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), STBCEnable, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        BOOL enable = _ansc_atoi(strValue);
        pCfg->X_CISCO_COM_STBCEnable = (enable == TRUE) ? TRUE : FALSE;
	wifi_setRadioSTBCEnable(wlanIndex, enable);
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    } else {
        pCfg->X_CISCO_COM_STBCEnable = pcfg->stbc_enable;
        wifi_setRadioSTBCEnable(wlanIndex, pCfg->X_CISCO_COM_STBCEnable);
    }

    if (!g_wifidb_rfc) {
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), GuardInterval, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        // if COSA_DML_WIFI_GUARD_INTVL_800ns set to FALSE otherwise was (400ns or Auto, set to TRUE)
        int guardInterval = _ansc_atoi(strValue);
        pCfg->GuardInterval = guardInterval;
        //BOOL enable = (guardInterval == 2) ? FALSE : TRUE;
		//wifi_setRadioGuardInterval(wlanIndex, enable);
		wifi_setRadioGuardInterval(wlanIndex, (pCfg->GuardInterval == 2)?"800nsec":"Auto");
		((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    } else {
        pCfg->GuardInterval = pcfg->guard_interval;
        wifi_setRadioGuardInterval(wlanIndex, (pCfg->GuardInterval == 2)?"800nsec":"Auto");
    }

    if (!g_wifidb_rfc) {
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName),TransmitPower, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        int transmitPower = _ansc_atoi(strValue);
        pCfg->TransmitPower = transmitPower;
        wifiDbgPrintf("%s: Found TransmitPower in PSM %d\n", __func__ , transmitPower);
        if ( (  gRadioPowerState[wlanIndex] == COSA_DML_WIFI_POWER_UP ) &&
             ( gRadioNextPowerSetting != COSA_DML_WIFI_POWER_DOWN ) )
        {
            CosaDmlWiFiRadioSetTransmitPowerPercent(wlanIndex,transmitPower);
        }
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    } else {
        pCfg->TransmitPower = pcfg->transmit_power;
        if ( (  gRadioPowerState[wlanIndex] == COSA_DML_WIFI_POWER_UP ) &&
             ( gRadioNextPowerSetting != COSA_DML_WIFI_POWER_DOWN ) )
        {
            CosaDmlWiFiRadioSetTransmitPowerPercent(wlanIndex,pCfg->TransmitPower);
        }
    }

    if (!g_wifidb_rfc) {
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), UserControl, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        pCfg->MbssUserControl = atoi(strValue);
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    } else {
        pCfg->MbssUserControl =  0x0003; // ath0 and ath1 on by default 
    }
    } else {
        pCfg->MbssUserControl = pcfg->user_control;
    }

    if (!g_wifidb_rfc) {
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), AdminControl, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        pCfg->AdminControl = atoi(strValue);
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    } else {
        pCfg->AdminControl =  0xFFFF; // all on by default
    }
    } else {
        pCfg->AdminControl = pcfg->admin_control;
    }

    if (!g_wifidb_rfc) {
	memset(recName, 0, sizeof(recName));
	snprintf(recName, sizeof(recName), GreenField, ulInstance);
	retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
	if (retPsmGet == CCSP_SUCCESS) {
        pCfg->X_CISCO_COM_11nGreenfieldEnabled =  _ansc_atoi(strValue);
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        }
    } else {
        pCfg->X_CISCO_COM_11nGreenfieldEnabled = pcfg->greenfield_enable;
    } 
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Returning Success \n",__FUNCTION__));
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiSetRadioPsmData
    (
        PCOSA_DML_WIFI_RADIO_CFG    pCfg,
        ULONG                       wlanIndex,
        ULONG                       ulInstance
    )
{
    char strValue[256];
    char recName[256];
    int retPsmSet = CCSP_SUCCESS;
    UNREFERENCED_PARAMETER(wlanIndex);
    struct schema_Wifi_Radio_Config  *pcfg= NULL;
    char radio_name[16] = {0};

    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }

    wifiDbgPrintf("%s g_Subsytem = %s\n",__FUNCTION__, g_Subsystem);
    CcspWifiTrace(("RDK_LOG_WARN,WIFI %s \n",__FUNCTION__));

    if (g_wifidb_rfc) {
        if (convert_radio_to_name((int)ulInstance,radio_name) == 0) {
            pcfg = (struct schema_Wifi_Radio_Config  *) wifi_db_get_table_entry(radio_name, "radio_name",&table_Wifi_Radio_Config,OCLM_STR);
            if (pcfg == NULL) {
                CcspWifiTrace(("RDK_LOG_ERROR, %s Radio instance ulInstance DB %d not found\n",__func__,(int)ulInstance));
                return ANSC_STATUS_FAILURE;
            }
        }
    }


    PCOSA_DML_WIFI_RADIO_CFG        pStoredCfg  = &sWiFiDmlRadioStoredCfg[pCfg->InstanceNumber-1];

    if (pCfg->CTSProtectionMode != pStoredCfg->CTSProtectionMode) {
        memset(recName, '\0', sizeof(recName));
        memset(strValue, '\0', sizeof(strValue));
        snprintf(recName, sizeof(recName), CTSProtection, ulInstance);
    snprintf(strValue, sizeof(strValue), "%d",pCfg->CTSProtectionMode);
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
    if (retPsmSet != CCSP_SUCCESS) {
	wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting CTSProtectionMode\n",__FUNCTION__, retPsmSet); 
    }
    if (g_wifidb_rfc) {
            pcfg->cts_protection = pCfg->CTSProtectionMode;    
        }
    }

    if (pCfg->BeaconInterval != pStoredCfg->BeaconInterval) {
        memset(recName, '\0', sizeof(recName));
        memset(strValue, '\0', sizeof(strValue));
        snprintf(recName, sizeof(recName), BeaconInterval, ulInstance);
    snprintf(strValue,  sizeof(strValue), "%lu",pCfg->BeaconInterval);
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
    if (retPsmSet != CCSP_SUCCESS) {
	wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting BeaconInterval\n",__FUNCTION__, retPsmSet); 
    }
        if (g_wifidb_rfc) {
            pcfg->beacon_interval = pCfg->BeaconInterval;
        }
    }

    if (pCfg->DTIMInterval != pStoredCfg->DTIMInterval) {
        memset(recName, '\0', sizeof(recName));
        memset(strValue, '\0', sizeof(strValue));
        snprintf(recName, sizeof(recName), DTIMInterval, ulInstance);
    snprintf(strValue,  sizeof(strValue), "%lu",pCfg->DTIMInterval);
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
    if (retPsmSet != CCSP_SUCCESS) {
	wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting DTIMInterval\n",__FUNCTION__, retPsmSet); 
    }
        if (g_wifidb_rfc) {
            pcfg->dtim_period = pCfg->DTIMInterval;
        }
    }

    if (pCfg->FragmentationThreshold != pStoredCfg->FragmentationThreshold) {
        memset(recName, '\0', sizeof(recName));
        memset(strValue, '\0', sizeof(strValue));
        snprintf(recName, sizeof(recName), FragThreshold, ulInstance);
    snprintf(strValue, sizeof(strValue),"%lu",pCfg->FragmentationThreshold);
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
    if (retPsmSet != CCSP_SUCCESS) {
	wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting FragmentationThreshold\n",__FUNCTION__, retPsmSet); 
    }
        if (g_wifidb_rfc) {
            pcfg->fragmentation_threshold = pCfg->FragmentationThreshold;
        }
    }

    if (pCfg->RTSThreshold != pStoredCfg->RTSThreshold) {
        memset(recName, '\0', sizeof(recName));
        memset(strValue, '\0', sizeof(strValue));
    snprintf(recName, sizeof(recName), RTSThreshold, ulInstance);
    snprintf(strValue, sizeof(strValue),"%lu",pCfg->RTSThreshold);
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
    if (retPsmSet != CCSP_SUCCESS) {
	wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting RTS Threshold\n",__FUNCTION__, retPsmSet); 
    }
        if (g_wifidb_rfc) {
            pcfg->rts_threshold = pCfg->RTSThreshold;
        }
    }

    if (pCfg->ObssCoex != pStoredCfg->ObssCoex) {
        memset(recName, '\0', sizeof(recName));
        memset(strValue, '\0', sizeof(strValue));
        snprintf(recName, sizeof(recName), ObssCoex, ulInstance);
    snprintf(strValue, sizeof(strValue),"%d",pCfg->ObssCoex);
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
    if (retPsmSet != CCSP_SUCCESS) {
	wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting ObssCoex\n",__FUNCTION__, retPsmSet); 
    }
        if (g_wifidb_rfc) {
            pcfg->obss_coex = pCfg->ObssCoex;
        }
    }

    if (pCfg->X_CISCO_COM_STBCEnable != pStoredCfg->X_CISCO_COM_STBCEnable) {
        memset(recName, '\0', sizeof(recName));
        memset(strValue, '\0', sizeof(strValue));
    snprintf(recName, sizeof(recName), STBCEnable, ulInstance);
    snprintf(strValue, sizeof(strValue),"%d",pCfg->X_CISCO_COM_STBCEnable);
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
    if (retPsmSet != CCSP_SUCCESS) {
	wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting STBC \n",__FUNCTION__, retPsmSet); 
    }
        if (g_wifidb_rfc) {
            pcfg->stbc_enable = pCfg->X_CISCO_COM_STBCEnable;
        }
    }

    if (pCfg->GuardInterval != pStoredCfg->GuardInterval) {
        memset(recName, '\0', sizeof(recName));
        memset(strValue, '\0', sizeof(strValue));
    snprintf(recName, sizeof(recName), GuardInterval, ulInstance);
    snprintf(strValue, sizeof(strValue),"%d",pCfg->GuardInterval);
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
    if (retPsmSet != CCSP_SUCCESS) {
	wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting Guard Interval \n",__FUNCTION__, retPsmSet); 
    }
        if (g_wifidb_rfc) {
            pcfg->guard_interval = pCfg->GuardInterval;
        }

    }

    if (pCfg->TransmitPower != pStoredCfg->TransmitPower ) {
        memset(recName, '\0', sizeof(recName));
        memset(strValue, '\0', sizeof(strValue));
        snprintf(recName, sizeof(recName), TransmitPower, ulInstance);
        snprintf(strValue, sizeof(strValue),"%d",pCfg->TransmitPower);
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
    if (retPsmSet != CCSP_SUCCESS) {
	wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting Transmit Power \n",__FUNCTION__, retPsmSet); 
    }
        if (g_wifidb_rfc) {
            pcfg->transmit_power = pCfg->TransmitPower;
        }
    }

    if (pCfg->MbssUserControl != pStoredCfg->MbssUserControl) {
        memset(recName, '\0', sizeof(recName));
        memset(strValue, '\0', sizeof(strValue));
        snprintf(recName, sizeof(recName), UserControl, ulInstance);
        snprintf(strValue, sizeof(strValue),"%d",pCfg->MbssUserControl);
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
    if (retPsmSet != CCSP_SUCCESS) {
	    wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting UserControl \n",__FUNCTION__, retPsmSet);
        }
        if (g_wifidb_rfc) {
            pcfg->user_control = pCfg->MbssUserControl;
        }
    }

    if (pCfg->AdminControl != pStoredCfg->AdminControl) {
        snprintf(recName, sizeof(recName), AdminControl, ulInstance);
    snprintf(strValue, sizeof(strValue), "%d",pCfg->AdminControl);
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
    if (retPsmSet != CCSP_SUCCESS) {
	    wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting AdminControl  \n",__FUNCTION__, retPsmSet); 
        }
        if (g_wifidb_rfc) {
            pcfg->admin_control = pCfg->AdminControl;
        }
    }

    if (pCfg->X_CISCO_COM_11nGreenfieldEnabled != pStoredCfg->X_CISCO_COM_11nGreenfieldEnabled) {
        memset(recName, '\0', sizeof(recName));
        memset(strValue, '\0', sizeof(strValue));
        snprintf(recName, sizeof(recName), GreenField, ulInstance);
        snprintf(strValue, sizeof(strValue),"%d",pCfg->X_CISCO_COM_11nGreenfieldEnabled);
        retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
        if (retPsmSet != CCSP_SUCCESS) {
            wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting GreenfieldEnabled  \n",__FUNCTION__, retPsmSet); 
    }
        if (g_wifidb_rfc) {
            pcfg->greenfield_enable = pCfg->X_CISCO_COM_11nGreenfieldEnabled;
        }
    }

    if (g_wifidb_rfc) {
        if (wifi_ovsdb_update_table_entry(radio_name,"radio_name",OCLM_STR,&table_Wifi_Radio_Config,pcfg,filter_radio) > 0) {
            CcspWifiTrace(("RDK_LOG_WARN,WIFI %s DB Updated successfully\n",__FUNCTION__));
        } else {
            CcspWifiTrace(("RDK_LOG_WARN,WIFI %s DB Update failed\n",__FUNCTION__));
        }
    }
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Returning Success \n",__FUNCTION__));
    return ANSC_STATUS_SUCCESS;
}

//>> zqiu
static INT CosaDmlWiFiGetRadioStandards(int radioIndex, COSA_DML_WIFI_FREQ_BAND OperatingFrequencyBand, ULONG *pOperatingStandards) {
	// Non-Vol cfg data
	char opStandards[32];
    ULONG OperatingStandards;
#if defined (_WIFI_CONSOLIDATED_STANDARDS_)
    UINT pureMode;
#else
    BOOL gOnly;
    BOOL nOnly;
    BOOL acOnly;
#endif
    
    if (!pOperatingStandards) return -1;
	
#if defined (_WIFI_CONSOLIDATED_STANDARDS_)
    // RDKB-25911: Consolidated Operating Standards as of now for new devices only!!
    wifi_getRadioMode(radioIndex, opStandards, &pureMode);

#if defined (_WIFI_AX_SUPPORT_)
    if (strstr(opStandards, "ax"))
    {        
    	if ( OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_2_4G )
    	{
    		OperatingStandards = COSA_DML_WIFI_STD_g | COSA_DML_WIFI_STD_n | COSA_DML_WIFI_STD_ax;
    	}
        else if ( OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_6G )
        {
            OperatingStandards = COSA_DML_WIFI_STD_ax;
        }
    	else
    	{			
    		OperatingStandards = COSA_DML_WIFI_STD_a | COSA_DML_WIFI_STD_n | COSA_DML_WIFI_STD_ac | COSA_DML_WIFI_STD_ax;
    	}
    }
    else
#endif
    {
    	if ( OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_2_4G )
    	{
    		OperatingStandards = COSA_DML_WIFI_STD_g | COSA_DML_WIFI_STD_n;
    	}
    	else
    	{			
                OperatingStandards = COSA_DML_WIFI_STD_a | COSA_DML_WIFI_STD_n | COSA_DML_WIFI_STD_ac;
    	}
    }

#else	
#ifdef _WIFI_AX_SUPPORT_ 
    UINT pureMode;
    
    gOnly=FALSE;
    nOnly=FALSE;
    acOnly=FALSE;
    
    wifi_getRadioMode(radioIndex, opStandards, &pureMode);
    if (pureMode == COSA_DML_WIFI_STD_g )
        gOnly = TRUE;
    else if (pureMode == COSA_DML_WIFI_STD_n)
        nOnly = TRUE;
    else if (pureMode == COSA_DML_WIFI_STD_ac)
        acOnly = TRUE;
#else
    wifi_getRadioStandard(radioIndex, opStandards, &gOnly, &nOnly, &acOnly);
#endif

    if (strcmp("a",opStandards)==0){ 
		OperatingStandards = COSA_DML_WIFI_STD_a;      /* Bitmask of COSA_DML_WIFI_STD */
    } else if (strcmp("b",opStandards)==0) { 
		OperatingStandards = COSA_DML_WIFI_STD_b;      /* Bitmask of COSA_DML_WIFI_STD */
    } else if (strcmp("g",opStandards)==0) { 
        if (gOnly == TRUE) {  
			OperatingStandards = COSA_DML_WIFI_STD_g;      /* Bitmask of COSA_DML_WIFI_STD */
        } else {
			OperatingStandards = COSA_DML_WIFI_STD_b | COSA_DML_WIFI_STD_g;      /* Bitmask of COSA_DML_WIFI_STD */
        }
    } else if (strncmp("n",opStandards,1)==0) { 
		if ( OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_2_4G ) {
            if (gOnly == TRUE) {
				OperatingStandards = COSA_DML_WIFI_STD_g | COSA_DML_WIFI_STD_n;      /* Bitmask of COSA_DML_WIFI_STD */
            } else if (nOnly == TRUE) {
				OperatingStandards = COSA_DML_WIFI_STD_n;      /* Bitmask of COSA_DML_WIFI_STD */
            } else {
				OperatingStandards = COSA_DML_WIFI_STD_b | COSA_DML_WIFI_STD_g | COSA_DML_WIFI_STD_n;      /* Bitmask of COSA_DML_WIFI_STD */
            }
        } else {
            if (nOnly == TRUE) {
				OperatingStandards = COSA_DML_WIFI_STD_n;      /* Bitmask of COSA_DML_WIFI_STD */
            } else {
				OperatingStandards = COSA_DML_WIFI_STD_a | COSA_DML_WIFI_STD_n;      /* Bitmask of COSA_DML_WIFI_STD */
            }
        }
    } else if (strcmp("ac",opStandards) == 0) {
        if (acOnly == TRUE) {
            OperatingStandards = COSA_DML_WIFI_STD_ac;
        } else  if (nOnly == TRUE) {
            OperatingStandards = COSA_DML_WIFI_STD_n | COSA_DML_WIFI_STD_ac;
        } else {
            OperatingStandards = COSA_DML_WIFI_STD_a | COSA_DML_WIFI_STD_n | COSA_DML_WIFI_STD_ac;
        }
    }
#ifdef _WIFI_AX_SUPPORT_
else if (strcmp("ax",opStandards) == 0) {
        if(pureMode == COSA_DML_WIFI_STD_ax)
            OperatingStandards = COSA_DML_WIFI_STD_ax;
        else{
            if ( OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_2_4G ){
                if (nOnly == TRUE) {
                    OperatingStandards = COSA_DML_WIFI_STD_n | COSA_DML_WIFI_STD_ax;
                } else {
                    OperatingStandards = COSA_DML_WIFI_STD_g | COSA_DML_WIFI_STD_n | COSA_DML_WIFI_STD_ax;
                }
            }
            else if ( OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_6G ){
                OperatingStandards = COSA_DML_WIFI_STD_ax;
            }
            else{
                if (acOnly == TRUE) 
                    OperatingStandards = COSA_DML_WIFI_STD_ac | COSA_DML_WIFI_STD_ax;      /* Bitmask of COSA_DML_WIFI_STD */
                else if( nOnly == TRUE)
                    OperatingStandards = COSA_DML_WIFI_STD_n | COSA_DML_WIFI_STD_ac | COSA_DML_WIFI_STD_ax;
                else 
                    OperatingStandards = COSA_DML_WIFI_STD_a | COSA_DML_WIFI_STD_n | COSA_DML_WIFI_STD_ac | COSA_DML_WIFI_STD_ax;
            }
        }
    }
#endif
#endif //(_WIFI_CONSOLIDATED_STANDARDS_)

    /*
       struct _COSA_DML_WIFI_RADIO_CFG is packed, so we can't rely on the elements
       within it being correctly aligned. Therefore memcpy into pOperatingStandards,
       which is always safe to do.
    */
    memcpy (pOperatingStandards, &OperatingStandards, sizeof(*pOperatingStandards));

	return 0;
}

#if 0
static INT CosaDmlWiFiGetApStandards(int apIndex, ULONG *pOperatingStandards) {
    return CosaDmlWiFiGetRadioStandards(apIndex%2, ((apIndex%2)==0)?COSA_DML_WIFI_FREQ_BAND_2_4G:COSA_DML_WIFI_FREQ_BAND_5G, pOperatingStandards);
}
#endif

INT CosaDmlWiFiSetApBeaconRateControl(int apIndex, ULONG  OperatingStandards) {
	char beaconRate[64]={0};
#ifdef _BEACONRATE_SUPPORT
	if(OperatingStandards == (COSA_DML_WIFI_STD_g | COSA_DML_WIFI_STD_n)) {
		wifi_getApBeaconRate(apIndex, beaconRate);
		if(strcmp(beaconRate, "1Mbps")==0)
			wifi_setApBeaconRate(apIndex, "6Mbps");	
	} else if (OperatingStandards == (COSA_DML_WIFI_STD_b | COSA_DML_WIFI_STD_g | COSA_DML_WIFI_STD_n)) {
		wifi_getApBeaconRate(apIndex, beaconRate);
		if(strcmp(beaconRate, "1Mbps")!=0)
			wifi_setApBeaconRate(apIndex, "1Mbps");	
	}
#if defined (_WIFI_AX_SUPPORT_)
	else if(OperatingStandards == (COSA_DML_WIFI_STD_g | COSA_DML_WIFI_STD_n | COSA_DML_WIFI_STD_ax)) {
		wifi_getApBeaconRate(apIndex, beaconRate);
		if(strcmp(beaconRate, "1Mbps")==0)
			wifi_setApBeaconRate(apIndex, "6Mbps");	
	}
#endif // (_WIFI_AX_SUPPORT_)
#endif
	return 0;
}
//<<

INT CosaWifiAdjustBeaconRate(int radioindex, char *beaconRate) {
	PCOSA_DATAMODEL_WIFI            pWiFi       = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
        PSINGLE_LINK_ENTRY              pAPLink     = NULL;
        PCOSA_DML_WIFI_AP               pWiFiAP     = NULL;
        ULONG                       wlanIndex;

	CcspWifiTrace(("RDK_LOG_WARN,WIFI Function= %s Start  \n",__FUNCTION__));
	int Instance=0;
        char recName[256];
	char StoredBeaconRate[32]={0};
        int retPsmSet = CCSP_SUCCESS;

    if (!beaconRate) return -1;
	if(radioindex==1) {
#ifdef _BEACONRATE_SUPPORT
		int rc = -1;
		for(Instance = 0;Instance <= 14;Instance+=2) {
                        snprintf(recName, sizeof(recName), BeaconRateCtl, Instance+1);
                        retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, beaconRate);
                        if (retPsmSet != CCSP_SUCCESS) {
                                wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting BeaconRate \n",__FUNCTION__, retPsmSet);
                        }
                        if (g_wifidb_rfc) {
                            struct schema_Wifi_VAP_Config  *pcfg= NULL;
                            pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[Instance],"vap_name",&table_Wifi_VAP_Config,OCLM_STR);
                            if (pcfg != NULL) {
                                rc = strcpy_s(pcfg->beacon_rate_ctl,sizeof(pcfg->beacon_rate_ctl),beaconRate);
				if (rc != 0) {
                                    ERR_CHK(rc);
                                    return -1;
                                }
                                if (wifi_ovsdb_update_table_entry(vap_names[Instance],"vap_name",OCLM_STR,&table_Wifi_VAP_Config,pcfg,filter_vaps) <= 0) {
                                    CcspTraceError(("%s Error in updating beaconrate vap %d\n",__func__,Instance));
                                }
                            }
                        }
			if( wifi_setApBeaconRate(Instance, beaconRate)  < 0 ) {
				wifiDbgPrintf("%s Unable to set the Beacon Rate for Index %s\n",__FUNCTION__, beaconRate);
			}
			if((pAPLink = AnscQueueGetEntryByIndex(&pWiFi->AccessPointQueue, Instance))==NULL) {
                                CcspTraceError(("%s Data Model object not found! and the instance is \n",__FUNCTION__));
                                return -1;
                        }
			if((pWiFiAP=ACCESS_COSA_CONTEXT_LINK_OBJECT(pAPLink)->hContext)==NULL) {
                                CcspTraceError(("%s Error linking Data Model object!\n",__FUNCTION__));
                                return -1;
                        }
			memcpy(StoredBeaconRate,&sWiFiDmlApStoredCfg[pWiFiAP->AP.Cfg.InstanceNumber - 1].Cfg.BeaconRate,sizeof(StoredBeaconRate));
			wlanIndex = pWiFiAP->AP.Cfg.InstanceNumber - 1;
			if(wifi_getApBeaconRate(wlanIndex , pWiFiAP->AP.Cfg.BeaconRate) < 0) {
				wifiDbgPrintf("%s Unable to get the BeaconRate for Index is %d\n",__FUNCTION__, Instance);
			}
			memcpy(&sWiFiDmlApStoredCfg[pWiFiAP->AP.Cfg.InstanceNumber - 1].Cfg.BeaconRate, pWiFiAP->AP.Cfg.BeaconRate, sizeof(pWiFiAP->AP.Cfg.BeaconRate));
			CcspWifiTrace(("RDK_LOG_WARN,BEACON RATE CHANGED vAP%d %s to %s by TR-181 Object Device.WiFi.Radio.%d.OperatingStandards\n",Instance,StoredBeaconRate,pWiFiAP->AP.Cfg.BeaconRate,radioindex));	
                }
		CcspWifiTrace(("RDK_LOG_WARN,WIFI Beacon Rate %s changed for 2.4G, Function= %s  \n",beaconRate,__FUNCTION__));
#endif
	} else {
#ifdef _BEACONRATE_SUPPORT
		int safe_rc = -1;
                for(Instance =1 ;Instance <= 15;Instance+=2) {
                        snprintf(recName, sizeof(recName), BeaconRateCtl, Instance+1);
                        retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, beaconRate);
                        if (retPsmSet != CCSP_SUCCESS) {
                                wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting BeaconRate \n",__FUNCTION__, retPsmSet);
                        }
                        if (g_wifidb_rfc) {
                            struct schema_Wifi_VAP_Config  *pcfg= NULL;
                            pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[Instance],"vap_name",&table_Wifi_VAP_Config,OCLM_STR);
                            if (pcfg != NULL) {
                                safe_rc = strcpy_s(pcfg->beacon_rate_ctl,sizeof(pcfg->beacon_rate_ctl),beaconRate);
				if (safe_rc != 0) {
                                    ERR_CHK(safe_rc);
                                    return -1;
                                }
                                if (wifi_ovsdb_update_table_entry(vap_names[Instance],"vap_name",OCLM_STR,&table_Wifi_VAP_Config,pcfg,filter_vaps) <= 0) {
                                    CcspTraceError(("%s Error in updating beaconrate vap %d\n",__func__,Instance));
                                }
                            }
                        }
			if(wifi_setApBeaconRate(Instance, beaconRate) < 0 ) {
				wifiDbgPrintf("%s Unable to set the BeaconRate for Index is %d\n",__FUNCTION__, Instance);
			}
			if((pAPLink = AnscQueueGetEntryByIndex(&pWiFi->AccessPointQueue, Instance))==NULL) {
                                CcspTraceError(("%s Data Model object not found! and the instance is \n",__FUNCTION__));
                                return -1;
                        }
                        if((pWiFiAP=ACCESS_COSA_CONTEXT_LINK_OBJECT(pAPLink)->hContext)==NULL) {
                                CcspTraceError(("%s Error linking Data Model object!\n",__FUNCTION__));
                                return -1;
                        }
                        memcpy(StoredBeaconRate,&sWiFiDmlApStoredCfg[pWiFiAP->AP.Cfg.InstanceNumber - 1].Cfg.BeaconRate,sizeof(StoredBeaconRate));
                        wlanIndex = pWiFiAP->AP.Cfg.InstanceNumber - 1;
                        if(wifi_getApBeaconRate(wlanIndex , pWiFiAP->AP.Cfg.BeaconRate) , 0 ) {
				wifiDbgPrintf("%s Unable to get the BeaconRate for Index is %d\n",__FUNCTION__, Instance);
			}
                        memcpy(&sWiFiDmlApStoredCfg[pWiFiAP->AP.Cfg.InstanceNumber - 1].Cfg.BeaconRate, pWiFiAP->AP.Cfg.BeaconRate, sizeof(pWiFiAP->AP.Cfg.BeaconRate));
                        CcspWifiTrace(("RDK_LOG_WARN,BEACON RATE CHANGED vAP%d %s to %s by TR-181 Object Device.WiFi.Radio.%d.OperatingStandards\n",Instance,StoredBeaconRate,pWiFiAP->AP.Cfg.BeaconRate,radioindex));

                }
		CcspWifiTrace(("RDK_LOG_WARN,WIFI Beacon Rate %s changed for 5G, Function= %s  \n",beaconRate,__FUNCTION__));
#endif
	}
	CcspWifiTrace(("RDK_LOG_WARN,WIFI Function= %s End  \n",__FUNCTION__));
	return 0;
}

INT CosaDmlWiFiGetApBeaconRate(int apIndex, char *BeaconRate) {

        if (!BeaconRate) return -1;
        if ((apIndex >= 0) &&  (apIndex <= 15) )
        {
#ifdef _BEACONRATE_SUPPORT
                wifi_getApBeaconRate(apIndex, BeaconRate);
                CcspWifiTrace(("RDK_LOG_WARN,WIFI APIndex %d , BeaconRate %s \n",apIndex,BeaconRate));
#endif
        }

        return 0;
}


INT
CosaDmlWiFiGetRadioBasicDataTransmitRates
(
int radioIndex,
char *TransmitRates
)
{
	if (!TransmitRates) return -1;
	if((radioIndex >=0) && (radioIndex <=1))
	{
		if(wifi_getRadioBasicDataTransmitRates(radioIndex,TransmitRates) == 0)
		{
		CcspWifiTrace(("RDK_LOG_WARN,WIFI radioIndex %d , TransmitRates %s \n",radioIndex,TransmitRates));
		}
		else
                {
                        CcspWifiTrace(("RDK_LOG_ERROR,wifi_getRadioBasicDataTransmitRates returning Error"));
                        return -1;
                }

	}
	else
        {
                CcspWifiTrace(("RDK_LOG_ERROR,radioIndex %d is out of Range",radioIndex));
                return -1;
        }

	return 0;
}


INT
CosaDmlWiFiGetRadioOperationalDataTransmitRates	
(
int radioIndex,
char *TransmitRates
)
{
	if (!TransmitRates) return -1;

	if((radioIndex >=0) && (radioIndex <=1))
    {
		if(wifi_getRadioOperationalDataTransmitRates(radioIndex,TransmitRates) == 0)
		{
			CcspWifiTrace(("RDK_LOG_WARN,WIFI radioIndex %d , TransmitRates %s \n",radioIndex,TransmitRates));
		}
		else
		{
			CcspWifiTrace(("RDK_LOG_ERROR,wifi_getRadioOperationalDataTransmitRates returning Error"));
			return -1;
		}
	}
	else
	{
		CcspWifiTrace(("RDK_LOG_ERROR,radioIndex %d is out of Range",radioIndex));
		return -1;
	}
	return 0;
}


INT
CosaDmlWiFiGetRadioSupportedDataTransmitRates
(
int radioIndex,
char *TransmitRates
)
{
	if (!TransmitRates) return -1;

	if((radioIndex >=0) && (radioIndex <=1))
	{
		if(wifi_getRadioSupportedDataTransmitRates(radioIndex,TransmitRates) == 0)
		{
			CcspWifiTrace(("RDK_LOG_WARN,WIFI radioIndex %d , TransmitRates %s \n",radioIndex,TransmitRates));
		}
		else
                {
                        CcspWifiTrace(("RDK_LOG_ERROR,wifi_getRadioSupportedDataTransmitRates returning Error"));
                        return -1;
                }

	}
	else
        {
                CcspWifiTrace(("RDK_LOG_ERROR,radioIndex %d is out of Range",radioIndex));
                return -1;
        }

	return 0;
}


ANSC_STATUS
CosaDmlWiFiGetAccessPointPsmData
    (
    PCOSA_DML_WIFI_AP_CFG       pCfg
)
{
    char *strValue = NULL;
    int intValue;
    char recName[256];
    int retPsmGet = CCSP_SUCCESS;
    BOOL enabled; 
    ULONG                       wlanIndex;
    ULONG                       ulInstance;
    struct schema_Wifi_VAP_Config  *pcfg= NULL;

    if (pCfg != NULL) {
        ulInstance = pCfg->InstanceNumber;
        wlanIndex = pCfg->InstanceNumber - 1;
    } else {
        return ANSC_STATUS_FAILURE;
    }

    wifi_getApEnable(wlanIndex, &enabled);

printf("%s g_Subsytem = %s wlanIndex %lu ulInstance %lu enabled = %s\n",__FUNCTION__, g_Subsystem, wlanIndex, ulInstance, 
       (enabled == TRUE) ? "TRUE" : "FALSE");
		CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wlanInex = %lu \n",__FUNCTION__, wlanIndex));
		
		CcspWifiTrace(("RDK_LOG_WARN,WIFI %s Get Factory Reset AccessPoint PsmData & Apply to WIFI \n",__FUNCTION__));

    if (g_wifidb_rfc) {
        pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[wlanIndex],"vap_name",&table_Wifi_VAP_Config,OCLM_STR);
        if (pcfg == NULL) {
            CcspWifiTrace(("RDK_LOG_WARN,%s WIFI DB Failed to get access point entry for wlan %d",__FUNCTION__,(int)wlanIndex));
            return ANSC_STATUS_FAILURE;
        }
    }

    if (!g_wifidb_rfc) {
    // SSID does not need to be enabled to push this param to the configuration
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), BssHotSpot, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    } else {
        strValue = pcfg->bss_hotspot?strdup("1"):strdup("0");
    }
    if (retPsmGet == CCSP_SUCCESS) {
        // if this is a HotSpot SSID, then set is EnableOnline=TRUE and
        //  it should only be brought up once the RouterEnabled=TRUE
        wifiDbgPrintf("%s: found BssHotSpot value = %s \n", __func__, strValue);
        BOOL enable = _ansc_atoi(strValue);
        pCfg->BssHotSpot  = (enable == TRUE) ? TRUE : FALSE;
#if !defined(_COSA_BCM_MIPS_)&& !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        wifi_setApEnableOnLine(wlanIndex,enable);
#endif
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    } else {
        wifiDbgPrintf("%s: didn't find BssHotSpot setting EnableOnline to FALSE \n", __func__);
#if !defined(_COSA_BCM_MIPS_)&& !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        wifi_setApEnableOnLine(wlanIndex,0);
#endif
    }

    if (!g_wifidb_rfc) {
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), BssMaxNumSta, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        intValue = _ansc_atoi(strValue);
        pCfg->BssMaxNumSta = intValue;
        if (enabled == TRUE) {
            wifi_setApMaxAssociatedDevices(wlanIndex, intValue);
        }
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    } else {
        pCfg->BssMaxNumSta = pcfg->bss_max_sta;
        if (enabled == TRUE) {
            wifi_setApMaxAssociatedDevices(wlanIndex, pCfg->BssMaxNumSta);
        }
    }

    if (!g_wifidb_rfc) {
    memset(recName, 0, sizeof(recName));
    snprintf(recName,  sizeof(recName), ApIsolationEnable, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        BOOL enable = atoi(strValue);
        pCfg->IsolationEnable = enable;
        if (enabled == TRUE) {
            wifi_setApIsolationEnable(wlanIndex, enable);
        }
        printf("%s: wifi_setApIsolationEnable %lu, %d \n", __FUNCTION__, wlanIndex, enable);
	    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    } else {
        pCfg->IsolationEnable = pcfg->isolation_enabled;
        if (enabled == TRUE) {
            wifi_setApIsolationEnable(wlanIndex, pCfg->IsolationEnable);
        }
    }

    if ((wlanIndex == 0) || (wlanIndex == 1) || (wlanIndex == 12) || (wlanIndex == 13)) {
        if (!g_wifidb_rfc) {
        memset(recName, 0, sizeof(recName));
        snprintf(recName, sizeof(recName), BSSTransitionActivated, ulInstance);
        retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
        } else {
            strValue = pcfg->bss_transition_activated?strdup("true"):strdup("false");
        }
        if (retPsmGet == CCSP_SUCCESS) {
            if (((strcmp (strValue, "true") == 0)) || (strcmp (strValue, "TRUE") == 0))
            {
                 pCfg->BSSTransitionActivated = true;
                 if (pCfg->BSSTransitionImplemented == TRUE && pCfg->WirelessManagementImplemented == TRUE) {
                      CcspTraceWarning(("%s: wifi_setBSSTransitionActivation wlanIndex:%lu BSSTransitionActivated:%d \n", __FUNCTION__, wlanIndex, pCfg->BSSTransitionActivated));
#if !defined(_HUB4_PRODUCT_REQ_) || defined(HUB4_WLDM_SUPPORT)
                      wifi_setBSSTransitionActivation(wlanIndex, true);
#endif/*!defined(_HUB4_PRODUCT_REQ_) || defined(HUB4_WLDM_SUPPORT)*/
                 }
            }
            else 
            {
                pCfg->BSSTransitionActivated = false;
                CcspTraceWarning(("%s: wifi_setBSSTransitionActivation wlanIndex:%lu BSSTransitionActivated:%d \n", __FUNCTION__, wlanIndex, pCfg->BSSTransitionActivated));
#if !defined(_HUB4_PRODUCT_REQ_) || defined(HUB4_WLDM_SUPPORT)
                wifi_setBSSTransitionActivation(wlanIndex, false);
#endif/*!defined(_HUB4_PRODUCT_REQ_) || defined(HUB4_WLDM_SUPPORT)*/
            }

            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        }
        else
        {
           CcspTraceWarning(("%s: PSM_Get_Record_Value2 Faliled for BSSTransitionActivated on wlanIndex:%lu\n", __FUNCTION__, wlanIndex));
           t2_event_d("WIFI_ERROR_PSM_GetRecordFail",1);
        }
    } 
    
//>> zqiu
  //RDKB-7475
	if((wlanIndex%2)==0) { //if it is 2.4G
//RDKB-18000 - Get the Beacon value from PSM database after reboot
         if (!g_wifidb_rfc) {
		memset(recName, 0, sizeof(recName));
		snprintf(recName,  sizeof(recName), BeaconRateCtl, ulInstance);
		retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
          } else {
              strValue = strdup(pcfg->beacon_rate_ctl);
          }
		if (retPsmGet == CCSP_SUCCESS) {
			char *rate = strValue;
			printf("%s: %s %s \n", __FUNCTION__, recName, rate);
			if(wifi_setApBeaconRate(wlanIndex,rate)<0) {
			    CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : Wifi_setApBeaconRate is failed\n",__FUNCTION__));
			}
			((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
		} 
        }

		/*else {
			PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "0");
			ULONG OperatingStandards;
			CosaDmlWiFiGetApStandards(wlanIndex, &OperatingStandards);
			CosaDmlWiFiSetApBeaconRateControl(wlanIndex, OperatingStandards);			
		}
	}
*/
//<<
#if !defined(_HUB4_PRODUCT_REQ_) || defined(HUB4_WLDM_SUPPORT)
#if defined(ENABLE_FEATURE_MESHWIFI) || defined(_CBR_PRODUCT_REQ_) || defined(_COSA_BCM_MIPS_)
    if ((wlanIndex == 0) || (wlanIndex == 1) || (wlanIndex == 12) || (wlanIndex == 13)) {
      if (!g_wifidb_rfc) {
        memset(recName, 0, sizeof(recName));
        snprintf(recName,  sizeof(recName), NeighborReportActivated, ulInstance);
        retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
      } else {
        strValue = pcfg->nbr_report_activated?strdup("true"):strdup("false");
      }
        BOOL bNeighborReportActivated = FALSE;
        if (retPsmGet == CCSP_SUCCESS) {
            if(((strncmp (strValue, "true", strlen("true")) == 0)) || (strncmp (strValue, "TRUE", strlen("TRUE")) == 0))
            {
              bNeighborReportActivated = TRUE;
            }
            pCfg->X_RDKCENTRAL_COM_NeighborReportActivated = bNeighborReportActivated;
            sWiFiDmlApStoredCfg[wlanIndex].Cfg.X_RDKCENTRAL_COM_NeighborReportActivated = bNeighborReportActivated;
            if (enabled == TRUE) {
              if ((wlanIndex == 0) || (wlanIndex == 1)) {
               wifi_setNeighborReportActivation(wlanIndex, bNeighborReportActivated);
              }
            }
            printf("%s: wifi_setNeighborReportActivation %lu, %d \n", __FUNCTION__, wlanIndex, bNeighborReportActivated);
	    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        }
    }
#endif
#endif/* !defined(_HUB4_PRODUCT_REQ_) || defined(HUB4_WLDM_SUPPORT)*/
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Returning Success \n",__FUNCTION__));
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiSetAccessPointPsmData
(
PCOSA_DML_WIFI_AP_CFG       pCfg
) {
    char strValue[256];
    char recName[256];
    int retPsmSet;
    ULONG                       ulInstance;
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    ULONG                       uIndex = 0;
#endif
    PCOSA_DML_WIFI_AP_CFG       pStoredCfg = (PCOSA_DML_WIFI_AP_CFG)NULL;
    struct schema_Wifi_VAP_Config  *pcfg= NULL;

    CcspWifiTrace(("RDK_LOG_WARN,WIFI %s \n",__FUNCTION__));
    if (pCfg != NULL) {
        ulInstance = pCfg->InstanceNumber;
    } else {
        CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pCfg is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
    if (g_wifidb_rfc) {
        pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[ulInstance-1],"vap_name",&table_Wifi_VAP_Config,OCLM_STR);
        if (pcfg == NULL) {
            CcspWifiTrace(("RDK_LOG_ERROR, %s Could not get WIFI DB VAP index %d\n",__func__,(int)ulInstance));
            return ANSC_STATUS_FAILURE;
        }
    }
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    pStoredCfg  = &sWiFiDmlApStoredCfg[pCfg->InstanceNumber-1].Cfg;  /*RDKB-6907,  CID-33117, null check before use*/
#else
    GET_AP_INDEX(sWiFiDmlApStoredCfg, pCfg->InstanceNumber, uIndex);
    pStoredCfg  = &sWiFiDmlApStoredCfg[uIndex].Cfg;  /*RDKB-6907,  CID-33117, null check before use*/
#endif

    wifiDbgPrintf("%s g_Subsytem = %s\n",__FUNCTION__, g_Subsystem);

    if (pCfg->WMMEnable != pStoredCfg->WMMEnable) {
        snprintf(recName, sizeof(recName), WmmEnable, ulInstance);
        snprintf(strValue, sizeof(strValue), "%d",pCfg->WMMEnable);
        retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
        if (retPsmSet != CCSP_SUCCESS) {
            wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting WmmEnable\n",__FUNCTION__, retPsmSet); 
        }
        if (g_wifidb_rfc) {
            pcfg->wmm_enabled = pCfg->WMMEnable;
        }

    }

    if (pCfg->UAPSDEnable != pStoredCfg->UAPSDEnable) {
        snprintf(recName, sizeof(recName), UAPSDEnable, ulInstance);
        snprintf(strValue, sizeof(strValue), "%d",pCfg->UAPSDEnable);
        retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
        if (retPsmSet != CCSP_SUCCESS) {
            wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting UAPSDEnable\n",__FUNCTION__, retPsmSet); 
        }
        if (g_wifidb_rfc) {
            pcfg->uapsd_enabled = pCfg->UAPSDEnable;
        }
    }

    // For Backwards compatibility with 1.3 versions, the PSM value for NoAck must be negated
    // When set/get from the PSM to DML the value must be interperted to the opposite
    // 1->0 and 0->1
    if (pCfg->WmmNoAck != pStoredCfg->WmmNoAck) {
        snprintf(recName, sizeof(recName), WmmNoAck, ulInstance);
        snprintf(strValue, sizeof(strValue), "%d",!pCfg->WmmNoAck);
        retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
        if (retPsmSet != CCSP_SUCCESS) {
            wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting WmmNoAck\n",__FUNCTION__, retPsmSet); 
        }
        if (g_wifidb_rfc) {
            pcfg->wmm_noack = pCfg->WmmNoAck;
        }
    }

    if (pCfg->BssMaxNumSta != pStoredCfg->BssMaxNumSta) {
        snprintf(recName, sizeof(recName), BssMaxNumSta, ulInstance);
        snprintf(strValue, sizeof(strValue), "%d",pCfg->BssMaxNumSta);
        retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
        if (retPsmSet != CCSP_SUCCESS) {
            wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting BssMaxNumSta\n",__FUNCTION__, retPsmSet); 
        }
        if (g_wifidb_rfc) {
            pcfg->bss_max_sta = pCfg->BssMaxNumSta;
        }

    }

    if (pCfg->IsolationEnable != pStoredCfg->IsolationEnable) {
        snprintf(recName,  sizeof(recName), ApIsolationEnable, ulInstance);
        snprintf(strValue, sizeof(strValue), "%d",(pCfg->IsolationEnable == TRUE) ? 1 : 0 );
        retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
        if (retPsmSet != CCSP_SUCCESS) {
            wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting ApIsolationEnable \n",__FUNCTION__, retPsmSet); 
        }
        if (g_wifidb_rfc) {
            pcfg->isolation_enabled = pCfg->IsolationEnable;
        }
    }

    if (pCfg->BssHotSpot != pStoredCfg->BssHotSpot) {
        snprintf(recName, sizeof(recName), BssHotSpot, ulInstance);
        snprintf(strValue, sizeof(strValue), "%d",(pCfg->BssHotSpot == TRUE) ? 1 : 0 );
        retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
        if (retPsmSet != CCSP_SUCCESS) {
            wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting BssHotSpot \n",__FUNCTION__, retPsmSet); 
        }
        if (g_wifidb_rfc) {
            pcfg->bss_hotspot = pCfg->BssHotSpot;
        }
    }
//RDKB-18000 Set the Beaconrate in PSM database
    if (strcmp(pCfg->BeaconRate, pStoredCfg->BeaconRate) != 0) {
	snprintf(recName, sizeof(recName), BeaconRateCtl, ulInstance);
	snprintf(strValue, sizeof(strValue), "%s",pCfg->BeaconRate);
	retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
	if (retPsmSet != CCSP_SUCCESS) {
	    wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting BeaconRate \n",__FUNCTION__, retPsmSet);
	}
        if (g_wifidb_rfc) {
            int rc = -1;
            rc = strcpy_s(pcfg->beacon_rate_ctl, sizeof(pcfg->beacon_rate_ctl), pCfg->BeaconRate);
	    if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
        }
    }
    if (g_wifidb_rfc) {
        if (wifi_ovsdb_update_table_entry(vap_names[ulInstance-1],"vap_name",OCLM_STR,&table_Wifi_VAP_Config,pcfg,filter_vaps) <= 0) {
            CcspWifiTrace(("RDK_LOG_WARN,%s : Failed to update WIFIDB",__FUNCTION__));
        }
    }
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Returning Success \n",__FUNCTION__));
        return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS CosaDmlWiFiGetBridge0PsmData(char *ip, char *sub) {

    (void) (ip) ;
    (void) (sub) ;
    #if 0
    char *strValue = NULL;
    char ipAddr[16]={0};
    char ipSubNet[16]={0};
	int retPsmGet = CCSP_SUCCESS;
	
	//zqiu>>
	if(ip) {
                /*CID: 135508 :BUFFER_SIZE_WARNING*/
		strncpy(ipAddr,ip, sizeof(ipAddr)-1);
	} else  {
		retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, "dmsb.atom.l3net.4.V4Addr", NULL, &strValue);
		if (retPsmGet == CCSP_SUCCESS) {
			strncpy(ipAddr,strValue, sizeof(ipAddr)-1); 
			((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
		} 
	}
        ipAddr[sizeof(ipAddr)-1] = '\0';
	if(sub) {
                /*CID: 135508 :BUFFER_SIZE_WARNING*/
		strncpy(ipSubNet,sub, sizeof(ipAddr)-1); 
	} else {
		retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, "dmsb.atom.l3net.4.V4SubnetMask", NULL, &strValue);
		if (retPsmGet == CCSP_SUCCESS) {
                        /*CID: 135508 :BUFFER_SIZE_WARNING*/
			strncpy(ipSubNet,strValue, sizeof(ipAddr)-1); 
			((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
		} 
	}
        ipSubNet[sizeof(ipAddr)-1] = '\0';
#ifdef DUAL_CORE_XB3	
	if(ipAddr[0]!=0 && ipSubNet[0]!=0) {
            v_secure_system("/usr/ccsp/wifi/br0_ip.sh %s %s", ipAddr, ipSubNet);   
	}
#endif	
#endif

#ifdef DUAL_CORE_XB3    
            v_secure_system("/usr/ccsp/wifi/br0_ip.sh");   
#endif  
	return ANSC_STATUS_SUCCESS;
}
	
ANSC_STATUS
CosaDmlWiFi_GetGoodRssiThresholdValue( int	*piRssiThresholdValue )
{
	char *strValue	= NULL;
	int retPsmGet = CCSP_SUCCESS;
	
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Calling PSM Get\n",__FUNCTION__ ));

	*piRssiThresholdValue = 0;

        if (!g_wifidb_rfc) {
	retPsmGet = PSM_Get_Record_Value2( bus_handle, g_Subsystem, GoodRssiThreshold, NULL, &strValue );
	if (retPsmGet == CCSP_SUCCESS) 
	{
		*piRssiThresholdValue = _ansc_atoi( strValue );
		((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
	}
	else
	{
		CcspTraceInfo(("%s Failed to get PSM\n", __FUNCTION__ ));
		return ANSC_STATUS_FAILURE; 	
	}
        } else {
            struct schema_Wifi_Global_Config *pcfg = NULL;
            pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
            if (pcfg != NULL) {
                *piRssiThresholdValue = pcfg->good_rssi_threshold;
                free(pcfg);
            } else {
                CcspTraceInfo(("%s WIFI DB Failed to get global config\n", __FUNCTION__));
                return ANSC_STATUS_FAILURE;
            }
        }
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetGoodRssiThresholdValue( int	iRssiThresholdValue )
{
	char  RSSIThreshold[ 8 ] = { 0 };
	int   retPsmSet 		  = CCSP_SUCCESS;
	
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Calling PSM Set \n",__FUNCTION__ ));

	snprintf( RSSIThreshold, sizeof(RSSIThreshold), "%d", iRssiThresholdValue );
	retPsmSet = PSM_Set_Record_Value2( bus_handle, g_Subsystem, GoodRssiThreshold, ccsp_string, RSSIThreshold );
	if (retPsmSet == CCSP_SUCCESS ) 
	{
		CcspTraceInfo(("%s PSM set success Value: %d\n", __FUNCTION__, iRssiThresholdValue));
	}
	else
	{
		CcspTraceInfo(("%s Failed to set PSM Value: %d\n", __FUNCTION__, iRssiThresholdValue));
		return ANSC_STATUS_FAILURE;
	}

        if (g_wifidb_rfc) {
            struct schema_Wifi_Global_Config *pcfg = NULL;
            pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
            if (pcfg != NULL) {
                pcfg->good_rssi_threshold = iRssiThresholdValue;
                if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                    CcspTraceInfo(("%s Failed to update WIFI DB global config\n",__FUNCTION__));
                    free(pcfg);
                }
            } else {
                CcspTraceInfo(("%s WIFI DB Failed to get global config\n", __FUNCTION__ ));
                return ANSC_STATUS_FAILURE;
            }
        }
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_GetAssocCountThresholdValue( int	*piAssocCountThresholdValue )
{
	char *strValue	= NULL;
	int  retPsmGet       = CCSP_SUCCESS;
	
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Calling PSM Get\n",__FUNCTION__ ));

	*piAssocCountThresholdValue = 0;
        assocCountThreshold = 0;
        deauthCountThreshold = 0;

        if (!g_wifidb_rfc) {
	retPsmGet = PSM_Get_Record_Value2( bus_handle, g_Subsystem, AssocCountThreshold, NULL, &strValue );
	if (retPsmGet == CCSP_SUCCESS) 
	{
		*piAssocCountThresholdValue = _ansc_atoi( strValue );
                assocCountThreshold = _ansc_atoi( strValue );
                deauthCountThreshold = _ansc_atoi( strValue );
		((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
	}
	else
	{
		CcspTraceInfo(("%s Failed to get PSM\n", __FUNCTION__ ));
		return ANSC_STATUS_FAILURE; 	
	}
        } else {
            struct schema_Wifi_Global_Config *pcfg = NULL;
            pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
            if (pcfg != NULL) {
                *piAssocCountThresholdValue= pcfg->assoc_count_threshold;
                assocCountThreshold = pcfg->assoc_count_threshold;
                deauthCountThreshold = pcfg->assoc_count_threshold;
                free(pcfg);
            } else {
                CcspTraceInfo(("%s WIFI DB Failed to get global config\n", __FUNCTION__ ));
                return ANSC_STATUS_FAILURE;
            }
        }
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetAssocCountThresholdValue( int	iAssocCountThresholdValue )
{
	char associationCountThreshold[ 8 ]               = { 0 };
	int   retPsmSet 		  = CCSP_SUCCESS;
	
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Calling PSM Set \n",__FUNCTION__ ));

	sprintf( associationCountThreshold, "%d", iAssocCountThresholdValue );
	retPsmSet = PSM_Set_Record_Value2( bus_handle, g_Subsystem, AssocCountThreshold, ccsp_string, associationCountThreshold );
	if (retPsmSet == CCSP_SUCCESS ) 
	{
		CcspTraceInfo(("%s PSM set success Value: %d\n", __FUNCTION__, iAssocCountThresholdValue));
                assocCountThreshold = iAssocCountThresholdValue;
                deauthCountThreshold = iAssocCountThresholdValue;
	}
	else
	{
		CcspTraceInfo(("%s Failed to set PSM Value: %d\n", __FUNCTION__, iAssocCountThresholdValue));
		return ANSC_STATUS_FAILURE;
	}
        
    if (g_wifidb_rfc) {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            pcfg->assoc_count_threshold = iAssocCountThresholdValue;
            if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                free(pcfg);
            }
            assocCountThreshold = iAssocCountThresholdValue;
            deauthCountThreshold = iAssocCountThresholdValue;
        } else {
            CcspTraceError(("%s: WIFI DB Failed to get global config\n", __FUNCTION__ ));
            return ANSC_STATUS_FAILURE;
        }
    }
   return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_GetAssocMonitorDurationValue( int	*piAssocMonitorDurationValue )
{
    char *strValue	= NULL;
    int  retPsmGet       = CCSP_SUCCESS;
	
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Calling PSM Get\n",__FUNCTION__ ));

	*piAssocMonitorDurationValue = 0;
        assocMonitorDuration = 0;
        deauthMonitorDuration = 0;

        if (!g_wifidb_rfc) {
	retPsmGet = PSM_Get_Record_Value2( bus_handle, g_Subsystem, AssocMonitorDuration, NULL, &strValue );
	if (retPsmGet == CCSP_SUCCESS) 
	{
		*piAssocMonitorDurationValue = _ansc_atoi( strValue );
                assocMonitorDuration = _ansc_atoi( strValue );
                deauthMonitorDuration = _ansc_atoi( strValue );
		CcspTraceInfo(("%s PSM get success Value: %d\n", __FUNCTION__, *piAssocMonitorDurationValue));
		((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
	}
	else
	{
		CcspTraceInfo(("%s Failed to get PSM\n", __FUNCTION__ ));
		return ANSC_STATUS_FAILURE; 	
	}
        } else {
            struct schema_Wifi_Global_Config *pcfg = NULL;
            pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
            if (pcfg != NULL) {
                *piAssocMonitorDurationValue = pcfg->assoc_monitor_duration;
                assocMonitorDuration = pcfg->assoc_monitor_duration;
                deauthMonitorDuration = pcfg->assoc_monitor_duration;
                CcspTraceInfo(("%s WIFI DB get success Value: %d\n", __FUNCTION__, *piAssocMonitorDurationValue))
                free(pcfg);
            } else {
                CcspTraceInfo(("%s WIFI DB Failed to get global config\n", __FUNCTION__ ));
                return ANSC_STATUS_FAILURE;
            }
        }
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetAssocMonitorDurationValue( int	iAssocMonitorDurationValue )
{
	char associationMonitorDuration[ 8 ]               = { 0 };
	int   retPsmSet 		  = CCSP_SUCCESS;
	
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Calling PSM Set \n",__FUNCTION__ ));

	sprintf( associationMonitorDuration, "%d", iAssocMonitorDurationValue );
	retPsmSet = PSM_Set_Record_Value2( bus_handle, g_Subsystem, AssocMonitorDuration, ccsp_string, associationMonitorDuration );
	if (retPsmSet == CCSP_SUCCESS ) 
	{
		CcspTraceInfo(("%s PSM set success Value: %d\n", __FUNCTION__, iAssocMonitorDurationValue));
                assocMonitorDuration = iAssocMonitorDurationValue;
                deauthMonitorDuration = iAssocMonitorDurationValue;
	}
	else
	{
		CcspTraceInfo(("%s Failed to set PSM Value: %d\n", __FUNCTION__, iAssocMonitorDurationValue));
		return ANSC_STATUS_FAILURE;
	}

    if (g_wifidb_rfc) {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            pcfg->assoc_monitor_duration = iAssocMonitorDurationValue;
            if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                free(pcfg);
            }
            assocMonitorDuration = iAssocMonitorDurationValue;
            deauthMonitorDuration = iAssocMonitorDurationValue;
        } else {
            CcspTraceError(("%s: WIFI DB Failed to get global config\n", __FUNCTION__ ));
            return ANSC_STATUS_FAILURE;
        }
    }
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_GetAssocGateTimeValue( int	*piAssocGateTimeValue )
{
	char *strValue	= NULL;
	int retPsmGet       = CCSP_SUCCESS;
	
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Calling PSM Get\n",__FUNCTION__ ));

	*piAssocGateTimeValue = 0;
        assocGateTime = 0;
        deauthGateTime = 0;

        if (!g_wifidb_rfc) {
	retPsmGet = PSM_Get_Record_Value2( bus_handle, g_Subsystem, AssocGateTime, NULL, &strValue );
	if (retPsmGet == CCSP_SUCCESS) 
	{
		*piAssocGateTimeValue = _ansc_atoi( strValue );
                assocGateTime = _ansc_atoi( strValue );
                deauthGateTime = _ansc_atoi( strValue );
		CcspTraceInfo(("%s PSM get success Value: %d\n", __FUNCTION__, *piAssocGateTimeValue));
		((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
	}
	else
	{
		CcspTraceInfo(("%s Failed to get PSM\n", __FUNCTION__ ));
		return ANSC_STATUS_FAILURE; 	
	}
        } else {
            struct schema_Wifi_Global_Config *pcfg = NULL;
            pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
            if (pcfg != NULL) {
                *piAssocGateTimeValue = pcfg->assoc_gate_time;
                assocGateTime = pcfg->assoc_gate_time;
                deauthGateTime = pcfg->assoc_gate_time;
                CcspTraceInfo(("%s WIFI DB get success Value: %d\n", __FUNCTION__, *piAssocGateTimeValue));
                free(pcfg);
            } else {
                CcspTraceInfo(("%s WIFI DB Failed to get global config\n", __FUNCTION__ ));
                return ANSC_STATUS_FAILURE;
            }
        }

	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetAssocGateTimeValue( int	iAssocGateTimeValue )
{
	char associationGateTime[ 8 ]               = { 0 };
	int   retPsmSet 		  = CCSP_SUCCESS;
	
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Calling PSM Set \n",__FUNCTION__ ));

	sprintf( associationGateTime, "%d", iAssocGateTimeValue );
	retPsmSet = PSM_Set_Record_Value2( bus_handle, g_Subsystem, AssocGateTime, ccsp_string, associationGateTime );
	if (retPsmSet == CCSP_SUCCESS ) 
	{
		CcspTraceInfo(("%s PSM set success Value: %d\n", __FUNCTION__, iAssocGateTimeValue));
                assocGateTime = iAssocGateTimeValue;
                deauthGateTime = iAssocGateTimeValue;
	}
	else
	{
		CcspTraceInfo(("%s Failed to set PSM Value: %d\n", __FUNCTION__, iAssocGateTimeValue));
		return ANSC_STATUS_FAILURE;
	}

    if (g_wifidb_rfc) {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            pcfg->assoc_gate_time = iAssocGateTimeValue;
            if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                free(pcfg);
            }
            assocGateTime = iAssocGateTimeValue;
            deauthGateTime = iAssocGateTimeValue;
        } else {
            CcspTraceError(("%s: WIFI DB Failed to get global config\n", __FUNCTION__ ));
            return ANSC_STATUS_FAILURE;
        }
    }
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_GetRapidReconnectThresholdValue(ULONG vAPIndex, int	*rapidReconnThresholdValue )
{
	char *strValue	= NULL;
	char  rapidReconnThreshold[ 128 ] = { 0 };
	int   retPsmGet = CCSP_SUCCESS;
	

	*rapidReconnThresholdValue = 0;
	sprintf(rapidReconnThreshold, RapidReconnThreshold, vAPIndex + 1 );

        if (!g_wifidb_rfc) {
	retPsmGet = PSM_Get_Record_Value2( bus_handle, g_Subsystem, rapidReconnThreshold, NULL, &strValue );
	if (retPsmGet == CCSP_SUCCESS) 
	{
		*rapidReconnThresholdValue = _ansc_atoi( strValue );
		((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
	}
	else
	{
		CcspTraceInfo(("%s Failed to get PSM\n", __FUNCTION__ ));
		return ANSC_STATUS_FAILURE; 	
	}
        } else {
            struct schema_Wifi_VAP_Config  *pcfg= NULL;
            pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[vAPIndex], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
            if (pcfg != NULL) {
                *rapidReconnThresholdValue = pcfg->rapid_connect_threshold;
            } else {
                CcspTraceInfo(("%s WIFI DB Failed to get global config\n", __FUNCTION__ ));
                return ANSC_STATUS_FAILURE;
            }
        }
	return ANSC_STATUS_SUCCESS;
}
ANSC_STATUS
CosaDmlWiFi_GetFeatureMFPConfigValue( BOOLEAN *pbFeatureMFPConfig )
{
	
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Calling PSM Get\n",__FUNCTION__ ));

	*pbFeatureMFPConfig = 0;

        if (!g_wifidb_rfc) {
        char *strValue  = NULL;
        int   retPsmGet = CCSP_SUCCESS;
	retPsmGet = PSM_Get_Record_Value2( bus_handle, g_Subsystem, FeatureMFPConfig, NULL, &strValue );
	if (retPsmGet == CCSP_SUCCESS) 
	{
		*pbFeatureMFPConfig = _ansc_atoi( strValue );
		CcspTraceInfo(("%s PSM get success Value: %d\n", __FUNCTION__, *pbFeatureMFPConfig));
		((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
	}
	else
	{
		CcspTraceInfo(("%s Failed to get PSM\n", __FUNCTION__ ));
		return ANSC_STATUS_FAILURE; 	
	}
        } else {
            struct schema_Wifi_Global_Config *pcfg = NULL;
            pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
            if (pcfg != NULL) {
               *pbFeatureMFPConfig = pcfg->mfp_config_feature;
                CcspTraceInfo(("%s MFP config value %d\n",__FUNCTION__,pcfg->mfp_config_feature));
                free(pcfg);
            } else {
               CcspTraceInfo(("%s Failed to get wifi db value\n", __FUNCTION__ ));
               return ANSC_STATUS_FAILURE;
            }
        }
        
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetRapidReconnectThresholdValue(ULONG vAPIndex, int	rapidReconnThresholdValue )
{
	char strValue[128]			  = { 0 },
		  rapidReconnThreshold[ 8 ] = { 0 };
	int   retPsmSet 		  = CCSP_SUCCESS;
	
        CcspWifiTrace(("RDK_LOG_WARN,%s : Calling PSM Set \n",__FUNCTION__ ));

	sprintf(rapidReconnThreshold, "%d", rapidReconnThresholdValue);
	sprintf(strValue, RapidReconnThreshold, vAPIndex + 1 );
	retPsmSet = PSM_Set_Record_Value2( bus_handle, g_Subsystem, strValue, ccsp_string, rapidReconnThreshold );
	if (retPsmSet == CCSP_SUCCESS ) 
	{
		CcspTraceInfo(("%s PSM set success Value: %d\n", __FUNCTION__, rapidReconnThresholdValue));
	}
	else
	{
		CcspTraceInfo(("%s Failed to set PSM Value: %d\n", __FUNCTION__, rapidReconnThresholdValue));
		return ANSC_STATUS_FAILURE;
	}
    
    if (g_wifidb_rfc) {
        struct schema_Wifi_VAP_Config  *pcfg= NULL;
        
        pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[vAPIndex], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
        if (pcfg != NULL) {
            pcfg->rapid_connect_threshold = rapidReconnThresholdValue;
            if (wifi_ovsdb_update_table_entry(vap_names[vAPIndex],"vap_name",OCLM_STR,&table_Wifi_VAP_Config,pcfg,filter_vaps) <= 0) {
                CcspWifiTrace(("RDK_LOG_ERROR,%s: WIFI DB Failed to update vap config\n",__FUNCTION__ ));
                return ANSC_STATUS_FAILURE;
            }
        }
    }

	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_GetApMFPConfigValue( ULONG vAPIndex, char *pMFPConfig )
{
	char sApMFPConfig[128] = { 0 };
        char* strValue = NULL;
        int retPsmGet = CCSP_SUCCESS;
	int rc = -1;

	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Calling PSM Get\n",__FUNCTION__ ));

        if (!g_wifidb_rfc) {
	snprintf(sApMFPConfig, sizeof(sApMFPConfig), ApMFPConfig, vAPIndex + 1 );

	retPsmGet = PSM_Get_Record_Value2( bus_handle, g_Subsystem, sApMFPConfig, NULL,  &strValue);
	if (retPsmGet == CCSP_SUCCESS)
	{
		/* There is a mismatch between HAL and CCSP. so in order to sync with HAL. 
		* We made below logic for HUB4 platform
		*/
		CHAR acMfpConfig[32] = {0};

		if( ( RETURN_OK == wifi_getApSecurityMFPConfig( vAPIndex, acMfpConfig ) ) &&
			( acMfpConfig[0] != '\0' ) &&
			( 0 != strcmp( strValue, acMfpConfig ) ) )
		{ 
			CcspTraceInfo(("%s - Synchronizing MFP configuration with HAL [Idx:%lu O:%s,N:%s]\n",__FUNCTION__,vAPIndex,strValue,acMfpConfig));

			if ( ANSC_STATUS_SUCCESS == CosaDmlWiFi_SetApMFPConfigValue( vAPIndex, acMfpConfig ) )
			{
				//Assign new value after PSM success
				rc = strcpy_s(pMFPConfig, strlen(acMfpConfig) + 1, acMfpConfig);
				if (rc != 0) {
		                    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
                                    ERR_CHK(rc);
                                    return ANSC_STATUS_FAILURE;
                                }
				CcspTraceInfo(("%s - Updated MFP configuration from HAL, Idx:%lu Value:%s\n",__FUNCTION__,vAPIndex,pMFPConfig));
			}
			else 
			{
				//Fallback to PSM value
				rc = strcpy_s(pMFPConfig, strlen(strValue) + 1, strValue);
		                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
				if (rc != 0) {
                                    ERR_CHK(rc);
                                    return ANSC_STATUS_FAILURE;
                                }
				CcspTraceInfo(("%s - Fallback MFP configuration from PSM, Idx:%lu Value:%s\n",__FUNCTION__,vAPIndex,pMFPConfig));
			}
		}
		else
		{
			rc = strcpy_s(pMFPConfig, strlen(strValue) + 1, strValue);
		        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
			if (rc != 0) {
                            ERR_CHK(rc);
                            return ANSC_STATUS_FAILURE;
                        }
		}

		CcspTraceInfo(("%s PSM get success Value: %s\n", __FUNCTION__, pMFPConfig));
	}
	else
	{
		if (strValue)
                    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );

		CcspTraceInfo(("%s Failed to get PSM\n", __FUNCTION__ ));
		return ANSC_STATUS_FAILURE;
	}
        } else {
            struct schema_Wifi_VAP_Config  *pcfg= NULL;
            pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[vAPIndex], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
            if (pcfg != NULL) {
                rc = strcpy_s(pMFPConfig, strlen(pcfg->mfp_config) + 1, pcfg->mfp_config);
		if (rc != 0) {
                    ERR_CHK(rc);
                    return ANSC_STATUS_FAILURE;
                }
            } else {
                CcspTraceInfo(("%s WIFI DB Failed to get Global config\n", __FUNCTION__ ));
                return ANSC_STATUS_FAILURE;
            }
        }
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetApMFPConfigValue ( ULONG vAPIndex, char *pMFPConfig )
{
	int   retPsmSet  = CCSP_SUCCESS;
	char strValue[128]  = { 0 };
        int rc = -1;

	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Calling PSM Set \n",__FUNCTION__ ));
        snprintf(strValue, sizeof(strValue), ApMFPConfig, vAPIndex + 1 );

	retPsmSet = PSM_Set_Record_Value2( bus_handle, g_Subsystem, strValue, ccsp_string, pMFPConfig );
	if (retPsmSet == CCSP_SUCCESS )
	{
		CcspTraceInfo(("%s PSM set success Value: %s\n", __FUNCTION__, pMFPConfig));
	}
	else
	{
		CcspTraceInfo(("%s Failed to set PSM Value: %s\n", __FUNCTION__, pMFPConfig));
		return ANSC_STATUS_FAILURE;
	}

    if (g_wifidb_rfc) {
        struct schema_Wifi_VAP_Config  *pcfg= NULL;

        pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[vAPIndex], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
        if (pcfg != NULL) {
            rc = strcpy_s(pcfg->mfp_config, sizeof(pcfg->mfp_config), pMFPConfig);
	    if (rc != 0) {
                ERR_CHK(rc);
                return  ANSC_STATUS_FAILURE;
            }
            if (wifi_ovsdb_update_table_entry(vap_names[vAPIndex],"vap_name",OCLM_STR,&table_Wifi_VAP_Config,pcfg,filter_vaps) <= 0) {
                CcspWifiTrace(("RDK_LOG_ERROR,%s: WIFI DB Failed to update vap config\n",__FUNCTION__ ));
            }
        }
    }
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetFeatureMFPConfigValue( BOOLEAN bFeatureMFPConfig )
{
	char  acFeatureMFPConfig[ 8 ] = { 0 };
	int   retPsmSet 		  	  = CCSP_SUCCESS;

	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Calling PSM Set \n",__FUNCTION__ ));

	snprintf( acFeatureMFPConfig, sizeof(acFeatureMFPConfig), "%d", bFeatureMFPConfig );
	retPsmSet = PSM_Set_Record_Value2( bus_handle, g_Subsystem, FeatureMFPConfig, ccsp_string, acFeatureMFPConfig );
	if (retPsmSet == CCSP_SUCCESS ) 
	{
		CcspTraceInfo(("%s PSM set success Value: %d\n", __FUNCTION__, bFeatureMFPConfig));
	}
	else
	{
		CcspTraceInfo(("%s Failed to set PSM Value: %d\n", __FUNCTION__, bFeatureMFPConfig));
		return ANSC_STATUS_FAILURE;
	}

    if (g_wifidb_rfc) {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            pcfg->mfp_config_feature = bFeatureMFPConfig;
            if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                free(pcfg);
            }
        } else {
           CcspTraceError(("%s Failed to get wifi db table\n", __FUNCTION__ ));
        }
    }
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_GetRapidReconnectCountEnable(ULONG vAPIndex, BOOLEAN *pbReconnectCountEnable, BOOLEAN usePersistent )
{
	char *strValue	= NULL;
	char  rapidReconnCountEnable[ 128 ] = { 0 };
	int   retPsmGet = CCSP_SUCCESS;

	*pbReconnectCountEnable = 0;

	if( false == usePersistent )
	{
		*pbReconnectCountEnable = sWiFiDmlApStoredCfg[vAPIndex].Cfg.X_RDKCENTRAL_COM_rapidReconnectCountEnable;
		return ANSC_STATUS_SUCCESS;
	}
	
	CcspWifiTrace(("RDK_LOG_WARN,%s : Calling PSM Get\n",__FUNCTION__ ));

        if (!g_wifidb_rfc) {
	snprintf(rapidReconnCountEnable, sizeof(rapidReconnCountEnable), RapidReconnCountEnable, vAPIndex + 1 );

	retPsmGet = PSM_Get_Record_Value2( bus_handle, g_Subsystem, rapidReconnCountEnable, NULL, &strValue );
	if (retPsmGet == CCSP_SUCCESS) 
	{
		*pbReconnectCountEnable = _ansc_atoi( strValue );
		sWiFiDmlApStoredCfg[vAPIndex].Cfg.X_RDKCENTRAL_COM_rapidReconnectCountEnable = *pbReconnectCountEnable;
		CcspTraceInfo(("%s PSM get success Value: %d\n", __FUNCTION__, *pbReconnectCountEnable));
		((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
	}
	else
	{
		CcspTraceInfo(("%s Failed to get PSM\n", __FUNCTION__ ));
		return ANSC_STATUS_FAILURE; 	
	}
        } else {
            struct schema_Wifi_VAP_Config  *pcfg= NULL;
            pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[vAPIndex], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
            if (pcfg != NULL) {
                *pbReconnectCountEnable = pcfg->rapid_connect_enabled;
                sWiFiDmlApStoredCfg[vAPIndex].Cfg.X_RDKCENTRAL_COM_rapidReconnectCountEnable = *pbReconnectCountEnable;
                CcspTraceInfo(("%s WIFI DB get success Value: %d\n", __FUNCTION__, *pbReconnectCountEnable));
            } else {
                CcspTraceInfo(("%s WIFI DB Failed to get vap config\n", __FUNCTION__ ));
                return ANSC_STATUS_FAILURE;
            }
        }

	return ANSC_STATUS_SUCCESS;
}


ANSC_STATUS
CosaDmlWiFi_SetRapidReconnectCountEnable(ULONG vAPIndex, BOOLEAN bReconnectCountEnable )
{
	char strValue[128]			  = { 0 },
		  rapidReconnCountEnable[ 8 ] = { 0 };
	int   retPsmSet 		  = CCSP_SUCCESS;
	
	CcspWifiTrace(("RDK_LOG_WARN,%s : Calling PSM Set \n",__FUNCTION__ ));

	snprintf(rapidReconnCountEnable, sizeof(rapidReconnCountEnable), "%d", bReconnectCountEnable);
	snprintf(strValue, sizeof(strValue), RapidReconnCountEnable, vAPIndex + 1 );
	retPsmSet = PSM_Set_Record_Value2( bus_handle, g_Subsystem, strValue, ccsp_string, rapidReconnCountEnable );
	if (retPsmSet == CCSP_SUCCESS ) 
	{
		sWiFiDmlApStoredCfg[vAPIndex].Cfg.X_RDKCENTRAL_COM_rapidReconnectCountEnable = bReconnectCountEnable;
		CcspTraceInfo(("%s PSM set success Value: %d\n", __FUNCTION__, bReconnectCountEnable));
	}
	else
	{
		CcspTraceInfo(("%s Failed to set PSM Value: %d\n", __FUNCTION__, bReconnectCountEnable));
		return ANSC_STATUS_FAILURE;
	}

    if (g_wifidb_rfc) {
        struct schema_Wifi_VAP_Config  *pcfg= NULL;

        pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[vAPIndex], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
        if (pcfg != NULL) {
            pcfg->rapid_connect_enabled = bReconnectCountEnable;
            if (wifi_ovsdb_update_table_entry(vap_names[vAPIndex],"vap_name",OCLM_STR,&table_Wifi_VAP_Config,pcfg,filter_vaps) <= 0) {
                CcspWifiTrace(("RDK_LOG_ERROR,%s: WIFI DB Failed to update vap config\n",__FUNCTION__ ));
                return ANSC_STATUS_FAILURE;
            }
        }
    }
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_setStatus(ULONG status, PANSC_HANDLE phContext)
{
        if(status == 1) {
                if(wifi_down() != 0)
		    return ANSC_STATUS_FAILURE;
#ifdef FEATURE_HOSTAP_AUTHENTICATOR
           BOOLEAN isNativeHostapdDisabled = FALSE;
           CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
           if (isNativeHostapdDisabled)
           {
#if defined (_XB7_PRODUCT_REQ_)
               libhostap_eloop_deinit();
#endif
               int vapIndex = 0;
               BOOL apEnabled = FALSE;
               for (vapIndex = 0; vapIndex < 16; vapIndex++)
               {
                    wifi_getApEnable(vapIndex, &apEnabled);
                    if (apEnabled) {
                        deinit_lib_hostapd(vapIndex);
                    }
               }
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST) && !defined(_XB7_PRODUCT_REQ_)
               wifi_stop_eapol_rx_thread();
               hapd_deregister_callback();
#endif
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
               deinit_lib_hostapd_greylisting();
#endif
#if defined (_XB7_PRODUCT_REQ_)
	       libhostapd_global_deinit();
#endif
	   }
#endif
        }
        else if(status == 2) {
                if(wifi_init() == 0)
		    return ANSC_STATUS_FAILURE;
#ifdef FEATURE_HOSTAP_AUTHENTICATOR
           BOOLEAN isNativeHostapdDisabled = FALSE;
	   BOOLEAN isVapEnabled = FALSE;
           CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
           if (isNativeHostapdDisabled)
           {
               PCOSA_DATAMODEL_WIFI            pWiFi     = (PCOSA_DATAMODEL_WIFI) phContext;
               PSINGLE_LINK_ENTRY              pSLinkEntrySsid = (PSINGLE_LINK_ENTRY       )NULL;
               PSINGLE_LINK_ENTRY              pSLinkEntryAp   = (PSINGLE_LINK_ENTRY       )NULL;
               PCOSA_DML_WIFI_AP               pWifiAp         = (PCOSA_DML_WIFI_AP        )NULL;
               PCOSA_DML_WIFI_SSID             pWifiSsid       = (PCOSA_DML_WIFI_SSID      )NULL;
               ULONG                           idx            = 0;

#if !defined (_XB7_PRODUCT_REQ_)
               hapd_register_callback();
#endif
               hapd_init_log_files();

#if defined (_XB7_PRODUCT_REQ_)
               libhostapd_global_init();
#endif

               pSLinkEntryAp = AnscQueueGetFirstEntry(&pWiFi->AccessPointQueue);
               pSLinkEntrySsid = AnscQueueGetFirstEntry(&pWiFi->SsidQueue);
               while (pSLinkEntryAp && pSLinkEntrySsid)
               {
                   if (!(pWifiAp = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntryAp)->hContext))
                   {
                       CcspTraceError(("%s Error linking Data Model object!\n",__FUNCTION__));
                       return FALSE;
                   }
                   if (!(pWifiSsid = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntrySsid)->hContext))
                   {
                       CcspTraceError(("%s Error linking Data Model object!\n",__FUNCTION__));
                       return FALSE;
                   }

		   wifi_getApEnable(idx,&isVapEnabled);
                   if (isVapEnabled || pWifiSsid->SSID.Cfg.bEnabled)
                   {
#if !defined (_XB7_PRODUCT_REQ_)
                       /* If security mode is set to open, init lib only if greylisting is enabled */
                       if (pWifiAp->SEC.Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_None && !pWiFi->bEnableRadiusGreyList)
                       {
                           pSLinkEntryAp = AnscQueueGetNextEntry(pSLinkEntryAp);
                           pSLinkEntrySsid = AnscQueueGetNextEntry(pSLinkEntrySsid);
                           idx++;
                           continue;
                       }
#endif
                       init_lib_hostapd(pWiFi, pWifiAp, pWifiSsid, (idx % 2 == 0) ? &(pWiFi->pRadio+0)->Radio : &(pWiFi->pRadio+1)->Radio);

#if defined (FEATURE_SUPPORT_INTERWORKING)
                       if (pWifiAp->AP.Cfg.InterworkingCapability == TRUE && ( pWifiAp->AP.Cfg.InterworkingEnable == TRUE))
                           CosaDmlWiFi_setInterworkingElement(&pWifiAp->AP.Cfg);
#endif
                       if (pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Status)
                           CosaDmlWiFi_ApplyRoamingConsortiumElement(&pWifiAp->AP.Cfg);

                       if (pWifiAp->AP.Cfg.BSSTransitionImplemented == TRUE && pWifiAp->AP.Cfg.BSSTransitionActivated)
                            CosaDmlWifi_setBSSTransitionActivated(&pWifiAp->AP.Cfg, pWifiAp->AP.Cfg.InstanceNumber - 1);
                   }
                   pSLinkEntryAp = AnscQueueGetNextEntry(pSLinkEntryAp);
                   pSLinkEntrySsid = AnscQueueGetNextEntry(pSLinkEntrySsid);
                   idx++;
               }
               libhostap_eloop_run();
           }
#if defined (_XB7_PRODUCT_REQ_)
           /* reload apps */
           if ( v_secure_system("wifi_setup.sh start_security_apps") != 0 ) {
               CcspWifiTrace(("RDK_LOG_INFO, %s:%d wifi_setup.sh start_security_apps failed\n", __FUNCTION__, __LINE__));
               return FALSE;
           }
#endif
#endif /* FEATURE_HOSTAP_AUTHENTICATOR */
        }
        UNREFERENCED_PARAMETER(phContext);

        return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApGetNeighborReportActivated(ULONG vAPIndex, BOOLEAN *pbNeighborReportActivated, BOOLEAN usePersistent )
{
	char *strValue	= NULL;
	char  neighborReportActivated[ 128 ] = { 0 };
	int   retPsmGet = CCSP_SUCCESS;

	*pbNeighborReportActivated = 0;

	if( false == usePersistent )
	{
		*pbNeighborReportActivated = sWiFiDmlApStoredCfg[vAPIndex].Cfg.X_RDKCENTRAL_COM_NeighborReportActivated;
		return ANSC_STATUS_SUCCESS;
	}
	
	CcspWifiTrace(("RDK_LOG_WARN,%s : Calling PSM Get\n",__FUNCTION__ ));

        if (g_wifidb_rfc) {
            struct schema_Wifi_VAP_Config  *pcfg= NULL;
            pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[vAPIndex], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
            if (pcfg != NULL) {
                strValue = pcfg->nbr_report_activated ? strdup("true") : strdup("false");
            } else {
                CcspTraceInfo(("%s WIFI DB Failed to get vap entry %lu\n", __FUNCTION__,vAPIndex ));
                return ANSC_STATUS_FAILURE;
            }
        } else {
        memset(neighborReportActivated, 0, sizeof(neighborReportActivated));
	snprintf(neighborReportActivated, sizeof(neighborReportActivated), NeighborReportActivated, vAPIndex + 1 );
        *pbNeighborReportActivated = FALSE;
	retPsmGet = PSM_Get_Record_Value2( bus_handle, g_Subsystem, neighborReportActivated, NULL, &strValue );
        }
	if (retPsmGet == CCSP_SUCCESS) 
	{
		if(((strncmp (strValue, "true", strlen("true")) == 0)) || (strncmp (strValue, "TRUE", strlen("TRUE")) == 0))
        {
            *pbNeighborReportActivated = TRUE;
        }
        sWiFiDmlApStoredCfg[vAPIndex].Cfg.X_RDKCENTRAL_COM_NeighborReportActivated = *pbNeighborReportActivated;
#if !defined(_HUB4_PRODUCT_REQ_) || defined(HUB4_WLDM_SUPPORT)
#if defined(ENABLE_FEATURE_MESHWIFI) || defined(_CBR_PRODUCT_REQ_) || defined(_COSA_BCM_MIPS_)
        //set to HAL
        CcspWifiTrace(("RDK_LOG_WARN,%s : setting value to HAL\n",__FUNCTION__ ));
        wifi_setNeighborReportActivation(vAPIndex, *pbNeighborReportActivated);
#endif
#endif
		CcspTraceInfo(("%s PSM get success Value: %d\n", __FUNCTION__, *pbNeighborReportActivated));
		((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
	}
	else
	{
		CcspTraceInfo(("%s Failed to get PSM\n", __FUNCTION__ ));
		return ANSC_STATUS_FAILURE;
	}

	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApSetNeighborReportActivated(ULONG vAPIndex, BOOLEAN bNeighborReportActivated )
{
	CcspWifiTrace(("RDK_LOG_WARN,%s : Calling PSM Set \n",__FUNCTION__ ));
#if !defined(_HUB4_PRODUCT_REQ_) || defined(HUB4_WLDM_SUPPORT)
#if defined(ENABLE_FEATURE_MESHWIFI) || defined(_CBR_PRODUCT_REQ_) || defined(_COSA_BCM_MIPS_)
	char strValue[128]		  = { 0 }, neighborReportActivated[ 8 ] = { 0 };
	int   retPsmSet 		  = CCSP_SUCCESS;

	if (wifi_setNeighborReportActivation(vAPIndex, bNeighborReportActivated) == RETURN_OK) {
		snprintf(neighborReportActivated,sizeof(neighborReportActivated), "%s", (bNeighborReportActivated ? "true" : "false"));
		snprintf(strValue,sizeof(strValue), NeighborReportActivated, vAPIndex + 1 );
		retPsmSet = PSM_Set_Record_Value2( bus_handle, g_Subsystem, strValue, ccsp_string, neighborReportActivated );
		if (retPsmSet == CCSP_SUCCESS ) 
		{
			sWiFiDmlApStoredCfg[vAPIndex].Cfg.X_RDKCENTRAL_COM_NeighborReportActivated = bNeighborReportActivated;
			CcspTraceInfo(("%s PSM set success Value: %d\n", __FUNCTION__, bNeighborReportActivated));
		}
		else
		{
			CcspTraceInfo(("%s Failed to set PSM Value: %d\n", __FUNCTION__, bNeighborReportActivated));
			return ANSC_STATUS_FAILURE;
		}
            if (g_wifidb_rfc) {
                struct schema_Wifi_VAP_Config  *pcfg= NULL;

                pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[vAPIndex], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
                if (pcfg != NULL) {
                    pcfg->rapid_connect_enabled = bNeighborReportActivated;
                    if (wifi_ovsdb_update_table_entry(vap_names[vAPIndex],"vap_name",OCLM_STR,&table_Wifi_VAP_Config,pcfg,filter_vaps) <= 0) {
                        CcspWifiTrace(("RDK_LOG_ERROR,%s: WIFI DB Failed to update vap config\n",__FUNCTION__ ));
                        return ANSC_STATUS_FAILURE;
                    }
                    sWiFiDmlApStoredCfg[vAPIndex].Cfg.X_RDKCENTRAL_COM_NeighborReportActivated = bNeighborReportActivated;
                }
            }
	}
#else
    UNREFERENCED_PARAMETER(vAPIndex);
    UNREFERENCED_PARAMETER(bNeighborReportActivated);
#endif
#else
    UNREFERENCED_PARAMETER(vAPIndex);
    UNREFERENCED_PARAMETER(bNeighborReportActivated);
#endif

	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_GetRapidReconnectIndicationEnable(BOOL *bEnable, BOOL usePersistent)
{
    char *strValue  = NULL;
    int  retPsmGet = CCSP_SUCCESS;

    *bEnable = false;
    if (usePersistent == false) {
        if (g_pCosaBEManager && g_pCosaBEManager->hWifi)
        {
                *bEnable = ((PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi)->bRapidReconnectIndicationEnabled;
                return ANSC_STATUS_SUCCESS;
        }
        else
        {
                CcspWifiTrace(("RDK_LOG_ERROR,%s : g_pCosaBEManager->hWifi is NULL\n",__FUNCTION__ ));
                return ANSC_STATUS_FAILURE;
        }
    }

    CcspWifiTrace(("RDK_LOG_WARN,%s : Calling PSM Get\n",__FUNCTION__ ));

    if (!g_wifidb_rfc) {
    retPsmGet = PSM_Get_Record_Value2(bus_handle, g_Subsystem, RapidReconnectIndicationEnable, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS)
    {
        *bEnable = _ansc_atoi( strValue );
        CcspTraceInfo(("%s PSM get success Value: %d\n", __FUNCTION__, *bEnable));
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
    }
    else
    {
        CcspTraceInfo(("%s Failed to get PSM\n", __FUNCTION__ ));
        return ANSC_STATUS_FAILURE;
    }
    } else {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            *bEnable = pcfg->rapid_reconnect_enable;
            free(pcfg);
        } else {
            CcspTraceInfo(("%s WIFI DB Failed to get global config\n", __FUNCTION__ ));
            return ANSC_STATUS_FAILURE;
        }
    }
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetRapidReconnectIndicationEnable(BOOL bEnable )
{
    char  FailureEnable[ 8 ] = { 0 };
    int   retPsmSet           = CCSP_SUCCESS;

    CcspWifiTrace(("RDK_LOG_WARN,%s : Calling PSM Set \n",__FUNCTION__ ));

    snprintf( FailureEnable, sizeof(FailureEnable), "%d", bEnable );
    retPsmSet = PSM_Set_Record_Value2( bus_handle, g_Subsystem, RapidReconnectIndicationEnable, ccsp_string, FailureEnable );
    if (retPsmSet == CCSP_SUCCESS )
    {
        ((PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi)->bRapidReconnectIndicationEnabled = bEnable;
        CcspTraceInfo(("%s PSM set success Value: %d\n", __FUNCTION__, bEnable));
    }
    else
    {
        CcspTraceInfo(("%s Failed to set PSM Value: %d\n", __FUNCTION__, bEnable));
        return ANSC_STATUS_FAILURE;
    }

    if (g_wifidb_rfc) {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            pcfg->rapid_reconnect_enable = bEnable;
            if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                free(pcfg);
            }
            ((PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi)->bRapidReconnectIndicationEnabled = bEnable;
        } else {
            CcspTraceError(("%s: WIFI DB Failed to get global config\n", __FUNCTION__ ));
            return ANSC_STATUS_FAILURE;
        }
    }
    
    return ANSC_STATUS_SUCCESS;
}

static ANSC_STATUS
CosaDmlWiFiGetBridgePsmData
    (
        void
    )
{
    PCOSA_DML_WIFI_SSID_BRIDGE  pBridge = NULL;
    char *l3netIpAddrValue = NULL;
    char *l3netIpSubNetValue = NULL;
    char *strValue = NULL;
    char *ssidStrValue = NULL;
    char recName[256]={0};
    int retPsmGet = CCSP_SUCCESS;
    unsigned int numInstances = 0;
    unsigned int *pInstanceArray = NULL;

    pBridgeVlanCfg = NULL;
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s \n",__FUNCTION__));
    wifiDbgPrintf("%s g_Subsytem = %s  \n",__FUNCTION__, g_Subsystem );

    retPsmGet =  PsmGetNextLevelInstances ( bus_handle,g_Subsystem, l2netBridgeInstances, &numInstances, &pInstanceArray);
    wifiDbgPrintf("%s: Got %d  Bridge instances \n", __func__, numInstances );

	
	
    unsigned int i;
    for (i = 0; i < numInstances; i++) {
        int bridgeIndex = (int)pInstanceArray[i];
        wifiDbgPrintf("%s: Index %d is Bridge instances  %d \n", __func__, i, pInstanceArray[i] );

        pBridge = NULL;

        memset(recName, 0, sizeof(recName));
        snprintf(recName, sizeof(recName), l2netBridge, bridgeIndex);
        retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &ssidStrValue);
        if ((retPsmGet == CCSP_SUCCESS) && (ssidStrValue != NULL)) {
            char *ssidName = ssidStrValue;
            BOOL firstSSID = TRUE;
            int wlanIndex = 0;
            int retVal;

            if (strlen(ssidName) > 0) {
                wifiDbgPrintf("%s: %s returned %s\n", __func__, recName, ssidName);

                ssidName = strtok(ssidName," ");
                while (ssidName != NULL) {

					//zqiu
                    //if (strstr(ssidName,"ath") != NULL) {
					if (strlen(ssidName) >=2) {
					
                        if (firstSSID == TRUE) {
                            firstSSID = FALSE;
                            pBridge = (PCOSA_DML_WIFI_SSID_BRIDGE)AnscAllocateMemory(sizeof(COSA_DML_WIFI_SSID_BRIDGE)*(1));
                            pBridge->InstanceNumber = bridgeIndex;
                            pBridge->SSIDCount = 0;

                            // Get the VlanId, IpAddress and Subnet once
                            memset(recName, 0, sizeof(recName));
#if defined (_BWG_PRODUCT_REQ_)
                            if((bridgeIndex == 3) || (bridgeIndex == 4)|| (bridgeIndex == 7) || (bridgeIndex == 8) || (bridgeIndex == 11)) {
                                snprintf(recName, sizeof(recName), XfinityNewl2netVlan, bridgeIndex);
                                CcspTraceWarning(("[NEWVLAN]: %s %s bridgeIndex= %d ...\n",__func__,recName, bridgeIndex));
                            }
                            else
                                snprintf(recName, sizeof(recName), l2netVlan, bridgeIndex);
#else
                            snprintf(recName, sizeof(recName), l2netVlan, bridgeIndex);
#endif
                            retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
                            if (retPsmGet == CCSP_SUCCESS) {
                                wifiDbgPrintf("%s: %s returned %s\n", __func__, recName, strValue);
                                pBridge->VlanId =  _ansc_atoi(strValue);
                                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
                            } else {
                                // No bridge with this id
                                if (ssidStrValue)
                                {
                                    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(ssidStrValue);
                                }
                                if (pInstanceArray)
                                {
                                    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(pInstanceArray);
                                }
                                return ANSC_STATUS_FAILURE;
                            }

                            memset(recName, 0, sizeof(recName));
                            snprintf(recName, sizeof(recName), l2netl3InstanceNum, bridgeIndex);
                            retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
                            if (retPsmGet == CCSP_SUCCESS) {
                                wifiDbgPrintf("%s: %s returned %s\n", __func__, recName, strValue);
                                int l3InstanceNum = _ansc_atoi(strValue);

                                memset(recName, 0, sizeof(recName));
                                snprintf(recName, sizeof(recName), l3netIpAddr, l3InstanceNum);
                                retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &l3netIpAddrValue);
                                if (retPsmGet == CCSP_SUCCESS) {
                                    wifiDbgPrintf("%s: %s returned %s\n", __func__, recName, l3netIpAddrValue);
                                    snprintf(pBridge->IpAddress, sizeof(pBridge->IpAddress), "%s", l3netIpAddrValue);
                                    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(l3netIpAddrValue);
                                } else {
                                    snprintf(pBridge->IpAddress, sizeof(pBridge->IpAddress), "0.0.0.0");
                                }

                                memset(recName, 0, sizeof(recName));
                                snprintf(recName, sizeof(recName), l3netIpSubNet, l3InstanceNum);
                                retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &l3netIpSubNetValue);
                                if (retPsmGet == CCSP_SUCCESS) {
                                    wifiDbgPrintf("%s: %s returned %s\n", __func__, recName, l3netIpSubNetValue);
                                    snprintf(pBridge->IpSubNet, sizeof(pBridge->IpSubNet), "%s", l3netIpSubNetValue);
                                    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(l3netIpSubNetValue);
                                } else {
                                    // No bridge with this id
                                    snprintf(pBridge->IpSubNet, sizeof(pBridge->IpSubNet), "255.255.255.0");
                                }
                                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
                            } else {
                                // If no link between l2net and l3net is found set to default values
                                wifiDbgPrintf("%s: %s returned %s\n", __func__, recName, strValue);
                                snprintf(pBridge->IpAddress, sizeof(pBridge->IpAddress), "0.0.0.0");
                                snprintf(pBridge->IpSubNet, sizeof(pBridge->IpSubNet), "255.255.255.0");
                            }

                        }

                        snprintf(pBridge->SSIDName[pBridge->SSIDCount], sizeof(pBridge->SSIDName[pBridge->SSIDCount]), "%s", ssidName);
                        wifiDbgPrintf("%s: ssidName  = %s \n", __FUNCTION__, ssidName);
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
                        pBridge->SSIDCount++;
                    } //(strlen(ssidName) >=2)
                    ssidName = strtok(NULL, " ");
                } //(ssidName != NULL)
                for (ULONG idx = 0; idx<pBridge->SSIDCount; idx++) {
                        retVal = wifi_getIndexFromName(pBridge->SSIDName[idx], &wlanIndex);
                        if (retVal == 0) {
                            char *bridgeName;
                            memset(recName, 0, sizeof(recName));
                            snprintf(recName, sizeof(recName), l2netBridgeName, bridgeIndex);
                            if (PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &bridgeName) == CCSP_SUCCESS) {
                                wifiDbgPrintf("%s: BridgeName  = %s \n", __FUNCTION__, (bridgeName ? bridgeName: "NULL"));

                                // Determine if bridge, ipaddress or subnet changed.  If so flag it and save it
                                char bridge[16];
                                char ip[32];
                                char ipSubNet[32];
                                wifi_getApBridgeInfo(wlanIndex, bridge, ip, ipSubNet);
                                if (strcmp(ip,pBridge->IpAddress) != 0 ||
                                    strcmp(ipSubNet,pBridge->IpSubNet) != 0 ||
                                    strcmp(bridge, bridgeName) != 0) {
                                    wifi_setApBridgeInfo(wlanIndex,bridgeName, pBridge->IpAddress, pBridge->IpSubNet);
                                    wifi_setApVlanID(wlanIndex,pBridge->VlanId);
                                    sWiFiDmlUpdateVlanCfg[wlanIndex] = TRUE;
                                }
                                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(bridgeName);
                            }
                      } //(retVal == 0)
                 } //for (int idx = 0; idx<pBridge->SSIDCount; idx++)
#else
                        retVal = wifi_getIndexFromName(pBridge->SSIDName[pBridge->SSIDCount], &wlanIndex);
                        if (retVal == 0) {
                            char bridgeName[32];
							//>>zqiu
							if(pBridge->InstanceNumber!=6) {
                            snprintf(bridgeName, sizeof(bridgeName), "br%lu", pBridge->InstanceNumber-1);
							} else { //br106
                            snprintf(bridgeName, sizeof(bridgeName), "br%lu", pBridge->VlanId);
							}
							//<<
                            
                            // Determine if bridge, ipaddress or subnet changed.  If so flag it and save it 
                            {
                                char bridge[16];
                                char ip[32];
                                char ipSubNet[32];
                                wifi_getApBridgeInfo(wlanIndex, bridge, ip, ipSubNet);
                                if (strcmp(ip,pBridge->IpAddress) != 0 ||
                                    strcmp(ipSubNet,pBridge->IpSubNet) != 0 ||
                                    strcmp(bridge, bridgeName) != 0) {
                                    wifi_setApBridgeInfo(wlanIndex,bridgeName, pBridge->IpAddress, pBridge->IpSubNet);
                                    wifi_setApVlanID(wlanIndex,pBridge->VlanId);
                                    sWiFiDmlUpdateVlanCfg[wlanIndex] = TRUE;
                                }
                            }
                        }

                        pBridge->SSIDCount++;

                    } //(strlen(ssidName) >=2)
                    ssidName = strtok('\0',",");
                } //(ssidName != NULL)
#endif //defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
            } //(strlen(ssidName) > 0)
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(ssidStrValue);
        } //(retPsmGet == CCSP_SUCCESS)
        // Added to gload list
        if (pBridge) {
            pBridge->next = pBridgeVlanCfg;
            pBridgeVlanCfg = pBridge;
        }
    }

    if (pInstanceArray) {
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(pInstanceArray);
    }
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Returning Success \n",__FUNCTION__));
    return ANSC_STATUS_SUCCESS;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFiDeAllocBridgeVlan                                   */
/*                                                                               */
/* DESCRIPTION   : This function destroy/ Free up the memory allocated for       */
/*                 pBridgeVlanCfg                                                */
/*                                                                               */
/* INPUT         : NONE                                                          */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/

ANSC_STATUS
CosaDmlWiFiDeAllocBridgeVlan (void)
{
    PCOSA_DML_WIFI_SSID_BRIDGE pBridge = pBridgeVlanCfg;
    while (pBridge)
    {
        PCOSA_DML_WIFI_SSID_BRIDGE pBridgeCur = pBridge;
        pBridge = pBridge->next;
        if (pBridgeCur)
        {
            free(pBridgeCur);
            pBridgeCur = NULL;
        }
    }
    pBridgeVlanCfg = NULL;
    return ANSC_STATUS_SUCCESS;
}
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
//Multinet SyncMembers for WiFi Interfaces.
static void
CosaDmlWiFiSyncBridgeMembers(void)
{
    PCOSA_DML_WIFI_SSID_BRIDGE pBridge = pBridgeVlanCfg;
    CHAR multinet_instance[COSA_DML_WIFI_STR_LENGHT_8] = {0};

   if (!gWifi_sysevent_fd)
       gWifi_sysevent_fd = sysevent_open("127.0.0.1", SE_SERVER_WELL_KNOWN_PORT, SE_VERSION, "ccsp_wifi_agent", &gWifi_sysEtoken);

    while(pBridge && gWifi_sysevent_fd)
    {
        CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : freeeing pBridge\n",__FUNCTION__));
        PCOSA_DML_WIFI_SSID_BRIDGE pBridgeCur = pBridge;
        snprintf(multinet_instance, COSA_DML_WIFI_STR_LENGHT_8, "%lu", pBridge->InstanceNumber);
        sysevent_set(gWifi_sysevent_fd, gWifi_sysEtoken, "multinet-syncMembers", multinet_instance, 0);

        pBridge = pBridge->next;
        if (pBridgeCur)
        {
            free(pBridgeCur);
            pBridgeCur = NULL;
        }
    }
    CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Returning Success \n",__FUNCTION__));
    pBridgeVlanCfg = NULL;
}
#endif

pthread_mutex_t Hotspot_MacFilt_ThreadMutex = PTHREAD_MUTEX_INITIALIZER;
void* Delete_Hotspot_MacFilt_Entries_Thread_Func( void * arg)
{
	UNREFERENCED_PARAMETER(arg);
	int retPsmGet = CCSP_SUCCESS;
	char recName[256];
	char *device_name = NULL;
	int i,j;
	BOOL ret = FALSE;
	int apIns;

	unsigned int 		InstNumCount    = 0;
	unsigned int*		pInstNumList    = NULL;

    wifiDbgPrintf("%s\n",__FUNCTION__);
    pthread_mutex_lock(&Hotspot_MacFilt_ThreadMutex);
	for(j=0;j<HOTSPOT_NO_OF_INDEX;j++)
	{
		apIns = Hotspot_Index[j];

		memset(recName, 0, sizeof(recName));
		snprintf(recName, sizeof(recName), "Device.WiFi.AccessPoint.%d.X_CISCO_COM_MacFilterTable.", apIns);

		if(CcspBaseIf_GetNextLevelInstances
		(
			bus_handle,
			WIFI_COMP,
			WIFI_BUS,
			recName,
			&InstNumCount,
			&pInstNumList
		) == CCSP_SUCCESS)
		{

			for(i=InstNumCount-1; i>=0; i--)
			{
				memset(recName, 0, sizeof(recName));
				snprintf(recName, sizeof(recName), MacFilterDevice, apIns, pInstNumList[i]);

				retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &device_name);
				if (retPsmGet == CCSP_SUCCESS)
				{
					if(strcasecmp(HOTSPOT_DEVICE_NAME, device_name)==0)
					{
						snprintf(recName, sizeof(recName), "Device.WiFi.AccessPoint.%d.X_CISCO_COM_MacFilterTable.%d.",apIns, pInstNumList[i]);
						ret = Cosa_DelEntry(WIFI_COMP,WIFI_BUS,recName);
						if( !ret)
						{
							CcspTraceError(("%s MAC_FILTER : Cosa_DelEntry for recName %s failed \n",__FUNCTION__,recName));
						}
					}
					((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(device_name);
					device_name = NULL;
				}
			}
			if(pInstNumList) {
				free(pInstNumList);
				pInstNumList = NULL;
			}

	        }
		else
		{
			CcspTraceError(("%s MAC_FILTER : CcspBaseIf_GetNextLevelInstances failed for %s \n",__FUNCTION__,recName));
		}
	}
    pthread_mutex_unlock(&Hotspot_MacFilt_ThreadMutex);
    return NULL;
}

void Delete_Hotspot_MacFilt_Entries() {

	pthread_t Delete_Hotspot_MacFilt_Entries_Thread;
	int res;
        pthread_attr_t attr;
        pthread_attr_t *attrp = NULL;

        attrp = &attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
	res = pthread_create(&Delete_Hotspot_MacFilt_Entries_Thread, attrp, Delete_Hotspot_MacFilt_Entries_Thread_Func, NULL);
        if(attrp != NULL)
            pthread_attr_destroy( attrp );
	if(res != 0) {
	    CcspTraceError(("%s MAC_FILTER : Create Delete_MacFilt_Entries_Thread failed for %d \n",__FUNCTION__,res));
	}
}
#if !defined(_XF3_PRODUCT_REQ_)
static ANSC_STATUS
CosaDmlWiFiCheckPreferPrivateFeature

    (
        BOOL* pbEnabled
    )
{
    BOOL bEnabled;
    int idx[4]={5,6,9,10}, index=0; 
    int apIndex=0;
    char recName[256];

    CcspWifiTrace(("RDK_LOG_INFO,%s \n",__FUNCTION__));
    printf("%s \n",__FUNCTION__);

    CosaDmlWiFi_GetPreferPrivatePsmData(&bEnabled);
    *pbEnabled = bEnabled;


    if (bEnabled == TRUE)
    {
    	for(index = 0; index <4 ; index++) {
       	        apIndex=idx[index];
  		memset(recName, 0, sizeof(recName));
    		snprintf(recName, sizeof(recName), MacFilterMode, apIndex);
        	wifi_setApMacAddressControlMode(apIndex-1, 2);
        	PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "2");
        }

    }
    else
    {
    	for(index = 0; index <4 ; index++) {
                apIndex=idx[index];

                memset(recName, 0, sizeof(recName));
                snprintf(recName, sizeof(recName), MacFilterMode, apIndex);
                wifi_setApMacAddressControlMode(apIndex-1, 0);
                PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "0");

    	}
	//Delete_Hotspot_MacFilt_Entries();
    }
   
     
    if (g_wifidb_rfc) {
        struct schema_Wifi_VAP_Config  *pcfg= NULL;
        for(index = 0; index <4 ; index++) {
            apIndex=idx[index];
            pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[apIndex], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
            if (pcfg != NULL) {
                if (bEnabled == TRUE) {
                    wifi_setApMacAddressControlMode(apIndex, 2);
                    pcfg->mac_filter_enabled = TRUE;
                    pcfg->mac_filter_mode = wifi_mac_filter_mode_black_list;
                } else {
                    wifi_setApMacAddressControlMode(apIndex, 0);
                    pcfg->mac_filter_enabled = FALSE;
                }
                if (wifi_ovsdb_update_table_entry(vap_names[apIndex],"vap_name",OCLM_STR,&table_Wifi_VAP_Config,pcfg,filter_vaps) <= 0) {
                    CcspWifiTrace(("RDK_LOG_ERROR,%s: WIFI DB Failed to update vap config\n",__FUNCTION__ ));
                }
            }
        }
    }
 
    CcspWifiTrace(("RDK_LOG_INFO,%s returning\n",__FUNCTION__));
    printf("%s returning\n",__FUNCTION__);

    return ANSC_STATUS_SUCCESS;
}
#endif

void *Wifi_Hosts_Sync_Func(void *pt, int index, wifi_associated_dev_t *associated_dev, BOOL bCallForFullSync, BOOL bCallFromDisConnCB);
/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFiCheckEnableRadiusGreylist                          */
/*                                                                               */
/* DESCRIPTION   : This function set the rdk_greylist to TRUE/FALSE in HAL       */
/* 			at bootup                                                */
/* 	                                                                         */
/* INPUT         : pbEnabled                                                     */
/*                                                                               */
/* OUTPUT        : TRUE / FALSE                                                  */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
static
ANSC_STATUS CosaDmlWiFiCheckEnableRadiusGreylist(BOOL* pbEnabled) {
    CcspTraceInfo(("[%s] Enter\n",__FUNCTION__));
    int index=0;
    int apIndex=0;
    char recName[256];
    BOOL bEnabled;


    CosaDmlWiFiGetEnableRadiusGreylist(&bEnabled);
    *pbEnabled = bEnabled;
    if (bEnabled == TRUE)
    {
        CcspTraceInfo(("[%s] Enabled\n",__FUNCTION__));
        for(index=0 ; index<HOTSPOT_NO_OF_INDEX ; index++) {
            apIndex=Hotspot_Index[index];
            memset(recName, 0, sizeof(recName));
            snprintf(recName, sizeof(recName), MacFilterMode, apIndex);
            wifi_setApMacAddressControlMode(Hotspot_Index[index], 2);
            PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "2");
        }
        wifi_enableGreylistAccessControl(bEnabled);
    }
    else {
        CcspTraceInfo(("[%s] Disabled\n",__FUNCTION__));
        wifi_enableGreylistAccessControl(bEnabled);
        /* In the call to enable the feature, greylist "CosaDmlWiFisetEnableRadiusGreylist",
         * we are doing this operation when the feature is turned off - set to false
         * so commenting out this part to reduce redundant calls */
/*
         for(index = 0; index <HOTSPOT_NO_OF_INDEX ; index++) {
                apIndex=Hotspot_Index[index];
                wifi_delApAclDevices(apIndex-1);
        }
*/
    }
    return ANSC_STATUS_SUCCESS;
}
#endif
void CosaDMLWiFi_Send_FullHostDetails_To_LMLite(LM_wifi_hosts_t *phosts);
void CosaDMLWiFi_Send_ReceivedHostDetails_To_LMLite(LM_wifi_host_t   *phost);

#if 0
SyncLMLite()
{

	parameterValStruct_t    value = { "Device.Hosts.X_RDKCENTRAL-COM_LMHost_Sync", "0", ccsp_unsignedInt};
	char compo[256] = "eRT.com.cisco.spvtg.ccsp.lmlite";
	char bus[256] = "/com/cisco/spvtg/ccsp/lmlite";
	char* faultParam = NULL;
	int ret = 0;	

    CcspWifiTrace(("RDK_LOG_WARN,WIFI %s  \n",__FUNCTION__));

	ret = CcspBaseIf_setParameterValues(
		  bus_handle,
		  compo,
		  bus,
		  0,
		  0,
		  &value,
		  1,
		  TRUE,
		  &faultParam
		  );

	if(ret == CCSP_SUCCESS)
	{
		CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Sync with LMLite\n",__FUNCTION__));
		Wifi_Hosts_Sync_Func(NULL,0, NULL, 1, 0);
	}
	else
	{
		if ( faultParam ) 
		{
			CCSP_MESSAGE_BUS_INFO *bus_info = (CCSP_MESSAGE_BUS_INFO *)bus_handle;
			bus_info->freefunc(faultParam);
		}

		CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : FAILED to sync with LMLite ret: %d \n",__FUNCTION__,ret));
	}
}
#endif

void *wait_for_brlan1_up()
{
    BOOL radioEnabled = FALSE;
    printf("****entering %s\n",__FUNCTION__);
    //sleep(100);
	int uptime = 0;

#if !defined(_HUB4_PRODUCT_REQ_)
    int timeout=240;
    CHAR ucEntryNameValue[128]       = {0};
    parameterValStruct_t varStruct;
    varStruct.parameterName = "Device.IP.Interface.5.Status";
    varStruct.parameterValue = ucEntryNameValue;
    ULONG     ulEntryNameLen;
#if defined(_XB6_PRODUCT_REQ_) || defined(_COSA_BCM_MIPS_)
    do
    {
        ulEntryNameLen = sizeof(ucEntryNameValue);
        if (COSAGetParamValueByPathName(g_MessageBusHandle,&varStruct,&ulEntryNameLen)==0 )
        {

           //printf("****%s, %s\n",__FUNCTION__, varStruct.parameterValue);
           timeout-=2;
           if(timeout<=0)  //wait at most 4 minutes
              break;
           sleep(2);
        }
    } while (strcasecmp(varStruct.parameterValue ,"Up") != 0);
#else
    do 
    {
        ulEntryNameLen = sizeof(ucEntryNameValue);
        if (COSAGetParamValueByPathName(g_MessageBusHandle,&varStruct,&ulEntryNameLen)==0 )
        {
               
           //printf("****%s, %s\n",__FUNCTION__, varStruct.parameterValue);
           timeout-=2;
    	   if(timeout<=0)  //wait at most 4 minutes
              break;
           sleep(2);  
        }
        if (access(RADIO_BROADCAST_FILE, F_OK) == 0) 
        {
            CcspWifiTrace(("RDK_LOG_INFO,%s is created Start Radio Broadcasting\n", RADIO_BROADCAST_FILE));
            break;
        }
        else
        {
            printf("%s is not created not starting Radio Broadcasting\n", RADIO_BROADCAST_FILE);
        }
    } while (strcasecmp(varStruct.parameterValue ,"Up"));
#endif
#endif

#ifdef _XB6_PRODUCT_REQ_
        /*Enabling mesh interface br403*/
        v_secure_system("sysevent set meshbhaul-setup 10");
 
        fprintf(stderr,"CALL VLAN UTIL TO SET UP LNF\n");
        v_secure_system("sysevent set lnf-setup 6");
        //wifi_setLFSecurityKeyPassphrase();
#endif
	//CosaDmlWiFi_SetRegionCode(NULL);


char SSID1_CUR[COSA_DML_WIFI_MAX_SSID_NAME_LEN]={0},SSID2_CUR[COSA_DML_WIFI_MAX_SSID_NAME_LEN]={0};
        /*TODO CID: 68270 Out-of-bounds access - Fix in QTN code*/
	wifi_getSSIDName(0,SSID1_CUR);
   	wifi_pushSsidAdvertisementEnable(0, AdvEnable24);
   	CcspTraceInfo(("\n"));
	get_uptime(&uptime);
	CcspWifiTrace(("RDK_LOG_WARN,Wifi_Broadcast_complete:%d\n",uptime));
        OnboardLog("Wifi_Broadcast_complete:%d\n",uptime);
	t2_event_d("bootuptime_WifiBroadcasted_split", uptime);
	CcspTraceInfo(("Wifi_Name_Broadcasted:%s\n",SSID1_CUR));
	OnboardLog("Wifi_Name_Broadcasted:%s\n",SSID1_CUR);
       /*TODO CID: 68270 Out-of-bounds access - Fix in QTN code*/
   	wifi_getSSIDName(1,SSID2_CUR);
   	wifi_pushSsidAdvertisementEnable(1, AdvEnable5);
	get_uptime(&uptime);
	CcspWifiTrace(("RDK_LOG_WARN,Wifi_Broadcast_complete:%d\n",uptime));
	OnboardLog("Wifi_Broadcast_complete:%d\n",uptime);
        t2_event_d("bootuptime_WifiBroadcasted_split", uptime);
	CcspTraceInfo(("Wifi_Name_Broadcasted:%s\n",SSID2_CUR));
	OnboardLog("Wifi_Name_Broadcasted:%s\n",SSID2_CUR);
    	
    wifi_getRadioEnable(0, &radioEnabled);
    if (radioEnabled == TRUE)
    {
        wifi_setLED(0, true);
    }
    else
    {
        fprintf(stderr,"Radio 0 is not Enabled\n");
    }
    wifi_getRadioEnable(1, &radioEnabled);
    if (radioEnabled == TRUE)
    {
        wifi_setLED(1, true);
    }
    else
    {
       fprintf(stderr, "Radio 1 is not Enabled\n");
    }
	//zqiu: move to CosaDmlWiFiGetBridge0PsmData
	//system("/usr/ccsp/wifi/br0_ip.sh"); 
	CosaDmlWiFiGetBridge0PsmData(NULL, NULL);	
	v_secure_system("/usr/ccsp/wifi/br106_addvlan.sh");
	return NULL;
}

//zqiu: set the passphrase for L&F SSID int wifi config
static void wifi_setLFSecurityKeyPassphrase() {
	v_secure_system("/usr/ccsp/wifi/lfp.sh");
}

#if defined (_HUB4_PRODUCT_REQ_)
ANSC_STATUS
CosaDmlWiFiCheckAndConfigureLEDS
    ( 
	void
    )
{
    BOOL radioEnabled = FALSE;
    wifi_getRadioEnable(0, &radioEnabled);
    if (radioEnabled == TRUE)
    {
        wifi_setLED(0, true);
    }
    else
    {
        fprintf(stderr,"Radio 0 is not Enabled\n");
    }

    //Initialize here again before get	
    radioEnabled = FALSE;

    wifi_getRadioEnable(1, &radioEnabled);
    if (radioEnabled == TRUE)
    {
        wifi_setLED(1, true);
    }
    else
    {
       fprintf(stderr, "Radio 1 is not Enabled\n");
    }
 return ANSC_STATUS_SUCCESS;
}
#endif /* _HUB4_PRODUCT_REQ_ */

ANSC_STATUS
CosaDmlWiFiFactoryReset
    (
    )
{
    int i;
    char recName[256];
    char *strValue = NULL;
    int retPsmGet = CCSP_SUCCESS;
    int resetSSID[2] = {0,0};

	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s \n",__FUNCTION__));
    for (i = 1; i <= gRadioCount; i++)
    {
        memset(recName, 0, sizeof(recName));
        snprintf(recName, sizeof(recName), FactoryResetSSID, i);
		CcspWifiTrace(("RDK_LOG_WARN,WIFI %s PSM GET for FactoryResetSSID \n",__FUNCTION__));
        retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
        if (retPsmGet == CCSP_SUCCESS)
        {
            resetSSID[i-1] = atoi(strValue);
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        }
        // Reset to 0
        PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "0");

        wifiDbgPrintf("%s: Radio %d has resetSSID = %d \n", __FUNCTION__, i, resetSSID[i-1]);
		CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Radio %d has resetSSID = %d\n",__FUNCTION__, i, resetSSID[i-1]));
    }

    // reset all SSIDs
    if ( (resetSSID[0] == COSA_DML_WIFI_FEATURE_ResetSsid1) && (resetSSID[1] == COSA_DML_WIFI_FEATURE_ResetSsid2) )
    {
        // delete current configuration
        wifi_factoryReset();

        //Clear all Wifi DB and Passpoint configurations in case of Factory Reset
        v_secure_system("rm -rf /nvram/wifi");
        v_secure_system("rm -rf /nvram/passpoint");

        // create current configuration
        // It is neccessary to first recreate the config
        // from the defaults and next we will override them with the Platform specific data
        wifi_createInitialConfigFiles();

        // These are the values PSM values that were generated from the ARM intel db
        // modidify current configuration

    #if COSA_DML_WIFI_FEATURE_LoadPsmDefaults
        for (i = 0; i < gRadioCount; i++)
        {
            CosaDmlWiFiGetRadioFactoryResetPsmData(i, i+1);
        }

        for (i = 0; i < gSsidCount; i++)
        {
            CosaDmlWiFiGetSSIDFactoryResetPsmData(i, i+1);
        }
    #endif
        CosaDmlWiFiGetBridgePsmData();

        BOOLEAN newVlanCfg = FALSE;
        CosaDmlWiFiGetResetRequired(&newVlanCfg);
        // if new Vlan configuration is required, write the new cfg version to PSM
        // wifi_init() is always called in this function
        if (newVlanCfg == TRUE)
        {
            char verString[32];
            sprintf(verString, "%d",gWifiVlanCfgVersion);
            PSM_Set_Record_Value2(bus_handle,g_Subsystem, WifiVlanCfgVersion, ccsp_string, verString);
            if (g_wifidb_rfc) {
                struct schema_Wifi_Global_Config *pcfg = NULL;
                pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
                if (pcfg != NULL) {
                    pcfg->vlan_cfg_version = gWifiVlanCfgVersion;
                    if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                        CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                        free(pcfg);
                    }
                } else {
                    CcspTraceError(("%s: WIFI DB Failed to get global config\n", __FUNCTION__ ));
                    return ANSC_STATUS_FAILURE;
                }
            }
        }
#if defined(_PLATFORM_RASPBERRYPI_) || defined(_PLATFORM_TURRIS_)
	// Reset Band Steering parameters
	int bsIndex = 0;
	for(bsIndex = 0; bsIndex < gSsidCount; bsIndex++)
	{
	    CosaDmlWiFiGetBSFactoryResetPsmData(bsIndex, bsIndex+1);
	}
#endif
    } else
    {
        // Only Apply to FactoryResetSSID list
        int ssidIndex = 0;
        for (ssidIndex = 0; ssidIndex < gSsidCount; ssidIndex++)
        {
            int radioIndex = (ssidIndex %2);
            printf("%s: ssidIndex = %d radioIndex = %d (1<<(ssidIndex/2)) & resetSSID[radioIndex] = %d \n", __FUNCTION__,  ssidIndex, radioIndex, ((1<<(ssidIndex/2)) & resetSSID[radioIndex] ));
            if ( ((1<<(ssidIndex/2)) & resetSSID[radioIndex] ) != 0)

            {
                CosaDmlWiFiGetSSIDFactoryResetPsmData(ssidIndex, ssidIndex+1);
            }
        }

    #if COSA_DML_WIFI_FEATURE_LoadPsmDefaults
        // Reset radio parameters
        wifi_factoryResetRadios();
        for (i = 0; i < gRadioCount; i++)
        {
            CosaDmlWiFiGetRadioFactoryResetPsmData(i, i+1);
        }
    #endif
    }

#if !defined (_HUB4_PRODUCT_REQ_)
    const char *meshAP = "/usr/ccsp/wifi/meshapcfg.sh"; 
    //Bring Mesh AP up after captive portal configuration
    if( access( meshAP, F_OK) != -1)
    {
      printf("Bringing up mesh interface after factory reset\n");
      v_secure_system("/usr/ccsp/wifi/meshapcfg.sh");
    }
#endif /* * _HUB4_PRODUCT_REQ_ */
    wifi_getApSsidAdvertisementEnable(0, &AdvEnable24);
    wifi_getApSsidAdvertisementEnable(1, &AdvEnable5);
    // Bring Radios Up again if we aren't doing PowerSaveMode
    if ( gRadioPowerSetting != COSA_DML_WIFI_POWER_DOWN &&
         gRadioNextPowerSetting != COSA_DML_WIFI_POWER_DOWN ) {
        //printf("******%s***Initializing wifi 3\n",__FUNCTION__);
        wifi_setLED(0, false);
        wifi_setLED(1, false);

        fprintf(stderr, "-- wifi_setLED off\n");
		wifi_setLFSecurityKeyPassphrase();
#if !defined(_COSA_BCM_MIPS_)&& !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_) && !defined(_INTEL_WAV_)
        wifi_pushSsidAdvertisementEnable(0, false);
        wifi_pushSsidAdvertisementEnable(1, false);
//Home Security is currently not supported for Raspberry Pi platform
#if !defined(_PLATFORM_RASPBERRYPI_) && !defined(_PLATFORM_TURRIS_)
        pthread_attr_t attr;
        pthread_attr_t *attrp = NULL;
        pthread_t tid4;
        attrp = &attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );		
        pthread_create(&tid4, attrp, &wait_for_brlan1_up, NULL);
        if(attrp != NULL)
            pthread_attr_destroy( attrp );
#endif
#endif

#if defined (_HUB4_PRODUCT_REQ_)
	//Needs to enable WiFi LED after reset
	CosaDmlWiFiCheckAndConfigureLEDS( );
#endif /* _HUB4_PRODUCT_REQ_ */
    }
    // As restart removed from the Hal layer, forcing the restart here
    m_wifi_init();

    // Set FixedWmmParams to TRUE on Factory Reset so that we won't override the data.
    // There were two required changes.  Set to 3 so that we know neither needs to be applied
    PSM_Set_Record_Value2(bus_handle,g_Subsystem, FixedWmmParams, ccsp_string, "3");
    /*CcspWifiEventTrace(("RDK_LOG_NOTICE, KeyPassphrase changed in Factory Reset\n"));*/
    if (g_wifidb_rfc) {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            pcfg->fixed_wmm_params = 3; 
            if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                free(pcfg);
            }
        } else {
            CcspTraceError(("%s: WIFI DB Failed to get global config\n", __FUNCTION__ ));
            return ANSC_STATUS_FAILURE;
        }
    }
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiFactoryResetRadioAndAp( ULONG radioIndex, ULONG apIndex, BOOL needRestart) {
    
	if(radioIndex>0) {
fprintf(stderr, "+++++++++++++++++++++ wifi_factoryResetRadio %lu\n", radioIndex-1);
		wifi_factoryResetRadio(radioIndex-1);
#if COSA_DML_WIFI_FEATURE_LoadPsmDefaults
                CosaDmlWiFiGetRadioFactoryResetPsmData(radioIndex-1, radioIndex);
#endif
    }
	if(apIndex>0) {
fprintf(stderr, "+++++++++++++++++++++ wifi_factoryResetAP %lu\n", apIndex-1);
		wifi_factoryResetAP(apIndex-1);
#if COSA_DML_WIFI_FEATURE_LoadPsmDefaults
                CosaDmlWiFiGetSSIDFactoryResetPsmData(apIndex-1, apIndex); 
#endif
	}
    // Bring Radios Up again if we aren't doing PowerSaveMode
    if (needRestart &&
		gRadioPowerSetting != COSA_DML_WIFI_POWER_DOWN &&
        gRadioNextPowerSetting != COSA_DML_WIFI_POWER_DOWN ) {
fprintf(stderr, "+++++++++++++++++++++ wifi_init\n");		
		wifi_setLFSecurityKeyPassphrase();
        m_wifi_init();
    }

    return ANSC_STATUS_SUCCESS;
}

static void *CosaDmlWiFiResetRadiosThread(void *arg) 
{
    printf("%s Calling pthread_mutex_lock for sWiFiThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 
    pthread_mutex_lock(&sWiFiThreadMutex);
    printf("%s Called pthread_mutex_lock for sWiFiThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 
    UNREFERENCED_PARAMETER(arg);
    // Restart Radios again if we aren't doing PowerSaveMode
    if ( gRadioPowerSetting != COSA_DML_WIFI_POWER_DOWN &&
         gRadioNextPowerSetting != COSA_DML_WIFI_POWER_DOWN ) {
        printf("%s: Calling wifi_reset  \n", __func__);
        //zqiu: wifi_reset has bug
		wifi_reset();
		//wifi_down();
		//m_wifi_init();
		
        wifiDbgPrintf("%s Calling Initialize() \n",__FUNCTION__);

        //Reset Telemetry statistics of all wifi clients for all vaps (Per Radio), while Radio reset is triggered
        if (radio_stats_flag_change(0, false) != ANSC_STATUS_SUCCESS) {
            wifiDbgPrintf("%s Error in clearing 2GHZ radio monitor stats",__FUNCTION__);
        }

        if (radio_stats_flag_change(1, false) != ANSC_STATUS_SUCCESS) {
            wifiDbgPrintf("%s Error in clearing 5GHZ radio monitor stats",__FUNCTION__);
        }
        pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;

        CosaWifiReInitialize((ANSC_HANDLE)pMyObject, 0);
        CosaWifiReInitialize((ANSC_HANDLE)pMyObject, 1);
        wifiDbgPrintf("%s Called Initialize() \n",__FUNCTION__);
        pthread_cond_signal(&reset_done);
    }

    printf("%s Calling pthread_mutex_unlock for sWiFiThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 
    pthread_mutex_unlock(&sWiFiThreadMutex);
    printf("%s Called pthread_mutex_unlock for sWiFiThreadMutex  %d \n",__FUNCTION__ , __LINE__ );  

    return(NULL);
}

ANSC_STATUS
CosaDmlWiFi_ResetRadios
    (
    )
{
    printf("%s: \n", __func__);
    {
        pthread_t tid; 

        printf("%s Reset WiFi in background.  Process will take upto 90 seconds to complete  \n",__FUNCTION__ ); 
        pthread_attr_t attr;
        pthread_attr_t *attrp = NULL;

        attrp = &attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );

        if (pthread_create(&tid,attrp,CosaDmlWiFiResetRadiosThread,NULL))
        {
            if(attrp != NULL)
                pthread_attr_destroy( attrp );
            return ANSC_STATUS_FAILURE;
        }
       Update_Hotspot_MacFilt_Entries(false);
       if(attrp != NULL)
            pthread_attr_destroy( attrp );
    }
    return ANSC_STATUS_SUCCESS;
}

static void CosaDmlWiFiCheckWmmParams
    (
    )
{
    char recName[256];
    char *strValue = NULL;
    int retPsmGet = CCSP_SUCCESS;
    BOOL resetNoAck = FALSE;

printf("%s \n",__FUNCTION__);

    // if the value is FALSE or not present WmmNoAck values should be reset
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, FixedWmmParams, NULL, &strValue);
    if (!g_wifidb_rfc) {
    if (retPsmGet == CCSP_SUCCESS) {
        int value = atoi(strValue);
        if (value != 3) {
            resetNoAck = TRUE;
        }
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    } else {
        resetNoAck = TRUE;
    }
    } else {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            if(pcfg->fixed_wmm_params != 3) {
                resetNoAck = TRUE;
            }
            free(pcfg);
        } else {
            resetNoAck = TRUE;
        }
    }
    // Force NoAck to 1 for now.  There are upgrade/downgrade issues that sometimes cause an issue with the values being inconsistent 
    resetNoAck = TRUE;

    if (resetNoAck == TRUE) {
        int i;
        struct schema_Wifi_VAP_Config  *pcfg= NULL;
        printf("%s: Resetting Wmm parameters \n",__FUNCTION__);

        for (i =0; i < 16; i++) {
            memset(recName, 0, sizeof(recName));
            snprintf(recName, sizeof(recName), WmmEnable, i+1);
            PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "1");
            
            memset(recName, 0, sizeof(recName));
#if defined(ENABLE_FEATURE_MESHWIFI)
            // Turn off power save for mesh access points (ath12 & ath13)
            if (i == 12 || i == 13) {
                snprintf(recName, sizeof(recName), UAPSDEnable, i+1);
                PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "0");
            } else {
                snprintf(recName, sizeof(recName), UAPSDEnable, i+1);
                PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "1");
            }
#else
            snprintf(recName, sizeof(recName), UAPSDEnable, i+1);
            PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "1");
#endif
            // For Backwards compatibility with 1.3 versions, the PSM value for NoAck must be 1
            // When set/get from the PSM to DML the value must be interperted to the opposite
            // 1->0 and 0->1
            memset(recName, 0, sizeof(recName));
            snprintf(recName, sizeof(recName), WmmNoAck, i+1);
            PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "1");
             if (g_wifidb_rfc) {
                pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[i], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
                if (pcfg != NULL) {
                    pcfg->wmm_enabled = TRUE;
                    if (i == 12 || i == 13) {
                        pcfg->uapsd_enabled = FALSE;
                    } else {
                        pcfg->uapsd_enabled = TRUE;
                    }
                    pcfg->wmm_noack = 1;
                    if (wifi_ovsdb_update_table_entry(vap_names[i],"vap_name",OCLM_STR,&table_Wifi_VAP_Config,pcfg,filter_vaps) <= 0) {
                        CcspWifiTrace(("RDK_LOG_ERROR,%s: WIFI DB Failed to update vap config\n",__FUNCTION__ ));
                    }
                }
            }
        }
    }

    // Set FixedWmmParams to TRUE so that we won't override the data again.
    PSM_Set_Record_Value2(bus_handle,g_Subsystem, FixedWmmParams, ccsp_string, "3");
    if (g_wifidb_rfc) {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            pcfg->fixed_wmm_params = 3;
            if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                free(pcfg);
            }
        } else {
            CcspTraceError(("%s: WIFI DB Failed to get global config\n", __FUNCTION__ ));
            return; 
        }
    }
}
/*zqiu
static void CosaDmlWiFiCheckSecurityParams
(
)
{
    char recName[256];
    char *strValue = NULL;
    int retPsmGet = CCSP_SUCCESS;
    BOOL resetNoAck = FALSE;
    int wlanIndex;
    char ssid[64];
    unsigned int wpsPin;
    char pskKey[64];

    printf("%s \n",__FUNCTION__);

    for (wlanIndex = 0; wlanIndex < 2; wlanIndex++)
    {
        wpsPin = 0;
        wlan_getWpsDevicePassword(wlanIndex,&wpsPin);
        printf("%s  called wlan_getWpsDevicePassword on ath%d\n",__FUNCTION__, wlanIndex);
        if (wpsPin == 0)
        {
            unsigned int password = 0;
            retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, WpsPin, NULL, &strValue);
            if (retPsmGet == CCSP_SUCCESS)
            {
                password = _ansc_atoi(strValue);
                wlan_setWpsDevicePassword(wlanIndex, password);
                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
            }
        }

        pskKey[0] = '\0';
        wlan_getKeyPassphrase(wlanIndex,pskKey);
        if (strlen(pskKey) == 0)
        {
            memset(recName, 0, sizeof(recName));
            sprintf(recName, Passphrase, wlanIndex+1);
            retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
            if (retPsmGet == CCSP_SUCCESS)
            {
                wlan_setKeyPassphrase(wlanIndex, strValue);
                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
            }
        }
    }
}
*/

int DeleteMacFilter(int AccessPointIndex, int MacfilterInstance)
{
    char recName[256];

    snprintf(recName, sizeof(recName), MacFilter, AccessPointIndex, MacfilterInstance);
    PSM_Del_Record(bus_handle,g_Subsystem, recName);
    snprintf(recName, sizeof(recName), MacFilterDevice, AccessPointIndex, MacfilterInstance);
    PSM_Del_Record(bus_handle,g_Subsystem, recName);
    return 0;
}

static void str_to_mac_bytes (char *key, mac_addr_t bmac) {
   unsigned int mac[6];
   if(strlen(key) > MIN_MAC_LEN)
       sscanf(key, "%02x:%02x:%02x:%02x:%02x:%02x",
             &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
   else
       sscanf(key, "%02x%02x%02x%02x%02x%02x",
             &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
   bmac[0] = mac[0]; bmac[1] = mac[1]; bmac[2] = mac[2];
   bmac[3] = mac[3]; bmac[4] = mac[4]; bmac[5] = mac[5];

}

BOOL Validate_InstClientMac(char * physAddress)
{

    CcspWifiTrace(("RDK_LOG_WARN, %s-%d mac is ***%s***\n",__FUNCTION__,__LINE__, physAddress));
    if (physAddress && physAddress[0]) {
        if (strlen(physAddress) != MIN_MAC_LEN)
        {
            CcspWifiTrace(("RDK_LOG_WARN, %s-%d mac length is not 12\n",__FUNCTION__,__LINE__));
            return FALSE;
        }

        if (!strcmp(physAddress,"000000000000"))
        {
            CcspWifiTrace(("RDK_LOG_WARN, %s-%d mac is all 0\n",__FUNCTION__,__LINE__));
            return FALSE;
        }

        return TRUE;
    }
    CcspWifiTrace(("RDK_LOG_WARN, %s-%d mac is NULL\n",__FUNCTION__,__LINE__));
    return FALSE;
}

BOOL Validate_mac(char * physAddress)
{
    unsigned int MacMaxsize = 17;
    if (physAddress && physAddress[0]) {
        if (strlen(physAddress) != MacMaxsize)
        {
            return FALSE;
        }
        if (!strcmp(physAddress,"00:00:00:00:00:00"))
        {
            return FALSE;
        }
        if(physAddress[2] == ':')
        if(physAddress[5] == ':')
            if(physAddress[8] == ':')
                if(physAddress[11] == ':')
                    if(physAddress[14] == ':')
                      return TRUE;
    }

    return FALSE;
}


BOOL IsValidMacfilter(int AccessPointIndex, int MacfilterInstance)
{
    char recName[256];
    char *MacFilterParam = NULL;
    int retPsmGet = CCSP_SUCCESS;
    BOOL valid = FALSE;

    snprintf(recName, sizeof(recName), MacFilter, AccessPointIndex, MacfilterInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &MacFilterParam);
    if ((retPsmGet == CCSP_SUCCESS) && (MacFilterParam) && (strlen(MacFilterParam) > 0))
    {
        if (Validate_mac(MacFilterParam))
        {
            valid = TRUE;
        }

    }

    if (MacFilterParam)
    {
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(MacFilterParam);
    }
    return valid;
}

int RemoveInvalidMacFilterList(int ulinstance)
{
    char out[128];
    char newbuf[256];
    int index = 0;
    int *index_list = NULL;
    int count = 0;
    int valid_entry = 0;
    char *start = NULL;
    char *end = NULL;
    char recname[256];
    char *macfilterlistparam = NULL;
    int retpsmget = CCSP_SUCCESS;
    int retpsmset = CCSP_SUCCESS;

    snprintf(recname, sizeof(recname), MacFilterList, ulinstance);
    retpsmget = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recname, NULL, &macfilterlistparam);
    /*
     * RDKB3939-878
     * Logic:
     * 1. Getting value from MacFilterList record (%d.MacFilterList)
     * 2. Check whether [%d.MacFilter.%d]entry for the each instance value of MacFilterList.
     * 3. Remove the MacFilter and MacFilterDevice record,
     *     if mac is invalid i.e (either [%d.MacFilter.%d] is empty or entry not available in psm)
     * 4. Reupdate the valid MacFilter instance values into MacFilterList record of PSM.
     */
    if (retpsmget == CCSP_SUCCESS && (macfilterlistparam))
    {
        start = macfilterlistparam;
        end =  strstr(macfilterlistparam,":");
        if (end)
            *end = '\0';
        count = atoi(start);
        start = end + 1;
        CcspTraceInfo(("MacFilterList count %d for AP %d\n",count,ulinstance));
        if (count > 0)
        {
            index_list = (int*)malloc(sizeof(int) * count);
            if (!index_list)
            {
                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(macfilterlistparam);
                return -1;
            }
            while (start)
            {
                end = strstr(start,",");
                if (end)
                {
                    *end = '\0';
                }
                else
                {
                    if (start)
                    {
                        index = atoi(start);
                        if (IsValidMacfilter(ulinstance,index))
                        {
                            index_list[valid_entry] = index;
                            ++valid_entry;
                        }
                        else
                        {
                            DeleteMacFilter(ulinstance,index);
                        }

                    }
                    break;
                }
                index = atoi(start);
                if (IsValidMacfilter(ulinstance,index))
                {
                    index_list[valid_entry] = index;
                    ++valid_entry;
                }
                else
                {
                    DeleteMacFilter(ulinstance,index);
                }

                start = end + 1;
            }
        }
        if (index_list)
        {
            int i = 0;
            snprintf(newbuf,sizeof(newbuf),"%d:",valid_entry);
            for (i = 0; i < valid_entry - 1; ++i)
            {
                snprintf(out,sizeof(out),"%d,",index_list[i]);
                strcat(newbuf,out);
            }
            if ( i < valid_entry)
            {
                snprintf(out,sizeof(out),"%d",index_list[i]);
                strcat(newbuf,out);
            }
            CcspTraceInfo(("updated AP %d MacFilterList val--> %s\n",ulinstance,newbuf));
            snprintf(recname, sizeof(recname), MacFilterList, ulinstance);
            retpsmset = PSM_Set_Record_Value2(bus_handle, g_Subsystem, recname, ccsp_string, newbuf);
            if (retpsmset != CCSP_SUCCESS)
            {
                CcspTraceWarning(("MacFilterList set status %d\n",retpsmset));
            }
            free(index_list);
            index_list = NULL;
        }
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(macfilterlistparam);
    }
    else
    {
        CcspTraceWarning(("MacFilterList get status %d instance %d\n",retpsmget,ulinstance));
        return -1;
    }
    return 0;
}

void* RemoveInvalidMacFilterListFromPsm()
{
    pthread_detach(pthread_self());
    int i = 0;
    for(i=0 ; i<HOTSPOT_NO_OF_INDEX ; i++)
    {
        RemoveInvalidMacFilterList(Hotspot_Index[i]);
    }
	return NULL;
}

void wifi_db_rfc_event_callback(char *info, void *data)
{
    int retval;
    CcspTraceInfo(("%s : WifiDb status received %s Data %s\n",__FUNCTION__,info, (char *) data));
    if ((int)g_wifidb_rfc != atoi((const char *)info)) {
        g_wifidb_rfc = atoi((const char *)info);
        if (g_wifidb_rfc) {
           retval = wifi_db_update_psm_values();
           if(retval == RETURN_OK) {
               CcspTraceInfo(("%s : Updated wifi db  successfully\n",__FUNCTION__));
           }
        }
    }
    return;
}

void RegisterWifiDbRfcCallback()
{
    int ret;

    ret = CcspBaseIf_Register_Event(bus_handle,NULL,"WifiDbStatus");
    if (ret != CCSP_SUCCESS) {
        CcspTraceError(("%s Failed to register for WifiDb status notification event",__FUNCTION__));
        return;
    }
    CcspBaseIf_SetCallback2(bus_handle, "WifiDbStatus", wifi_db_rfc_event_callback, NULL);
    return;
}

ANSC_STATUS
CosaDmlWiFiInit
    (
        ANSC_HANDLE                 hDml,
        PANSC_HANDLE                phContext
    )
{
printf("%s \n",__FUNCTION__);
    BOOLEAN factoryResetFlag = FALSE;
    pthread_t tidbootlog;
    static BOOL firstTime = TRUE;
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI) phContext;
    UNREFERENCED_PARAMETER(hDml);
    char *strValue = NULL;
    int retPsmGet = CCSP_SUCCESS;

    UNREFERENCED_PARAMETER(hDml);
    CosaDmlWiFiGetDFSAtBootUp(&(pMyObject->bDFSAtBootUp));

    CosaDmlWiFiGetFactoryResetPsmData(&factoryResetFlag);
    if (factoryResetFlag == TRUE) {
#if 0
#if defined(_COSA_INTEL_USG_ATOM_) && !defined(INTEL_PUMA7) && !defined(_PLATFORM_RASPBERRYPI_) && !defined(_PLATFORM_TURRIS_)
        // This is kind of a weird case. If a factory reset has been performed, we need to make sure
        // that the syscfg.db file has been cleared on the ATOM side since a PIN reset only
        // clears out the ARM side version.
        system("/usr/bin/syscfg_destroy -f");
        if ( system("rm -f /nvram/syscfg.db;echo -n > /nvram/syscfg.db;/usr/bin/syscfg_create -f /nvram/syscfg.db") != 0 ) {
            CcspWifiTrace(("RDK_LOG_WARN,WIFI %s Unable to remove syscfg.db during factory reset",__FUNCTION__));
        } else {
            CcspWifiTrace(("RDK_LOG_WARN,WIFI %s Removed syscfg.db for factory reset",__FUNCTION__));
        }
#endif
#endif
printf("%s: Calling CosaDmlWiFiFactoryReset \n",__FUNCTION__);
	CosaDmlWiFiFactoryReset();
printf("%s: Called CosaDmlWiFiFactoryReset \n",__FUNCTION__);
        // Set to FALSE after FactoryReset has been applied
	PSM_Set_Record_Value2(bus_handle,g_Subsystem, FactoryReset, ccsp_string, "0");
printf("%s: Reset FactoryReset to 0 \n",__FUNCTION__);
    }

#if defined(DUAL_CORE_XB3) || defined(CISCO_XB3_PLATFORM_CHANGES) || defined (_XB6_PRODUCT_REQ_) || defined (_COSA_BCM_MIPS_) || defined (_HUB4_PRODUCT_REQ_)
    pthread_t tid4;
#endif
    // Only do once and store BSSID and MacAddress in memory
    if (firstTime == TRUE) {
        #if defined (FEATURE_SUPPORT_PASSPOINT) && defined(ENABLE_FEATURE_MESHWIFI)
    //OVSDB Start
            start_ovsdb();
            init_ovsdb_tables();
       #endif

        retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.WiFi-PSM-DB.Enable", NULL, &strValue);
        if (retPsmGet == CCSP_SUCCESS) {
            g_wifidb_rfc = _ansc_atoi(strValue);
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
            if (g_wifidb_rfc) {
                wifi_db_init();
            }
        }
#if defined (FEATURE_SUPPORT_INTERWORKING)
    //RDKB-33024: Cleanup all existing PSM entries
    CosaDmlWiFiPsmDelInterworkingEntry();
#endif

        firstTime = FALSE;

#if defined (_COSA_BCM_MIPS_) || defined (_PLATFORM_RASPBERRYPI_)|| defined (_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_) || defined(_INTEL_WAV_)
		//Scott: Broadcom hal needs wifi_init to be called when we are started up
		//wifi_setLFSecurityKeyPassphrase();
		m_wifi_init();
#endif

        //zqiu: do not merge
//        CosaDmlWiFiCheckSecurityParams();
        //CosaDmlWiFiCheckWmmParams();

        //>>zqiu
		// Fill Cache
        //CosaDmlWiFiSsidFillSinfoCache(NULL, 1);
        //CosaDmlWiFiSsidFillSinfoCache(NULL, 2);
		//<<
		
        // Temporary fix for HotSpot builds prior to 11/22/2013 had a bug that cuased the 
        // HotSpot param of the Primary SSIDs to be set to 1.  
        BOOLEAN resetHotSpot = FALSE;
        CosaDmlWiFiGetResetHotSpotRequired(&resetHotSpot);
        // if new Vlan configuration is required, write the new cfg version to PSM 
        if (resetHotSpot == TRUE) {      
            char recName[256];

            int i;
            for (i = 1; i <= gRadioCount; i++) {
                snprintf(recName, sizeof(recName), BssHotSpot, i);
                PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "0");
                if (g_wifidb_rfc) {
                    struct schema_Wifi_VAP_Config  *pcfg= NULL;

                    pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[i-1], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
                    if (pcfg != NULL) {
                        pcfg->bss_hotspot = 0;
                        if (wifi_ovsdb_update_table_entry(vap_names[i-1],"vap_name",OCLM_STR,&table_Wifi_VAP_Config,pcfg,filter_vaps) <= 0) {
                            CcspWifiTrace(("RDK_LOG_ERROR,%s: WIFI DB Failed to update vap config\n",__FUNCTION__ ));
                        }
                    }
                }
#if !defined (_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
                wifi_setApEnableOnLine(i-1,0);
#endif
            }            
        }

            wifi_getApSsidAdvertisementEnable(0, &AdvEnable24);
            wifi_getApSsidAdvertisementEnable(1, &AdvEnable5);
        // If no VAPs were up or we have new Vlan Cfg re-init both Radios
        if ( resetHotSpot == TRUE && 
             gRadioPowerSetting != COSA_DML_WIFI_POWER_DOWN &&
             gRadioNextPowerSetting != COSA_DML_WIFI_POWER_DOWN ) {
            //printf("%s: calling wifi_init 1 \n", __func__);
            wifi_setLED(0, false);
            wifi_setLED(1, false);
            fprintf(stderr, "-- wifi_setLED off\n");
			wifi_setLFSecurityKeyPassphrase();
			//CosaDmlWiFi_SetRegionCode(NULL);
            m_wifi_init();

#if !defined(_COSA_BCM_MIPS_)&& !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_) && !defined(_INTEL_WAV_)
            wifi_pushSsidAdvertisementEnable(0, false);
            wifi_pushSsidAdvertisementEnable(1, false);

            pthread_attr_t attr;
            pthread_attr_t *attrp = NULL;
            attrp = &attr;
           pthread_attr_init(&attr);
           pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
		   /* crate a thread and wait */
    	   pthread_create(&tid4, attrp, &wait_for_brlan1_up, NULL);
           if(attrp != NULL)
                pthread_attr_destroy( attrp );
#endif
        }

        BOOLEAN noEnableVaps = TRUE;
                BOOL radioActive = TRUE;
        wifi_getRadioStatus(0, &radioActive);
        printf("%s: radioActive wifi0 = %s \n", __func__, (radioActive == TRUE) ? "TRUE" : "FALSE");
        if (radioActive == TRUE) {
            noEnableVaps = FALSE;
        }
        wifi_getRadioStatus(1,&radioActive);
        printf("%s: radioActive wifi1 = %s \n", __func__, (radioActive == TRUE) ? "TRUE" : "FALSE");
        if (radioActive == TRUE) {
            noEnableVaps = FALSE;
        }
        printf("%s: noEnableVaps = %s \n", __func__, (noEnableVaps == TRUE) ? "TRUE" : "FALSE");

        CosaDmlWiFiGetBridgePsmData();
        BOOLEAN newVlanCfg = FALSE;
        CosaDmlWiFiGetResetRequired(&newVlanCfg);
        // if new Vlan configuration is required, write the new cfg version to PSM 
        if (newVlanCfg == TRUE) {
            char verString[32];
            sprintf(verString, "%d",gWifiVlanCfgVersion);
            PSM_Set_Record_Value2(bus_handle,g_Subsystem, WifiVlanCfgVersion, ccsp_string, verString);
            if (g_wifidb_rfc) {
                struct schema_Wifi_Global_Config *pcfg = NULL;
                pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
                if (pcfg != NULL) {
                    pcfg->vlan_cfg_version = gWifiVlanCfgVersion;
                    if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                        CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                       free(pcfg);
                    }
                } else {
                    CcspTraceError(("%s: WIFI DB Failed to get global config\n", __FUNCTION__ ));
                    return ANSC_STATUS_FAILURE;
                }
            }
        }

        // If no VAPs were up or we have new Vlan Cfg re-init both Radios
        if ( (noEnableVaps == TRUE || newVlanCfg == TRUE)  &&
             gRadioPowerSetting != COSA_DML_WIFI_POWER_DOWN &&
             gRadioNextPowerSetting != COSA_DML_WIFI_POWER_DOWN ) {
            //printf("%s: calling wifi_init 2 \n", __func__);

            wifi_setLED(0, false);
            wifi_setLED(1, false);
            fprintf(stderr, "-- wifi_setLED off\n");
			wifi_setLFSecurityKeyPassphrase();
			//CosaDmlWiFi_SetRegionCode(NULL);
            m_wifi_init();
#if !defined(_COSA_BCM_MIPS_)&& !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_) && !defined(_INTEL_WAV_)
            wifi_pushSsidAdvertisementEnable(0, false);
            wifi_pushSsidAdvertisementEnable(1, false);
//Home Security is currently not supported for Raspberry Pi platform
#if !defined(_PLATFORM_RASPBERRYPI_) && !defined(_PLATFORM_TURRIS_)
            pthread_attr_t attr;
            pthread_attr_t *attrp = NULL;

            attrp = &attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
            pthread_create(&tid4, attrp, &wait_for_brlan1_up, NULL);
            if(attrp != NULL)
                pthread_attr_destroy( attrp );
#endif
#endif
        }

//XB6 phase 1 lost and Found
#ifdef _XB6_PRODUCT_REQ_
            pthread_attr_t attr;
            pthread_attr_t *attrp = NULL;

            attrp = &attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
	    pthread_create(&tid4, attrp, &wait_for_brlan1_up, NULL);
            if(attrp != NULL)
                pthread_attr_destroy( attrp );
#elif defined(_COSA_BCM_MIPS_) || defined (_HUB4_PRODUCT_REQ_)
        pthread_attr_t attr;
        pthread_attr_t *attrp = NULL;

        attrp = &attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
        // Startup brlan1 thread
        pthread_create(&tid4, attrp, &wait_for_brlan1_up, NULL);
        if(attrp != NULL)
            pthread_attr_destroy( attrp );
#endif
         RegisterWifiDbRfcCallback();
    }


    BOOL retInit = Cosa_Init(bus_handle);
    printf("%s: Cosa_Init returned %s \n", __func__, (retInit == 1) ? "True" : "False");

#if defined(ENABLE_FEATURE_MESHWIFI)
	wifi_handle_sysevent_async();
#endif
	CosaDmlWiFi_startHealthMonitorThread();
#if !defined(_XF3_PRODUCT_REQ_)
    CosaDmlWiFiCheckPreferPrivateFeature(&(pMyObject->bPreferPrivateEnabled));
#endif
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
    CosaDmlWiFiCheckEnableRadiusGreylist(&(pMyObject->bEnableRadiusGreyList));
#endif

    CosaDmlWiFi_GetGoodRssiThresholdValue(&(pMyObject->iX_RDKCENTRAL_COM_GoodRssiThreshold));
    CosaDmlWiFi_GetAssocCountThresholdValue(&(pMyObject->iX_RDKCENTRAL_COM_AssocCountThreshold));
    CosaDmlWiFi_GetAssocMonitorDurationValue(&(pMyObject->iX_RDKCENTRAL_COM_AssocMonitorDuration));
    CosaDmlWiFi_GetAssocGateTimeValue(&(pMyObject->iX_RDKCENTRAL_COM_AssocGateTime));
    CosaDmlWiFi_GetWiFiReservedSSIDNames(pMyObject->ReservedSSIDNames); //LGI addition for forbidden ssid DM
   
    if (!updateBootTimeRunning) { 
        pthread_attr_t attr;
        pthread_attr_t *attrp = NULL;

        attrp = &attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
        pthread_create(&tidbootlog, attrp, &updateBootLogTime, NULL);
        if(attrp != NULL)
            pthread_attr_destroy( attrp );
        updateBootTimeRunning = TRUE;
    }
    CosaDmlWiFi_GetFeatureMFPConfigValue( &(pMyObject->bFeatureMFPConfig) );

    CosaDmlWiFi_GetRapidReconnectIndicationEnable(&(pMyObject->bRapidReconnectIndicationEnabled), true);
    CosaDmlWiFiGetvAPStatsFeatureEnable(&(pMyObject->bX_RDKCENTRAL_COM_vAPStatsEnable));
    CosaDmlWiFiGetTxOverflowSelfheal(&(pMyObject->bTxOverflowSelfheal));
#if !defined(_PLATFORM_RASPBERRYPI_) && !defined(_PLATFORM_TURRIS_)
    CosaDmlWiFiGetForceDisableWiFiRadio(&(pMyObject->bForceDisableWiFiRadio));
#endif
#if defined(FEATURE_HOSTAP_AUTHENTICATOR)
    CosaDmlWiFiGetHostapdAuthenticatorEnable(&(pMyObject->bEnableHostapdAuthenticator));
#endif
    CosaDmlWiFiGetDFS(&(pMyObject->bDFS));

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiRegionInit
  (
	PCOSA_DATAMODEL_RDKB_WIFIREGION PWiFiRegion
  )
{
    char *strValue = NULL;

    if (!PWiFiRegion)
    {
        CcspTraceWarning(("%s-%d : NULL param\n" , __FUNCTION__, __LINE__ ));
        return ANSC_STATUS_FAILURE;
    }

#if defined(_COSA_BCM_MIPS_) || defined(_XB6_PRODUCT_REQ_) || defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_)
    memset(PWiFiRegion->Code.ActiveValue, 0, sizeof(PWiFiRegion->Code.ActiveValue));

    if (!g_wifidb_rfc) {
    if (PSM_Get_Record_Value2(bus_handle, g_Subsystem, TR181_WIFIREGION_Code, NULL, &strValue) != CCSP_SUCCESS)
    {
        AnscCopyString(PWiFiRegion->Code.ActiveValue, "USI");
    }

    if(strValue) {
        AnscCopyString(PWiFiRegion->Code.ActiveValue, strValue);
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    } else {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            AnscCopyString(PWiFiRegion->Code.ActiveValue, pcfg->wifi_region_code);
            free(pcfg);
        } else {
            AnscCopyString(PWiFiRegion->Code.ActiveValue, "USI");
        }
    }
    CosaWiFiInitializeParmUpdateSource(PWiFiRegion);
#else
    memset(PWiFiRegion->Code, 0, sizeof(PWiFiRegion->Code));

    if (!g_wifidb_rfc) {
    if (PSM_Get_Record_Value2(bus_handle, g_Subsystem, TR181_WIFIREGION_Code, NULL, &strValue) != CCSP_SUCCESS)
    {
        AnscCopyString(PWiFiRegion->Code, "USI");
    }

    if(strValue) {
        AnscCopyString(PWiFiRegion->Code, strValue);
		((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    } else {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            AnscCopyString(PWiFiRegion->Code, pcfg->wifi_region_code);
            free(pcfg);
        } else {
            AnscCopyString(PWiFiRegion->Code, "USI");
        }
    }
#endif

    return ANSC_STATUS_SUCCESS;
}

static ANSC_STATUS
CosaDmlWiFi_PsmSaveRegionCode(char *code) 
{
    int retPsmGet = CCSP_SUCCESS;
    int rc = -1;
     /* Updating the WiFiRegion Code in PSM database  */
    retPsmGet = PSM_Set_Record_Value2(bus_handle, g_Subsystem, TR181_WIFIREGION_Code, ccsp_string, code);
    if (retPsmGet != CCSP_SUCCESS) 
	{
        CcspTraceError(("Set failed for WiFiRegion Code \n"));
		return ANSC_STATUS_FAILURE;
    }
    if (g_wifidb_rfc) {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            rc = strcpy_s(pcfg->wifi_region_code,sizeof(pcfg->wifi_region_code),code);
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
            }
            if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                free(pcfg);
            }
        } else {
            CcspTraceError(("%s: WIFI DB Failed to get global config\n", __FUNCTION__ ));
            return ANSC_STATUS_FAILURE;
        }
    }

    return ANSC_STATUS_SUCCESS;
}

static ANSC_STATUS
CosaDmlWiFi_SetRegionCode(char *code) {
        char countryCode0[4] = {0};
        char countryCode1[4] = {0};

        if(code==NULL)
		return ANSC_STATUS_FAILURE;

        /* Check if country codes are already updated in wifi hal */
        wifi_getRadioCountryCode(0, countryCode0);
        wifi_getRadioCountryCode(1, countryCode1);

        if((strcmp(countryCode0, code) != 0 ) || (strcmp(countryCode1, code) != 0 ))
        {
			int retCodeForRadio1 = 0,
				retCodeForRadio2 = 0;
			
            retCodeForRadio1 = wifi_setRadioCountryCode( 0, code );
            retCodeForRadio2 = wifi_setRadioCountryCode( 1, code );

			//Check the HAL error code. If failure return error or return success
			if( ( 0 != retCodeForRadio1 ) || ( 0 != retCodeForRadio2 ) )
			{
				CcspTraceError(("%s Failed to set WiFiRegion Code[%s] Ret:[%d,%d]\n", __FUNCTION__, code, retCodeForRadio1, retCodeForRadio2 ));
				return ANSC_STATUS_FAILURE;
			}
        }

        return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS SetWiFiRegionCode(char *code)
{
	if ( ANSC_STATUS_SUCCESS == CosaDmlWiFi_SetRegionCode( code ) )
	{
		if ( ANSC_STATUS_SUCCESS == CosaDmlWiFi_PsmSaveRegionCode( code ) )
		{
			return ANSC_STATUS_SUCCESS;
		}
	}

	return ANSC_STATUS_FAILURE;
}

void AssociatedDevice_callback_register()
{
	pthread_mutex_lock(&g_apRegister_lock);
	wifi_newApAssociatedDevice_callback_register(CosaDmlWiFi_AssociatedDevice_callback);
#if !defined(_PLATFORM_RASPBERRYPI_) && !defined(_PLATFORM_TURRIS_)
	wifi_apDisassociatedDevice_callback_register(CosaDmlWiFi_DisAssociatedDevice_callback);
#endif
	pthread_mutex_unlock(&g_apRegister_lock);
}

static ANSC_STATUS
CosaDmlWiFiPsmDelMacFilterTable( ULONG ulInstance )
{
    char recName[256];
    char *strValue = NULL;
    int retPsmGet;
   
wifiDbgPrintf("%s ulInstance = %lu\n",__FUNCTION__, ulInstance);

    snprintf(recName, sizeof(recName),MacFilterList, ulInstance);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        int numFilters = 0;
        int macInstance = 0;

        if (strlen(strValue) > 0) {
            char *start = strValue;
            char *end = NULL;
        
            end = strValue + strlen(strValue);

            if ((end = strstr(strValue, ":" ))) {
                *end = 0;
             
		numFilters = _ansc_atoi(start);
		wifiDbgPrintf("%s numFilters = %d \n",__FUNCTION__, numFilters);
                start = end+1;

                if (numFilters > 0 && strlen(start) > 0) {
		    wifiDbgPrintf("%s filterList = %s \n",__FUNCTION__, start);
                }

                while ((end = strstr(start,","))) {
                    *end = 0;
		    macInstance = _ansc_atoi(start);
		    wifiDbgPrintf("%s macInstance  = %d \n", __FUNCTION__, macInstance);
		    start = end+1;

		    snprintf(recName, sizeof(recName), MacFilter, ulInstance, macInstance);
		    PSM_Del_Record(bus_handle,g_Subsystem, recName);
		    snprintf(recName, sizeof(recName), MacFilterDevice, ulInstance, macInstance);
		    PSM_Del_Record(bus_handle,g_Subsystem, recName);
                }

		// get last one
                if (strlen(start) > 0) {
		    macInstance = _ansc_atoi(start);
		    wifiDbgPrintf("%s macInstance  = %d \n", __FUNCTION__, macInstance);

		    sprintf(recName, MacFilter, ulInstance, macInstance);
		    PSM_Del_Record(bus_handle,g_Subsystem, recName);
		    sprintf(recName, MacFilterDevice, ulInstance, macInstance);
		    PSM_Del_Record(bus_handle,g_Subsystem, recName);
                }
	    } 
        }
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);

    }
    snprintf(recName, sizeof(recName), MacFilterList, ulInstance);
    PSM_Del_Record(bus_handle,g_Subsystem,recName);

    snprintf(recName, sizeof(recName), MacFilterMode, ulInstance);
    PSM_Del_Record(bus_handle,g_Subsystem,recName);

    return ANSC_STATUS_SUCCESS;
}

static void *CosaDmlWiFiFactoryResetThread(void *arg) 
{
    UNREFERENCED_PARAMETER(arg);
    printf("%s Calling pthread_mutex_lock for sWiFiThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 
    pthread_mutex_lock(&sWiFiThreadMutex);
    printf("%s Called pthread_mutex_lock for sWiFiThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 

    PSM_Set_Record_Value2(bus_handle,g_Subsystem, ReloadConfig, ccsp_string, "TRUE");

    wifiDbgPrintf("%s Calling Initialize() \n",__FUNCTION__);

    pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;

    CosaWifiReInitialize((ANSC_HANDLE)pMyObject, 0);
    CosaWifiReInitialize((ANSC_HANDLE)pMyObject, 1);

    wifiDbgPrintf("%s Called Initialize() \n",__FUNCTION__);

    printf("%s Calling pthread_mutex_unlock for sWiFiThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 
    pthread_mutex_unlock(&sWiFiThreadMutex);
    printf("%s Called pthread_mutex_unlock for sWiFiThreadMutex  %d \n",__FUNCTION__ , __LINE__ );  

    return(NULL);
}
static void *CosaDmlWiFiFactoryResetRadioAndApThread(void *arg) 
{
    if (!arg) return NULL;
    ULONG indexes = (ULONG)arg;
    pthread_mutex_lock(&sWiFiThreadMutex);

    PSM_Set_Record_Value2(bus_handle,g_Subsystem, ReloadConfig, ccsp_string, "TRUE");

    /*Setting ReloadConfig PSM to TRUE will set the FactoryReset as 1. we dont need to
     * do FR for all the Radios & APs. So, setting FactoryReset record to 0. */
    PSM_Set_Record_Value2(bus_handle,g_Subsystem, FactoryReset, ccsp_string, "0");

    pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;

    CosaWifiReInitializeRadioAndAp((ANSC_HANDLE)pMyObject, indexes);    

    pthread_mutex_unlock(&sWiFiThreadMutex);


    pthread_t tid3;
    pthread_attr_t attr;
    pthread_attr_t *attrp = NULL;

    attrp = &attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
    pthread_create(&tid3, attrp, &RegisterWiFiConfigureCallBack, NULL);
    if(attrp != NULL)
        pthread_attr_destroy( attrp );

    return(NULL);
}

#define WIFI_DM_COMP  "eRT.com.cisco.spvtg.ccsp.wifi"
#define WIFI_DM_BUS   "/com/cisco/spvtg/ccsp/wifi"

void MacFiltTab_CleanAll (void)
{
    PCOSA_DATAMODEL_WIFI        pWiFi = NULL;
    PSINGLE_LINK_ENTRY          pAPLink;
    PCOSA_CONTEXT_LINK_OBJECT   pAPLinkObj;
    PCOSA_DML_WIFI_AP           pWifiAp;
    PCOSA_DML_WIFI_AP_MF_CFG    pWifiApMf;
    PCOSA_DML_WIFI_AP_FULL      pWifiApFull;
    PSINGLE_LINK_ENTRY          pSListEntry;
    PCOSA_CONTEXT_LINK_OBJECT   pCxtLink;

    if (g_pCosaBEManager->hWifi)
    {
        pWiFi = (PCOSA_DATAMODEL_WIFI) g_pCosaBEManager->hWifi;
    }

    if (pWiFi)
    {
        for (pAPLink = AnscQueueGetFirstEntry(&pWiFi->AccessPointQueue); pAPLink != NULL; pAPLink = AnscQueueGetNextEntry(pAPLink))
        {
            pAPLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pAPLink);

            if (!pAPLinkObj)
            {
                continue;
            }

            pWifiAp = (PCOSA_DML_WIFI_AP) pAPLinkObj->hContext;
            pWifiApMf = (PCOSA_DML_WIFI_APWPS_FULL) &pWifiAp->MF;
            pWifiApMf->bEnabled = false;                            // default value for MAC filter Enabled
            pWifiApMf->FilterAsBlackList = false;                   // default value for MAC filter FilterAsBlackList
            pWifiApFull = (PCOSA_DML_WIFI_AP_FULL) &pWifiAp->AP;
            pSListEntry = AnscSListGetFirstEntry (&pWifiApFull->MacFilterList);

            while (pSListEntry)
            {
                char recName[256];

                pCxtLink = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSListEntry);
                pSListEntry = AnscSListGetNextEntry(pSListEntry);
                sprintf (recName, "Device.WiFi.AccessPoint.%d.X_CISCO_COM_MacFilterTable.%d.", pAPLinkObj->InstanceNumber, pCxtLink->InstanceNumber);
                Cosa_DelEntry (WIFI_DM_COMP,WIFI_DM_BUS, recName);
            }
        }
    }
}

ANSC_STATUS
CosaDmlWiFi_FactoryReset()
{
    char recName[256];
    char *strValue = NULL;
    int retPsmGet = CCSP_SUCCESS;
    int resetSSID[2] = {0,0};
    int i = 0;
printf("%s g_Subsytem = %s\n",__FUNCTION__,g_Subsystem);

    // This function is only called when the WiFi DML FactoryReset is set
    // From this interface only reset the UserControlled SSIDs

    for (i = 1; i <= gRadioCount; i++)
    {
    if (!g_wifidb_rfc) {
        memset(recName, 0, sizeof(recName));
        snprintf(recName, sizeof(recName), UserControl, i);
        retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    } else {
        struct schema_Wifi_Radio_Config  *pcfg= NULL;
        char radio_name[16] = {0};
        if (convert_radio_to_name(i,radio_name) == 0) {
            pcfg = (struct schema_Wifi_Radio_Config  *) wifi_db_get_table_entry(radio_name, "radio_name",&table_Wifi_Radio_Config,OCLM_STR);
            if (pcfg != NULL) {
                char usr_ctl[4] = {0};
                snprintf(usr_ctl, sizeof(usr_ctl), "%d",pcfg->user_control);
                strValue = strdup(usr_ctl);
            } else { 
                retPsmGet = CCSP_FAILURE;
            }
        }
    }
        if (retPsmGet == CCSP_SUCCESS) {
            resetSSID[i-1] = atoi(strValue);
printf("%s: resetSSID[%d] = %d \n", __FUNCTION__, i-1,  resetSSID[i-1]);
            memset(recName, 0, sizeof(recName));
            snprintf(recName, sizeof(recName), FactoryResetSSID, i);
            PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        } 
    }    

    // Delete PSM entries for Wifi Primary SSIDs related values
#if 0
    for (i = 1; i <= gRadioCount; i++) {
	sprintf(recName, CTSProtection, i);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	sprintf(recName, BeaconInterval, i);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	sprintf(recName, DTIMInterval, i);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	sprintf(recName, FragThreshold, i);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	sprintf(recName, RTSThreshold, i);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	sprintf(recName, ObssCoex, i);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);
    }
#endif
    for (i = 1; i <= gSsidCount; i++)
    {
        int ssidIndex = i -1;
	int radioIndex = (ssidIndex %2);
	printf("%s: ssidIndex = %d radioIndex = %d (1<<(ssidIndex/2)) & resetSSID[radioIndex] = %d \n", __FUNCTION__,  ssidIndex, radioIndex, ((1<<(ssidIndex/2)) & resetSSID[radioIndex] ));
	if ( ((1<<(ssidIndex/2)) & resetSSID[radioIndex] ) != 0)
    {
printf("%s: deleting records for index %d \n", __FUNCTION__, i);
	    snprintf(recName, sizeof(recName), WmmEnable, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    snprintf(recName, sizeof(recName), UAPSDEnable, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    snprintf(recName, sizeof(recName), WmmNoAck, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    snprintf(recName, sizeof(recName), BssMaxNumSta, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    snprintf(recName, sizeof(recName), BssHotSpot, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    snprintf(recName, sizeof(recName), BeaconRateCtl, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    CosaDmlWiFiPsmDelMacFilterTable(i);

	    // Platform specific data that is stored in the ARM Intel DB and converted to PSM entries
	    // They will be read only on Factory Reset command and override the current Wifi configuration
	    snprintf(recName, sizeof(recName), RadioIndex, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    snprintf(recName, sizeof(recName), WlanEnable, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    snprintf(recName, sizeof(recName), BssSsid, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    snprintf(recName, sizeof(recName), HideSsid, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    snprintf(recName, sizeof(recName), SecurityMode, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    snprintf(recName, sizeof(recName), EncryptionMethod, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    snprintf(recName, sizeof(recName), Passphrase, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    snprintf(recName, sizeof(recName), WmmRadioEnable, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    snprintf(recName, sizeof(recName), WpsEnable, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);

	    snprintf(recName, sizeof(recName), Vlan, i);
	    PSM_Del_Record(bus_handle,g_Subsystem,recName);
        }
    }

    pthread_t tid1;
    pthread_create(&tid1, NULL, &MacFiltTab_CleanAll, NULL);

    PSM_Del_Record(bus_handle,g_Subsystem,WpsPin);

    PSM_Del_Record(bus_handle,g_Subsystem,FactoryReset);

    PSM_Reset_UserChangeFlag(bus_handle,g_Subsystem,"Device.WiFi.");

    {
        pthread_t tid; 

        printf("%s Factory Reset WiFi  \n",__FUNCTION__ ); 

        pthread_attr_t attr;
        pthread_attr_t *attrp = NULL;

        attrp = &attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
		
        if (pthread_create(&tid,attrp,CosaDmlWiFiFactoryResetThread,NULL))
        {
            if(attrp != NULL)
                pthread_attr_destroy( attrp );
            return ANSC_STATUS_FAILURE;
        }
        if(attrp != NULL)
            pthread_attr_destroy( attrp );
    }
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_GetInterworkingInternetAvailable(BOOL *value)
{
        char *strValue = NULL; 
        int retPsmGet = 0;
        char *Tunnel_status = "dmsb.hotspot.tunnel.1.Enable";
        /*get Tunnel status for xfinity ssids*/
        retPsmGet = PSM_Get_Record_Value2(bus_handle, g_Subsystem, Tunnel_status, NULL, &strValue);
        if ((retPsmGet != CCSP_SUCCESS) || (strValue == NULL)) 
        {
            CcspTraceError(("(%s), InternetAvailable PSM get Error !!!\n", __func__));
            return ANSC_STATUS_FAILURE;
        }
        *value = atoi(strValue);
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        return ANSC_STATUS_SUCCESS;

}

ANSC_STATUS
CosaDmlWiFi_Get2G80211axEnabled(BOOL *value)
{
        PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
        *value = pMyObject->b2G80211axEnabled;
        return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_GetPreferPrivateData(BOOL *value)
{
	PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	*value = pMyObject->bPreferPrivateEnabled;
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_GetPreferPrivatePsmData(BOOL *value)
{
    char *strValue = NULL;
    char str[2];
    int retPsmGet = CCSP_SUCCESS;

    if (!value) return ANSC_STATUS_FAILURE;

    if (g_wifidb_rfc) {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            *value = pcfg->prefer_private;
             free(pcfg);
        } else {
            *value = TRUE;
        } /*Handle set case here*/
    } else {
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, PreferPrivate, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        *value = _ansc_atoi(strValue);
        CcspWifiTrace(("RDK_LOG_WARN,%s-%d Enable PreferPrivate is %d\n",__FUNCTION__,__LINE__,*value));
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, PreferPrivate_configured, NULL, &strValue);
        if (retPsmGet == CCSP_SUCCESS)
          {
          ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
          }
        else
          {
             *value = TRUE; //Default value , TRUE
             sprintf(str,"%d",*value);
             CcspWifiTrace(("RDK_LOG_WARN,%s-%d Enable PreferPrivate by default\n",__FUNCTION__,__LINE__));
             retPsmGet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, PreferPrivate, ccsp_string, str);
             if (retPsmGet != CCSP_SUCCESS) {
                CcspWifiTrace(("RDK_LOG_WARN,%s PSM_Set_Record_Value2 returned error %d while setting %s \n",__FUNCTION__, retPsmGet, PreferPrivate));
             return ANSC_STATUS_FAILURE;
             }
             retPsmGet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, PreferPrivate_configured, ccsp_string, str);
             if (retPsmGet != CCSP_SUCCESS) {
                CcspWifiTrace(("RDK_LOG_WARN,%s PSM_Set_Record_Value2 returned error %d while setting %s \n",__FUNCTION__, retPsmGet, PreferPrivate_configured));
                return ANSC_STATUS_FAILURE;
             } 

          }

    }
    else
    {
        *value = TRUE; //Default value , TRUE
        sprintf(str,"%d",*value);
        CcspWifiTrace(("RDK_LOG_WARN,%s Enable PreferPrivate by default\n",__FUNCTION__));
        retPsmGet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, PreferPrivate, ccsp_string, str);
        if (retPsmGet != CCSP_SUCCESS) {
           CcspWifiTrace(("RDK_LOG_WARN,%s PSM_Set_Record_Value2 returned error %d while setting %s \n",__FUNCTION__, retPsmGet, PreferPrivate));
           return ANSC_STATUS_FAILURE;
        }
        retPsmGet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, PreferPrivate_configured, ccsp_string, str);
        if (retPsmGet != CCSP_SUCCESS) {
           CcspWifiTrace(("RDK_LOG_WARN,%s PSM_Set_Record_Value2 returned error %d while setting %s \n",__FUNCTION__, retPsmGet, PreferPrivate_configured));
           return ANSC_STATUS_FAILURE;
        }


    }
    }
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_Set2G80211axEnabled(BOOL value)
{
#if (defined (_XB7_PRODUCT_REQ_) && defined (_COSA_BCM_ARM_)) || defined(_CBR2_PRODUCT_REQ_)

    INT ret = 0;

    ret = wifi_allow2G80211ax(value);
    if(ret != 0)
    {
        return ANSC_STATUS_FAILURE;
    }
#else 
    UNREFERENCED_PARAMETER(value);
#endif

    return ANSC_STATUS_SUCCESS;
}


ANSC_STATUS
CosaDmlWiFi_SetPreferPrivatePsmData(BOOL value)
{
    char strValue[2] = {0};
    int retPsmSet = CCSP_SUCCESS;
    int idx[4]={5,6,9,10}, index=0; 
    int apIndex=0;
    char recName[256];

    sprintf(strValue,"%d",value);
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, PreferPrivate, ccsp_string, strValue);
    if (retPsmSet != CCSP_SUCCESS) {
        CcspWifiTrace(("RDK_LOG_INFO,%s PSM_Set_Record_Value2 returned error %d while setting %s \n",__FUNCTION__, retPsmSet, PreferPrivate));
        return ANSC_STATUS_FAILURE;
    }
    if (g_wifidb_rfc) {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            pcfg->prefer_private = value;
            if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                free(pcfg);
            }
        } else {
            CcspTraceError(("%s: WIFI DB Failed to get global config\n", __FUNCTION__ ));
            return ANSC_STATUS_FAILURE;
        }
    }
#if !defined(_PLATFORM_RASPBERRYPI_) && !defined(_PLATFORM_TURRIS_)
   if(value == TRUE)
   {
    for(index = 0; index <4 ; index++) {
                apIndex=idx[index];
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), MacFilterMode, apIndex);
    wifi_setApMacAddressControlMode(apIndex-1, 2);
    PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "2");
    }

   }
   
   if(value == FALSE)
   { 
    for(index = 0; index <4 ; index++) {
                apIndex=idx[index];

    		memset(recName, 0, sizeof(recName));
    		snprintf(recName, sizeof(recName), MacFilterMode, apIndex);
		wifi_setApMacAddressControlMode(apIndex-1, 0);
		PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "0");
    }
    
    if (g_wifidb_rfc) {
        struct schema_Wifi_VAP_Config  *pcfg= NULL;
        for(index = 0; index <4 ; index++) {
            apIndex=idx[index];
            pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[apIndex], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
            if (pcfg != NULL) {
                if (value == TRUE) {
                    wifi_setApMacAddressControlMode(apIndex, 2);
                    pcfg->mac_filter_enabled = TRUE;
                    pcfg->mac_filter_mode = wifi_mac_filter_mode_black_list;
                } else {
                    wifi_setApMacAddressControlMode(apIndex, 0);
                    pcfg->mac_filter_enabled = FALSE;
                }
                if (wifi_ovsdb_update_table_entry(vap_names[apIndex],"vap_name",OCLM_STR,&table_Wifi_VAP_Config,pcfg,filter_vaps) <= 0) {
                    CcspWifiTrace(("RDK_LOG_ERROR,%s: WIFI DB Failed to update vap config\n",__FUNCTION__ ));
                }
            }
        }
    }
	Delete_Hotspot_MacFilt_Entries();
  }
    CosaDmlWiFi_UpdateMfCfg();
#else
	wifi_setPreferPrivateConnection(value);
#endif
    return ANSC_STATUS_SUCCESS;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFiGetEnableRadiusGreylist                            */
/*                                                                               */
/* DESCRIPTION   : This function is to get the value of RadiusGreyList           */
/*                        from the PSM Database                                  */
/*                                                                               */
/* INPUT         : pbEnableRadiusGreyList - pointer to the return value          */
/*                                                                               */
/* OUTPUT        : TRUE / FALSE                                                  */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/
void CosaDmlWiFiGetEnableRadiusGreylist(BOOLEAN *pbEnableRadiusGreyList)
{
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
    char *psmStrValue = NULL;

    *pbEnableRadiusGreyList = FALSE;
    CcspTraceInfo(("[%s] Get EnableRadiusGreylist Value \n",__FUNCTION__));

    if (PSM_Get_Record_Value2(bus_handle, g_Subsystem,
            "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.RadiusGreyList.Enable",
            NULL, &psmStrValue) == CCSP_SUCCESS)
    {
        *pbEnableRadiusGreyList = _ansc_atoi(psmStrValue);
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(psmStrValue);
    }
#else
    UNREFERENCED_PARAMETER(pbEnableRadiusGreyList);
#endif
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFiCheckEnableRadiusGreylist                          */
/*                                                                               */
/* DESCRIPTION   : This function set the rdk_greylist to TRUE/FALSE in HAL       */
/*                      at bootup                                                */
/*                                                                               */
/* INPUT         : Value                                                         */
/*                                                                               */
/* OUTPUT        : TRUE / FALSE                                                  */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/
ANSC_STATUS
CosaDmlWiFiSetEnableRadiusGreylist(BOOLEAN value) {

#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
    CcspTraceInfo(("[%s] Enter\n",__FUNCTION__));
    int index=0;
    int apIndex=0;
    char recName[256];

    if (value == TRUE)
    {
	CcspTraceInfo(("[%s] Enabled\n",__FUNCTION__));
	CosaDmlWiFi_SetPreferPrivatePsmData(FALSE);
	for(index=0 ; index<HOTSPOT_NO_OF_INDEX ; index++) {
	    apIndex=Hotspot_Index[index];
	    memset(recName, 0, sizeof(recName));
	    sprintf(recName, MacFilterMode, apIndex);
	    wifi_setApMacAddressControlMode(apIndex-1, 2);
	    PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "2");
        }
	wifi_enableGreylistAccessControl(value);
    }
    else {
	CcspTraceInfo(("[%s] Disabled\n",__FUNCTION__));
	CosaDmlWiFi_SetPreferPrivatePsmData(TRUE);
	wifi_enableGreylistAccessControl(value);
	for(index=0 ; index<HOTSPOT_NO_OF_INDEX ; index++) {
		apIndex=Hotspot_Index[index];
		wifi_delApAclDevices(apIndex-1);
	}
    }
#else 
    UNREFERENCED_PARAMETER(value);
#endif    
    return ANSC_STATUS_SUCCESS;
}

/* CosaDmlWiFiGetvAPStatsFeatureEnable() */
ANSC_STATUS CosaDmlWiFiGetvAPStatsFeatureEnable(BOOLEAN *pbValue)
{
    char* strValue = NULL;

    // Initialize the value as FALSE always
    *pbValue = TRUE;
    sWiFiDmlvApStatsFeatureEnableCfg = TRUE;

    if (!g_wifidb_rfc) {
    if (CCSP_SUCCESS == PSM_Get_Record_Value2(bus_handle,
                g_Subsystem, WiFivAPStatsFeatureEnable, NULL, &strValue))
    {
        if (0 == strcmp(strValue, "true"))
        {
            *pbValue = TRUE;
            sWiFiDmlvApStatsFeatureEnableCfg = TRUE;
        }
        else {
            *pbValue = FALSE;
            sWiFiDmlvApStatsFeatureEnableCfg = FALSE;
        }
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );

        return ANSC_STATUS_SUCCESS;
    }
    } else {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            *pbValue = pcfg->vap_stats_feature;
            sWiFiDmlvApStatsFeatureEnableCfg = pcfg->vap_stats_feature;
            free(pcfg);
            return ANSC_STATUS_SUCCESS;
        }
    }
    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS CosaDmlWiFiGetTxOverflowSelfheal(BOOLEAN *pbValue)
{
    char* strValue = NULL;

    // Initialize the value as FALSE always
    *pbValue = FALSE;

    if (!g_wifidb_rfc) { 
    if (CCSP_SUCCESS == PSM_Get_Record_Value2(bus_handle,
                g_Subsystem, WiFiTxOverflowSelfheal, NULL, &strValue))
    {
        if(((strcmp (strValue, "true") == 0)) || (strcmp (strValue, "TRUE") == 0)){
            *pbValue = TRUE;
        }
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
        return ANSC_STATUS_SUCCESS;
    }
    } else {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            *pbValue = pcfg->tx_overflow_selfheal;
            free(pcfg);
            return ANSC_STATUS_SUCCESS;
        }
    }
    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS CosaDmlWiFiSetTxOverflowSelfheal(BOOLEAN bValue)
{
    char recValue[16] = {0};

    snprintf(recValue, sizeof(recValue), "%s", (bValue ? "true" : "false"));

    if (CCSP_SUCCESS == PSM_Set_Record_Value2(bus_handle,
            g_Subsystem, WiFiTxOverflowSelfheal, ccsp_string, recValue))
    {
        return ANSC_STATUS_SUCCESS;
    }

    if (g_wifidb_rfc) {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            pcfg->tx_overflow_selfheal = bValue;
            if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                free(pcfg);
            } else {
                return ANSC_STATUS_SUCCESS;
            }
        } else {
            CcspTraceError(("%s: WIFI DB Failed to get global config\n", __FUNCTION__ ));
        }
    }
    return ANSC_STATUS_FAILURE;
}

#define RADIO_5G        1
/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFiSetDFSAtBootUp                                     */
/*                                                                               */
/* DESCRIPTION   : This function is to set the value of DFSatBootUp              */
/*                 in the PSM Database                                           */
/*                                                                               */
/* INPUT         : bValue - Either FALSE or TRUE                                 */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/
ANSC_STATUS CosaDmlWiFiSetDFSAtBootUp(BOOLEAN bValue)
{
#if defined CONFIG_DFS
    wifi_setRadioDfsAtBootUpEnable(RADIO_5G, bValue);
#if defined(_COSA_BCM_ARM_) && defined(_XB7_PRODUCT_REQ_)
    wifi_nvramCommit();
#endif
#endif
    return ANSC_STATUS_SUCCESS;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFiSetDFS                                             */
/*                                                                               */
/* DESCRIPTION   : This function is to set the value of DFS                      */
/*                 in the PSM Database                                           */
/*                                                                               */
/* INPUT         : bValue - Either FALSE or TRUE                                 */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/
ANSC_STATUS CosaDmlWiFiSetDFS(BOOLEAN bValue)
{
#if defined CONFIG_DFS
    BOOLEAN dfsSupported;
    wifi_getRadioDfsSupport(RADIO_5G, &dfsSupported);

    if(!dfsSupported) {
        return ANSC_STATUS_FAILURE;
    }

    char *strValue = NULL;
    if (PSM_Get_Record_Value2(bus_handle, g_Subsystem,
        "Device.WiFi.Radio.2.X_COMCAST_COM_DFSEnable",
        NULL, &strValue) == CCSP_SUCCESS)
    {
        if(bValue != _ansc_atoi(strValue)) {
            char dfsValue[10];
            memset(dfsValue, 0, sizeof(dfsValue));
            sprintf(dfsValue, "%d", bValue);
            if(PSM_Set_Record_Value2(bus_handle, g_Subsystem,
               "Device.WiFi.Radio.2.X_COMCAST_COM_DFSEnable", ccsp_string, dfsValue) != CCSP_SUCCESS)
               CcspWifiTrace(("RDK_LOG_INFO, PSM Set Error !!!\n"));
        }
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }

    wifi_setRadioDfsEnable(RADIO_5G, bValue);
#if defined(_COSA_BCM_ARM_) && defined(_XB7_PRODUCT_REQ_)
    wifi_apply();
#endif
#endif
    return ANSC_STATUS_SUCCESS;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFiGetDFS                                             */
/*                                                                               */
/* DESCRIPTION   : This function is to get the value of DFS                      */
/*                        from the PSM Database                                  */
/*                                                                               */
/* INPUT         : pbValue - pointer to the return value                         */
/*                                                                               */
/* OUTPUT        : TRUE / FALSE                                                  */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/
ANSC_STATUS CosaDmlWiFiGetDFS(BOOLEAN *pbValue)
{
    char *strValue = NULL;

    *pbValue = FALSE;

    if (PSM_Get_Record_Value2(bus_handle, g_Subsystem,
        "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.DFS.Enable",
        NULL, &strValue) == CCSP_SUCCESS)
    {
        *pbValue = _ansc_atoi(strValue);

#if defined CONFIG_DFS
        BOOLEAN dfsEnable;
        wifi_getRadioDfsEnable(RADIO_5G, &dfsEnable);

        if(*pbValue != dfsEnable) {
            CcspWifiTrace(("RDK_LOG_INFO,WIFI %s: mismatch in rfc:%d and driver:%d values\n",__FUNCTION__, *pbValue, dfsEnable));
            wifi_setRadioDfsEnable(RADIO_5G, *pbValue);
#if defined(_COSA_BCM_ARM_) && defined(_XB7_PRODUCT_REQ_)
            wifi_apply();
#endif
        }
#endif

        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        return ANSC_STATUS_SUCCESS;
    }

    return ANSC_STATUS_FAILURE;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFiGetDFSAtBootUp                                     */
/*                                                                               */
/* DESCRIPTION   : This function is to get the value of DFSAtBootUp              */
/*                        from the PSM Database                                  */
/*                                                                               */
/* INPUT         : pbValue - pointer to the return value                         */
/*                                                                               */
/* OUTPUT        : TRUE / FALSE                                                  */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/
ANSC_STATUS CosaDmlWiFiGetDFSAtBootUp(BOOLEAN *pbValue)
{
    char *psmStrValue = NULL;

    *pbValue = FALSE;

    if (PSM_Get_Record_Value2(bus_handle, g_Subsystem,
            "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.DFSatBootUp.Enable",
            NULL, &psmStrValue) == CCSP_SUCCESS)
    {
        *pbValue = _ansc_atoi(psmStrValue);

#if defined CONFIG_DFS
        BOOLEAN dfsEnableAtBootUp;
        wifi_getRadioDfsAtBootUpEnable(RADIO_5G, &dfsEnableAtBootUp);

        if(*pbValue != dfsEnableAtBootUp) {
            CcspWifiTrace(("RDK_LOG_INFO,WIFI %s: mismatch in rfc:%d and driver:%d values\n",__FUNCTION__, *pbValue, dfsEnableAtBootUp));
            wifi_setRadioDfsAtBootUpEnable(RADIO_5G, *pbValue);
#if defined(_COSA_BCM_ARM_) && defined(_XB7_PRODUCT_REQ_)
            wifi_nvramCommit();
#endif
        }
#endif
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(psmStrValue);
        return ANSC_STATUS_SUCCESS;
    }
    return ANSC_STATUS_FAILURE;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFiGetCurrForceDisableWiFiRadio                       */
/*                                                                               */
/* DESCRIPTION   : This function returns the value of ForceDisableFlag from      */
/*                 from g_pCosaBEManager instead of fetching from PSM            */
/*                                                                               */
/* INPUT         : pbValue - pointer to the return value                         */
/*                                                                               */
/* OUTPUT        : TRUE / FALSE                                                  */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/
ANSC_STATUS CosaDmlWiFiGetCurrForceDisableWiFiRadio(BOOLEAN *pbValue)
{
    PCOSA_DATAMODEL_WIFI pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    *pbValue = pMyObject->bForceDisableWiFiRadio;
    return ANSC_STATUS_SUCCESS;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFiGetForceDisableWiFiRadio                           */
/*                                                                               */
/* DESCRIPTION   : This function will fetch the value from the PSM database.     */
/*                                                                               */
/* INPUT         : pbValue - pointer to the return value                         */
/*                                                                               */
/* OUTPUT        : TRUE / FALSE                                                  */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/
ANSC_STATUS CosaDmlWiFiGetForceDisableWiFiRadio(BOOLEAN *pbValue)
{
    char* strValue = NULL;

    // Initialize the value as FALSE always
    *pbValue = FALSE;
#if defined(_PLATFORM_RASPBERRYPI_) || defined(_PLATFORM_TURRIS_)
    return ANSC_STATUS_SUCCESS;
#else
    if (!g_wifidb_rfc) {
    if (CCSP_SUCCESS == PSM_Get_Record_Value2(bus_handle,
                g_Subsystem, WiFiForceDisableWiFiRadio, NULL, &strValue))
    {
        if(((strcmp (strValue, "true") == 0)) || (strcmp (strValue, "TRUE") == 0)){
            *pbValue = TRUE;
        }
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
        return ANSC_STATUS_SUCCESS;
    }
    } else {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            *pbValue = pcfg->force_disable_radio_feature;
            free(pcfg);
            return ANSC_STATUS_SUCCESS;
        }
    }
#endif
    return ANSC_STATUS_FAILURE;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFiSetForceDisableWiFiRadio                           */
/*                                                                               */
/* DESCRIPTION   : This function will disable all the radios once set to TRUE    */
/*                 and update the PSM database. PSM database alone will be       */
/*                 updated if set to FALSE.                                      */
/*                                                                               */
/* INPUT         : pbValue - Either FALSE or TRUE                                */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/
ANSC_STATUS CosaDmlWiFiSetForceDisableWiFiRadio(BOOLEAN bValue)
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio  = NULL;
    PCOSA_DATAMODEL_WIFI            pWiFi       = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    char recValue[16] = {0};
    char PreValue[16] = {0};
    char* strValue = NULL;
    int radioIndex=0;
    int radioStatus = 0;
    BOOL radioActive = FALSE;
    struct schema_Wifi_Global_Config *pcfg = NULL;

    sprintf(recValue, "%s", (bValue ? "true" : "false"));

    if (g_wifidb_rfc) {
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg == NULL) {
            CcspTraceError(("%s: WIFI DB Failed to get global config\n", __FUNCTION__ ));
        }
    }
    if(bValue)
    {
        for(radioIndex=0; radioIndex < 2; radioIndex++) {
            /* Before disabling the radios, check whether the radio status is TRUE/FALSE.
               If TRUE then add the radio in the radioStatus list which will be used to
               turn on particular radios once ForceDisableWiFiRadio feature is disabled.
             */
            if (RETURN_OK != wifi_getRadioEnable(radioIndex, &radioActive)) {
                CcspWifiTrace(("RDK_LOG_ERROR, %s Failed to Get Radio status for radio %d!!!\n",__FUNCTION__,(radioIndex+1)));
                return ANSC_STATUS_FAILURE;
            }
            if (radioActive) {
                if( RETURN_OK != wifi_setRadioEnable(radioIndex, FALSE)) {
                    CcspWifiTrace(("RDK_LOG_ERROR, %s Failed to Disable Radio %d!!!\n",__FUNCTION__,(radioIndex+1)));
                    return ANSC_STATUS_FAILURE;
                }
                pWifiRadio = pWiFi->pRadio+radioIndex;
                pWifiRadio->Radio.Cfg.bEnabled = FALSE;
                radioStatus = radioStatus | (1<<radioIndex);
            }
        }
        snprintf(PreValue, sizeof(PreValue), "%d",radioStatus);
        if (CCSP_SUCCESS != PSM_Set_Record_Value2(bus_handle,
                g_Subsystem, WiFiForceDisableRadioStatus, ccsp_string, PreValue))
        {
            return ANSC_STATUS_FAILURE;
        }
        if (g_wifidb_rfc) {
            if (pcfg) {
                pcfg->force_disable_radio_status = FALSE;
                if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                    CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                }
            }
        }
    } else {
        /* If ForceRadioDisable feature has been disabled, then the radio status should be restored to the original 
           status present before enabling the feature. Hence a PSM entry to record the values of the previous radio
           status has been maintained to restore those values. */

        if (!g_wifidb_rfc) {
        if (CCSP_SUCCESS != PSM_Get_Record_Value2(bus_handle,g_Subsystem, WiFiForceDisableRadioStatus, NULL, &strValue))
        {
            return ANSC_STATUS_FAILURE;
        }
        radioStatus = _ansc_atoi(strValue);
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
        } else {
            if (pcfg != NULL) {
               radioStatus = pcfg->force_disable_radio_status;
            }
        }

        for(radioIndex=0; radioIndex < 2; radioIndex++) {
           if(radioStatus & (1<<radioIndex)) {
               pWifiRadio = pWiFi->pRadio+radioIndex;
               pWifiRadio->Radio.Cfg.bEnabled = TRUE;
               if( RETURN_OK != wifi_setRadioEnable(radioIndex, TRUE)) {
                   CcspWifiTrace(("RDK_LOG_ERROR, %s Failed to Enable Radio %d!!!\n",__FUNCTION__,(radioIndex+1)));
                   return ANSC_STATUS_FAILURE;
               }
               radioStatus = radioStatus ^ (1<<radioIndex);
           }
       }
       sprintf(PreValue, "%d",radioStatus);
       if (CCSP_SUCCESS != PSM_Set_Record_Value2(bus_handle,
               g_Subsystem, WiFiForceDisableRadioStatus, ccsp_string, PreValue))
       {
           return ANSC_STATUS_FAILURE;
       }
       if (g_wifidb_rfc) {
            if (pcfg) {
                pcfg->force_disable_radio_status = TRUE;
                if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                    CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                }
            }
        }
    }
    if (CCSP_SUCCESS == PSM_Set_Record_Value2(bus_handle,
            g_Subsystem, WiFiForceDisableWiFiRadio, ccsp_string, recValue))
    {
        if(bValue) {
            CcspWifiTrace(("RDK_LOG_WARN, WIFI_FORCE_DISABLE_CHANGED_TO_TRUE\n"));
        } else {
            CcspWifiTrace(("RDK_LOG_WARN, WIFI_FORCE_DISABLE_CHANGED_TO_FALSE\n"));
        }
        return ANSC_STATUS_SUCCESS;
    }
    if (g_wifidb_rfc) {
        if (pcfg) {
            pcfg->force_disable_radio_feature = bValue;;
            if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                return ANSC_STATUS_FAILURE;
            }
            free(pcfg);
            return ANSC_STATUS_SUCCESS;
        }
    }
    return ANSC_STATUS_FAILURE;
}

#if defined (FEATURE_HOSTAP_AUTHENTICATOR)
/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFiGetHostapdAuthenticatorEnable                      */
/*                                                                               */
/* DESCRIPTION   : Retrieves the current Value of hostapd running daemon         */
/*                 FALSE : Native hostapd daemon                                 */
/*                 TRUE  : Lib hostapd running                                   */
/*                                                                               */
/* INPUT         : NONE                                                          */
/*                                                                               */
/* OUTPUT        : pbEnableHostapdAuthenticator - FALSE or TRUE                  */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*********************************************************************************/
void CosaDmlWiFiGetHostapdAuthenticatorEnable(BOOLEAN *pbEnableHostapdAuthenticator)
{
    char *psmStrValue = NULL;

    *pbEnableHostapdAuthenticator = FALSE;
    CcspTraceInfo(("[%s] Get DisableNativeHostapd Value \n",__FUNCTION__));

    if (PSM_Get_Record_Value2(bus_handle, g_Subsystem,
            "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Control.DisableNativeHostapd",
            NULL, &psmStrValue) == CCSP_SUCCESS)
    {
        *pbEnableHostapdAuthenticator = _ansc_atoi(psmStrValue);
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(psmStrValue);
#if defined (_XB7_PRODUCT_REQ_)
        BOOLEAN nvramLibHostapdStatus = FALSE;

        if(wifi_getLibhostapd(&nvramLibHostapdStatus) != RETURN_OK) {
            CcspTraceDebug(("%s nvram get libhapd_enable failed\n",__FUNCTION__));
            nvramLibHostapdStatus = 0;
        }

        if(*pbEnableHostapdAuthenticator != nvramLibHostapdStatus) {
            CcspTraceDebug(("%s mismatch, updating libhapd_enable=%d in nvram", __FUNCTION__, *pbEnableHostapdAuthenticator));
    	    if(wifi_setLibhostapd(*pbEnableHostapdAuthenticator) != RETURN_OK) {
        	CcspTraceError(("%s nvram update libhapd_enable=%d failed\n",__FUNCTION__, *pbEnableHostapdAuthenticator));
    	    }
        }
#endif
    }
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFiSetHostapdAuthenticatorEnable                      */
/*                                                                               */
/* DESCRIPTION   : This function will Enable/Disable the hostapd library         */
/*                 and also Enable/Disable the native hostapd running.           */
/*                 At a given time, Either Native OR library hostapd will        */
/*                 be running                                                    */
/*                                                                               */
/* INPUT         : PANSC_HANDLE - PCOSA_DATAMODEL_WIFI, BOOLEAN TRUE or FALSE,   */
/*                 BOOLEAN TRUE or FALSE - bootup indication                     */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : TRUE / FALSE                                                  */
/*                                                                               */
/*********************************************************************************/
BOOL CosaDmlWiFiSetHostapdAuthenticatorEnable(PANSC_HANDLE phContext, BOOLEAN bValue, BOOLEAN bInit)
{
    int vapIndex = 0, wpsCfg = 0;
    BOOL enableWps = FALSE, apEnabled = FALSE;
    char securityType[32] = {0};

    PCOSA_DATAMODEL_WIFI            pWiFi     = (PCOSA_DATAMODEL_WIFI) phContext;
    if(!pWiFi){
        CcspTraceError(("%s:%d: Wifi Instance is NULL\n", __func__, __LINE__));
        return FALSE;
    }

    PSINGLE_LINK_ENTRY              pSLinkEntrySsid = (PSINGLE_LINK_ENTRY       )NULL;
    PSINGLE_LINK_ENTRY              pSLinkEntryAp   = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_DML_WIFI_AP               pWifiAp         = (PCOSA_DML_WIFI_AP        )NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid       = (PCOSA_DML_WIFI_SSID      )NULL;
    ULONG                           idx            = 0;

    char recValue[16] = {0};

    sprintf(recValue, "%d", bValue);
    if (CCSP_SUCCESS == PSM_Set_Record_Value2(bus_handle, g_Subsystem,
    			    WiFiEnableHostapdAuthenticator, ccsp_string, recValue))
    {
        CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : PSM Set success\n",__FUNCTION__ ));
#if defined (_XB7_PRODUCT_REQ_)
	if(bValue && wifi_setLibhostapd(bValue) != RETURN_OK) {
            CcspTraceError(("%s nvram update libhapd_enable=%d failed\n",__FUNCTION__, bValue));
            return FALSE;
    	}
#endif
    } else {
        CcspTraceError(("%s Failed to set PSM Value: %d\n", __FUNCTION__, bValue));
        return FALSE;
    }

    if (bValue)
    {
#if defined (_XB7_PRODUCT_REQ_)
        if(bInit) {
	        sleep(5);
        }
#endif
        wifi_stopHostApd();
#if !defined (_XB7_PRODUCT_REQ_)
        hapd_register_callback();
#endif
        hapd_init_log_files();

#if defined (_XB7_PRODUCT_REQ_)
        libhostapd_global_init();
#endif

        pSLinkEntryAp = AnscQueueGetFirstEntry(&pWiFi->AccessPointQueue);
        pSLinkEntrySsid = AnscQueueGetFirstEntry(&pWiFi->SsidQueue);
        while (pSLinkEntryAp && pSLinkEntrySsid)
        {
            if (!(pWifiAp = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntryAp)->hContext))
            {
                CcspTraceError(("%s Error linking Data Model object!\n",__FUNCTION__));
                return FALSE;
            }
            if (!(pWifiSsid = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntrySsid)->hContext))
            {
                CcspTraceError(("%s Error linking Data Model object!\n",__FUNCTION__));
                return FALSE;
            }

	    wifi_getApEnable(idx,&apEnabled);
	    if (apEnabled)
            {
#if !defined (_XB7_PRODUCT_REQ_)
                /* If security mode is set to open, init lib only if greylisting is enabled */
                if (pWifiAp->SEC.Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_None && !pWiFi->bEnableRadiusGreyList)
                {
                    pSLinkEntryAp = AnscQueueGetNextEntry(pSLinkEntryAp);
                    pSLinkEntrySsid = AnscQueueGetNextEntry(pSLinkEntrySsid);
                    idx++;
                    continue;
                }
#endif
                init_lib_hostapd(pWiFi, pWifiAp, pWifiSsid, (idx % 2 == 0) ? &(pWiFi->pRadio+0)->Radio : &(pWiFi->pRadio+1)->Radio);

#if defined (FEATURE_SUPPORT_INTERWORKING)
                if (pWifiAp->AP.Cfg.InterworkingCapability == TRUE && ( pWifiAp->AP.Cfg.InterworkingEnable == TRUE))
                    CosaDmlWiFi_setInterworkingElement(&pWifiAp->AP.Cfg);
#endif
                if (pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Status)
                    CosaDmlWiFi_ApplyRoamingConsortiumElement(&pWifiAp->AP.Cfg);

                if (pWifiAp->AP.Cfg.BSSTransitionImplemented == TRUE && pWifiAp->AP.Cfg.BSSTransitionActivated)
                     CosaDmlWifi_setBSSTransitionActivated(&pWifiAp->AP.Cfg, pWifiAp->AP.Cfg.InstanceNumber - 1);
            }
            pSLinkEntryAp = AnscQueueGetNextEntry(pSLinkEntryAp);
            pSLinkEntrySsid = AnscQueueGetNextEntry(pSLinkEntrySsid);
	    idx++;
        }
        libhostap_eloop_run();
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
        init_lib_hostapd_greylisting();
#endif
#if defined (_XB7_PRODUCT_REQ_)
        /* reload apps */
        if ( v_secure_system("wifi_setup.sh start_security_apps") != 0 ) {
            CcspWifiTrace(("RDK_LOG_INFO, %s:%d wifi_setup.sh start_security_apps failed\n", __FUNCTION__, __LINE__));
            return FALSE;
        }
#endif
    }
    else
    {
#if defined (_XB7_PRODUCT_REQ_)
        libhostap_eloop_deinit();
#endif
        for (vapIndex = 0; vapIndex < 16; vapIndex++)
        {
            wifi_getApWpsEnable(vapIndex, (BOOL *)&wpsCfg);
            enableWps = (wpsCfg == 0) ? FALSE : TRUE;

            wifi_getApEnable(vapIndex, &apEnabled);
            wifi_getApSecurityModeEnabled(vapIndex, securityType);
#if !defined (_XB7_PRODUCT_REQ_)
            if (apEnabled && (strncmp(securityType, "None", strlen("None") != 0)))
#else
            if (apEnabled)
#endif
            {
#if !defined (_XB7_PRODUCT_REQ_)
                wifi_createHostApdConfig(vapIndex, enableWps);
#endif
                deinit_lib_hostapd(vapIndex);
            }
        }
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST) && !defined(_XB7_PRODUCT_REQ_)
        wifi_stop_eapol_rx_thread();
        hapd_deregister_callback();
#endif
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
	deinit_lib_hostapd_greylisting();
#endif
#if defined (_XB7_PRODUCT_REQ_)
	libhostapd_global_deinit();
#endif
	wifi_startHostApd();
    }
    UNREFERENCED_PARAMETER(enableWps);
    return TRUE;
}

#if defined (FEATURE_HOSTAP_AUTHENTICATOR) && defined(_XB7_PRODUCT_REQ_)
BOOL mCosaDmlWiFiSetHostapdAuthenticatorEnable(PANSC_HANDLE phContext, BOOLEAN bValue, BOOLEAN bInit)
{
    int vapIndex = 0;
    BOOL apEnabled = FALSE;

    PCOSA_DATAMODEL_WIFI            pWiFi     = (PCOSA_DATAMODEL_WIFI) phContext;
    if(!pWiFi){
        CcspTraceError(("%s:%d: Wifi Instance is NULL\n", __func__, __LINE__));
        return FALSE;
    }

    PSINGLE_LINK_ENTRY              pSLinkEntrySsid = (PSINGLE_LINK_ENTRY       )NULL;
    PSINGLE_LINK_ENTRY              pSLinkEntryAp   = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_DML_WIFI_AP               pWifiAp         = (PCOSA_DML_WIFI_AP        )NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid       = (PCOSA_DML_WIFI_SSID      )NULL;
    ULONG                           idx            = 0;

    if (bValue)
    {
        libhostapd_global_init();

        pSLinkEntryAp = AnscQueueGetFirstEntry(&pWiFi->AccessPointQueue);
        pSLinkEntrySsid = AnscQueueGetFirstEntry(&pWiFi->SsidQueue);
        while (pSLinkEntryAp && pSLinkEntrySsid)
        {
            if (!(pWifiAp = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntryAp)->hContext))
            {
                CcspTraceError(("%s Error linking Data Model object!\n",__FUNCTION__));
                return FALSE;
            }
            if (!(pWifiSsid = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntrySsid)->hContext))
            {
                CcspTraceError(("%s Error linking Data Model object!\n",__FUNCTION__));
                return FALSE;
            }

            wifi_getApEnable(idx,&apEnabled);
            if (apEnabled || pWifiSsid->SSID.Cfg.bEnabled)
            {
                init_lib_hostapd(pWiFi, pWifiAp, pWifiSsid, (idx % 2 == 0) ? &(pWiFi->pRadio+0)->Radio : &(pWiFi->pRadio+1)->Radio);
#if defined (FEATURE_SUPPORT_INTERWORKING)
                if (pWifiAp->AP.Cfg.InterworkingCapability == TRUE && ( pWifiAp->AP.Cfg.InterworkingEnable == TRUE))
                    CosaDmlWiFi_setInterworkingElement(&pWifiAp->AP.Cfg);
#endif
                if (pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Status)
                    CosaDmlWiFi_ApplyRoamingConsortiumElement(&pWifiAp->AP.Cfg);

                if (pWifiAp->AP.Cfg.BSSTransitionImplemented == TRUE && pWifiAp->AP.Cfg.BSSTransitionActivated)
                     CosaDmlWifi_setBSSTransitionActivated(&pWifiAp->AP.Cfg, pWifiAp->AP.Cfg.InstanceNumber - 1);
            }
            pSLinkEntryAp = AnscQueueGetNextEntry(pSLinkEntryAp);
            pSLinkEntrySsid = AnscQueueGetNextEntry(pSLinkEntrySsid);
            idx++;
        }
        libhostap_eloop_run();
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
        init_lib_hostapd_greylisting();
#endif
    }
    else
    {
        libhostap_eloop_deinit();
        for (vapIndex = 0; vapIndex < 16; vapIndex++)
        {
            wifi_getApEnable(vapIndex, &apEnabled);
            if (apEnabled)
            {
                deinit_lib_hostapd(vapIndex);
		sleep(1);
            }
        }
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
        deinit_lib_hostapd_greylisting();
#endif
        libhostapd_global_deinit();
    }
    return TRUE;
}
#endif

int CosaDmlWiFiReConfigAuthKeyMgmt(PCOSA_DATAMODEL_WIFI pWifi, PCOSA_DML_WIFI_AP pWifiAp, PCOSA_DML_WIFI_SSID pWifiSsid, COSA_DML_WIFI_SECURITY prevMode, COSA_DML_WIFI_SECURITY ModeEnabled)
{
    int apIndex = pWifiAp->AP.Cfg.InstanceNumber - 1;
    PCOSA_DATAMODEL_WIFI  pWiFi	= (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;

    if (ModeEnabled == COSA_DML_WIFI_SECURITY_None)
    {
        hapd_reset_ap_interface(apIndex);
        deinit_lib_hostapd(apIndex);
    }
    else if (prevMode == COSA_DML_WIFI_SECURITY_None)
    {
        /* If prevMode is None, then hapd struct will not be populated in lib
           hostapd. Hence, need to initialize the lib with the apIndex.
        */

        if ((ModeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Personal) ||
            (ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_WPA2_Personal))
        {
             wifi_setApWpsEnable(apIndex, TRUE);
             wifi_setApBasicAuthenticationMode(apIndex, "PSKAuthentication");
             if (ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_WPA2_Personal)
                 wifi_setApBeaconType(apIndex, "WPAand11i");
             else
                 wifi_setApBeaconType(apIndex, "11i");
        }
        else
        {
             wifi_setApBasicAuthenticationMode(apIndex, "EAPAuthentication");
        }
        init_lib_hostapd(pWifi, pWifiAp, pWifiSsid, (apIndex % 2 == 0) ? &(pWiFi->pRadio+0)->Radio : &(pWiFi->pRadio+1)->Radio);
#if defined (FEATURE_SUPPORT_INTERWORKING)
        if (pWifiAp->AP.Cfg.InterworkingCapability == TRUE && ( pWifiAp->AP.Cfg.InterworkingEnable == TRUE))
            CosaDmlWiFi_setInterworkingElement(&pWifiAp->AP.Cfg);
#endif
        if (pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Status)
            CosaDmlWiFi_ApplyRoamingConsortiumElement(&pWifiAp->AP.Cfg);

        if (pWifiAp->AP.Cfg.BSSTransitionImplemented == TRUE && pWifiAp->AP.Cfg.BSSTransitionActivated)
             CosaDmlWifi_setBSSTransitionActivated(&pWifiAp->AP.Cfg, pWifiAp->AP.Cfg.InstanceNumber - 1);
    }
    else
    {
        /* Enters when, prevMode is PSK/EAP, now the mode is changed to EAP/PSK
           respectively */
        hapd_reset_ap_interface(apIndex);
        deinit_lib_hostapd(apIndex);

        if ((ModeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Personal) ||
            (ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_WPA2_Personal))
        {
             wifi_setApWpsEnable(apIndex, TRUE);
             wifi_setApBasicAuthenticationMode(apIndex, "PSKAuthentication");
             if (ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_WPA2_Personal)
                 wifi_setApBeaconType(apIndex, "WPAand11i");
             else
                 wifi_setApBeaconType(apIndex, "11i");
        }
        else
        {
             wifi_setApWpsEnable(apIndex, FALSE);
             wifi_setApBasicAuthenticationMode(apIndex, "EAPAuthentication");
        }
        init_lib_hostapd(pWifi, pWifiAp, pWifiSsid, (apIndex % 2 == 0) ? &(pWiFi->pRadio+0)->Radio : &(pWiFi->pRadio+1)->Radio);
#if defined (FEATURE_SUPPORT_INTERWORKING)
        if (pWifiAp->AP.Cfg.InterworkingCapability == TRUE && ( pWifiAp->AP.Cfg.InterworkingEnable == TRUE))
            CosaDmlWiFi_setInterworkingElement(&pWifiAp->AP.Cfg);
#endif
        if (pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Status)
            CosaDmlWiFi_ApplyRoamingConsortiumElement(&pWifiAp->AP.Cfg);

        if (pWifiAp->AP.Cfg.BSSTransitionImplemented == TRUE && pWifiAp->AP.Cfg.BSSTransitionActivated)
             CosaDmlWifi_setBSSTransitionActivated(&pWifiAp->AP.Cfg, pWifiAp->AP.Cfg.InstanceNumber - 1);
    }
    return 0;
}

void CosaDmlWiFiWpsConfigUpdate(int apIndex, PCOSA_DML_WIFI_AP pWifiAp)
{
     hapd_update_wps_config(apIndex, pWifiAp);
}
#endif //FEATURE_HOSTAP_AUTHENTICATOR

/* CosaDmlWiFiSetvAPStatsFeatureEnable() */
ANSC_STATUS CosaDmlWiFiSetvAPStatsFeatureEnable(BOOLEAN bValue)
{
    char recValue[16] = {0};

    snprintf(recValue, sizeof(recValue), "%s", (bValue ? "true" : "false"));

    if (CCSP_SUCCESS == PSM_Set_Record_Value2(bus_handle,
            g_Subsystem, WiFivAPStatsFeatureEnable, ccsp_string, recValue))
    {
        sWiFiDmlvApStatsFeatureEnableCfg = bValue;
        wifi_stats_flag_change(0, bValue, 0);

        return ANSC_STATUS_SUCCESS;
    }
    if (g_wifidb_rfc) {
        struct schema_Wifi_Global_Config *pcfg = NULL;
        pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
        if (pcfg != NULL) {
            pcfg->vap_stats_feature = bValue;
            if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                free(pcfg);
            }
        } else {
            CcspTraceError(("%s: WIFI DB Failed to get global config\n", __FUNCTION__ ));
            return ANSC_STATUS_FAILURE;
        }
    }

    return ANSC_STATUS_FAILURE;
}

/* IsCosaDmlWiFivAPStatsFeatureEnabled() */
BOOLEAN IsCosaDmlWiFivAPStatsFeatureEnabled( void )
{
    return (sWiFiDmlvApStatsFeatureEnableCfg ? TRUE : FALSE);
}

ANSC_STATUS CosaDmlWiFi_PSM_Del_Radio(ULONG radioIndex) { 
	char recName[256];
	
	printf("-- %s: deleting PSM radio %lu \n", __FUNCTION__, radioIndex);
	snprintf(recName, sizeof(recName), CTSProtection, radioIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), BeaconInterval, radioIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), DTIMInterval, radioIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), FragThreshold, radioIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), RTSThreshold, radioIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), ObssCoex, radioIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), GuardInterval, radioIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

#if defined(_INTEL_BUG_FIXES_)
	snprintf(recName, sizeof(recName), TransmitPower, radioIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);
#endif
	return CCSP_SUCCESS;
}
		
ANSC_STATUS CosaDmlWiFi_PSM_Del_Ap(ULONG apIndex) { 		
	char recName[256];
	
	printf("-- %s: deleting PSM Ap %lu \n", __FUNCTION__, apIndex);

	snprintf(recName, sizeof(recName), WmmEnable, apIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), UAPSDEnable, apIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), WmmNoAck, apIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), BssMaxNumSta, apIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), BssHotSpot, apIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), BeaconRateCtl, apIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	CosaDmlWiFiPsmDelMacFilterTable(apIndex);

	// Platform specific data that is stored in the ARM Intel DB and converted to PSM entries
	// They will be read only on Factory Reset command and override the current Wifi configuration
	snprintf(recName, sizeof(recName), RadioIndex, apIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), WlanEnable, apIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), BssSsid, apIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), HideSsid, apIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), SecurityMode, apIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), EncryptionMethod, apIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), Passphrase, apIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), WmmRadioEnable, apIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), WpsEnable, apIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

	snprintf(recName, sizeof(recName), Vlan, apIndex);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);
	
	return CCSP_SUCCESS;
}


ANSC_STATUS
CosaDmlWiFi_FactoryResetRadioAndAp(ULONG radioIndex, ULONG radioIndex_2, ULONG apIndex, ULONG apIndex_2) {   

#ifdef CISCO_XB3_PLATFORM_CHANGES
    int retPsmSet = CCSP_SUCCESS;
#endif
fprintf(stderr, "-- %s %lu %lu %lu %lu\n", __func__,  radioIndex,   radioIndex_2,  apIndex, apIndex_2);
    // Delete PSM entries for Wifi Primary SSIDs related values
	if(radioIndex>0 && radioIndex<=2) 
		CosaDmlWiFi_PSM_Del_Radio(radioIndex);
	else
		radioIndex=0;
		
    if(radioIndex_2>0 && radioIndex_2<=2) 
		CosaDmlWiFi_PSM_Del_Radio(radioIndex_2);
	else
		radioIndex_2=0;
	if(apIndex>0 && apIndex<=16) 
        CosaDmlWiFi_PSM_Del_Ap(apIndex);
	else
		apIndex=0;
		
    if(apIndex_2>0 && apIndex_2<=16) 
        CosaDmlWiFi_PSM_Del_Ap(apIndex_2);
	else
		apIndex_2=0;

    PSM_Del_Record(bus_handle,g_Subsystem,WpsPin);
    PSM_Del_Record(bus_handle,g_Subsystem,FactoryReset);
    PSM_Reset_UserChangeFlag(bus_handle,g_Subsystem,"Device.WiFi.");

    {
        pthread_t tid; 
		ULONG indexes=0;

		indexes=(radioIndex<<24) + (radioIndex_2<<16) + (apIndex<<8) + apIndex_2;
		printf("%s Factory Reset Radio %lu %lu and AP %lu %lu  (indexes=%lu)\n",__FUNCTION__, radioIndex, radioIndex_2, apIndex, apIndex_2, indexes ); 
        pthread_attr_t attr;
        pthread_attr_t *attrp = NULL;

        attrp = &attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
        if (pthread_create(&tid,attrp,CosaDmlWiFiFactoryResetRadioAndApThread, (void *)indexes))
        {
            if(attrp != NULL)
                pthread_attr_destroy( attrp );
            return ANSC_STATUS_FAILURE;
        }
        if(attrp != NULL)
                pthread_attr_destroy( attrp );
#ifdef CISCO_XB3_PLATFORM_CHANGES
            retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, NotifyWiFiChanges, ccsp_string,"true");
 
        if (retPsmSet == CCSP_SUCCESS) {
                CcspWifiTrace(("RDK_LOG_INFO,CaptivePortal:%s - PSM set of NotifyWiFiChanges success ...\n",__FUNCTION__));
        }
        else
        {
                CcspWifiTrace(("RDK_LOG_ERROR,CaptivePortal:%s - PSM set of NotifyWiFiChanges failed and ret value is %d...\n",__FUNCTION__,retPsmSet));
        }
 
            printf("%s, Setting factory reset after migration parameter in PSM \n",__FUNCTION__);
         retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, WiFiRestored_AfterMigration, ccsp_string,"true");
 
        if (retPsmSet == CCSP_SUCCESS) {
                CcspWifiTrace(("RDK_LOG_INFO,CaptivePortal:%s - PSM set of WiFiRestored_AfterMigration success ...\n",__FUNCTION__));
        }
        else
        {
                CcspWifiTrace(("RDK_LOG_ERROR,CaptivePortal:%s - PSM set of WiFiRestored_AfterMigration failed and ret value is %d...\n",__FUNCTION__,retPsmSet));
        }
#else
            PSM_Set_Record_Value2(bus_handle,g_Subsystem, NotifyWiFiChanges, ccsp_string,"true");
#endif
        if (g_wifidb_rfc) {
            struct schema_Wifi_Global_Config *pcfg = NULL;
            pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
            if (pcfg != NULL) {
                pcfg->notify_wifi_changes = TRUE;
                if (wifi_ovsdb_update_table_entry(NULL,NULL,OCLM_UUID,&table_Wifi_Global_Config,pcfg,filter_global) <= 0) {
                    CcspTraceError(("%s: WIFI DB Failed to update WIFI DB global config\n",__FUNCTION__));
                    free(pcfg);
                }
            } else {
                CcspTraceError(("%s: WIFI DB Failed to get global config\n", __FUNCTION__ ));
                return ANSC_STATUS_FAILURE;
            }
        }
        int rc = -1;
        rc = strcpy_s(notifyWiFiChangesVal,sizeof(notifyWiFiChangesVal),"true");
	if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }


#if 0
		FILE *fp;
		char command[30];

		memset(command,0,sizeof(command));
		sprintf(command, "ls /tmp/*walledgarden*");
		char buffer[50];
		memset(buffer,0,sizeof(buffer));
        if(!(fp = popen(command, "r")))
		{
              exit(1);
        }
		while(fgets(buffer, sizeof(buffer), fp)!=NULL)
		{
			buffer[strlen(buffer) - 1] = '\0';
		}

		if ( strlen(buffer) == 0 )
		{
			//pthread_t captive;
			//pthread_create(&captive, NULL, &configWifi, NULL);
			BOOLEAN redirect;
			redirect = TRUE;
			CcspWifiTrace(("RDK_LOG_WARN,CaptivePortal:%s - WiFi restore case, setting system in Captive Portal redirection mode...\n",__FUNCTION__));
			configWifi(redirect);

		}
		pclose(fp);
#endif
		BOOLEAN redirect;
		redirect = TRUE;
		CcspWifiTrace(("RDK_LOG_WARN,CaptivePortal:%s - WiFi restore case, setting system in Captive Portal redirection mode...\n",__FUNCTION__));
                t2_event_d("SYS_INFO_RestoreWIFISettings", 1);
		v_secure_system("/usr/bin/sysevent set CaptivePortalCheck true");
		configWifi(redirect);

    }

    return ANSC_STATUS_SUCCESS;
}

int readRemoteIP(char *sIP, int size,char *sName)
{

        #define DATA_SIZE 1024
	int rc = -1;
        FILE *fp1;
        char buf[DATA_SIZE] = {0};
        char *urlPtr = NULL;
        int ret=-1;

        // Grab the ARM or ATOM RPC IP address

        fp1 = fopen("/etc/device.properties", "r");
        if (fp1 == NULL) {
            CcspTraceError(("Error opening properties file! \n"));
            return -1;
        }

        while (fgets(buf, DATA_SIZE, fp1) != NULL) {
            // Look for ARM_ARPING_IP or ATOM_ARPING_IP
            if (strstr(buf, sName) != NULL) {
                buf[strcspn(buf, "\r\n")] = 0; // Strip off any carriage returns

                // grab URL from string
                urlPtr = strstr(buf, "=");
                urlPtr++;
                rc = strcpy_s(sIP, (unsigned)size, urlPtr);
		if (rc != 0) {
                    ERR_CHK(rc);
                    return -1;
                }
              ret=0;
              break;
            }
        }

        fclose(fp1);
        return ret;

}

ANSC_STATUS
CosaDmlWiFi_EnableTelnet(BOOL bEnabled)
{

    if (bEnabled) {
	// Attempt to start the telnet daemon on ATOM
        char NpRemoteIP[128]="";
        readRemoteIP(NpRemoteIP, 128,"ATOM_ARPING_IP");
        if (NpRemoteIP[0] != 0 && strlen(NpRemoteIP) > 0) {
                if (v_secure_system("/usr/sbin/telnetd -b %s",NpRemoteIP) != 0)
                {
                        return ANSC_STATUS_FAILURE;
                }
        }
    }

    else {
        // Attempt to kill the telnet daemon on ATOM
        if ( v_secure_system("pkill telnetd") != 0 ) {
	    return ANSC_STATUS_FAILURE;
        }
    }

    return ANSC_STATUS_SUCCESS;

}

#if 0
#include "intel_ce_pm.h"

BOOL sCosaDmlWiFi_AtomInSuspension = FALSE;

static int CosaDmlWiFi_PsmEventHandler(icepm_event_t event, void * cookie)
{
    switch (event) {
    case ICEPM_EVT_SUSPEND:
        // printf("%s: Got event that Atom is being suspended\n",__FUNCTION__);
        sCosaDmlWiFi_AtomInSuspension = TRUE; 

        break;

    case ICEPM_EVT_RESUME:
        printf("%s: Got event that Atom is being resumed from suspended \n",__FUNCTION__);

        sCosaDmlWiFi_AtomInSuspension = FALSE; 
        // Execute resume logic here

        break;
    case ICEPM_EVT_INTERRUPTED:
        // Should only happen if application is killed.
        printf("%s: Process was killed \n",__FUNCTION__);
        break;
    default:
        // Should not happen
        break;
    }
    return 0;
}

static void CosaDmlWiFi_RegisterPsmEventHandler(void)
{
    printf("%s: Entered \n",__FUNCTION__);
    icepm_ret_t rc = icepm_register_callback(CosaDmlWiFi_PsmEventHandler, "CcspPandM", NULL);
    if (rc != ICEPM_OK) {
        printf("%s: Process was killed \n",__FUNCTION__);
    }
}
#endif

static ANSC_STATUS
CosaDmlWiFi_ShutdownAtom(void)
{ 
    switch(fork()) {
    case 0: /*Child*/
        {
	    wifiDbgPrintf("%s: write mem to /sys/power/state to suspend Atom cpu\n",__FUNCTION__);
            char *myArgv[] = { "suspendAtom.sh", NULL};
            execvp("/etc/ath/suspendAtom.sh", myArgv);
            exit(-1);
        }
    default:
        break;
    }

    return ANSC_STATUS_SUCCESS;
}

static void * CosaDmlWifi_RadioPowerThread(void *arg)
{
    COSA_DML_WIFI_RADIO_POWER power = gRadioNextPowerSetting;
    UNREFERENCED_PARAMETER(arg);
    printf("%s Calling pthread_mutex_lock for sWiFiThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 
    pthread_mutex_lock(&sWiFiThreadMutex);
    printf("%s Called pthread_mutex_lock for sWiFiThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 

    if (power != gRadioPowerSetting) {

        switch (power) {
        case COSA_DML_WIFI_POWER_DOWN:
            wifi_down();
            gRadioPowerState[0] = power;
            gRadioPowerState[1] = power;
            gRadioPowerSetting = power;

            CosaDmlWiFi_ShutdownAtom();

#ifdef FEATURE_HOSTAP_AUTHENTICATOR
            BOOLEAN isNativeHostapdDisabled = FALSE;
            CosaDmlWiFiGetHostapdAuthenticatorEnable(&isNativeHostapdDisabled);
            if (isNativeHostapdDisabled)
            {
                int vapIndex = 0;
                BOOL apEnabled = FALSE;
                for (vapIndex = 0; vapIndex < 16; vapIndex++)
                {
                     wifi_getApEnable(vapIndex, &apEnabled);
                     if (apEnabled) {
                         deinit_lib_hostapd(vapIndex);
                     }
                }
            }
#endif
            break;
        case COSA_DML_WIFI_POWER_UP:
            // Only call wifi_init if the radios are currently down
            // When the DML is reint, the radios will be brought to full power
            if (gRadioPowerSetting == COSA_DML_WIFI_POWER_DOWN) {
#ifdef CISCO_XB3_PLATFORM_CHANGES
		// RDKB3939-199 WiFi PCI needs to be reset after exits powersave mode
		wifi_PCIReset();
#endif
                m_wifi_init();
            }
            gRadioPowerState[0] = power;
            gRadioPowerState[1] = power;
            gRadioPowerSetting = power;
            wifiDbgPrintf("%s Calling Initialize() \n",__FUNCTION__);
            pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;

            CosaWifiReInitialize((ANSC_HANDLE)pMyObject, 0);
            CosaWifiReInitialize((ANSC_HANDLE)pMyObject, 1);
            wifiDbgPrintf("%s Called Initialize() \n",__FUNCTION__);
            break;
        case COSA_DML_WIFI_POWER_LOW:
            wifi_setRadioTransmitPower(0, 5);
            wifi_setRadioTransmitPower(1, 5);
            gRadioPowerState[0] = power;
            gRadioPowerState[1] = power;

            gRadioPowerSetting = power;
            wifiDbgPrintf("%s Calling Initialize() \n",__FUNCTION__);
            pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;

            CosaWifiReInitialize((ANSC_HANDLE)pMyObject, 0);
            CosaWifiReInitialize((ANSC_HANDLE)pMyObject, 1);

            wifiDbgPrintf("%s Called Initialize() \n",__FUNCTION__);
            break;
        default:
            printf("%s Got invalid Radio Power %d, not updating the power \n",__FUNCTION__, power);
        }

    } else {

        ULONG ath0Power = 0, ath1Power = 0;
        wifi_getRadioTransmitPower(0,&ath0Power);
        wifi_getRadioTransmitPower(1, &ath1Power);
        if ( ( power == COSA_DML_WIFI_POWER_UP ) &&
             ( (ath0Power == 5 ) || (ath1Power == 5) ) ) {
            // Power may have been lowered with interrupt, but PSM didn't send power down
            // may happen if AC is reconnected before it notifies WiFi or
            // because Docsis was not operational
            // Calling CosaWifiReInitialize() will bring it back to full power
            wifiDbgPrintf("%s Calling Initialize() \n",__FUNCTION__);
            pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
            CosaWifiReInitialize((ANSC_HANDLE)pMyObject, 0);
            CosaWifiReInitialize((ANSC_HANDLE)pMyObject, 1);
            
            wifiDbgPrintf("%s Called Initialize() \n",__FUNCTION__);
        } else {
            printf("%s Noting to do.  Power was already in desired state %d(%d) \n",__FUNCTION__, gRadioPowerSetting, power);
        }
    }

    printf("%s Calling pthread_mutex_unlock for sWiFiThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 
    pthread_mutex_unlock(&sWiFiThreadMutex);
    printf("%s Called pthread_mutex_unlock for sWiFiThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 

    return(NULL);
}

ANSC_STATUS
CosaDmlWiFi_GetRadioPower(COSA_DML_WIFI_RADIO_POWER *power)
{ 
    if (!power) return ANSC_STATUS_FAILURE;
    *power = gRadioPowerSetting;
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetRadioPower(COSA_DML_WIFI_RADIO_POWER power)
{ 
    pthread_t tid; 

    printf("%s Changing Radio Power to = %d\n",__FUNCTION__, power); 

    gRadioNextPowerSetting = power;

    pthread_attr_t attr;
    pthread_attr_t *attrp = NULL;

    attrp = &attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
    if(pthread_create(&tid,attrp,CosaDmlWifi_RadioPowerThread,NULL))  {
        if(attrp != NULL)
            pthread_attr_destroy( attrp );
        return ANSC_STATUS_FAILURE;
    }

    if(attrp != NULL)
        pthread_attr_destroy( attrp );
    return ANSC_STATUS_SUCCESS;
}

/*
 *  Description:
 *     The API retrieves the number of WiFi radios in the system.
 */
ULONG
CosaDmlWiFiRadioGetNumberOfEntries
    (
        ANSC_HANDLE                 hContext
    )
{
    UNREFERENCED_PARAMETER(hContext);
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    wifi_getRadioNumberOfEntries((ULONG*)&gRadioCount);
#endif
    return gRadioCount;
}    
    
ANSC_STATUS
CosaDmlWiFiRadioGetSinfo
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_SINFO  pInfo
    )
{
	wifiDbgPrintf("%s\n",__FUNCTION__);
	UNREFERENCED_PARAMETER(hContext);
    // Atheros Radio index start at 0, not 1 as in the DM
    int wlanIndex = ulInstanceNumber-1;

    if (!pInfo || (wlanIndex<0) || (wlanIndex>=RADIO_INDEX_MAX))
    {
        return ANSC_STATUS_FAILURE;
	}
	//zqiu
    //sprintf(pInfo->Name, "wifi%d", wlanIndex);
    wifi_getRadioIfName(wlanIndex, pInfo->Name);

    pInfo->bUpstream = FALSE;

    //  Currently this is not working
    { 
	char maxBitRate[32] = {0};
	wifi_getRadioMaxBitRate(wlanIndex, maxBitRate);
	wifiDbgPrintf("%s: wifi_getRadioMaxBitRate returned %s\n", __FUNCTION__, maxBitRate);
//>> zqiu: fix Wifi MaxBitRate Parsing
	if (strstr(maxBitRate, "Mb/s")) {
		//216.7 Mb/s
		pInfo->MaxBitRate = strtof(maxBitRate,0);
	} else if (strstr(maxBitRate, "Gb/s")) {
        //1.3 Gb/s
        pInfo->MaxBitRate = strtof(maxBitRate,0) * 1000;
    } else {
		//Auto or Kb/s
		pInfo->MaxBitRate = 0;
	}


//	if (strncmp(maxBitRate,"Auto",strlen("Auto")) == 0)
//	{ 
//	    pInfo->MaxBitRate = COSA_DML_WIFI_TXRATE_Auto;
//
//	} else if (strncmp(maxBitRate,"6M",strlen("6M")) == 0)
//	{
//	    pInfo->MaxBitRate = COSA_DML_WIFI_TXRATE_6M;
//	    
//	} else if (strncmp(maxBitRate,"9M",strlen("9M")) == 0)
//	{
//	    pInfo->MaxBitRate = COSA_DML_WIFI_TXRATE_9M;
//	    
//	} else if (strncmp(maxBitRate,"12M",strlen("12M")) == 0)
//	{
//	    pInfo->MaxBitRate = COSA_DML_WIFI_TXRATE_12M;
//	    
//	} else if (strncmp(maxBitRate,"18M",strlen("18M")) == 0)
//	{
//	    pInfo->MaxBitRate = COSA_DML_WIFI_TXRATE_18M;
//	    
//	} else if (strncmp(maxBitRate,"24M",strlen("24M")) == 0)
//	{
//	    pInfo->MaxBitRate = COSA_DML_WIFI_TXRATE_24M;
//	    
//	} else if (strncmp(maxBitRate,"36M",strlen("36M")) == 0)
//	{
//	    pInfo->MaxBitRate = COSA_DML_WIFI_TXRATE_36M;
//	    
//	} else if (strncmp(maxBitRate,"48M",strlen("48M")) == 0)
//	{
//	    pInfo->MaxBitRate = COSA_DML_WIFI_TXRATE_48M;
//	    
//	} else if (strncmp(maxBitRate,"54M",strlen("54M")) == 0)
//	{
//	    pInfo->MaxBitRate = COSA_DML_WIFI_TXRATE_54M;
//	    
//	}
//<<
    }

    /*CID: 69532 Out-of-bounds access*/
    char frequencyBand[64] = {0};
    int ret = RETURN_ERR;
    ULONG supportedStandards = 0;

    wifi_getRadioSupportedFrequencyBands(wlanIndex, frequencyBand);
    ret = CosaDmlWiFiRadiogetSupportedStandards(wlanIndex, &supportedStandards);
    if(ret == RETURN_OK)
    {
        pInfo->SupportedStandards =  supportedStandards;

    }
    //zqiu: Make it more generic
    if (strstr(frequencyBand,"2.4") != NULL) {
        pInfo->SupportedFrequencyBands = COSA_DML_WIFI_FREQ_BAND_2_4G; /* Bitmask of COSA_DML_WIFI_FREQ_BAND */
        pInfo->IEEE80211hSupported     = FALSE;
    } else if (strstr(frequencyBand,"5G_11N") != NULL) {
        pInfo->SupportedFrequencyBands = COSA_DML_WIFI_FREQ_BAND_5G; /* Bitmask of COSA_DML_WIFI_FREQ_BAND */
        pInfo->IEEE80211hSupported     = TRUE;	    
    } else if (strstr(frequencyBand,"5G_11AC") != NULL) {
        pInfo->SupportedFrequencyBands = COSA_DML_WIFI_FREQ_BAND_5G; /* Bitmask of COSA_DML_WIFI_FREQ_BAND */
        pInfo->IEEE80211hSupported     = TRUE;
    } 
#if defined (_WIFI_AX_SUPPORT_)
    else if (strstr(frequencyBand,"5G_11AX") != NULL) {
        pInfo->SupportedFrequencyBands = COSA_DML_WIFI_FREQ_BAND_5G; /* Bitmask of COSA_DML_WIFI_FREQ_BAND */
        pInfo->IEEE80211hSupported     = TRUE;
    }
#endif
    else {
        // if we can't determine frequency band assue wifi0 is 2.4 and wifi1 is 5 11n
        if (wlanIndex == 0)
    {
        pInfo->SupportedFrequencyBands = COSA_DML_WIFI_FREQ_BAND_2_4G; /* Bitmask of COSA_DML_WIFI_FREQ_BAND */
        pInfo->IEEE80211hSupported     = FALSE;
    }
    else 
    {	
	//zqiu: set 11ac as 5G default
        pInfo->SupportedFrequencyBands = COSA_DML_WIFI_FREQ_BAND_5G; /* Bitmask of COSA_DML_WIFI_FREQ_BAND */
        pInfo->IEEE80211hSupported     = TRUE;
        }
    }

    wifi_getRadioPossibleChannels(wlanIndex, pInfo->PossibleChannels);

#if defined(_HUB4_PRODUCT_REQ_)
    wifi_getRadioAutoChannelSupported(wlanIndex, &pInfo->AutoChannelSupported);
#else
    pInfo->AutoChannelSupported = TRUE;
#endif

    /*RDKB-20055*/
    wifi_getRadioTransmitPowerSupported(wlanIndex, pInfo->TransmitPowerSupported);

    return ANSC_STATUS_SUCCESS;
}

    
/* Description:
 *	The API retrieves the complete info of the WiFi radio designated by index. 
 *	The usual process is the caller gets the total number of entries, 
 *	then iterate through those by calling this API.
 * Arguments:
 * 	ulIndex		Indicates the index number of the entry.
 * 	pEntry		To receive the complete info of the entry.
 */
ANSC_STATUS
CosaDmlWiFiRadioGetEntry
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        PCOSA_DML_WIFI_RADIO_FULL   pEntry
    )
{
wifiDbgPrintf("%s\n",__FUNCTION__);
    if (!pEntry) return ANSC_STATUS_FAILURE;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadio      = pEntry;
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg   = &pWifiRadio->Cfg;
    PCOSA_DML_WIFI_RADIO_SINFO      pWifiRadioSinfo = &pWifiRadio->StaticInfo;
    PCOSA_DML_WIFI_RADIO_DINFO      pWifiRadioDinfo = &pWifiRadio->DynamicInfo;

    pWifiRadio->Cfg.InstanceNumber = ulIndex+1;
    CosaDmlWiFiRadioSetDefaultCfgValues(hContext,ulIndex,pWifiRadioCfg);
    CosaDmlWiFiRadioGetCfg(NULL, pWifiRadioCfg);
    CosaDmlWiFiRadioGetSinfo(NULL, pWifiRadioCfg->InstanceNumber, pWifiRadioSinfo);    
    CosaDmlWiFiRadioGetDinfo(NULL, pWifiRadioCfg->InstanceNumber, pWifiRadioDinfo);    

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiRadioSetDefaultCfgValues
    (
        ANSC_HANDLE                 hContext,
        unsigned long               ulIndex,
        PCOSA_DML_WIFI_RADIO_CFG    pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    if (!pCfg) return ANSC_STATUS_FAILURE;



    if (0 == ulIndex)
    {
	strcpy(pCfg->Alias,"Radio0");
    } else
    {
        strcpy(pCfg->Alias,"Radio1");
    }
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiRadioSetValues
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        ULONG                       ulInstanceNumber,
        char*                       pAlias
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulIndex);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    UNREFERENCED_PARAMETER(pAlias);
        return ANSC_STATUS_SUCCESS;
}

#if 0
static ANSC_STATUS
CosaDmlWiFiRadioGetTransmitPowerPercent
    (
        int wlanIndex,
        int *transmitPowerPercent
    )
{
    int percent = 0;
    ULONG curTransmitPower;

    if (!transmitPowerPercent) return ANSC_STATUS_FAILURE;

    wifi_getRadioTransmitPower(wlanIndex, &curTransmitPower);

#if defined(_COSA_BCM_MIPS_)|| defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_)
    percent = curTransmitPower;
#else
    ULONG maxTransmitPower;
    // If you set to > than the max it sets to max - Atheros logic
    wifi_setRadioTransmitPower(wlanIndex, 30);
    wifi_getRadioTransmitPower(wlanIndex, &maxTransmitPower);
/* zqiu: do not merge
    int retries = 0;
    while ( (retries < 5) && ( (maxTransmitPower <= 5) || (maxTransmitPower >= 30) ) ) {
          wifiDbgPrintf("%s: maxTransmitPower wifi%d = %d sleep and retry (%d) \n", __func__, wlanIndex, maxTransmitPower, retries);
          sleep(1);
          wifi_getRadioTransmitPower(wlanIndex, &maxTransmitPower);
          retries++;
    } 
*/
    wifi_setRadioTransmitPower(wlanIndex, curTransmitPower);

    if (maxTransmitPower == curTransmitPower) percent = 100;
    else if ((maxTransmitPower-2) == curTransmitPower) percent = 75;
    else if ((maxTransmitPower-3) == curTransmitPower) percent = 50;
    else if ((maxTransmitPower-6) == curTransmitPower) percent = 25;
    else if ((maxTransmitPower-9) == curTransmitPower) percent = 12;

    // if a match was not found set to 100%
    if ( percent == 0 ) {
        // set to max Transmit Power
        percent = 100;
        wifi_setRadioTransmitPower(wlanIndex, maxTransmitPower);
    }
#endif

    *transmitPowerPercent = percent;

    return ANSC_STATUS_SUCCESS;
}
#endif

static ANSC_STATUS
CosaDmlWiFiRadioSetTransmitPowerPercent
    (
        int wlanIndex,
        int transmitPowerPercent
    )
{



    wifiDbgPrintf("%s: enter wlanIndex %d transmitPowerPercent %d \n", __func__, wlanIndex, transmitPowerPercent);

#if defined(_COSA_BCM_MIPS_)|| defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_) || defined(_INTEL_WAV_)
    wifi_setRadioTransmitPower(wlanIndex, transmitPowerPercent);
#else
    ULONG curTransmitPower;
    int retries = 0;
    int transmitPower = 0;
    ULONG maxTransmitPower;
    int ret = wifi_getRadioTransmitPower(wlanIndex, &curTransmitPower);
    if (ret == 0) {

        // If you set to > than the max it sets to max - Atheros logic
        wifi_setRadioTransmitPower(wlanIndex, 30);
        wifi_getRadioTransmitPower(wlanIndex, &maxTransmitPower);
        while ( (retries < 5) && ((maxTransmitPower <= 5) || (maxTransmitPower >= 30) )  ) {
              wifiDbgPrintf("%s: maxTransmitPower wifi%d = %lu sleep and retry (%d) \n", __func__, wlanIndex, maxTransmitPower, retries);
              sleep(1);
              wifi_getRadioTransmitPower(wlanIndex, &maxTransmitPower);
              retries++;
        } 
        wifi_setRadioTransmitPower(wlanIndex, curTransmitPower);
        wifiDbgPrintf("%s: maxTransmitPower wifi%d = %lu \n", __func__, wlanIndex, maxTransmitPower);
        if (maxTransmitPower > 0) {
            if (transmitPowerPercent == 100) transmitPower = maxTransmitPower;
            if (transmitPowerPercent == 75) transmitPower = maxTransmitPower-2;
            if (transmitPowerPercent == 50) transmitPower = maxTransmitPower-3;
            if (transmitPowerPercent == 25) transmitPower = maxTransmitPower-6;
            if (transmitPowerPercent == 12) transmitPower = maxTransmitPower-9;

            wifiDbgPrintf("%s: transmitPower wifi%d = %d percent = %d \n", __func__, wlanIndex, transmitPower, transmitPowerPercent);

            if (transmitPower != 0) {
                wifi_setRadioTransmitPower(wlanIndex, transmitPower);
            } 
        }
    }
#endif

    return ANSC_STATUS_SUCCESS;
}

/* zqiu: keep old function
ANSC_STATUS
CosaDmlWiFiRadioPushCfg
    (
        PCOSA_DML_WIFI_RADIO_CFG    pCfg,        // Identified by InstanceNumber 
        ULONG  wlanAthIndex,
        BOOLEAN firstVap
    )
{
    PCOSA_DML_WIFI_RADIO_CFG        pRunningCfg  = &sWiFiDmlRadioRunningCfg[pCfg->InstanceNumber-1];
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    wifiDbgPrintf("%s Config changes  \n",__FUNCTION__);
    int  wlanIndex;

    if (!pCfg )
    {
        return ANSC_STATUS_FAILURE;
    }

    wlanIndex = (ULONG) pCfg->InstanceNumber-1;  
    wifiDbgPrintf("%s[%d] Config changes  wlanIndex %d wlanAthIndex %d firstVap %s\n",__FUNCTION__, __LINE__, wlanIndex, wlanAthIndex, (firstVap==TRUE) ? "TRUE" : "FALSE");

    // Push parameters that are set on the wifi0/wifi1 interfaces if this is the first VAP enabled on the radio
    // iwpriv / iwconfig wifi# cmds
    if (firstVap == TRUE) {
        wifi_pushTxChainMask(wlanIndex);	//, pCfg->X_CISCO_COM_HTTxStream);
        wifi_pushRxChainMask(wlanIndex);	//, pCfg->X_CISCO_COM_HTRxStream);
        wifi_pushDefaultValues(wlanIndex);
        CosaDmlWiFiRadioSetTransmitPowerPercent(wlanIndex, pCfg->TransmitPower);
        wifi_setRadioAMSDUEnable(wlanIndex, pCfg->X_CISCO_COM_AggregationMSDU);
        wifi_setRadioSTBCEnable(wlanIndex,pCfg->X_CISCO_COM_STBCEnable);
    }

    // Push the parameters that are set based on the ath interface
    // iwpriv / iwconfig ath# cmds
    wifi_pushChannelMode(wlanAthIndex);
    if (pCfg->AutoChannelEnable == TRUE) {
        wifi_pushChannel(wlanAthIndex, 0);
    } else {
        wifi_pushChannel(wlanAthIndex, pCfg->Channel);
    }

    BOOL enable = (pCfg->GuardInterval == 2) ? FALSE : TRUE;
    wifi_setRadioGuardInterval(wlanAthIndex, enable);

    wifi_setRadioCtsProtectionEnable(wlanAthIndex, pCfg->CTSProtectionMode);
    wifi_setApBeaconInterval(wlanAthIndex, pCfg->BeaconInterval);
    wifi_setDTIMInterval(wlanAthIndex, pCfg->DTIMInterval);

    //  Only set Fragmentation if mode is not n and therefore not HT
    if ( (pCfg->OperatingStandards|COSA_DML_WIFI_STD_n) == 0) {
        wifi_setRadioFragmentationThreshold(wlanAthIndex, pCfg->FragmentationThreshold);
    }
    wifi_setApRtsThreshold(wlanAthIndex, pCfg->RTSThreshold);
    wifi_setRadioObssCoexistenceEnable(wlanAthIndex, pCfg->ObssCoex); 

    return ANSC_STATUS_SUCCESS;
}
*/

ANSC_STATUS
CosaDmlWiFiRadioPushCfg
    (
        PCOSA_DML_WIFI_RADIO_CFG    pCfg        /* Identified by InstanceNumber */
    )
{
    wifiDbgPrintf("%s Config changes  \n",__FUNCTION__);
    int  wlanIndex;

    if (!pCfg )
    {
        return ANSC_STATUS_FAILURE;
    }

    wlanIndex = (ULONG) pCfg->InstanceNumber-1;  
    wifiDbgPrintf("%s[%d] Config changes  wlanIndex %d \n",__FUNCTION__, __LINE__, wlanIndex);

	//zqiu: replace with INT wifi_applyRadioSettings(INT radioIndex);
	//>>	
    //wifi_pushChannelMode(wlanIndex);
    //wifi_pushChannel(wlanIndex, pCfg->Channel);
    //wifi_pushTxChainMask(wlanIndex);
    //wifi_pushRxChainMask(wlanIndex);
    //wifi_pushDefaultValues(wlanIndex);
	wifi_setRadioChannel(wlanIndex, pCfg->Channel);
	wifi_applyRadioSettings(wlanIndex);
	//<<
	
    //BOOL enable = (pCfg->GuardInterval == 2) ? FALSE : TRUE;
    wifi_setRadioGuardInterval(wlanIndex, (pCfg->GuardInterval == 2)?"800nsec":"Auto");

    CosaDmlWiFiRadioSetTransmitPowerPercent(wlanIndex, pCfg->TransmitPower);

    wifi_setRadioCtsProtectionEnable(wlanIndex, pCfg->CTSProtectionMode);
    wifi_setApBeaconInterval(wlanIndex, pCfg->BeaconInterval);
	wifi_setApDTIMInterval(wlanIndex, pCfg->DTIMInterval);

    //  Only set Fragmentation if mode is not n and therefore not HT
    if ( (pCfg->OperatingStandards|COSA_DML_WIFI_STD_n) == 0) {
        wifi_setRadioFragmentationThreshold(wlanIndex, pCfg->FragmentationThreshold);
    }
    wifi_setApRtsThreshold(wlanIndex, pCfg->RTSThreshold);
    wifi_setRadioObssCoexistenceEnable(wlanIndex, pCfg->ObssCoex); 
    wifi_setRadioAMSDUEnable(wlanIndex, pCfg->X_CISCO_COM_AggregationMSDU);
    wifi_setRadioSTBCEnable(wlanIndex,pCfg->X_CISCO_COM_STBCEnable);

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiRadioApplyCfg
(
PCOSA_DML_WIFI_RADIO_CFG    pCfg        /* Identified by InstanceNumber */
) 
{
    PCOSA_DML_WIFI_RADIO_CFG        pRunningCfg    = (PCOSA_DML_WIFI_RADIO_CFG )NULL;
    int  wlanIndex;
    BOOL createdNewVap = FALSE;
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    PCOSA_DML_WIFI_SSID             pWifiSsid           = (PCOSA_DML_WIFI_SSID      )NULL;
    PCOSA_DML_WIFI_AP               pWifiAp             = (PCOSA_DML_WIFI_AP        )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj            = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
#endif

    wifiDbgPrintf("%s Config changes  \n", __FUNCTION__);

    if (!pCfg )
    {
        return ANSC_STATUS_FAILURE;
    }

    wlanIndex = (ULONG) pCfg->InstanceNumber-1;  
    if (( wlanIndex < 0 ) || ( wlanIndex >= RADIO_INDEX_MAX ) ) /*RDKB-13101 & CID:-34558*/
    {
        return ANSC_STATUS_FAILURE;
    }

    /*RDKB-6907, CID-33379, null check before use*/
    pRunningCfg  = &sWiFiDmlRadioRunningCfg[pCfg->InstanceNumber-1];

    // Apply Settings to Radio
    printf("%s Calling pthread_mutex_lock for sWiFiThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 
    pthread_mutex_lock(&sWiFiThreadMutex);
    printf("%s Called pthread_mutex_lock for sWiFiThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 
    wifiDbgPrintf("%s Config changes  %d\n",__FUNCTION__, __LINE__);

    wifiDbgPrintf("%s[%d] wlanIndex %d\n",__FUNCTION__, __LINE__, wlanIndex);

    // Need to get vlan configuration
    CosaDmlWiFiGetBridgePsmData();

    if ( gRadioPowerSetting != COSA_DML_WIFI_POWER_DOWN  &&
         gRadioNextPowerSetting != COSA_DML_WIFI_POWER_DOWN )
    {

        // If the Radio is disabled see if there are any SSIDs that need to be brought down
        if (pCfg->bEnabled == FALSE )
        {
            BOOL activeVaps = FALSE;

	    CcspWifiEventTrace(("RDK_LOG_NOTICE, WiFi radio %s is set to DOWN\n ",pCfg->Alias));
            CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : WiFi radio %s is set to DOWN \n ",pCfg->Alias));
            t2_event_d("WIFI_INFO_2GRadio_Down", 1);

            wifi_getRadioStatus(wlanIndex, &activeVaps);
            // bring down all VAPs
            if (activeVaps == TRUE)
            {
                int i;
                for (i = wlanIndex; i < gSsidCount; i+=2)
                {
                    BOOL vapEnabled = FALSE;
					//zqiu:>>
					char status[64]={0};
                    //wifi_getApEnable(i, &vapEnabled); 
					wifi_getSSIDStatus(i, status);
					vapEnabled=(strcmp(status,"Enabled")==0);					
fprintf(stderr, "----# %s %d 	ath%d %s\n", __func__, __LINE__, i, status);
					//<<
                    if (vapEnabled == TRUE)
                    {
                        //PCOSA_DML_WIFI_APSEC_CFG pRunningApSecCfg = &sWiFiDmlApSecurityRunning[i].Cfg;

					char buf[1024];
					wifi_getApName(i, buf);	
					v_secure_system("ifconfig %s down 2>/dev/null", buf);
					buf[0]='\0';

#if 0
fprintf(stderr, "----# %s %d 	wifi_setApEnable %d false\n", __func__, __LINE__, i);
						int retStatus = wifi_setApEnable(i, FALSE);
						if(retStatus == 0) {
							CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wifi_setApEnable success index %d , false ",__FUNCTION__,i));
						}
						else {
								CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wifi_setApEnable failed  index %d , false",__FUNCTION__,i));
						}
                        wifi_deleteAp(i); 
						//<<
						
                        if (pRunningApSecCfg->ModeEnabled >= COSA_DML_WIFI_SECURITY_WPA_Personal)
                        {
                            wifi_removeApSecVaribles(i);
                            sWiFiDmlRestartHostapd = TRUE;
                            wifiDbgPrintf("%s %d sWiFiDmlRestartHostapd set to TRUE\n",__FUNCTION__, __LINE__);
                        }
#endif
                    }
                }

#if 0
                if (sWiFiDmlRestartHostapd == TRUE)
                {
                    // Bounce hostapd to pick up security changes
                    wifi_stopHostApd();
                    wifi_startHostApd(); 
                    sWiFiDmlRestartHostapd = FALSE;
                    wifiDbgPrintf("%s %d sWiFiDmlRestartHostapd set to FALSE\n",__FUNCTION__, __LINE__);
                }
#endif
            }
        } else
        {

            int i;
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
            int vapIndex = 0;
#endif
            BOOL activeVaps = FALSE;

	    CcspWifiEventTrace(("RDK_LOG_NOTICE, WiFi radio %s is set to UP\n ",pCfg->Alias));
            CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : WiFi radio %s is set to UP \n ",pCfg->Alias));

            wifi_getRadioStatus(wlanIndex, &activeVaps);
            wifiDbgPrintf("%s Config changes   %dApplySettingSSID = %d\n",__FUNCTION__, __LINE__, pCfg->ApplySettingSSID);

#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
            for (i=wlanIndex; i < 16; i += 2)
#else
            for (i=wlanIndex; i < gSsidCount; i++)
#endif
            {
                // if ApplySettingSSID has been set, only apply changes to the specified SSIDs
                if ( (pCfg->ApplySettingSSID != 0) && !((1<<i) & pCfg->ApplySettingSSID ))
                {
                    printf("%s: Skipping SSID %d, it was not in ApplySettingSSID(%d)\n", __func__, i, pCfg->ApplySettingSSID);
                    sWiFiDmlAffectedVap[i] = FALSE;
                    continue;
                }
                PCOSA_DML_WIFI_SSID_CFG pStoredSsidCfg = &sWiFiDmlSsidStoredCfg[i];
                PCOSA_DML_WIFI_AP_CFG pStoredApCfg = &sWiFiDmlApStoredCfg[i].Cfg;
                PCOSA_DML_WIFI_APSEC_FULL pStoredApSecEntry = &sWiFiDmlApSecurityStored[i];
                PCOSA_DML_WIFI_APSEC_CFG pStoredApSecCfg = &sWiFiDmlApSecurityStored[i].Cfg;
                PCOSA_DML_WIFI_APWPS_CFG pStoredApWpsCfg = &sWiFiDmlApWpsStored[i].Cfg;
                PCOSA_DML_WIFI_APACCT_CFG pStoredApAcctCfg = &sWiFiDmlApAcctStored[i].Cfg;

                PCOSA_DML_WIFI_SSID_CFG pRunningSsidCfg = &sWiFiDmlSsidRunningCfg[i];
                PCOSA_DML_WIFI_APSEC_FULL pRunningApSecEntry = &sWiFiDmlApSecurityRunning[i];
                PCOSA_DML_WIFI_APSEC_CFG pRunningApSecCfg = &sWiFiDmlApSecurityRunning[i].Cfg;
                PCOSA_DML_WIFI_APWPS_CFG pRunningApWpsCfg = &sWiFiDmlApWpsRunning[i].Cfg;
                PCOSA_DML_WIFI_APACCT_CFG pRunningApAcctCfg = &sWiFiDmlApAcctRunning[i].Cfg;
                BOOL up;
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
                vapIndex = pStoredSsidCfg->InstanceNumber-1;
#endif
				//zqiu:>>
				char status[64]={0};
                //wifi_getApEnable(i,&up); 
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
				wifi_getSSIDStatus(i, status);
#else
                                wifi_getSSIDStatus(vapIndex, status);
#endif
				up=(strcmp(status,"Enabled")==0);				
fprintf(stderr, "----# %s %d 	ath%d %s\n", __func__, __LINE__, i, status);
fprintf(stderr, "----# %s %d 	pStoredSsidCfg->bEnabled=%d  pRunningSsidCfg->bEnabled=%d\n", __func__, __LINE__, pStoredSsidCfg->bEnabled, pRunningSsidCfg->bEnabled);				
fprintf(stderr, "----# %s %d 	pStoredSsidCfg->EnableOnline=%d  pStoredSsidCfg->RouterEnabled=%d\n", __func__, __LINE__, pStoredSsidCfg->EnableOnline, pStoredSsidCfg->RouterEnabled);				
				//<<
				
                if ( (up == FALSE) && 
                     (pStoredSsidCfg->bEnabled == TRUE) &&  
                     ( (pStoredSsidCfg->EnableOnline == FALSE) ||
                       (pStoredSsidCfg->RouterEnabled == TRUE)))
                {
                    sWiFiDmlAffectedVap[i] = TRUE;
#if (_XB6_PRODUCT_REQ_)
//Enabling  the ssids
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
                    wifi_setSSIDEnable(i,TRUE);
#else
                    wifi_setSSIDEnable(vapIndex,TRUE);
                    wifi_createAp(vapIndex,wlanIndex,pStoredSsidCfg->SSID, (pStoredApCfg->SSIDAdvertisementEnabled == TRUE) ? FALSE : TRUE);
#endif
#else
                    wifi_createAp(i,wlanIndex,pStoredSsidCfg->SSID, (pStoredApCfg->SSIDAdvertisementEnabled == TRUE) ? FALSE : TRUE);
#endif
                    createdNewVap = TRUE;
                    // push Radio config to new VAP
					//zqiu:
					// first VAP created for radio, push Radio config
                    if (activeVaps == FALSE)
                    {

						//CosaDmlWiFiRadioPushCfg(pCfg, i,((activeVaps == FALSE) ? TRUE : FALSE));
						CosaDmlWiFiRadioPushCfg(pCfg);
						activeVaps = TRUE;
					}
                    CosaDmlWiFiApPushCfg(pStoredApCfg); 
					// push mac filters
                    CosaDmlWiFiApMfPushCfg(sWiFiDmlApMfCfg[i], i);					
                    CosaDmlWiFiApPushMacFilter(sWiFiDmlApMfQueue[i], i);
                    CosaDmlWiFiApAcctApplyCfg(pStoredApAcctCfg, pStoredApCfg->InstanceNumber);
                    // push security and restart hostapd
                    CosaDmlWiFiApSecPushCfg(pStoredApSecCfg, pStoredApCfg->InstanceNumber);
                    
                    wifi_pushBridgeInfo(i);

                    // else if up=TRUE, but should be down
                } else if (up==TRUE && 
                           (  (pStoredSsidCfg->bEnabled == FALSE) ||  
                              (  (pStoredSsidCfg->EnableOnline == TRUE) && 
                                 (pStoredSsidCfg->RouterEnabled == FALSE )) ) )
                {
					//zqiu:>>
					if(pStoredSsidCfg->bEnabled==FALSE) {
fprintf(stderr, "----# %s %d 	wifi_setApEnable %d false\n", __func__, __LINE__, i);				
						int retStatus = wifi_setApEnable(i, FALSE);
						if(retStatus == 0) {
							CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wifi_setApEnable success index %d , false \n",__FUNCTION__,i));
						}
						else {
								CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wifi_setApEnable failed index %d , false \n",__FUNCTION__,i));
						}
					}
					//<<
//Disbling the ssids
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
sWiFiDmlAffectedVap[i] = TRUE;
wifi_setSSIDEnable(vapIndex, FALSE);
#else
wifi_deleteAp(i);
#endif
                    if (pRunningApSecCfg->ModeEnabled >= COSA_DML_WIFI_SECURITY_WPA_Personal)
                    {
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
                        wifi_removeApSecVaribles(i);
#else
                        wifi_removeApSecVaribles(vapIndex);
#endif
                        sWiFiDmlRestartVap[i] = TRUE; 
                        sWiFiDmlRestartHostapd = TRUE;
                        wifiDbgPrintf("%s %d sWiFiDmlRestartHostapd set to TRUE\n",__FUNCTION__, __LINE__);
                    }

                    // else if up=TRUE and changes, apply them
                } else if (up==TRUE)
                {
                    BOOLEAN wpsChange = (strcmp(pRunningSsidCfg->SSID, pStoredSsidCfg->SSID) != 0) ? TRUE : FALSE;
                    sWiFiDmlAffectedVap[i] = TRUE;

                    // wifi_ifConfigDown(i);
					//zqiu:>>
					if(pStoredSsidCfg->bEnabled==TRUE) {
fprintf(stderr, "----# %s %d 	wifi_setApEnable %d true\n", __func__, __LINE__, i);				
						int retStatus = wifi_setApEnable(i, TRUE);
						if(retStatus == 0) {
							CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wifi_setApEnable success index %d , true \n",__FUNCTION__,i));
						}
						else {
								CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wifi_setApEnable failed  index %d , true \n",__FUNCTION__,i));
						}
					}
					//<<
					
                    CosaDmlWiFiSsidApplyCfg(pStoredSsidCfg);
                    CosaDmlWiFiApApplyCfg(pStoredApCfg);

                    if ( memcmp(pStoredApSecEntry,pRunningApSecEntry,sizeof(COSA_DML_WIFI_APSEC_FULL)) != 0 ||
                         memcmp(pStoredApWpsCfg, pRunningApWpsCfg, sizeof(COSA_DML_WIFI_APWPS_CFG)) != 0  ||
                         memcmp(pStoredApAcctCfg, pRunningApAcctCfg, sizeof(COSA_DML_WIFI_APACCT_CFG)) != 0  ||
                         sWiFiDmlWepChg[i] == TRUE ||
                         sWiFiDmlUpdatedAdvertisement[i] == TRUE ||
                         wpsChange == TRUE )
                    {
                        CosaDmlWiFiApSecApplyCfg(pStoredApSecCfg, pStoredApCfg->InstanceNumber); 
                        sWiFiDmlWepChg[i] = FALSE;
                        sWiFiDmlUpdatedAdvertisement[i] = FALSE;
                    }
                    CosaDmlWiFiApWpsApplyCfg(pStoredApWpsCfg,i);
                    CosaDmlWiFiApAcctApplyCfg(pStoredApAcctCfg,i);

#if defined(ENABLE_FEATURE_MESHWIFI)
                    // Notify Mesh components of an AP config change
                    MeshNotifySecurityChange(i, pStoredApSecCfg);
#endif
                }
            } // for each SSID

            /* if any of the user-controlled SSID is up, turn on the WiFi LED */
            int  MbssEnable = 0;
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
            for (i=wlanIndex; i < 16; i += 2)
#else
            for (i=wlanIndex; i < gSsidCount; i++)
#endif
            {
                if (sWiFiDmlSsidStoredCfg[i].bEnabled == TRUE)
                    MbssEnable += 1<<((i-wlanIndex)/2);
            }
            //zqiu: let driver contorl LED
			//wifi_setLED(wlanIndex, (MbssEnable & pCfg->MbssUserControl)?1:0);

            if (sWiFiDmlRestartHostapd == TRUE)
            {
                // Bounce hostapd to pick up security changes
#if defined(FEATURE_HOSTAP_AUTHENTICATOR) && !defined (_XB7_PRODUCT_REQ_)
                BOOLEAN isHostapdAuthEnabled = FALSE;
		BOOLEAN isVapEnabled = FALSE;
                CosaDmlWiFiGetHostapdAuthenticatorEnable(&isHostapdAuthEnabled);
                if (isHostapdAuthEnabled)
                {
                    i = 0;
                    PCOSA_DATAMODEL_WIFI            pWiFi     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
                    if(!pWiFi){
                            CcspTraceError(("%s:%d: Wifi Instance is NULL\n", __func__, __LINE__));
                            return FALSE;
                    }

                    PSINGLE_LINK_ENTRY              pSLinkEntrySsid = (PSINGLE_LINK_ENTRY       )NULL;
                    PSINGLE_LINK_ENTRY              pSLinkEntryAp   = (PSINGLE_LINK_ENTRY       )NULL;
                    PCOSA_DML_WIFI_AP               pWifiAp         = (PCOSA_DML_WIFI_AP        )NULL;
                    PCOSA_DML_WIFI_SSID             pWifiSsid       = (PCOSA_DML_WIFI_SSID      )NULL;

                    pSLinkEntryAp = AnscQueueGetFirstEntry(&pWiFi->AccessPointQueue);
                    pSLinkEntrySsid = AnscQueueGetFirstEntry(&pWiFi->SsidQueue);
                    while (pSLinkEntryAp && pSLinkEntrySsid)
                    {
                        if (!(pWifiAp = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntryAp)->hContext))
                        {
                                CcspTraceError(("%s Error linking Data Model object!\n",__FUNCTION__));
                                return FALSE;
                        }
                        if (!(pWifiSsid = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntrySsid)->hContext))
                        {
                                CcspTraceError(("%s Error linking Data Model object!\n",__FUNCTION__));
                                return FALSE;
                        }
			wifi_getApEnable(i,&isVapEnabled);
                        if (isVapEnabled)
                        {
#if !defined (_XB7_PRODUCT_REQ_)
				/* If security mode is set to open, init lib only if greylisting is enabled */
				if (pWifiAp->SEC.Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_None && !pWiFi->bEnableRadiusGreyList)
				{
                                    i++;
                                    pSLinkEntryAp = AnscQueueGetNextEntry(pSLinkEntryAp);
                                    pSLinkEntrySsid = AnscQueueGetNextEntry(pSLinkEntrySsid);
                                    continue;
				}
#endif

                                if ((wlanIndex == 0 && (i % 2 == 0)) || (wlanIndex == 1 && (i % 2 == 1))) {
                            	    deinit_lib_hostapd(i);
                                    init_lib_hostapd(pWiFi, pWifiAp, pWifiSsid, (i % 2 == 0) ? &(pWiFi->pRadio+0)->Radio : &(pWiFi->pRadio+1)->Radio);
#if defined (FEATURE_SUPPORT_INTERWORKING)
                                    if (pWifiAp->AP.Cfg.InterworkingCapability == TRUE && ( pWifiAp->AP.Cfg.InterworkingEnable == TRUE))
                                        CosaDmlWiFi_setInterworkingElement(&pWifiAp->AP.Cfg);
#endif
                                    if (pWifiAp->AP.Cfg.IEEE80211uCfg.PasspointCfg.Status)
                                        CosaDmlWiFi_ApplyRoamingConsortiumElement(&pWifiAp->AP.Cfg);

                                    if (pWifiAp->AP.Cfg.BSSTransitionImplemented == TRUE && pWifiAp->AP.Cfg.BSSTransitionActivated)
                                        CosaDmlWifi_setBSSTransitionActivated(&pWifiAp->AP.Cfg, pWifiAp->AP.Cfg.InstanceNumber - 1);
                                }
                        }
                        i++;
                        pSLinkEntryAp = AnscQueueGetNextEntry(pSLinkEntryAp);
                        pSLinkEntrySsid = AnscQueueGetNextEntry(pSLinkEntrySsid);
                    }
                }
                else
                {
                    wifi_stopHostApd();
                    wifi_startHostApd();
                }
#elif (defined(_COSA_INTEL_USG_ATOM_) && !defined(_INTEL_WAV_) ) || ( (defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_)) && !defined(_CBR_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_) )
                wifi_restartHostApd();
#else
#if defined(FEATURE_HOSTAP_AUTHENTICATOR)
                BOOLEAN isHostapdAuthEnabled = FALSE;
                CosaDmlWiFiGetHostapdAuthenticatorEnable(&isHostapdAuthEnabled);
                if (!isHostapdAuthEnabled)
                {
                    wifi_stopHostApd();
                    wifi_startHostApd();
                }
#else
                wifi_stopHostApd();
                wifi_startHostApd();
#endif //FEATURE_HOSTAP_AUTHENTICATOR
#endif
                sWiFiDmlRestartHostapd = FALSE;
                wifiDbgPrintf("%s %d sWiFiDmlRestartHostapd set to FALSE\n",__FUNCTION__, __LINE__);
            }

            // If a new SSID was not created, then the radio parameters still need to be pushed.
            if (createdNewVap == FALSE && activeVaps == TRUE)
            {
                int athIndex;


                wifiDbgPrintf("%s Pushing Radio Config changes  %d\n",__FUNCTION__, __LINE__);
                if ( pCfg->OperatingStandards != pRunningCfg->OperatingStandards ||
                     pCfg->OperatingChannelBandwidth != pRunningCfg->OperatingChannelBandwidth ||
                     pCfg->ExtensionChannel != pRunningCfg->ExtensionChannel )
                {

                    // Currently do not allow the channel mode to change while the SSIDs are running
                    // so this code should never be called.  
					//zqiu: Deprecated
                    //wifi_pushChannelMode(wlanIndex);

                } // Mode changed

                if (pCfg->AutoChannelEnable != pRunningCfg->AutoChannelEnable || 
                    (pRunningCfg->AutoChannelEnable == FALSE && pCfg->Channel != pRunningCfg->Channel) )
                {
                    // Currently do not allow the channel to change while the SSIDs are running
                    // so this code should never be called.  
                    //zqiu: Deprecated
					//wifi_pushChannel(wlanIndex, pCfg->Channel); 
                }

/*                if ( pCfg->X_CISCO_COM_HTTxStream != pRunningCfg->X_CISCO_COM_HTTxStream)
                {
                    wifi_pushRadioTxChainMask(wlanIndex);
                }
                if (pCfg->X_CISCO_COM_HTRxStream != pRunningCfg->X_CISCO_COM_HTRxStream)
                {
                    wifi_pushRadioRxChainMask(wlanIndex);
                }
*/                
		if (pCfg->MCS != pRunningCfg->MCS)
                {
                    wifi_setRadioMCS(wlanIndex, pCfg->MCS);
                }

                if (pCfg->TransmitPower != pRunningCfg->TransmitPower)
                {
                    if ( ( gRadioPowerState[wlanIndex] == COSA_DML_WIFI_POWER_UP ) &&
                         ( gRadioNextPowerSetting != COSA_DML_WIFI_POWER_DOWN ) )
                    {
                        CosaDmlWiFiRadioSetTransmitPowerPercent(wlanIndex, pCfg->TransmitPower);
                    } else if ( gRadioPowerState[wlanIndex] == COSA_DML_WIFI_POWER_LOW )
                    {
                        wifiDbgPrintf("%s calling wifi_setRadioTransmitPower to 5 dbM, RadioPower was set to COSA_DML_WIFI_POWER_LOW \n",__FUNCTION__);
                        wifi_setRadioTransmitPower(wlanIndex, 5);
                    }
                }
                
                if (pCfg->X_CISCO_COM_AggregationMSDU != pRunningCfg->X_CISCO_COM_AggregationMSDU)
                {
                    wifi_setRadioAMSDUEnable(wlanIndex, pCfg->X_CISCO_COM_AggregationMSDU);
                }
                if (pCfg->X_CISCO_COM_STBCEnable != pRunningCfg->X_CISCO_COM_STBCEnable )
                {
                    wifi_setRadioSTBCEnable(wlanIndex, pCfg->X_CISCO_COM_STBCEnable);
                }
#if defined(_COSA_BCM_MIPS_)
                if ( pCfg->GuardInterval != pRunningCfg->GuardInterval )
                {
	                wifi_setRadioGuardInterval(wlanIndex, (pCfg->GuardInterval == 2)?"800nsec":"Auto");
                }
#endif
                wifiDbgPrintf("%s Pushing Radio Config changes  %d\n",__FUNCTION__, __LINE__);
                // Find the first ath that is up on the given radio
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
                for (athIndex = wlanIndex; athIndex < 16; athIndex+=2) {
#else
                for (athIndex = wlanIndex; athIndex < gSsidCount; athIndex++) {
#endif
                    BOOL enabled;
                    wifi_getApEnable(athIndex, &enabled);
                    wifiDbgPrintf("%s Pushing Radio Config changes %d %d\n",__FUNCTION__, athIndex, __LINE__);

                    if (enabled == TRUE) {
#if !defined (_COSA_BCM_MIPS_)
                        // These Radio parameters are set on SSID basis (iwpriv/iwconfig ath%d commands) 
                        if (pCfg->GuardInterval != pRunningCfg->GuardInterval)
                        {
                            // pCfg->GuardInterval   
                            // COSA_DML_WIFI_GUARD_INTVL_400ns and COSA_DML_WIFI_GUARD_INTVL_Auto are the
                            // same in the WiFi driver
                            //BOOL enable = (pCfg->GuardInterval == 2) ? FALSE : TRUE;
                            wifi_setRadioGuardInterval(athIndex, (pCfg->GuardInterval == 2)?"800nsec":"Auto");
                        }
#endif
                        if (pCfg->CTSProtectionMode != pRunningCfg->CTSProtectionMode)
                        {
                            wifi_setRadioCtsProtectionEnable(athIndex, pCfg->CTSProtectionMode);
                        }
                        if (pCfg->BeaconInterval != pRunningCfg->BeaconInterval)
                        {
                            wifi_setApBeaconInterval(athIndex, pCfg->BeaconInterval);
                        }
                        if (pCfg->DTIMInterval != pRunningCfg->DTIMInterval)
                        {
                            wifi_setApDTIMInterval(athIndex, pCfg->DTIMInterval);
                        }
                        //  Only set Fragmentation if mode is not n and therefore not HT
                        if (pCfg->FragmentationThreshold != pRunningCfg->FragmentationThreshold &&
                            (pCfg->OperatingStandards|COSA_DML_WIFI_STD_n) == 0)
                        {
                            wifi_setRadioFragmentationThreshold(athIndex, pCfg->FragmentationThreshold);
                        }
                        if (pCfg->RTSThreshold != pRunningCfg->RTSThreshold)
                        {
                            wifi_setApRtsThreshold(athIndex, pCfg->RTSThreshold);
                        }
                        if (pCfg->ObssCoex != pRunningCfg->ObssCoex)
                        {
                            wifi_setRadioObssCoexistenceEnable(athIndex, pCfg->ObssCoex); 
                        }
#if ((!(defined (INTEL_PUMA7) && defined (_XB7_PRODUCT_REQ_))) && (!(defined (DUAL_CORE_XB3)))) //Not including CMXB7 and XB3 as there is no function for setting AutoChannelRefreshPeriod 
                        if (pCfg->AutoChannelRefreshPeriod != pRunningCfg->AutoChannelRefreshPeriod)
                        {
                            wifi_setRadioAutoChannelRefreshPeriod(athIndex, pCfg->AutoChannelRefreshPeriod);
                            CcspTraceInfo(("%s: added new value %lu for RefreshPeriod for %d \n", __FUNCTION__, pCfg->AutoChannelRefreshPeriod, athIndex));
                        }
#endif
                    }
                }
            }

#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
            for (i=wlanIndex; i < 16; i += 2) { 
#else
            for (i=wlanIndex; i < gSsidCount; i++) {
#endif
                if (sWiFiDmlAffectedVap[i] == TRUE || sWiFiDmlRestartVap[i] == TRUE)
                {
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
                    vapIndex = sWiFiDmlSsidStoredCfg[i].InstanceNumber - 1;
#endif
                    if (sWiFiDmlPushWepKeys[i] == TRUE)
                    {
                        PCOSA_DML_WIFI_APSEC_CFG pCfg = &sWiFiDmlApSecurityStored[i].Cfg;
                        CosaDmlWiFiApSecApplyWepCfg(pCfg,i+1);
                    }
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
                    wifi_ifConfigUp(i);
#else
                    /*For new VAPs, update datamodel parameters*/
                    if (createdNewVap == TRUE)
                    {
                        pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
                        pLinkObj = CosaSListGetEntryByInsNum((PSLIST_HEADER)&pMyObject->SsidQueue, sWiFiDmlSsidStoredCfg[i].InstanceNumber);
                        pWifiSsid = pLinkObj->hContext;

                        CosaDmlWiFiSsidGetSinfo((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.InstanceNumber, &(pWifiSsid->SSID.StaticInfo));

                        pLinkObj = CosaSListGetEntryByInsNum((PSLIST_HEADER)&pMyObject->AccessPointQueue, sWiFiDmlApStoredCfg[i].Cfg.InstanceNumber);
                        pWifiAp = pLinkObj->hContext;

                        CosaDmlWiFiApGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->AP);
                        CosaDmlWiFiApSecGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->SEC);
                        CosaDmlWiFiApWpsGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->WPS);
                    }

                    wifi_ifConfigUp(vapIndex);
#endif
                }
                if (sWiFiDmlUpdateVlanCfg[i] == TRUE) {
                    // update vlan configuration
                    wifi_resetApVlanCfg(i); 
                    sWiFiDmlUpdateVlanCfg[i] = FALSE;
                }
                int x=0;
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
                for(x = i; x < 16; x += 2)
#else
                for(x = i; x < gSsidCount; x += 2)
#endif
                {
                    sWiFiDmlAffectedVap[x] = FALSE;
                    sWiFiDmlRestartVap[x]  = FALSE;
                }

                sWiFiDmlAffectedVap[i] = FALSE;
                sWiFiDmlPushWepKeys[i] = FALSE;
                sWiFiDmlRestartVap[i]  = FALSE; 
            }
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
            CosaDmlWiFiSyncBridgeMembers();
#else
            CosaDmlWiFiDeAllocBridgeVlan();
#endif
        }

        pCfg->ApplySetting = FALSE;
        pCfg->ApplySettingSSID = 0;
    }

    printf("%s Calling pthread_mutex_unlock for sWiFiThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 
    pthread_mutex_unlock(&sWiFiThreadMutex);
    printf("%s Called pthread_mutex_unlock for sWiFiThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 

    memcpy(&sWiFiDmlRadioRunningCfg[pCfg->InstanceNumber-1], pCfg, sizeof(COSA_DML_WIFI_RADIO_CFG));
    memcpy(&sWiFiDmlRadioStoredCfg[pCfg->InstanceNumber-1], pCfg, sizeof(COSA_DML_WIFI_RADIO_CFG));

    return ANSC_STATUS_SUCCESS;
}

#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
ANSC_STATUS
CosaDmlWiFiGetNumberOfAPsOnRadio
    (
        UINT    radioIndex,
        UINT    *output_count
    )
{
    INT ret = 0;

    ret = wifi_getNumberOfAPsOnRadio(radioIndex, (INT*)output_count);
    if(ret != 0)
    {
        *output_count = -1;
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
}
#endif

#if defined (FEATURE_HOSTAP_AUTHENTICATOR) && defined(_XB7_PRODUCT_REQ_)
void *wifi_libhostap_apply_settings(void *arg)
{
    UNREFERENCED_PARAMETER(arg);

    pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    sleep(1);

    if ( v_secure_system("wifi_setup.sh stop_security_daemons wl0 wl1") != 0 ) {
        CcspWifiTrace(("RDK_LOG_INFO, %s:%d wifi_setup.sh stop_security_daemons wl0 wl1 failed\n", __FUNCTION__, __LINE__));
        return FALSE;
    }

    if (!mCosaDmlWiFiSetHostapdAuthenticatorEnable((ANSC_HANDLE)pMyObject, FALSE, FALSE))
    {
        CcspWifiTrace(("RDK_LOG_INFO, %s:%d deinit libhostapd failed\n", __FUNCTION__, __LINE__));
    }

    sleep(2);

    if ( v_secure_system("wifi_setup.sh setup_ifaces wl0 wl1") != 0 ) {
        CcspWifiTrace(("RDK_LOG_INFO, %s:%d wifi_setup.sh setup_ifaces wl0 wl1 failed\n", __FUNCTION__, __LINE__));
        return FALSE;
    }

    if (!mCosaDmlWiFiSetHostapdAuthenticatorEnable((ANSC_HANDLE)pMyObject, TRUE, FALSE))
    {
       CcspWifiTrace(("RDK_LOG_INFO, %s:%d init libhostapd failed\n", __FUNCTION__, __LINE__));
    }

    if ( v_secure_system("wifi_setup.sh enable_wifi wl0 wl1") != 0 ) {
        CcspWifiTrace(("RDK_LOG_INFO, %s:%d wifi_setup.sh enable_wifi wl0 wl1 failed\n", __FUNCTION__, __LINE__));
        return FALSE;
    }

    isWifiApplyLibHostapRunning = FALSE;
    pthread_exit(NULL);
}
#endif

#if defined (FEATURE_HOSTAP_AUTHENTICATOR) && defined(_XB7_PRODUCT_REQ_)
void reInitLibHostapd()
{
    pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;

    if ( v_secure_system("wifi_setup.sh stop_security_daemons wl0 wl1") != 0 ) {
        CcspWifiTrace(("RDK_LOG_INFO, %s:%d wifi_setup.sh stop_security_daemons wl0 wl1 failed\n", __FUNCTION__, __LINE__));
        return;
    }

    if (!mCosaDmlWiFiSetHostapdAuthenticatorEnable((ANSC_HANDLE)pMyObject, FALSE, FALSE))
    {
        CcspWifiTrace(("RDK_LOG_INFO, %s:%d deinit libhostapd failed\n", __FUNCTION__, __LINE__));
    }

    sleep(1);

    if ( v_secure_system("wifi_setup.sh setup_ifaces wl0 wl1") != 0 ) {
        CcspWifiTrace(("RDK_LOG_INFO, %s:%d wifi_setup.sh setup_ifaces wl0 wl1 failed\n", __FUNCTION__, __LINE__));
        return ;
    }

    if (!mCosaDmlWiFiSetHostapdAuthenticatorEnable((ANSC_HANDLE)pMyObject, TRUE, FALSE))
    {
       CcspWifiTrace(("RDK_LOG_INFO, %s:%d init libhostapd failed\n", __FUNCTION__, __LINE__));
    }

    if ( v_secure_system("wifi_setup.sh enable_wifi wl0 wl1") != 0 ) {
        CcspWifiTrace(("RDK_LOG_INFO, %s:%d wifi_setup.sh enable_wifi wl0 wl1 failed\n", __FUNCTION__, __LINE__));
        return ;
    }
}
#endif

ANSC_STATUS
CosaDmlWiFiRadioSetCfg
(
ANSC_HANDLE                 hContext,
PCOSA_DML_WIFI_RADIO_CFG    pCfg        /* Identified by InstanceNumber */
)
{
    PCOSA_DML_WIFI_RADIO_CFG        pStoredCfg  = (PCOSA_DML_WIFI_RADIO_CFG)NULL;
    int  wlanIndex;
    BOOL wlanRestart = FALSE;
    BOOLEAN bForceDisableFlag = FALSE;
    BOOL reset_both_radios = FALSE;

    UNREFERENCED_PARAMETER(hContext);
    wifiDbgPrintf("%s Config changes  \n",__FUNCTION__);
    CcspWifiTrace(("RDK_LOG_WARN,%s\n",__FUNCTION__));

    if (!pCfg )
    {
        return ANSC_STATUS_FAILURE;
    }

    wlanIndex = (ULONG) pCfg->InstanceNumber-1;
    if ( (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
        return ANSC_STATUS_FAILURE;
    }
    /*RDKB-6907, CID-32973, null check before use*/
    pStoredCfg = &sWiFiDmlRadioStoredCfg[pCfg->InstanceNumber-1];


    pCfg->LastChange             = AnscGetTickInSeconds();
    printf("%s: LastChange %lu \n", __func__,pCfg->LastChange);

#if defined(_INTEL_BUG_FIXES_)
#define INTEL_START_INVALID_160_CHANNELS 132
#define INTEL_160_MHZ_SAFE_CHANNEL 36
    // Check for DFS combined with 160 MHz bandwidth
    CcspWifiTrace(("RDK_LOG_WARN, DFSEnable: %d, OperatingChannelBandwidth: %d  Channel: %lu ACS: %d\n ", pCfg->X_COMCAST_COM_DFSEnable, pCfg->OperatingChannelBandwidth, pCfg->Channel, pCfg->AutoChannelEnable));
    if (pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_160M)
    {
        if (pCfg->X_COMCAST_COM_DFSEnable == false)
        {
            CcspWifiTrace(("RDK_LOG_WARN, Error configuration requested with 160 MHz bandwidth: DFS disabled or channel invalid! \n "));
            // Avoid mismatch between DMCLI db and uci db
            pCfg->OperatingChannelBandwidth = pStoredCfg->OperatingChannelBandwidth;
            return ANSC_STATUS_FAILURE;
        }
        if ((pCfg->Channel >= INTEL_START_INVALID_160_CHANNELS) && (pCfg->AutoChannelEnable == FALSE)) {
            CcspWifiTrace(("RDK_LOG_WARN, Error configuration requested with 160 MHz bandwidth and channel invalid, using safe channel! \n "));
            //Move to safe 160 MHz channel.
            pCfg->Channel = INTEL_160_MHZ_SAFE_CHANNEL;
        }
    }
#endif

    // Push changed parameters to persistent store, but don't push to the radio
    // Until after ApplySettings is set to TRUE
    CosaDmlWiFiSetRadioPsmData(pCfg, wlanIndex, pCfg->InstanceNumber);

    if (pStoredCfg->bEnabled != pCfg->bEnabled )
    {
        // this function will set a global Radio flag that will be used by the apup script
        // if the value is FALSE, the SSIDs on that radio will not be brought up even if they are enabled
        // if ((SSID.Enable==TRUE)&&(Radio.Enable==true)) then bring up SSID
        if(pCfg->bEnabled) {
            /* when ForceDisableWiFiRadio feature is enabled all the radios are in disabled state.
               Hence we can't modify any radio or AP related params. Hence added a check to validate
               whether ForceDisableWiFiRadio feature is enabled or not.
             */
            if(ANSC_STATUS_FAILURE == CosaDmlWiFiGetCurrForceDisableWiFiRadio(&bForceDisableFlag))
            {
                CcspWifiTrace(("RDK_LOG_WARN, %s Failed to fetch ForceDisableWiFiRadio flag!!!\n",__FUNCTION__));
            }
            if(bForceDisableFlag == FALSE) {
                wifi_setRadioEnable(wlanIndex,pCfg->bEnabled);
            } else {
                CcspWifiTrace(("RDK_LOG_WARN, WIFI_ATTEMPT_TO_CHANGE_CONFIG_WHEN_FORCE_DISABLED \n"));
            }
        } else {
               wifi_setRadioEnable(wlanIndex,pCfg->bEnabled);
        }

        if(pCfg->bEnabled)
        {
            wifi_setLED(wlanIndex,true);
			//>>zqiu
			sWiFiDmlRestartHostapd=TRUE;
			//<<
        }
#if defined(_INTEL_WAV_)
            wifi_applyRadioSettings(wlanIndex);
#endif
	//Reset Telemetry statistics of all wifi clients for all vaps (Per Radio), while Radio interface UP/DOWN.
	radio_stats_flag_change(pCfg->InstanceNumber-1, pCfg->bEnabled);
    }

    if (pStoredCfg->X_COMCAST_COM_DFSEnable != pCfg->X_COMCAST_COM_DFSEnable )
    {
        if ( RETURN_OK != wifi_setRadioDfsEnable(wlanIndex,pCfg->X_COMCAST_COM_DFSEnable) )
        {
            pCfg->X_COMCAST_COM_DFSEnable = pStoredCfg->X_COMCAST_COM_DFSEnable;
            CcspWifiTrace(("RDK_LOG_WARN, %s Failed to configure DFS settings!!!\n",__FUNCTION__));
        }

#if defined(_INTEL_WAV_) || defined(_LG_MV1_CELENO_)
         wlanRestart = TRUE;
 #endif
    }

#if defined(_LG_MV1_CELENO_)
    if (pStoredCfg->EnhancedACS.DFSMoveBack != pCfg->EnhancedACS.DFSMoveBack)
    {
        wifi_setRadioDfsMoveBackEnable(wlanIndex, pCfg->EnhancedACS.DFSMoveBack);
        wlanRestart = TRUE;
    }
    if (pStoredCfg->EnhancedACS.ExcludeDFS != pCfg->EnhancedACS.ExcludeDFS)
    {
        wifi_setRadioExcludeDfs(wlanIndex, pCfg->EnhancedACS.ExcludeDFS);
        wlanRestart = TRUE;
    }
    if (memcmp(pStoredCfg->EnhancedACS.ChannelWeights, pCfg->EnhancedACS.ChannelWeights, sizeof(pCfg->EnhancedACS.ChannelWeights)))
    {
        wifi_setRadioChannelWeights(wlanIndex, pCfg->EnhancedACS.ChannelWeights);
        wlanRestart = TRUE;
    }
#endif

	if (pStoredCfg->X_COMCAST_COM_DCSEnable != pCfg->X_COMCAST_COM_DCSEnable )
    {
        wifi_setRadioDCSEnable(wlanIndex,pCfg->X_COMCAST_COM_DCSEnable);
    }

    if (pStoredCfg->X_COMCAST_COM_IGMPSnoopingEnable != pCfg->X_COMCAST_COM_IGMPSnoopingEnable )
    {
        wifi_setRadioIGMPSnoopingEnable(wlanIndex,pCfg->X_COMCAST_COM_IGMPSnoopingEnable);
    }

#if defined(_HUB4_PRODUCT_REQ_)
    if (pCfg->AutoChannelRefreshPeriod != pStoredCfg->AutoChannelRefreshPeriod)
    {
        wifi_setRadioAutoChannelRefreshPeriod(wlanIndex, pCfg->AutoChannelRefreshPeriod);
    }
#endif

    //>>zqiu
    if (pStoredCfg->X_CISCO_COM_11nGreenfieldEnabled != pCfg->X_CISCO_COM_11nGreenfieldEnabled )
    {
        wifi_setRadio11nGreenfieldEnable(wlanIndex,pCfg->X_CISCO_COM_11nGreenfieldEnabled);
    }
    //<<
    if((AnscEqualString(pCfg->RegulatoryDomain, pStoredCfg->RegulatoryDomain, TRUE)) == FALSE)
    {
        wifi_setRadioCountryCode(wlanIndex, pCfg->RegulatoryDomain);
        wlanRestart = TRUE;
    }

    if (pCfg->AutoChannelEnable != pStoredCfg->AutoChannelEnable)
    {
        // If ACS is turned off or on the radio must be restarted to pick up the new channel
        wlanRestart = TRUE;  // Radio Restart Needed
        wifiDbgPrintf("%s: Radio Reset Needed!!!!\n",__FUNCTION__);
		CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CONFIG_CHANGED : %s Radio Reset Needed!!!!\n",__FUNCTION__)); 
        if (pCfg->AutoChannelEnable == TRUE)
        {
            printf("%s: Setting Auto Channel Selection to TRUE \n",__FUNCTION__);
	   		CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CONFIG_CHANGED : %s Setting Auto Channel Selection to TRUE\n",__FUNCTION__));
            if (RETURN_OK != wifi_setRadioAutoChannelEnable(wlanIndex, pCfg->AutoChannelEnable))
            {
                pCfg->AutoChannelEnable = pStoredCfg->AutoChannelEnable;
                CcspWifiTrace(("RDK_LOG_WARN, %s not able to set Auto Channel Selection to TRUE for index:%d\n",__FUNCTION__,wlanIndex));
            }
        } else {
            printf("%s: Setting Auto Channel Selection to FALSE and Setting the Manually Selected Channel= %lu\n",__FUNCTION__,pCfg->Channel);
            CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : %s Setting Auto Channel Selection to FALSE and Setting the Manually Selected Channel= %lu \n",__FUNCTION__,pCfg->Channel));
            wifi_setRadioChannel(wlanIndex, pCfg->Channel);
        }

    } else if (  (pCfg->AutoChannelEnable == FALSE) && (pCfg->Channel != pStoredCfg->Channel) )
    {
        printf("%s: In Manual mode Setting Channel= %lu\n",__FUNCTION__,pCfg->Channel);
		CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : %s In Manual mode Setting Channel= %lu \n",__FUNCTION__,pCfg->Channel));
        wifi_setRadioChannel(wlanIndex, pCfg->Channel);
        wlanRestart=TRUE; // FIX ME !!!
    }

    // In certain releases GUI sends down the ExtensionChannel, but the GUI only supports Auto
    // and the driver does not support Auto.  Ignore the for now because it is causing the Radio to be restarted.
    if (pCfg->ExtensionChannel != pStoredCfg->ExtensionChannel &&
        pCfg->ExtensionChannel == COSA_DML_WIFI_EXT_CHAN_Auto ) {
        pCfg->ExtensionChannel = pStoredCfg->ExtensionChannel;
    }
    if ( pCfg->OperatingStandards != pStoredCfg->OperatingStandards ||
         pCfg->OperatingChannelBandwidth != pStoredCfg->OperatingChannelBandwidth ||
#if !defined(_INTEL_BUG_FIXES_)
         pCfg->ExtensionChannel != pStoredCfg->ExtensionChannel )
#else
         pCfg->ExtensionChannel != pStoredCfg->ExtensionChannel ||
         pCfg->Channel != pStoredCfg->Channel || // Update pCfg->ExtensionChannel to VHT40+ or VHT40- when change channel in the same BW, like 40MHz
         pCfg->AutoChannelEnable != pStoredCfg->AutoChannelEnable ) // Change from ACS to mannual, but without changing the channel
#endif
    {

        char chnMode[32];
#if !defined (_WIFI_CONSOLIDATED_STANDARDS_)
        BOOL gOnlyFlag = FALSE;
        BOOL nOnlyFlag = FALSE;
        BOOL acOnlyFlag = FALSE;
#endif
        wlanRestart = TRUE;      // Radio Restart Needed

        // Note based on current channel, the Extension Channel may need to change, if channel is not auto. Deal with that first!
        // Only care about fixing the ExtensionChannel and Channel number pairing if Radio is not  set to AutoChannel
        if ( (pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_40M) &&
             (pCfg->AutoChannelEnable == FALSE) )
        {
            if (pCfg->OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_2_4G)
            {
                if (pCfg->ExtensionChannel == COSA_DML_WIFI_EXT_CHAN_Above)
                {
                    if(pCfg->Channel > 7 ) 
                    {
                        pCfg->ExtensionChannel = COSA_DML_WIFI_EXT_CHAN_Below;
                    }
                } else { // trying to set secondary below ...
                    if(pCfg->Channel < 5 ) {   //zqiu
                        pCfg->ExtensionChannel = COSA_DML_WIFI_EXT_CHAN_Above;
                    }
                }
            } else { // else 5GHz
                if (pCfg->ExtensionChannel == COSA_DML_WIFI_EXT_CHAN_Above )
                {
                    switch( pCfg->Channel ) {
                        case 40: case 48: case 56: case 64: case 104: case 112: case 120: case 128: case 136: case 153: case 161:
                        pCfg->ExtensionChannel = COSA_DML_WIFI_EXT_CHAN_Below;
                        break;
                    default:
                        break;
                    }
                } else { // Trying to set it below
                    switch( pCfg->Channel ) {
                        case 36: case 44: case 52: case 60: case 100: case 108: case 116: case 124: case 132: case 149: case 157:
                        pCfg->ExtensionChannel = COSA_DML_WIFI_EXT_CHAN_Above;
                        break;
                    default:
                        break;
                    }
                }
            } // endif(FrequencBand)   
        } // endif (40MHz)

#if defined (_WIFI_CONSOLIDATED_STANDARDS_)
        // RDKB-25911: Consolidated Operating Standards as of now for new devices only!!
        UINT pureMode = 0;

        if ( (pCfg->OperatingStandards == COSA_DML_WIFI_STD_b) || (pCfg->OperatingStandards == COSA_DML_WIFI_STD_g) || (pCfg->OperatingStandards == COSA_DML_WIFI_STD_n)
             || (pCfg->OperatingStandards == COSA_DML_WIFI_STD_a) || (pCfg->OperatingStandards == COSA_DML_WIFI_STD_ac)
#if defined (_WIFI_AX_SUPPORT_)
             || (pCfg->OperatingStandards == COSA_DML_WIFI_STD_ax) 
#endif
             )
        {
            CcspWifiTrace(("RDK_LOG_WARN, %s: Error configuration requested Pure Modes Not Supported! \n ", __FUNCTION__));
            pCfg->OperatingStandards = pStoredCfg->OperatingStandards;
            return ANSC_STATUS_FAILURE;
        } 
        else if (pCfg->OperatingStandards&COSA_DML_WIFI_STD_ac
#if defined (_WIFI_AX_SUPPORT_)
            && !(pCfg->OperatingStandards&COSA_DML_WIFI_STD_ax)
#endif
        )
        {
            if (pCfg->OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_5G)
            {
                if ( pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_20M)
                {
                  sprintf(chnMode,"11ACVHT20");
                } else if (pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_40M)
                {
                  if (pCfg->ExtensionChannel == COSA_DML_WIFI_EXT_CHAN_Above )
                  {
                    sprintf(chnMode,"11ACVHT40PLUS");
                  } else
                  {
                    sprintf(chnMode,"11ACVHT40MINUS");
                  }
                } else if (pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_80M)
                {
                  sprintf(chnMode,"11ACVHT80");
                }else if (pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_160M)
                {
                  sprintf(chnMode,"11ACVHT160");
                }
            }
        }
#if defined (_WIFI_AX_SUPPORT_)
        else if (pCfg->OperatingStandards&COSA_DML_WIFI_STD_ax)
        {
            if ( pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_20M)
            {
                sprintf(chnMode,"11AXHE20");
            }
            else if (pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_40M)
            {
                if (pCfg->ExtensionChannel == COSA_DML_WIFI_EXT_CHAN_Above )
                {
                    sprintf(chnMode,"11AXHE40PLUS");
                }
                else
                {
                    sprintf(chnMode,"11AXHE40MINUS");
                }
            }

            if ((pCfg->OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_5G) ||
                (pCfg->OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_6G))
            {
                if (pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_80M)
                {
                  sprintf(chnMode,"11AXHE80");
                }
                else if (pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_160M)
                {
                  sprintf(chnMode,"11AXHE160");
                }
            }
        }
#endif
        else
        {
            // n but not ac modes
            if ( pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_20M)
            {

                if (pCfg->OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_2_4G)
                {
                    sprintf(chnMode,"11NGHT20");
                } else // COSA_DML_WIFI_FREQ_BAND_5G
                {
                    CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CONFIG_CHANGED : %s: 5Ghz Band MUST Be Set to AC or AX\n",__FUNCTION__))
                }
            } else if (pCfg->OperatingChannelBandwidth & COSA_DML_WIFI_CHAN_BW_40M)
            {
                // treat 40 and Auto as 40MHz, the driver does not have an 'Auto setting' that can be toggled

                if (pCfg->OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_2_4G)
                {

                    if (pCfg->ExtensionChannel == COSA_DML_WIFI_EXT_CHAN_Above )
                    {
                        sprintf(chnMode,"11NGHT40PLUS");
                    } else
                    {
                        sprintf(chnMode,"11NGHT40MINUS");
                    }
                } else // else 5GHz
                {
                    CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CONFIG_CHANGED : %s: 5Ghz Band MUST Be Set to AC or AX\n",__FUNCTION__))
                }
            }
        }

        CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CONFIG_CHANGED : %s: wifi_setRadioMode= Wlan%d, Mode: %s, pureMode =: %d\n",__FUNCTION__,wlanIndex,chnMode,pureMode))
        wifi_setRadioMode(wlanIndex, chnMode, pureMode);
#ifdef DUAL_CORE_XB3
        wifi_setRadioBasicDataTransmitRates(wlanIndex, NULL);
#endif
		
#if defined(ENABLE_FEATURE_MESHWIFI)
        {
            // notify mesh components that wifi radio settings changed
            CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh of Radio Channel Mode changes\n",__FUNCTION__));
            v_secure_system("/usr/bin/sysevent set wifi_RadioChannelMode 'RDK|%d|%s|false|false|false'", wlanIndex, chnMode);
        }
#endif		

#else
        if (pCfg->OperatingStandards == COSA_DML_WIFI_STD_a)
        {
            sprintf(chnMode,"11A");
        } else if (pCfg->OperatingStandards == COSA_DML_WIFI_STD_b)
        {
            sprintf(chnMode,"11B");
        } else if (pCfg->OperatingStandards == COSA_DML_WIFI_STD_g)
        {
            sprintf(chnMode,"11G");
        } else if (pCfg->OperatingStandards == ( COSA_DML_WIFI_STD_b | COSA_DML_WIFI_STD_g ) )
        {
            sprintf(chnMode,"11G");
            // all below are n with a possible combination of a, b and g
        } else if ( !(pCfg->OperatingStandards&COSA_DML_WIFI_STD_ac) 
#ifdef _WIFI_AX_SUPPORT_
            && !(pCfg->OperatingStandards&COSA_DML_WIFI_STD_ax) 
#endif
        )
        {
            // n but not ac modes
            if ( pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_20M)
            {

                if (pCfg->OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_2_4G)
                {
                    sprintf(chnMode,"11NGHT20");
                } else // COSA_DML_WIFI_FREQ_BAND_5G
                {
                    sprintf(chnMode,"11NAHT20");
                }
            } else if (pCfg->OperatingChannelBandwidth & COSA_DML_WIFI_CHAN_BW_40M)
            {
                // treat 40 and Auto as 40MHz, the driver does not have an 'Auto setting' that can be toggled

                if (pCfg->OperatingFrequencyBand == COSA_DML_WIFI_FREQ_BAND_2_4G)
                {

                    if (pCfg->ExtensionChannel == COSA_DML_WIFI_EXT_CHAN_Above )
                    {
                        sprintf(chnMode,"11NGHT40PLUS");
                    } else
                    {
                        sprintf(chnMode,"11NGHT40MINUS");
                    }
                } else // else 5GHz
                {
                    if (pCfg->ExtensionChannel == COSA_DML_WIFI_EXT_CHAN_Above )
                    {
                        sprintf(chnMode,"11NAHT40PLUS");
                    } else
                    {
                        sprintf(chnMode,"11NAHT40MINUS");
                    }
                }
            }
        } else if (pCfg->OperatingStandards&COSA_DML_WIFI_STD_ac
#ifdef _WIFI_AX_SUPPORT_
            && !(pCfg->OperatingStandards&COSA_DML_WIFI_STD_ax)
#endif
        )
        {

            if ( pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_20M)
            {
                sprintf(chnMode,"11ACVHT20");
            } else if (pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_40M)
            {
                if (pCfg->ExtensionChannel == COSA_DML_WIFI_EXT_CHAN_Above )
                {
                    sprintf(chnMode,"11ACVHT40PLUS");
                } else
                {
                    sprintf(chnMode,"11ACVHT40MINUS");
                }
            } else if (pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_80M)
            {
                sprintf(chnMode,"11ACVHT80");
            }else if (pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_160M)
            {
                sprintf(chnMode,"11ACVHT160");
            }
        }
#ifdef _WIFI_AX_SUPPORT_
        else if (pCfg->OperatingStandards&COSA_DML_WIFI_STD_ax)
        {
            if ( pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_20M)
            {
                sprintf(chnMode,"11AXHE20");
            }
            else if (pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_40M)
            {
                if (pCfg->ExtensionChannel == COSA_DML_WIFI_EXT_CHAN_Above )
                {
                    sprintf(chnMode,"11AXHE40PLUS");
                }
                else
                {
                    sprintf(chnMode,"11AXHE40MINUS");
                }
            }
            else if (pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_80M)
            {
                sprintf(chnMode,"11AXHE80");
            }
            else if (pCfg->OperatingChannelBandwidth == COSA_DML_WIFI_CHAN_BW_160M)
            {
                sprintf(chnMode,"11AXHE160");
            }

        }
#endif

        // if OperatingStandards is set to only g or only n, set only flag to TRUE.
        // wifi_setRadioChannelMode will set PUREG=1/PUREN=1 in the config
        if ( (pCfg->OperatingStandards == COSA_DML_WIFI_STD_g) ||
             (pCfg->OperatingStandards == (COSA_DML_WIFI_STD_g | COSA_DML_WIFI_STD_n) ) )
        {
            gOnlyFlag = TRUE;
        }

        if ( ( pCfg->OperatingStandards == COSA_DML_WIFI_STD_n ) ||
             ( pCfg->OperatingStandards == (COSA_DML_WIFI_STD_n | COSA_DML_WIFI_STD_ac) )
#ifdef _WIFI_AX_SUPPORT_
             || ( pCfg->OperatingStandards == (COSA_DML_WIFI_STD_n | COSA_DML_WIFI_STD_ax) )
#endif
        )
        {
            nOnlyFlag = TRUE;
        }

        if (pCfg->OperatingStandards == COSA_DML_WIFI_STD_ac )
        {
            acOnlyFlag = TRUE;
        }
#ifdef _WIFI_AX_SUPPORT_
        UINT pureMode = 0;
        if (gOnlyFlag)
            pureMode = COSA_DML_WIFI_STD_g;
        if (nOnlyFlag)
            pureMode = COSA_DML_WIFI_STD_n;
        if(acOnlyFlag)
            pureMode = COSA_DML_WIFI_STD_ac;
        if(pCfg->OperatingStandards == COSA_DML_WIFI_STD_ax)
            pureMode = COSA_DML_WIFI_STD_ax;

        CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CONFIG_CHANGED : %s: wifi_setRadioMode= Wlan%d, Mode: %s, pureMode =: %d\n",__FUNCTION__,wlanIndex,chnMode,pureMode))
        wifi_setRadioMode(wlanIndex, chnMode, pureMode);
#else
        printf("%s: wifi_setRadioChannelMode= Wlan%d, Mode: %s, gOnlyFlag: %d, nOnlyFlag: %d\n acOnlyFlag: %d\n",__FUNCTION__,wlanIndex,chnMode,gOnlyFlag,nOnlyFlag, acOnlyFlag);
	
       CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CONFIG_CHANGED : %s wifi_setChannelMode= Wlan: %d, Mode: %s, gOnlyFlag: %d, nOnlyFlag: %d acOnlyFlag: %d \n",__FUNCTION__,wlanIndex,chnMode,gOnlyFlag,nOnlyFlag, acOnlyFlag));

        wifi_setRadioChannelMode(wlanIndex, chnMode, gOnlyFlag, nOnlyFlag, acOnlyFlag);
#ifdef DUAL_CORE_XB3
        wifi_setRadioBasicDataTransmitRates(wlanIndex, NULL);
#endif
#endif

#if defined(ENABLE_FEATURE_MESHWIFI)
        {
            // notify mesh components that wifi radio settings changed
            CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh of Radio Channel Mode changes\n",__FUNCTION__));
            v_secure_system("/usr/bin/sysevent set wifi_RadioChannelMode 'RDK|%d|%s|%s|%s|%s'", wlanIndex, chnMode, (gOnlyFlag?"true":"false"), (nOnlyFlag?"true":"false"), (acOnlyFlag?"true":"false"));
        }
#endif
#endif // (_WIFI_CONSOLIDATED_STANDARDS_)
    } // Done with Mode settings
/*
    if (pCfg->X_CISCO_COM_HTTxStream != pStoredCfg->X_CISCO_COM_HTTxStream)
    {
		CcspWifiTrace(("RDK_LOG_WARN,%s : wlanIndex: %d X_CISCO_COM_HTTxStream : %lu \n",__FUNCTION__,wlanIndex,pCfg->X_CISCO_COM_HTTxStream));
        wifi_setRadioTxChainMask(wlanIndex, pCfg->X_CISCO_COM_HTTxStream);
    }
    if (pCfg->X_CISCO_COM_HTRxStream != pStoredCfg->X_CISCO_COM_HTRxStream)
    {
		CcspWifiTrace(("RDK_LOG_WARN,%s : wlanIndex: %d X_CISCO_COM_HTRxStream : %lu \n",__FUNCTION__,wlanIndex,pCfg->X_CISCO_COM_HTRxStream));
        wifi_setRadioRxChainMask(wlanIndex, pCfg->X_CISCO_COM_HTRxStream);
    }
*/
	if (strcmp(pStoredCfg->BasicDataTransmitRates, pCfg->BasicDataTransmitRates)!=0 )
    {	//zqiu
		CcspWifiTrace(("RDK_LOG_WARN,%s : wlanIndex: %d BasicDataTransmitRates : %s \n",__FUNCTION__,wlanIndex,pCfg->BasicDataTransmitRates));
        wifi_setRadioBasicDataTransmitRates(wlanIndex,pCfg->BasicDataTransmitRates);
    }
    	if (strcmp(pStoredCfg->OperationalDataTransmitRates, pCfg->OperationalDataTransmitRates)!=0 )
    {	
		CcspWifiTrace(("RDK_LOG_WARN,%s : wlanIndex: %d OperationalDataTransmitRates : %s \n",__FUNCTION__,wlanIndex,pCfg->OperationalDataTransmitRates));
        wifi_setRadioOperationalDataTransmitRates(wlanIndex,pCfg->OperationalDataTransmitRates);
    }

#if defined(ENABLE_FEATURE_MESHWIFI)
        {
            if (strcmp(pStoredCfg->BasicDataTransmitRates, pCfg->BasicDataTransmitRates)!=0 ||
                strcmp(pStoredCfg->OperationalDataTransmitRates, pCfg->OperationalDataTransmitRates)!=0)
            {
                // notify mesh components that wifi radio transmission rate changed
                CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh of Radio Transmission Rate changes\n",__FUNCTION__));
                v_secure_system("/usr/bin/sysevent set wifi_TxRate 'RDK|%d|BasicRates:%s|OperationalRates:%s'", wlanIndex+1, pCfg->BasicDataTransmitRates, pCfg->OperationalDataTransmitRates);
            }
        }
#endif

    // pCfg->AutoChannelRefreshPeriod       = 3600;
    // Modulation Coding Scheme 0-23, value of -1 means Auto
    // pCfg->MCS                            = 1;

    // ######## Needs to be update to get actual value
    // pCfg->BasicRate = COSA_DML_WIFI_BASICRATE_Default;

    // Need to translate
#if 0
    {
        char maxBitRate[128];
        wifi_getRadioMaxBitRate(wlanIndex, maxBitRate);
        wifiDbgPrintf("%s: wifi_getRadioMaxBitRate returned %s\n", __FUNCTION__, maxBitRate);
        if (strcmp(maxBitRate,"Auto") == 0)
        {
            pCfg->TxRate = COSA_DML_WIFI_TXRATE_Auto;
            wifiDbgPrintf("%s: set to auto pCfg->TxRate = %d\n", __FUNCTION__, pCfg->TxRate);
        } else
        {
            char *pos;
            pos = strstr(maxBitRate, " ");
            if (pos != NULL) pos = 0;
            wifiDbgPrintf("%s: wifi_getRadioMaxBitRate returned %s\n", __FUNCTION__, maxBitRate);
            pCfg->TxRate = atoi(maxBitRate);
            pCfg->TxRate = COSA_DML_WIFI_TXRATE_48M;
            wifiDbgPrintf("%s: pCfg->TxRate = %d\n", __FUNCTION__, pCfg->TxRate);
        }
    }
    pCfg->TxRate                         = COSA_DML_WIFI_TXRATE_Auto;
#endif

    /* Below is Cisco Extensions */
    // pCfg->APIsolation                    = TRUE;
    // pCfg->FrameBurst                     = TRUE;
    // Not for first GA
    // pCfg->OnOffPushButtonTime           = 23;
    // pCfg->MulticastRate                 = 45;
    // BOOL                            X_CISCO_COM_ReverseDirectionGrant;
    // BOOL                            X_CISCO_COM_AutoBlockAck;
    // BOOL                            X_CISCO_COM_DeclineBARequest;
    if (pCfg->X_CISCO_COM_AutoBlockAck != pStoredCfg->X_CISCO_COM_AutoBlockAck)
    {
       wifi_setRadioAutoBlockAckEnable(wlanIndex, pCfg->X_CISCO_COM_AutoBlockAck);
   }

	//>>Deprecated
	//if (pCfg->X_CISCO_COM_WirelessOnOffButton != pStoredCfg->X_CISCO_COM_WirelessOnOffButton)
    //{
    //    wifi_getWifiEnableStatus(wlanIndex, &pCfg->X_CISCO_COM_WirelessOnOffButton);
    //    printf("%s: called wifi_getWifiEnableStatus\n",__FUNCTION__);
    //}
    //<<
  
    if ( pCfg->ApplySetting == TRUE )
    {
        wifiDbgPrintf("%s: ApplySettings---!!!!!! \n",__FUNCTION__);
		
		//zqiu: >>
		if(gRadioRestartRequest[0] || gRadioRestartRequest[1] || gRadioRestartRequest[2]) {
fprintf(stderr, "----# %s %d gRadioRestartRequest=%d %d \n", __func__, __LINE__, gRadioRestartRequest[0], gRadioRestartRequest[1] );		
			wlanRestart=TRUE;
			if(gRadioRestartRequest[2] == TRUE)
			{
				gRadioRestartRequest[0]=FALSE;
				gRadioRestartRequest[1]=FALSE;
				gRadioRestartRequest[2]=FALSE;
				reset_both_radios = TRUE;
			}
			else
			{
				if(wlanIndex == 0)
					gRadioRestartRequest[0]=FALSE;
				else
					gRadioRestartRequest[1]=FALSE;
			}
		}
		//<<
#if (defined(_COSA_BCM_ARM_) && defined(_XB7_PRODUCT_REQ_)) || defined(_XB8_PRODUCT_REQ_) || defined(_CBR2_PRODUCT_REQ_)
	       /* Converting brcm patch to code and this code will be removed as part of Hal Version 3 changes */
               if (wlanRestart) {
                       wifiDbgPrintf("### %s: ignore wlanRestart for instance %lu ###\n", __FUNCTION__, pCfg->InstanceNumber - 1);
                       wlanRestart = FALSE;
               }
#endif
		if(wlanRestart == TRUE)
		{
            memcpy(&sWiFiDmlRadioStoredCfg[pCfg->InstanceNumber-1], pCfg, sizeof(COSA_DML_WIFI_RADIO_CFG));
            wifiDbgPrintf("%s: ***** RESTARTING RADIO !!! *****\n",__FUNCTION__);
			CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CONFIG_CHANGED : %s RESTARTING RADIO !!! \n",__FUNCTION__)); 
#if defined(_INTEL_WAV_)
            wifi_applyRadioSettings(wlanIndex);
#else
            if(reset_both_radios)
                wifi_reset();
            else
                wifi_initRadio(wlanIndex);
#endif

			CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : %s RADIO Restarted !!! \n",__FUNCTION__)); 
            /*TODO RDKB-34680 CID: 135386 Data race condition*/
            pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
#if defined(FEATURE_HOSTAP_AUTHENTICATOR)
            BOOL prevLibMode = pMyObject->bEnableHostapdAuthenticator;

            if (prevLibMode)
            {
#if !defined(_XB7_PRODUCT_REQ_)
                ULONG uIndex = 0, uSsidCount = 0;
                uSsidCount = CosaDmlWiFiSsidGetNumberOfEntries((ANSC_HANDLE)pMyObject->hPoamWiFiDm);
                for (uIndex = 0; uIndex < uSsidCount; uIndex++)
                   CosaDmlWifi_ReInitLibHostapd((uIndex % 2 == 0) ? 0 : 1, uIndex, pMyObject);
#else
                reInitLibHostapd();
#endif
                //Change the FLAG to avoid re-init again in CosaWifiReInitialize API
                pMyObject->bEnableHostapdAuthenticator = FALSE;
            }
#endif /*FEATURE_HOSTAP_AUTHENTICATOR */

            CosaWifiReInitialize((ANSC_HANDLE)pMyObject, wlanIndex);
#if defined(FEATURE_HOSTAP_AUTHENTICATOR)
            pMyObject->bEnableHostapdAuthenticator = prevLibMode;
#endif /*FEATURE_HOSTAP_AUTHENTICATOR */

            Load_Hotspot_APIsolation_Settings();

            Update_Hotspot_MacFilt_Entries(true);

        } else {
            CosaDmlWiFiRadioApplyCfg(pCfg);
	    memcpy(&sWiFiDmlRadioStoredCfg[pCfg->InstanceNumber-1], pCfg, sizeof(COSA_DML_WIFI_RADIO_CFG));
        }

#if defined (FEATURE_HOSTAP_AUTHENTICATOR) && defined(_XB7_PRODUCT_REQ_)
#if defined(ENABLE_FEATURE_MESHWIFI)
        {
            // notify mesh components that wifi radio settings changed
            CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh of Radio Config changes\n",__FUNCTION__));
            v_secure_system("/usr/bin/sysevent set wifi_RadioChannel 'RDK|%lu|%lu'", pCfg->InstanceNumber-1, pCfg->Channel);
        }
#endif
        pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
        if (pMyObject->bEnableHostapdAuthenticator) {
            CcspWifiTrace(("RDK_LOG_INFO,%s:%d skipping wifi_apply() as libhostap is enabled\n",__FUNCTION__, __LINE__));
            wifi_nvramCommit();

            if (!isWifiApplyLibHostapRunning)
            {
                pthread_t tid;
                pthread_attr_t attr;

                pthread_attr_init(&attr);
                pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );

                if (pthread_create(&tid, &attr, wifi_libhostap_apply_settings, NULL) != 0)
                    CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify libhostap is FAILED\n", __FUNCTION__));

                isWifiApplyLibHostapRunning = TRUE;
            }
        }

        /* Doesn't need WiFi apply if Libhostap is enabled and wifi_apply will be called
           after BRCM patch apply */
        if (pMyObject->bEnableHostapdAuthenticator) {
            return ANSC_STATUS_SUCCESS;
        } else {
/* CMWIFI_RDKB, call wifi_apply in CosaDmlWiFiRadioSetCfg. */
#if (defined(_COSA_BCM_ARM_) && defined(_XB7_PRODUCT_REQ_)) || defined(_XB8_PRODUCT_REQ_) || defined(_CBR2_PRODUCT_REQ_)
            fprintf(stderr, "%s: calling wifi_apply()...\n", __func__);
            wifi_apply();
#endif
        }

#else /* defined (FEATURE_HOSTAP_AUTHENTICATOR) && defined(_XB7_PRODUCT_REQ_) */

#if defined(ENABLE_FEATURE_MESHWIFI)
		{
		    // notify mesh components that wifi radio settings changed
		    CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh of Radio Config changes\n",__FUNCTION__));
                    v_secure_system("/usr/bin/sysevent set wifi_RadioChannel 'RDK|%lu|%lu'", pCfg->InstanceNumber-1, pCfg->Channel);
		}
#endif

#if (defined(_COSA_BCM_ARM_) && defined(_XB7_PRODUCT_REQ_)) || defined(_XB8_PRODUCT_REQ_) || defined(_CBR2_PRODUCT_REQ_)
	/* Converting brcm patch to code and this code will be removed as part of Hal Version 3 changes */
	fprintf(stderr, "%s: calling wifi_apply()...\n", __func__);
        wifi_apply();
#endif
#endif
    }
    else
    {
      /*Update the RadioStoredCfg even if Applysettings not set*/
       memcpy(&sWiFiDmlRadioStoredCfg[pCfg->InstanceNumber-1], pCfg, sizeof(COSA_DML_WIFI_RADIO_CFG));
      /*Set the radio reset flag*/
       if(wlanRestart)
           enable_reset_radio_flag(wlanIndex);
    }

    return ANSC_STATUS_SUCCESS;
}

// Called from middle layer to get Cfg information that can be changed by the Radio
// for example, when in Auto channel mode, the radio can change the Channel so it should 
// always be quired.
ANSC_STATUS
CosaDmlWiFiRadioGetDCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_RADIO_CFG    pCfg        /* Identified by InstanceNumber */
    )
{
    int                             wlanIndex;
    UNREFERENCED_PARAMETER(hContext);
    if (!pCfg )
    {
        return ANSC_STATUS_FAILURE;
    }
    
    wlanIndex = (ULONG) pCfg->InstanceNumber-1;  
    if ( (wlanIndex < 0) || (wlanIndex >= RADIO_INDEX_MAX) )
    {
        return ANSC_STATUS_FAILURE;
    }

	wifi_getRadioChannel(wlanIndex, &pCfg->Channel);

    return ANSC_STATUS_SUCCESS;
}


// Called from middle layer to get Cfg information that can be changed by the Radio
// for example: 
// when 2.4G failed to set to 40MHz due to "ignore_40_mhz_intolerant=0", the bandwidth in Web UI should reflect active bandwidth
// when radar is detected in 5G (eg, from CH100 @160MHz to CH36 @80MHz), the bandwidth in Web UI should reflect active bandwidth
ANSC_STATUS
CosaDmlWiFiRadioGetDBWCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_RADIO_CFG    pCfg        /* Identified by InstanceNumber */
    )
{
    int                             wlanIndex;
    char channelBW[64] = {'\0'};
    UNREFERENCED_PARAMETER(hContext);
    memset(channelBW, 0, sizeof(channelBW));

    if (!pCfg )
    {
        return ANSC_STATUS_FAILURE;
    }

    wlanIndex = (ULONG) pCfg->InstanceNumber-1;
    if ( (wlanIndex < 0) || (wlanIndex >= RADIO_INDEX_MAX) )
    {
        return ANSC_STATUS_FAILURE;
    }

    wifi_getRadioOperatingChannelBandwidth(wlanIndex, channelBW);

    if (strncmp(channelBW,"20MHz", strlen("20MHz")) == 0)
        pCfg->OperatingChannelBandwidth = COSA_DML_WIFI_CHAN_BW_20M;
    else if (strncmp(channelBW,"40MHz", strlen("40MHz")) == 0)
        pCfg->OperatingChannelBandwidth = COSA_DML_WIFI_CHAN_BW_40M;
    else if (strncmp(channelBW,"80MHz", strlen("80MHz")) == 0)
        pCfg->OperatingChannelBandwidth = COSA_DML_WIFI_CHAN_BW_80M;
    else if (strncmp(channelBW,"160MHz", strlen("160MHz")) == 0)
        pCfg->OperatingChannelBandwidth = COSA_DML_WIFI_CHAN_BW_160M;
    else
        pCfg->OperatingChannelBandwidth = COSA_DML_WIFI_CHAN_BW_AUTO;

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiRadioGetCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_RADIO_CFG    pCfg        /* Identified by InstanceNumber */
    )
{
    int                             wlanIndex;
    BOOL radioEnabled = FALSE;
    BOOL enabled = FALSE;
    //BOOL DFSEnabled = FALSE;
    BOOL IGMPEnable = FALSE;
    static BOOL firstTime[2] = { TRUE, true};
    UNREFERENCED_PARAMETER(hContext);
    if (!pCfg )
    {
        return ANSC_STATUS_FAILURE;
    }

    if ( CosaDmlWiFiGetRadioPsmData(pCfg) == ANSC_STATUS_FAILURE )
    {
        return ANSC_STATUS_FAILURE;
    }

    wlanIndex = (ULONG) pCfg->InstanceNumber-1; 

    if ( (wlanIndex < 0) || (wlanIndex >= RADIO_INDEX_MAX) )
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if (firstTime[wlanIndex] == TRUE) {
        pCfg->LastChange             = AnscGetTickInSeconds(); 
        printf("%s: LastChange %lu \n", __func__, pCfg->LastChange);
		CcspWifiTrace(("RDK_LOG_WARN,%s : LastChange %lu!!!!!! \n",__FUNCTION__,pCfg->LastChange));
        firstTime[wlanIndex] = FALSE;
    }

    wifi_getRadioEnable(wlanIndex, &radioEnabled);
    pCfg->bEnabled = (radioEnabled == TRUE) ? 1 : 0;

    /*CID: 69532 Out-of-bounds access*/
    char frequencyBand[64] = {0};

    wifi_getRadioSupportedFrequencyBands(wlanIndex, frequencyBand);
    if (strstr(frequencyBand,"2.4G") != NULL)
    {
        pCfg->OperatingFrequencyBand = COSA_DML_WIFI_FREQ_BAND_2_4G; 
    } else if (strstr(frequencyBand,"5G_11N") != NULL)
    {
        pCfg->OperatingFrequencyBand = COSA_DML_WIFI_FREQ_BAND_5G;
    } else if (strstr(frequencyBand,"5G_11AC") != NULL)
    {
        pCfg->OperatingFrequencyBand = COSA_DML_WIFI_FREQ_BAND_5G;
#if defined(_INTEL_BUG_FIXES_)
    } else if ( strstr(frequencyBand,"5G_11AX") != NULL)
    {
        pCfg->OperatingFrequencyBand = COSA_DML_WIFI_FREQ_BAND_5G;
#endif
    } else if ( strstr(frequencyBand,"6G_11AX") != NULL)
    {
        pCfg->OperatingFrequencyBand = COSA_DML_WIFI_FREQ_BAND_6G;
    } else
    {
        // if we can't determine frequency band assume wifi0 is 2.4 and wifi1 is 5 11n
        if (wlanIndex == 0)
        {
            pCfg->OperatingFrequencyBand = COSA_DML_WIFI_FREQ_BAND_2_4G;
        } else if (wlanIndex == 2)
        {
            pCfg->OperatingFrequencyBand = COSA_DML_WIFI_FREQ_BAND_6G;
        } else
        {
            pCfg->OperatingFrequencyBand = COSA_DML_WIFI_FREQ_BAND_5G; 
        }
    }

//>> zqiu	
	CosaDmlWiFiGetRadioStandards(wlanIndex, pCfg->OperatingFrequencyBand, &pCfg->OperatingStandards);
//<<

	wifi_getRadioChannel(wlanIndex, &pCfg->Channel);

#if defined(_LG_MV1_CELENO_)
    wifi_getRadioDfsMoveBackEnable(wlanIndex, &pCfg->EnhancedACS.DFSMoveBack);
    wifi_getRadioExcludeDfs(wlanIndex, &pCfg->EnhancedACS.ExcludeDFS);
    memset(pCfg->EnhancedACS.ChannelWeights, 0, sizeof(pCfg->EnhancedACS.ChannelWeights));
    wifi_getRadioChannelWeights(wlanIndex, pCfg->EnhancedACS.ChannelWeights);
#endif

    wifi_getRadioDfsSupport(wlanIndex,&pCfg->X_COMCAST_COM_DFSSupport);
	wifi_getRadioDfsEnable(wlanIndex, &pCfg->X_COMCAST_COM_DFSEnable);

	wifi_getRadioDCSSupported(wlanIndex,&pCfg->X_COMCAST_COM_DCSSupported);
    wifi_getRadioDCSEnable(wlanIndex, &pCfg->X_COMCAST_COM_DCSEnable);
	
    wifi_getRadioIGMPSnoopingEnable(wlanIndex, &IGMPEnable);
    pCfg->X_COMCAST_COM_IGMPSnoopingEnable = (IGMPEnable == TRUE) ? 1 : 0;

    //>>zqiu
    wifi_getRadioIGMPSnoopingEnable(wlanIndex, &enabled);
    pCfg->X_CISCO_COM_11nGreenfieldEnabled = enabled;
    //<<

    wifi_getRadioAutoChannelEnable(wlanIndex, &enabled);
    pCfg->AutoChannelEnable = (enabled == TRUE) ? TRUE : FALSE;

    wifi_getRadioAutoChannelRefreshPeriod(wlanIndex, &pCfg->AutoChannelRefreshPeriod);
    
    	wifi_getRadioAutoChannelRefreshPeriodSupported(wlanIndex,&pCfg->X_COMCAST_COM_AutoChannelRefreshPeriodSupported);
	
	wifi_getRadioIEEE80211hSupported(wlanIndex,&pCfg->X_COMCAST_COM_IEEE80211hSupported);
	
	wifi_getRadioReverseDirectionGrantSupported(wlanIndex,&pCfg->X_COMCAST_COM_ReverseDirectionGrantSupported);
	
	wifi_getApRtsThresholdSupported(wlanIndex,&pCfg->X_COMCAST_COM_RtsThresholdSupported);
	
	
	//zqiu: >>
    //wifi_getRadioStandard(wlanIndex, channelMode, &gOnly, &nOnly, &acOnly);
	char bandwidth[64] = {0};
	char extchan[64] = {0};
	wifi_getRadioOperatingChannelBandwidth(wlanIndex, bandwidth);
	if (strstr(bandwidth, "40MHz") != NULL) {
		wifi_getRadioExtChannel(wlanIndex, extchan);
		pCfg->OperatingChannelBandwidth = COSA_DML_WIFI_CHAN_BW_40M;
		if (strstr(extchan, "AboveControlChannel") != NULL) {
			pCfg->ExtensionChannel = COSA_DML_WIFI_EXT_CHAN_Above;
		} else if (strstr(extchan, "BelowControlChannel") != NULL) {
			pCfg->ExtensionChannel = COSA_DML_WIFI_EXT_CHAN_Below;
		} else {
			pCfg->ExtensionChannel = COSA_DML_WIFI_EXT_CHAN_Auto;
		}
    } else if (strstr(bandwidth, "80MHz") != NULL) {
#if !defined(_INTEL_BUG_FIXES_)
        pCfg->OperatingChannelBandwidth = COSA_DML_WIFI_CHAN_BW_80M;
        pCfg->ExtensionChannel = COSA_DML_WIFI_EXT_CHAN_Auto;
#else
               wifi_getRadioExtChannel(wlanIndex, extchan);
               pCfg->OperatingChannelBandwidth = COSA_DML_WIFI_CHAN_BW_80M;
               if (strstr(extchan, "AboveControlChannel") != NULL) {
                       pCfg->ExtensionChannel = COSA_DML_WIFI_EXT_CHAN_Above;
               } else if (strstr(extchan, "BelowControlChannel") != NULL) {
                       pCfg->ExtensionChannel = COSA_DML_WIFI_EXT_CHAN_Below;
               } else {
                       pCfg->ExtensionChannel = COSA_DML_WIFI_EXT_CHAN_Auto;
               }
#endif
    } else if (strstr(bandwidth, "160") != NULL) {
        pCfg->OperatingChannelBandwidth = COSA_DML_WIFI_CHAN_BW_160M;		
        pCfg->ExtensionChannel = COSA_DML_WIFI_EXT_CHAN_Auto;
    } else if (strstr(bandwidth, "80+80") != NULL) {
        //pCfg->OperatingChannelBandwidth = COSA_DML_WIFI_CHAN_BW_8080M;	//Todo: add definition
        pCfg->ExtensionChannel = COSA_DML_WIFI_EXT_CHAN_Auto;
    } else if (strstr(bandwidth, "20MHz") != NULL) {
        pCfg->OperatingChannelBandwidth = COSA_DML_WIFI_CHAN_BW_20M;
        pCfg->ExtensionChannel = COSA_DML_WIFI_EXT_CHAN_Auto;
	}
	//zqiu: <<
	
    // Modulation Coding Scheme 0-15, value of -1 means Auto
    //pCfg->MCS                            = -1;
    wifi_getRadioMCS(wlanIndex, &pCfg->MCS);

    // got from CosaDmlWiFiGetRadioPsmData
    {
            if ( ( gRadioPowerState[wlanIndex] == COSA_DML_WIFI_POWER_UP) &&
                 ( gRadioNextPowerSetting != COSA_DML_WIFI_POWER_DOWN ) )
            {
				CcspWifiTrace(("RDK_LOG_WARN,%s : setTransmitPowerPercent  wlanIndex:%d TransmitPower:%d \n",__FUNCTION__,wlanIndex,pCfg->TransmitPower));
                CosaDmlWiFiRadioSetTransmitPowerPercent(wlanIndex, pCfg->TransmitPower);
            } else {
                printf("%s: Radio was not in Power Up mode, didn't set the tranmitPower level \n", __func__);
	}
    }

    if (wlanIndex == 0) {
        pCfg->IEEE80211hEnabled              = FALSE;
    } else {
        pCfg->IEEE80211hEnabled              = TRUE;
    }

    //wifi_getCountryCode(wlanIndex, pCfg->RegulatoryDomain);
	//snprintf(pCfg->RegulatoryDomain, 4, "US");
	wifi_getRadioCountryCode(wlanIndex, pCfg->RegulatoryDomain);
    //zqiu: RDKB-3346
    /*TODO CID: 80249 Out-of-bounds access - Fix in QTN code*/
	wifi_getRadioBasicDataTransmitRates(wlanIndex,pCfg->BasicDataTransmitRates);
	//RDKB-10526
	wifi_getRadioSupportedDataTransmitRates(wlanIndex,pCfg->SupportedDataTransmitRates);
	wifi_getRadioOperationalDataTransmitRates(wlanIndex,pCfg->OperationalDataTransmitRates);	
    // ######## Needs to be update to get actual value
    pCfg->BasicRate = COSA_DML_WIFI_BASICRATE_Default;

    { 
        char maxBitRate[128] = {0};
        wifi_getRadioMaxBitRate(wlanIndex, maxBitRate); 

        if (strcmp(maxBitRate,"Auto") == 0)
        {
            pCfg->TxRate = COSA_DML_WIFI_TXRATE_Auto;
        } else {
            char *pos;
            pos = strstr(maxBitRate, " ");
            if (pos != NULL) pos = 0;
            pCfg->TxRate = atoi(maxBitRate);
            pCfg->TxRate = COSA_DML_WIFI_TXRATE_48M;
        }
    }
    pCfg->TxRate                         = COSA_DML_WIFI_TXRATE_Auto;

    /* Below is Cisco Extensions */
    pCfg->APIsolation                    = TRUE;
    pCfg->FrameBurst                     = TRUE;

    // Not for first GA
    // pCfg->OnOffPushButtonTime           = 23;

    // pCfg->MulticastRate                 = 45;

    // BOOL                            X_CISCO_COM_ReverseDirectionGrant;

    wifi_getRadioAMSDUEnable(wlanIndex, &enabled);
    pCfg->X_CISCO_COM_AggregationMSDU = (enabled == TRUE) ? TRUE : FALSE;

    // BOOL                            X_CISCO_COM_AutoBlockAck;

    wifi_getRadioAutoBlockAckEnable(wlanIndex, &enabled);
    pCfg->X_CISCO_COM_AutoBlockAck = (enabled == TRUE) ? TRUE : FALSE;

    // BOOL                            X_CISCO_COM_DeclineBARequest;
    
/*    wifi_getRadioTxChainMask(wlanIndex, (int *) &pCfg->X_CISCO_COM_HTTxStream);
    wifi_getRadioRxChainMask(wlanIndex, (int *) &pCfg->X_CISCO_COM_HTRxStream);*/
    wifi_getRadioAutoChannelRefreshPeriodSupported(wlanIndex, &pCfg->AutoChannelRefreshPeriodSupported);
    wifi_getApRtsThresholdSupported(wlanIndex, &pCfg->RtsThresholdSupported);
    wifi_getRadioReverseDirectionGrantSupported(wlanIndex, &pCfg->ReverseDirectionGrantSupported);

	//>>Deprecated
    //wifi_getWifiEnableStatus(wlanIndex, &enabled);
    //pCfg->X_CISCO_COM_WirelessOnOffButton = (enabled == TRUE) ? TRUE : FALSE;
	//<<
	
    /* if any of the user-controlled SSID is up, turn on the WiFi LED */
    int i, MbssEnable = 0;
    BOOL vapEnabled = FALSE;
    for (i=wlanIndex; i < 16; i += 2)
    {
        wifi_getApEnable(i, &vapEnabled);
        if (vapEnabled == TRUE)
            MbssEnable += 1<<((i-wlanIndex)/2);
    }
    //zqiu: let driver control LED
	//wifi_setLED(wlanIndex, (MbssEnable & pCfg->MbssUserControl)?1:0);

    // Should this be Write-Only parameter?
    pCfg->ApplySetting  = FALSE;
    pCfg->ApplySettingSSID = 0;

    memcpy(&sWiFiDmlRadioStoredCfg[pCfg->InstanceNumber-1], pCfg, sizeof(COSA_DML_WIFI_RADIO_CFG));
    memcpy(&sWiFiDmlRadioRunningCfg[pCfg->InstanceNumber-1], pCfg, sizeof(COSA_DML_WIFI_RADIO_CFG));

    return ANSC_STATUS_SUCCESS;
}

	
ANSC_STATUS
CosaDmlWiFiRadioGetDinfo
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_DINFO  pInfo
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);

    if (!pInfo || (ulInstanceNumber<1) || (ulInstanceNumber>RADIO_INDEX_MAX))
    {
        return ANSC_STATUS_FAILURE;
    }
    
    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
        BOOL radioActive = TRUE;

        wifi_getRadioStatus(ulInstanceNumber-1,&radioActive);

		if( TRUE == radioActive )
		{
			pInfo->Status = COSA_DML_IF_STATUS_Up;
		}
		else
		{
			pInfo->Status = COSA_DML_IF_STATUS_Down;
		}

        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiRadioGetChannelsInUse
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_DINFO  pInfo
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);

    if (!pInfo || (ulInstanceNumber<1) || (ulInstanceNumber>RADIO_INDEX_MAX))
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pCfg is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
    
    if (FALSE)
    {
        return returnStatus;
    }
    else
    {
        pInfo->ChannelsInUse[0] = 0;
        wifi_getRadioChannelsInUse(ulInstanceNumber-1, pInfo->ChannelsInUse);
        CcspWifiTrace(("RDK_LOG_WARN,%s : ChannelsInUse = %s \n",__FUNCTION__,pInfo->ChannelsInUse));
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS
CosaDmlWiFiRadioGetApChannelScan
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_DINFO  pInfo
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    if (!pInfo)
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pCfg is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
    
    if (FALSE)
    {
        return returnStatus;
    }
    else
    { 
        if ( (strlen(pInfo->ApChannelScan) == 0) || (AnscGetTickInSeconds() -  pInfo->LastScan) > 60) 
        {
            pInfo->ApChannelScan[0] = 0;
            //zqiu: TODO: repleaced with wifi_getNeighboringWiFiDiagnosticResult2()
			//wifi_scanApChannels(ulInstanceNumber-1, pInfo->ApChannelScan); 
            pInfo->LastScan = AnscGetTickInSeconds();
        }
        return ANSC_STATUS_SUCCESS;
    }
}

// Called from middle layer to get current Radio channel RX and TX stats.

ANSC_STATUS
CosaDmlWiFiRadioChannelGetStats
    (
        char*                 ParamName,
        ULONG                 InstanceNumber,
        PCOSA_DML_WIFI_RADIO_CHANNEL_STATS  pChStats,
        UINT                  *percentage
    )
{
    ULONG currentchannel=0;
    ULLONG bss_total = 0, Tx_count = 0 , Rx_count = 0;
    *percentage = 0;
    wifi_channelStats_t chan_stats;
    ULONG currentTime = AnscGetTickInSeconds();

    //Do not re pull within 5 sec
    if ( ( currentTime - pChStats->LastUpdatedTime ) > 5 )
    {
        if ( (InstanceNumber < 1) || (InstanceNumber > RADIO_INDEX_MAX) )

        {
            return ANSC_STATUS_FAILURE;
        }
        if (wifi_getRadioChannel(InstanceNumber-1, &currentchannel) == RETURN_OK)
        {
            chan_stats.ch_number = currentchannel;
            chan_stats.ch_in_pool= TRUE;

            if (wifi_getRadioChannelStats(InstanceNumber-1, &chan_stats, 1) == RETURN_OK)
            {
                if ((pChStats->ch_number == chan_stats.ch_number) && (chan_stats.ch_utilization_busy_tx > pChStats->ch_utilization_busy_tx) )
                {
                    Tx_count = chan_stats.ch_utilization_busy_tx -  pChStats->ch_utilization_busy_tx;
                }
                else
                {
                    Tx_count = chan_stats.ch_utilization_busy_tx;
                }

                if ((pChStats->ch_number == chan_stats.ch_number) && (chan_stats.ch_utilization_busy_self > pChStats->ch_utilization_busy_self))
                {
                    Rx_count = chan_stats.ch_utilization_busy_self - pChStats->ch_utilization_busy_self;
                }
                else
                {
                    Rx_count = chan_stats.ch_utilization_busy_self;
                }
            }

            CcspWifiTrace(("RDK_LOG_INFO,%s: %d Radio %lu channel %d stats Tx_self : %llu Rx_self: %llu  \n"
                        ,__FUNCTION__,__LINE__,InstanceNumber,chan_stats.ch_number
                        ,chan_stats.ch_utilization_busy_tx, chan_stats.ch_utilization_busy_self));
            /* Update prev var for next call */
            pChStats->LastUpdatedTime = currentTime;
            pChStats->ch_number = chan_stats.ch_number;
            pChStats->ch_utilization_busy_tx = chan_stats.ch_utilization_busy_tx;
            pChStats->ch_utilization_busy_self = chan_stats.ch_utilization_busy_self;
            pChStats->last_tx_count = Tx_count;
            pChStats->last_rx_count = Rx_count;
        }
        else
        {
            CcspWifiTrace(("RDK_LOG_ERROR,%s: %d failed to get channel stats for radio %lu \n",__FUNCTION__,__LINE__,InstanceNumber));
        }
    }
    else
    {
        Tx_count = pChStats->last_tx_count;
        Rx_count = pChStats->last_rx_count;
    }

    bss_total = Tx_count + Rx_count;

    if (bss_total != 0)
    {
        if (strcmp(ParamName, "X_RDKCENTRAL-COM_AFTX") == 0)
        {
            *percentage = (UINT)round( (float) Tx_count / bss_total * 100 );
        }

        if (strcmp(ParamName, "X_RDKCENTRAL-COM_AFRX") == 0)
        {
            *percentage = (UINT)round( (float) Rx_count / bss_total * 100 );
        }
    }
    CcspWifiTrace(("RDK_LOG_INFO,%s: %d Radio %lu Current channel stats bss_total : %llu Tx_count : %llu Rx_count: %llu percentage: %d  \n"
                           ,__FUNCTION__,__LINE__,InstanceNumber, bss_total, Tx_count, Rx_count, *percentage));
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiRadioGetStats
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_RADIO_STATS  pStats
    )
{
	wifi_radioTrafficStats2_t 		radioTrafficStats;		
	UNREFERENCED_PARAMETER(hContext);

	ULONG currentTime = AnscGetTickInSeconds();
	if ( ( currentTime - pStats->StatisticsStartTime ) < 5 )	//Do not re pull within 5 sec
		return ANSC_STATUS_SUCCESS;

    if ((ulInstanceNumber<1) || (ulInstanceNumber>RADIO_INDEX_MAX))
    {
        return ANSC_STATUS_FAILURE;
    }

	CcspWifiTrace(("RDK_LOG_INFO,%s Getting Radio Stats last poll was %lu seconds ago \n",__FUNCTION__, currentTime - pStats->StatisticsStartTime ));
	pStats->StatisticsStartTime = currentTime;

	wifi_getRadioTrafficStats2(ulInstanceNumber-1, &radioTrafficStats);
	//zqiu: use the wifi_radioTrafficStats_t in phase3 wifi hal
	pStats->BytesSent				= radioTrafficStats.radio_BytesSent;
    pStats->BytesReceived			= radioTrafficStats.radio_BytesReceived;
    pStats->PacketsSent				= radioTrafficStats.radio_PacketsSent;
    pStats->PacketsReceived			= radioTrafficStats.radio_PacketsReceived;
    pStats->ErrorsSent				= radioTrafficStats.radio_ErrorsSent;
    pStats->ErrorsReceived			= radioTrafficStats.radio_ErrorsReceived;
    pStats->DiscardPacketsSent		= radioTrafficStats.radio_DiscardPacketsSent;
    pStats->DiscardPacketsReceived	= radioTrafficStats.radio_DiscardPacketsReceived;
	pStats->PLCPErrorCount			= radioTrafficStats.radio_PLCPErrorCount;
    pStats->FCSErrorCount			= radioTrafficStats.radio_FCSErrorCount;
    pStats->InvalidMACCount			= radioTrafficStats.radio_InvalidMACCount;
    pStats->PacketsOtherReceived	= radioTrafficStats.radio_PacketsOtherReceived;
	pStats->NoiseFloor				= radioTrafficStats.radio_NoiseFloor;

	pStats->ChannelUtilization				= radioTrafficStats.radio_ChannelUtilization;
	pStats->ActivityFactor					= radioTrafficStats.radio_ActivityFactor;
	pStats->CarrierSenseThreshold_Exceeded	= radioTrafficStats.radio_CarrierSenseThreshold_Exceeded;
	pStats->RetransmissionMetric			= radioTrafficStats.radio_RetransmissionMetirc;
	pStats->MaximumNoiseFloorOnChannel		= radioTrafficStats.radio_MaximumNoiseFloorOnChannel;
	pStats->MinimumNoiseFloorOnChannel		= radioTrafficStats.radio_MinimumNoiseFloorOnChannel;
	pStats->MedianNoiseFloorOnChannel		= radioTrafficStats.radio_MedianNoiseFloorOnChannel;
	
    return ANSC_STATUS_SUCCESS;
}

/* Description:
 *	The API retrieves the number of WiFi SSIDs in the system.
 */
ULONG
CosaDmlWiFiSsidGetNumberOfEntries
    (
        ANSC_HANDLE                 hContext
    )
{
    UNREFERENCED_PARAMETER(hContext);
#if defined (MULTILAN_FEATURE)
    if (!gSsidCount)
        wifi_getSSIDNumberOfEntries((ULONG*)&gSsidCount);

    if (gSsidCount < WIFI_INDEX_MIN)
    {
        gSsidCount = WIFI_INDEX_MIN;
    }
    else if (gSsidCount > WIFI_INDEX_MAX)
#else
    if (gSsidCount < 0)
    {
        gSsidCount = 1;
    }

    if (gSsidCount > WIFI_INDEX_MAX)
#endif
    {
        gSsidCount = WIFI_INDEX_MAX;
    }
    return gSsidCount;
}

/* Description:
 *	The API retrieves the complete info of the WiFi SSID designated by index. The usual process is the caller gets the total number of entries, then iterate through those by calling this API.
 * Arguments:
 * 	ulIndex		Indicates the index number of the entry.
 * 	pEntry		To receive the complete info of the entry.
 */
ANSC_STATUS
CosaDmlWiFiSsidGetEntry
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        PCOSA_DML_WIFI_SSID_FULL    pEntry
    )
{
wifiDbgPrintf("%s ulIndex = %lu \n",__FUNCTION__, ulIndex);
    int wlanIndex;

    int wlanRadioIndex;
    if (!pEntry || (ulIndex>=WIFI_INDEX_MAX)) return ANSC_STATUS_FAILURE;
    // sscanf(entry,"ath%d", &wlanIndex);
    wlanIndex = ulIndex;

    wifi_getApRadioIndex(wlanIndex, &wlanRadioIndex);

    /*Set default Name & Alias*/
    wifi_getApName(wlanIndex, pEntry->StaticInfo.Name);

    pEntry->Cfg.InstanceNumber    = wlanIndex+1;

    CosaDmlWiFiSsidGetCfg((ANSC_HANDLE)hContext,&pEntry->Cfg);
    CosaDmlWiFiSsidGetDinfo((ANSC_HANDLE)hContext,pEntry->Cfg.InstanceNumber,&pEntry->DynamicInfo);
    CosaDmlWiFiSsidGetSinfo((ANSC_HANDLE)hContext,pEntry->Cfg.InstanceNumber,&pEntry->StaticInfo);

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiSsidSetValues
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        ULONG                       ulInstanceNumber,
        char*                       pAlias
    )
{
    wifiDbgPrintf("%s\n",__FUNCTION__);
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulIndex);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    UNREFERENCED_PARAMETER(pAlias);    
    return ANSC_STATUS_SUCCESS;
}    

/* Description:
 *	The API adds a new WiFi SSID into the system. 
 * Arguments:
 *	hContext	reserved.
 *	pEntry		Caller pass in the configuration through pEntry->Cfg field and gets back the generated pEntry->StaticInfo.Name, MACAddress, etc.
 */
ANSC_STATUS
CosaDmlWiFiSsidAddEntry
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_SSID_FULL    pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
#if !defined (MULTILAN_FEATURE)
    UNREFERENCED_PARAMETER(pEntry);
#endif

#if defined (MULTILAN_FEATURE)
    PCOSA_DML_WIFI_SSID_CFG         pCfg           = &pEntry->Cfg;
    PUCHAR                          pLowerLayer    = NULL;
    CHAR                            paramName[COSA_DML_WIFI_ATM_MAX_APLIST_STR_LEN] = {0};
#endif
wifiDbgPrintf("%s\n",__FUNCTION__);
#if defined (MULTILAN_FEATURE)
    if(!pEntry)
    {
        CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pEntry is NULL \n",__FUNCTION__));

        return ANSC_STATUS_FAILURE;
    }
    
    if (gSsidCount >= WIFI_INDEX_MAX)
    {
        CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : Max SSID count \n",__FUNCTION__));

        return ANSC_STATUS_FAILURE;
    }
    int iRadioNameLength = strlen(pCfg->WiFiRadioName);
    if (iRadioNameLength >= 0)
    {
         wifiDbgPrintf("%s radioName %s\n", __func__, pCfg->WiFiRadioName);

         pLowerLayer = CosaUtilGetLowerLayers((PUCHAR)"Device.WiFi.Radio.", (PUCHAR)pCfg->WiFiRadioName);

         if (pLowerLayer != NULL)
         {
             snprintf(paramName, sizeof(paramName), SsidLowerLayers, pCfg->InstanceNumber);

             if (PSM_Set_Record_Value2(bus_handle,g_Subsystem, paramName, ccsp_string, (char*)pLowerLayer) != CCSP_SUCCESS)
             {
                 CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : PSM Set failed for %s\n",__FUNCTION__, paramName));
             }

             AnscFreeMemory(pLowerLayer);
         }
    }

#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    ++gSsidCount;

    AnscCopyMemory(&sWiFiDmlSsidStoredCfg[pCfg->InstanceNumber-1], pCfg, sizeof(COSA_DML_WIFI_SSID_CFG));
    AnscCopyMemory(&sWiFiDmlSsidRunningCfg[pCfg->InstanceNumber-1], pCfg, sizeof(COSA_DML_WIFI_SSID_CFG));
#else
    AnscCopyMemory(&sWiFiDmlSsidStoredCfg[gSsidCount], pCfg, sizeof(COSA_DML_WIFI_SSID_CFG));
    AnscCopyMemory(&sWiFiDmlSsidRunningCfg[gSsidCount++], pCfg, sizeof(COSA_DML_WIFI_SSID_CFG));
#endif

    return ANSC_STATUS_SUCCESS;
#else
    if (/*pPoamWiFiDm*/FALSE)
    {
        return ANSC_STATUS_SUCCESS;
    }
    else
    {
        if (gSsidCount < WIFI_INDEX_MAX)
        {
            gSsidCount++;
        }
        return ANSC_STATUS_SUCCESS;
    }
#endif
}

ANSC_STATUS
CosaDmlWiFiSsidDelEntry
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber
    )
{
    UNREFERENCED_PARAMETER(hContext);
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    int                             wRet = 0;
    PCOSA_DML_WIFI_AP_CFG           pStoredCfg = NULL;
    PCOSA_DML_WIFI_AP_CFG           pRunningCfg = NULL;
    ULONG                           uIndex = 0;
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
#endif
wifiDbgPrintf("%s\n",__FUNCTION__);

#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    if (FALSE/*pPoamWiFiDm*/)
    {
        return ANSC_STATUS_SUCCESS;
    }
    else
#else
    GET_SSID_INDEX(sWiFiDmlSsidStoredCfg, ulInstanceNumber, uIndex);

    pStoredCfg = &sWiFiDmlApStoredCfg[uIndex].Cfg;
    pRunningCfg = &sWiFiDmlApRunningCfg[uIndex].Cfg;

    wRet = wifi_deleteAp(ulInstanceNumber-1);
    if(wRet == 0)
#endif
    {
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
        if (gSsidCount > 0)
#else
        for (; uIndex < (ULONG)(gSsidCount-1); uIndex++)
#endif
        {
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
            gSsidCount--;
#else
            sWiFiDmlSsidStoredCfg[uIndex] = sWiFiDmlSsidStoredCfg[uIndex+1];
            sWiFiDmlSsidRunningCfg[uIndex] = sWiFiDmlSsidRunningCfg[uIndex+1];
#endif
        }
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
        return ANSC_STATUS_SUCCESS;
#else
        AnscZeroMemory(&sWiFiDmlSsidStoredCfg[uIndex], sizeof(COSA_DML_WIFI_SSID_CFG));
        AnscZeroMemory(&sWiFiDmlSsidRunningCfg[uIndex], sizeof(COSA_DML_WIFI_SSID_CFG));

        AnscCopyString(pStoredCfg->SSID, "");
        AnscCopyString(pRunningCfg->SSID, "");
        gSsidCount--;
#endif
    }
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    else
        returnStatus = ANSC_STATUS_FAILURE;

#if (defined(_COSA_BCM_ARM_) && defined(_XB7_PRODUCT_REQ_)) || defined(_XB8_PRODUCT_REQ_) || defined(_CBR2_PRODUCT_REQ_)
    /* Converting brcm patch to code and this code will be removed as part of Hal Version 3 changes */
    fprintf(stderr, "%s: calling wifi_apply()\n", __func__);
    wifi_apply();
#endif

    return returnStatus;
#endif
}

ANSC_STATUS
CosaDmlWiFiSsidSetCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_SSID_CFG     pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    PCOSA_DML_WIFI_SSID_CFG pStoredCfg = NULL;
    int wlanIndex = 0;
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
    int i = 0;
#endif
    UNREFERENCED_PARAMETER(hContext);

#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    ULONG uIndex = 0;
#endif
    BOOL cfgChange = FALSE;
	char status[64];
    BOOL bEnabled;
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
    BOOL bRadiusEnabled = FALSE;
#endif
    BOOLEAN bForceDisableFlag = FALSE;
    BOOL bsEnabled = FALSE;

wifiDbgPrintf("%s\n",__FUNCTION__);

    if (!pCfg)
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pCfg is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
    wlanIndex = pCfg->InstanceNumber-1;
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    if ( (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
        return ANSC_STATUS_FAILURE;
    }
#endif
    if(ANSC_STATUS_FAILURE == CosaDmlWiFiGetCurrForceDisableWiFiRadio(&bForceDisableFlag))
    {
        CcspWifiTrace(("RDK_LOG_WARN, %s Failed to fetch ForceDisableWiFiRadio flag!!!\n",__FUNCTION__));
    }
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    pStoredCfg = &sWiFiDmlSsidStoredCfg[pCfg->InstanceNumber-1];
#else
    GET_SSID_INDEX(sWiFiDmlSsidStoredCfg, pCfg->InstanceNumber, uIndex);

    pStoredCfg = &sWiFiDmlSsidStoredCfg[uIndex];
#endif
    /* when ForceDisableWiFiRadio feature is enabled all the radios are in disabled state.
       Hence we can't modify any radio or AP related params. Hence added a check to validate
       whether ForceDisableWiFiRadio feature is enabled or not.
     */
    if(bForceDisableFlag == FALSE) {
        if (pCfg->bEnabled != pStoredCfg->bEnabled) {
		CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : %s Calling wifi_setEnable to enable/disable SSID on interface:  %d enable: %d \n",__FUNCTION__,wlanIndex,pCfg->bEnabled));
                if (pCfg->bEnabled == 0) {
                      t2_event_d("WIFI_INFO_XHSSID_disabled", 1);
                } else if (pCfg->bEnabled == 1) {
                      t2_event_d("WIFI_INFO_XHSSID_enabled", 1);
                }
                int retStatus = wifi_setApEnable(wlanIndex, pCfg->bEnabled);
	        if(retStatus == 0) {
#ifdef _LG_MV1_CELENO_
	            wifi_getBandSteeringEnable_perSSID(wlanIndex/2,&bsEnabled);
	            if(bsEnabled)
	                enable_reset_both_radio_flag();
	            else
	                enable_reset_radio_flag(wlanIndex);
#endif
                    CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wifi_setApEnable success  index %d , %d\n",__FUNCTION__,wlanIndex,pCfg->bEnabled));
		 if (pCfg->InstanceNumber == 4) {
			char passph[128]={0};
			wifi_getApSecurityKeyPassphrase(2, passph);
			wifi_setApSecurityKeyPassphrase(3, passph);
			wifi_getApSecurityPreSharedKey(2, passph);
			wifi_setApSecurityPreSharedKey(3, passph);
			g_newXH5Gpass=TRUE;
			CcspWifiTrace(("RDK_LOG_INFO, XH 5G passphrase is set\n"));
		}

	     }
	   else {
        	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s wifi_setApEnable failed  index %d ,%d \n",__FUNCTION__,wlanIndex,pCfg->bEnabled));
	   }
#if defined(_INTEL_BUG_FIXES_)
           if (retStatus==0) {
#endif
		//zqiu:flag radio >>
		if(pCfg->bEnabled) {
			wifi_getSSIDStatus(wlanIndex, status);
fprintf(stderr, "----# %s %d ath%d Status=%s \n", __func__, __LINE__, wlanIndex, status );		
			if(strcmp(status, "Enabled")!=0) {
fprintf(stderr, "----# %s %d gRadioRestartRequest[%d]=true \n", __func__, __LINE__, wlanIndex%2 );			
				gRadioRestartRequest[wlanIndex%2]=TRUE;
			}
		}
		//<<
#if defined(_INTEL_BUG_FIXES_)
           }
           cfgChange = TRUE;
    }
    if(strcmp(pCfg->WiFiRadioName, pStoredCfg->WiFiRadioName) != 0) {
#endif
        cfgChange = TRUE;
    }
    if (strcmp(pCfg->SSID, pStoredCfg->SSID) != 0) {

#if defined(_COSA_FOR_BCI_)
        // Need to print out when the default SSID value is changed on BCI devices here since they don't have a captive portal mode.
        if (strcmp(pStoredCfg->SSID, SSID1_DEF) == 0 || strcmp(pStoredCfg->SSID, SSID2_DEF) == 0)
        {
            int uptime = 0;
            get_uptime(&uptime);
            CcspWifiTrace(("RDK_LOG_WARN,SSID_name_changed:%d\n",uptime));
            OnboardLog("SSID_name_changed:%d\n",uptime);
            t2_event_d("bootuptime_SSIDchanged_split", uptime);
            OnboardLog("SSID_name:%s\n",pCfg->SSID);
            CcspTraceInfo(("SSID_name:%s\n",pCfg->SSID));
        }
        else
        {
            CcspTraceInfo(("RDKB_WIFI_CONFIG_CHANGED : %s Calling wifi_setSSID to change SSID name on interface: %d SSID \n",__FUNCTION__,wlanIndex));
            t2_event_d("WIFI_INFO_XHCofigchanged", 1);
        }
#else
            CcspTraceInfo(("RDKB_WIFI_CONFIG_CHANGED : %s Calling wifi_setSSID to change SSID name on interface: %d SSID: %s \n",__FUNCTION__,wlanIndex,pCfg->SSID));
            t2_event_d("WIFI_INFO_XHCofigchanged", 1);
#endif

        wifi_setSSIDName(wlanIndex, pCfg->SSID);
#if defined(ENABLE_FEATURE_MESHWIFI)
        // Notify Mesh components of SSID change
        {
            CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh of SSID change\n",__FUNCTION__));
            // notify mesh components that wifi ssid setting changed
            // index|ssid
            v_secure_system("/usr/bin/sysevent set wifi_SSIDName \"RDK|%d|%s\"", wlanIndex, pCfg->SSID);
        }
#endif
        /*Restart Radio needed for 5GHz SSID, in case of 2.4GHz SSID pushSSID function is sufficient*/
#ifdef _LG_MV1_CELENO_
        wifi_getBandSteeringEnable_perSSID(wlanIndex/2,&bsEnabled);
        if(bsEnabled)
            enable_reset_both_radio_flag();
        else
        {
            if(wlanIndex%2 == 1)
            {
                enable_reset_radio_flag(wlanIndex);
            }
        }
#endif
        cfgChange = TRUE;
        CosaDmlWiFi_GetPreferPrivateData(&bEnabled);
        if (bEnabled == TRUE)
        {
            if(wlanIndex==0 || wlanIndex==1)
            {
                Delete_Hotspot_MacFilt_Entries();
            }
        }
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
	CosaDmlWiFiGetEnableRadiusGreylist(&bRadiusEnabled);
	if (bRadiusEnabled == TRUE)
	{
	    if(wlanIndex==0 || wlanIndex==1)
	    {
		for(i=0 ; i<HOTSPOT_NO_OF_INDEX ; i++)
		{
		    wifi_delApAclDevices(Hotspot_Index[i] - 1);
		}
	    }
	}
#endif
    }
    } else {
        CcspWifiTrace(("RDK_LOG_WARN, WIFI_ATTEMPT_TO_CHANGE_CONFIG_WHEN_FORCE_DISABLED \n"));
    }
    if (pCfg->EnableOnline != pStoredCfg->EnableOnline) {
#if !defined (_COSA_BCM_MIPS_)&& !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        wifi_setApEnableOnLine(wlanIndex, pCfg->EnableOnline);  
#endif
        cfgChange = TRUE;
    }
	
	//zqiu: 
    if (pCfg->RouterEnabled != pStoredCfg->RouterEnabled) {
		CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Calling wifi_setRouterEnable interface: %d SSID :%d \n",__FUNCTION__,wlanIndex,pCfg->RouterEnabled));
#if !defined (_COSA_BCM_MIPS_)&& !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
		wifi_setRouterEnable(wlanIndex, pCfg->RouterEnabled);
#endif
		cfgChange = TRUE;
    }


    if (cfgChange == TRUE)
    {
	pCfg->LastChange = AnscGetTickInSeconds(); 
	printf("%s: LastChange %lu \n", __func__, pCfg->LastChange);
	//Reset Telemetry statistics of all wifi clients for a specific vap (Per VAP), while VAP interface UP/DOWN.
	vap_stats_flag_change(pCfg->InstanceNumber-1, pCfg->bEnabled);	//reset vap client stats
    }

#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    memcpy(&sWiFiDmlSsidStoredCfg[pCfg->InstanceNumber-1], pCfg, sizeof(COSA_DML_WIFI_SSID_CFG));
#else
    memcpy(pStoredCfg, pCfg, sizeof(COSA_DML_WIFI_SSID_CFG));
#endif
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiSsidApplyCfg
    (
        PCOSA_DML_WIFI_SSID_CFG     pCfg
    )
{
    PCOSA_DML_WIFI_SSID_CFG pRunningCfg = NULL;
    int wlanIndex = 0;
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    ULONG uIndex = 0;
#endif

wifiDbgPrintf("%s\n",__FUNCTION__);
    if (!pCfg)
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pCfg is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
    wlanIndex = pCfg->InstanceNumber-1;
    if ( (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
        return ANSC_STATUS_FAILURE;
    }
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    GET_SSID_INDEX(sWiFiDmlSsidRunningCfg, pCfg->InstanceNumber, uIndex);
#endif
    wifiDbgPrintf("%s[%d] wlanIndex %d\n",__FUNCTION__, __LINE__, wlanIndex);
    CcspWifiTrace(("RDK_LOG_WARN,%s : wlanIndex %d \n",__FUNCTION__,wlanIndex));
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    pRunningCfg = &sWiFiDmlSsidRunningCfg[wlanIndex];
#else
    pRunningCfg = &sWiFiDmlSsidRunningCfg[uIndex];
#endif

    if (strcmp(pCfg->SSID, pRunningCfg->SSID) != 0) {
#if defined (FEATURE_HOSTAP_AUTHENTICATOR) && defined(_XB7_PRODUCT_REQ_)
        BOOLEAN isHostapdAuthEnabled = FALSE;
        CosaDmlWiFiGetHostapdAuthenticatorEnable(&isHostapdAuthEnabled);
        if (!isHostapdAuthEnabled) {
            wifi_pushSSID(wlanIndex, pCfg->SSID);
        }
#else
        wifi_pushSSID(wlanIndex, pCfg->SSID);
#endif
     }

#if defined(ENABLE_FEATURE_MESHWIFI)
    // Notify Mesh components of SSID change
    {
        //arg array will hold "RDK|" + index + "|" + ssid + NULL --> 4 + size(index) + 1 + 32 + 1
        CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh of SSID change\n",__FUNCTION__));

        // notify mesh components that wifi ssid setting changed
        // index|ssid
        v_secure_system("/usr/bin/sysevent set wifi_SSIDName 'RDK|%d|%s'", wlanIndex, pCfg->SSID);
    }
#endif

#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    memcpy(&sWiFiDmlSsidRunningCfg[pCfg->InstanceNumber-1], pCfg, sizeof(COSA_DML_WIFI_SSID_CFG));
#else
    memcpy(pRunningCfg, pCfg, sizeof(COSA_DML_WIFI_SSID_CFG));
#endif
	CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Returning Success \n",__FUNCTION__));
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiSsidGetCfg
    (
        ANSC_HANDLE                 hContext,
        PCOSA_DML_WIFI_SSID_CFG     pCfg
    )
{ 
    char rec[128];
    char *sval = NULL;
    BOOL bValue = 0;
    int wlanIndex = 0;
    UNREFERENCED_PARAMETER(hContext);

#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    int wlan_ret = 0;
#endif
    int wlanRadioIndex;
    BOOL enabled = FALSE;
    static BOOL firstTime[16] = { TRUE, true, true, true, true, true, true, true, 
                                        TRUE, true, true, true, true, true, true, true };
    
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    CHAR   ssidStatus[COSA_DML_WIFI_ATM_MAX_APLIST_STR_LEN] = {0};
    ULONG  uIndex = 0;
#endif
    if (!pCfg)
    {
        CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pCfg is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
    /*RDKB-6907, CID-32945, null check before use, avoid negative numbers*/
    if(pCfg->InstanceNumber > 0)
    {
        wlanIndex = pCfg->InstanceNumber-1;
    }
    else
    {
        wlanIndex = 0;
    }
    if (wlanIndex >= WIFI_INDEX_MAX)
    {
        return ANSC_STATUS_FAILURE;
    }
    wifiDbgPrintf("[%s] THE wlanIndex = %d\n",__FUNCTION__, wlanIndex);

    if (firstTime[wlanIndex] == TRUE) {
        pCfg->LastChange = AnscGetTickInSeconds(); 
        firstTime[wlanIndex] = FALSE;
    }

#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    wifi_getApRadioIndex(wlanIndex, &wlanRadioIndex);
#else
    uIndex = wlanIndex;

    while((wlanIndex < WIFI_INDEX_MAX) && ((wlan_ret=wifi_getSSIDStatus(wlanIndex, ssidStatus)) != 0))
        ++wlanIndex;

    if ( wlanIndex < WIFI_INDEX_MAX)
        pCfg->InstanceNumber = wlanIndex+1;
    else
        wlanIndex = uIndex;

    wlan_ret = wifi_getApRadioIndex(wlanIndex, &wlanRadioIndex);
#endif

//#if defined(_COSA_BCM_MIPS_) || defined(INTEL_PUMA7)
//	_ansc_sprintf(pCfg->Alias, "ath%d",wlanIndex);    //zqiu: need BCM to fix bug in wifi_getApName, then we could remove this code
//#else
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
	wifi_getApName(wlanIndex, pCfg->Alias);
#else
        sprintf(pCfg->Alias,"cpe-SSID%lu", pCfg->InstanceNumber);
#endif
//#endif    

    wifi_getApEnable(wlanIndex, &enabled);
    pCfg->bEnabled = (enabled == TRUE) ? TRUE : FALSE;

    //zqiu
    //_ansc_sprintf(pCfg->WiFiRadioName, "wifi%d",wlanRadioIndex);
    wifi_getRadioIfName(wlanRadioIndex, pCfg->WiFiRadioName);

//LGI add begin
//Need to check for Primary and guest except hotspot
    snprintf(rec, sizeof(rec), BssHotSpot, pCfg->InstanceNumber);
    if (PSM_Get_Record_Value2(bus_handle, g_Subsystem, rec, NULL, &sval) == CCSP_SUCCESS) {
       bValue = (1 == atoi(sval)) ? FALSE : TRUE;
       ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(sval);
    } else {
        AnscTraceError(("%s: fail to get PSM record !\n", __FUNCTION__));
    }
       if(bValue)
           checkforbiddenSSID(wlanIndex);
//LGI add end

    wifi_getSSIDName(wlanIndex, pCfg->SSID);

    getDefaultSSID(wlanIndex,pCfg->DefaultSSID);

#if !defined(_COSA_BCM_MIPS_)&& !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
    wifi_getApEnableOnLine(wlanIndex, &enabled);
#else
    wifi_getApEnable(wlanIndex, &enabled);
#endif
    pCfg->EnableOnline = (enabled == TRUE) ? TRUE : FALSE;

#if !defined(_COSA_BCM_MIPS_)&& !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
	//zqiu:
    wifi_getRouterEnable(wlanIndex, &enabled);
    pCfg->RouterEnabled = (enabled == TRUE) ? TRUE : FALSE;
	//pCfg->RouterEnabled = TRUE;
#else
    pCfg->RouterEnabled = TRUE;
#endif
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    for ( uIndex = 0; uIndex < (ULONG)gSsidCount; uIndex++ )
    {
        if ( sWiFiDmlSsidStoredCfg[uIndex].InstanceNumber == pCfg->InstanceNumber || sWiFiDmlSsidStoredCfg[uIndex].InstanceNumber == 0)
            break;
    }
/*RDKB-13101 & CID:-34063*/
    memcpy(&sWiFiDmlSsidStoredCfg[uIndex], pCfg, sizeof(COSA_DML_WIFI_SSID_CFG));
/*RDKB-13101 & CID:-34059*/
    memcpy(&sWiFiDmlSsidRunningCfg[uIndex], pCfg, sizeof(COSA_DML_WIFI_SSID_CFG));
#else
/*RDKB-13101 & CID:-34063*/
    memcpy(&sWiFiDmlSsidStoredCfg[wlanIndex], pCfg, sizeof(COSA_DML_WIFI_SSID_CFG));
/*RDKB-13101 & CID:-34059*/
    memcpy(&sWiFiDmlSsidRunningCfg[wlanIndex], pCfg, sizeof(COSA_DML_WIFI_SSID_CFG));
#endif

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiSsidGetDinfo
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_SSID_DINFO   pInfo
    )
{
    ULONG wlanIndex = ulInstanceNumber-1;
    UNREFERENCED_PARAMETER(hContext);

    if (!pInfo)
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pCfg is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
    
    if ( (wlanIndex >= WIFI_INDEX_MAX) )
    {
        return ANSC_STATUS_FAILURE;
    }

    char vapStatus[32];
	BOOL enabled; 


	wifi_getApEnable(wlanIndex, &enabled);
	// Nothing to do if VAP is not enabled
	if (enabled == FALSE) {
            pInfo->Status = COSA_DML_IF_STATUS_Down;
	} else {

        wifi_getApStatus(wlanIndex, vapStatus);

        if ((strncmp(vapStatus, "Up", strlen("Up")) == 0) || (strncmp(vapStatus, "Enabled", strlen("Enabled")) == 0))
        {
            pInfo->Status = COSA_DML_IF_STATUS_Up;
        } else if (strncmp(vapStatus,"Disable", strlen("Disable")) == 0)
        {
            pInfo->Status = COSA_DML_IF_STATUS_Down;
        } else 
        {
            pInfo->Status = COSA_DML_IF_STATUS_Error;
        }

    }

    return ANSC_STATUS_SUCCESS;
}
/*
ANSC_STATUS
CosaDmlWiFiSsidFillSinfoCache
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
wifiDbgPrintf("%s: ulInstanceNumber = %d\n",__FUNCTION__, ulInstanceNumber);

    ULONG wlanIndex = ulInstanceNumber-1;
    char bssid[32];
    PCOSA_DML_WIFI_SSID_SINFO   pInfo = &gCachedSsidInfo[wlanIndex];
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : ulInstanceNumber = %d \n",__FUNCTION__,ulInstanceNumber));
//>> zqiu  
	char mac[32];
	char status[32];
	
#if !defined(_COSA_BCM_MIPS_) && !defined(INTEL_PUMA7)
	wifi_getSSIDStatus(wlanIndex, status);
	if(strcmp(status,"Enabled")==0) {
		wifi_getApName(wlanIndex, pInfo->Name);
		wifi_getBaseBSSID(wlanIndex, bssid);
		wifi_getSSIDMACAddress(wlanIndex, mac);

		sMac_to_cMac(mac, &pInfo->MacAddress);
		sMac_to_cMac(bssid, &pInfo->BSSID);
	}  

#else
    sprintf(pInfo->Name,"ath%d", wlanIndex);

    memset(bssid,0,sizeof(bssid));
    memset(pInfo->BSSID,0,sizeof(pInfo->BSSID));
    memset(pInfo->MacAddress,0,sizeof(pInfo->MacAddress));

    wifi_getBaseBSSID(wlanIndex, bssid);
    wifi_getSSIDMACAddress(wlanIndex, mac);
    wifiDbgPrintf("%s: wifi_getBaseBSSID returned bssid = %s\n",__FUNCTION__, bssid);
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : wifi_getBaseBSSID returned bssid = %s \n",__FUNCTION__,bssid));

    wifiDbgPrintf("%s: wifi_getSSIDMACAddress returned mac = %s\n",__FUNCTION__, mac);
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : wifi_getSSIDMACAddress returned mac = %s \n",__FUNCTION__,mac));

    // 1st set the main radio mac/bssid
    sMac_to_cMac(mac, &pInfo->MacAddress);
	// The Bssid will be zeros if the radio is not up. This is a problem since we never refresh the cache
	// For now we will just set the bssid to the mac
   	// sMac_to_cMac(bssid, &pInfo->BSSID);
    sMac_to_cMac(mac, &pInfo->BSSID);

    wifiDbgPrintf("%s: BSSID %02x%02x%02x%02x%02x%02x\n", __func__,
       pInfo->BSSID[0], pInfo->BSSID[1], pInfo->BSSID[2],
       pInfo->BSSID[3], pInfo->BSSID[4], pInfo->BSSID[5]);
    wifiDbgPrintf("%s: MacAddress %02x%02x%02x%02x%02x%02x\n", __func__,
       pInfo->MacAddress[0], pInfo->MacAddress[1], pInfo->MacAddress[2],
       pInfo->MacAddress[3], pInfo->MacAddress[4], pInfo->MacAddress[5]);

	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : ath%d BSSID %02x%02x%02x%02x%02x%02x\n",__FUNCTION__,wlanIndex,pInfo->BSSID[0], pInfo->BSSID[1], pInfo->BSSID[2],pInfo->BSSID[3], pInfo->BSSID[4], pInfo->BSSID[5]));
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s :  ath%d MacAddress %02x%02x%02x%02x%02x%02x\n",__FUNCTION__,wlanIndex,pInfo->MacAddress[0], pInfo->MacAddress[1], pInfo->MacAddress[2],pInfo->MacAddress[3], pInfo->MacAddress[4], pInfo->MacAddress[5]));

	int i=0;
	// Now set the sub radio mac addresses and bssids
    for (i = wlanIndex+2; i < gSsidCount; i += 2) {
    	int ret;
        sprintf(gCachedSsidInfo[i].Name,"ath%d", i);

        wifi_getSSIDMACAddress(i, mac);
        sMac_to_cMac(mac, &gCachedSsidInfo[i].MacAddress);

        wifi_getBaseBSSID(i, bssid);
       	// sMac_to_cMac(bssid, &gCachedSsidInfo[i].BSSID);
		// The Bssid will be zeros if the radio is not up. This is a problem since we never refresh the cache
		// For now we will just set the bssid to the mac
       	sMac_to_cMac(mac, &gCachedSsidInfo[i].BSSID);

    	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : ath%d BSSID %02x%02x%02x%02x%02x%02x\n",__FUNCTION__,i,gCachedSsidInfo[i].BSSID[0], gCachedSsidInfo[i].BSSID[1], gCachedSsidInfo[i].BSSID[2],gCachedSsidInfo[i].BSSID[3], gCachedSsidInfo[i].BSSID[4], gCachedSsidInfo[i].BSSID[5]));
    	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : ath%d MacAddress %02x%02x%02x%02x%02x%02x\n",__FUNCTION__,i,gCachedSsidInfo[i].MacAddress[0], gCachedSsidInfo[i].MacAddress[1], gCachedSsidInfo[i].MacAddress[2],gCachedSsidInfo[i].MacAddress[3], gCachedSsidInfo[i].MacAddress[4], gCachedSsidInfo[i].MacAddress[5]));
    }
#endif
//<<
	CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Returning Success \n",__FUNCTION__));
    return ANSC_STATUS_SUCCESS;
}
*/
ANSC_STATUS
CosaDmlWiFiSsidGetSinfo
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_SSID_SINFO   pInfo
    )
{
    UNREFERENCED_PARAMETER(hContext);
    CcspWifiTrace(("RDK_LOG_INFO,%s: ulInstanceNumber = %lu\n",__FUNCTION__, ulInstanceNumber));

    if (!pInfo)
    {
        CcspWifiTrace(("RDK_LOG_ERROR, %s pCfg NULL\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    char bssid[64];

    ULONG wlanIndex = ulInstanceNumber-1;
    if ( (wlanIndex >= WIFI_INDEX_MAX) )
    {
        return ANSC_STATUS_FAILURE;
    }

	//>>zqiu
//#if defined(_COSA_BCM_MIPS_) || defined(INTEL_PUMA7)
//	sprintf(pInfo->Name,"ath%d", wlanIndex);     //zqiu: need BCM to fix bug in wifi_getApName, then we could remove this code
//#else
	wifi_getApName(wlanIndex, pInfo->Name);
//#endif
	//memcpy(pInfo,&gCachedSsidInfo[wlanIndex],sizeof(COSA_DML_WIFI_SSID_SINFO));
        /*TODO CID: 78777 Out-of-bounds access - Fix in QTN code*/
        /*CID:67080 Unchecked return value - returns RETURN_OK (0) on success*/
	if((wifi_getBaseBSSID(wlanIndex, bssid)) && (!strcmp(bssid,"")))
	{
#if defined(_HUB4_PRODUCT_REQ_)
      /* * For HUB4 only supports 4 SSIDs so remaning error prints are not required for this case */  
      if( ( ( AP_INDEX_1 - 1 ) == wlanIndex ) || ( ( AP_INDEX_2 - 1 ) == wlanIndex ) || ( ( AP_INDEX_13 - 1 ) == wlanIndex ) || ( ( AP_INDEX_14 - 1 ) == wlanIndex ) ) 
#endif
      {
          CcspWifiTrace(("RDK_LOG_WARN,%s : HAL function returns bssid as NULL string for wlanIndex[%lu]\n", __FUNCTION__, wlanIndex));
      }
      
      return ANSC_STATUS_FAILURE;
	}
        CcspTraceInfo(("%s : wlanIndex[%lu] BSSID [%s] \n",__FUNCTION__, wlanIndex, bssid));

	sMac_to_cMac(bssid, pInfo->BSSID);
	sMac_to_cMac(bssid, pInfo->MacAddress);  
        CcspWifiTrace(("RDK_LOG_DEBUG,%s: %s BSSID %s\n",__FUNCTION__,pInfo->Name,bssid));
	//<<
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiSsidGetStats
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber,
        PCOSA_DML_WIFI_SSID_STATS   pStats
    )
{
	ULONG currentTime = AnscGetTickInSeconds(); 
	UNREFERENCED_PARAMETER(hContext);

    if ( (ulInstanceNumber < 1) || (ulInstanceNumber > WIFI_INDEX_MAX) )
    {
        CcspWifiTrace(("RDK_LOG_DEBUG,%s: %d invalid instance number : %lu \n",__FUNCTION__,__LINE__,ulInstanceNumber));
        return ANSC_STATUS_FAILURE;
    }
    // if the last poll was within 10 seconds skip the poll
    // When the whole Stat table is pull the top level DML calls this funtion once
    // for each parameter in the table
    if ( (currentTime - sWiFiDmlSsidLastStatPoll[ulInstanceNumber-1]) < 10)  {
        return ANSC_STATUS_SUCCESS;
    } 
    CcspWifiTrace(("RDK_LOG_INFO,%s Getting Stats last poll was %lu seconds ago \n",__FUNCTION__, currentTime-sWiFiDmlSsidLastStatPoll[ulInstanceNumber-1] ));
    sWiFiDmlSsidLastStatPoll[ulInstanceNumber-1] = currentTime;

    if (!pStats)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    ULONG wlanIndex = ulInstanceNumber-1;
    wifi_basicTrafficStats_t basicStats;
    wifi_trafficStats_t errorStats;
    wifi_ssidTrafficStats2_t transStats;

    char status[16];
    BOOL enabled; 

    memset(pStats,0,sizeof(COSA_DML_WIFI_SSID_STATS));

    wifi_getApEnable(wlanIndex, &enabled);
    // Nothing to do if VAP is not enabled
    if (enabled == FALSE) {
        return ANSC_STATUS_SUCCESS; 
    }

    wifi_getApStatus(wlanIndex, status);
    if (strstr(status,"Up") == NULL) {
        return ANSC_STATUS_SUCCESS; 
    }

    int result = 0;
    result = wifi_getBasicTrafficStats(wlanIndex, &basicStats);
    if (result == 0) {
        pStats->BytesSent = basicStats.wifi_BytesSent;
        pStats->BytesReceived = basicStats.wifi_BytesReceived;
        pStats->PacketsSent = basicStats.wifi_PacketsSent; 
        pStats->PacketsReceived = basicStats.wifi_PacketsReceived;
    }

    result = wifi_getWifiTrafficStats(wlanIndex, &errorStats);
    if (result == 0) {
        pStats->ErrorsSent                         = errorStats.wifi_ErrorsSent;
        pStats->ErrorsReceived                     = errorStats.wifi_ErrorsReceived;
        pStats->UnicastPacketsSent                 = errorStats.wifi_UnicastPacketsSent;
        pStats->UnicastPacketsReceived             = errorStats.wifi_UnicastPacketsReceived;
        pStats->DiscardPacketsSent                 = errorStats.wifi_DiscardedPacketsSent;
        pStats->DiscardPacketsReceived             = errorStats.wifi_DiscardedPacketsReceived;
        pStats->MulticastPacketsSent               = errorStats.wifi_MulticastPacketsSent;
        pStats->MulticastPacketsReceived           = errorStats.wifi_MulticastPacketsReceived;
        pStats->BroadcastPacketsSent               = errorStats.wifi_BroadcastPacketsSent;
        pStats->BroadcastPacketsReceived           = errorStats.wifi_BroadcastPacketsRecevied;
        pStats->UnknownProtoPacketsReceived        = errorStats.wifi_UnknownPacketsReceived;
    }

    result = wifi_getSSIDTrafficStats2(wlanIndex, &transStats);
    if (result == 0) {
        pStats->RetransCount                       = transStats.ssid_RetransCount;
        pStats->FailedRetransCount                 = transStats.ssid_FailedRetransCount;
        pStats->RetryCount	                       = transStats.ssid_RetryCount;
        pStats->MultipleRetryCount                 = transStats.ssid_MultipleRetryCount	;
        pStats->ACKFailureCount                    = transStats.ssid_ACKFailureCount;
        pStats->AggregatedPacketCount              = transStats.ssid_AggregatedPacketCount;
    }
    return ANSC_STATUS_SUCCESS;
}

BOOL CosaDmlWiFiSsidValidateSSID (void)
{
    BOOL validateFlag = FALSE;
    char *strValue = NULL;
    int retPsmGet = CCSP_SUCCESS;
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s Calling PSM GET\n",__FUNCTION__));

        if (!g_wifidb_rfc) {
	retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, ValidateSSIDName, NULL, &strValue);
	if (retPsmGet == CCSP_SUCCESS) {
		CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : PSG GET Success \n",__FUNCTION__));
        validateFlag = _ansc_atoi(strValue);
	    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
	}
	else
	{
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : PSM Get Failed \n",__FUNCTION__));
	}
        } else {
            struct schema_Wifi_Global_Config *pcfg = NULL;
            pcfg = (struct schema_Wifi_Global_Config  *) wifi_db_get_table_entry(NULL, NULL,&table_Wifi_Global_Config,OCLM_UUID);
            if (pcfg != NULL) {
                validateFlag = pcfg->validate_ssid;
                CcspWifiTrace(("RDK_LOG_INFO,%s WIFI DB GET Success \n",__FUNCTION__));
                free(pcfg);
            } else {
                CcspWifiTrace(("RDK_LOG_ERROR, %s WIFI DB Get Failed \n",__FUNCTION__));
            }
        }

    return validateFlag;
}

ANSC_STATUS
CosaDmlWiFiSsidGetSSID
    (
        ULONG                       ulInstanceNumber,
        char                            *ssid
    )
{
wifiDbgPrintf("%s\n",__FUNCTION__);

    if (!ssid) return ANSC_STATUS_FAILURE;
    ULONG wlanIndex = ulInstanceNumber-1;
    if ( (ulInstanceNumber < 1) || (ulInstanceNumber > WIFI_INDEX_MAX) )
    {
        return ANSC_STATUS_FAILURE;
    }
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s Calling wifi_getSSIDName ulInstanceNumber = %lu\n",__FUNCTION__,ulInstanceNumber));
    wifi_getSSIDName(wlanIndex, ssid);
	CcspTraceInfo(("WIFI %s wlanIndex = %lu ssid = %s\n",__FUNCTION__,ulInstanceNumber,ssid));
    return ANSC_STATUS_SUCCESS;

}

/* WiFi AP is always associated with a SSID in the system */
ULONG
CosaDmlWiFiAPGetNumberOfEntries
    (
        ANSC_HANDLE                 hContext
    )
{
    wifiDbgPrintf("%s\n",__FUNCTION__);
    UNREFERENCED_PARAMETER(hContext);
    return gApCount;
}

ANSC_STATUS
CosaDmlWiFiApGetEntry
    (
        ANSC_HANDLE                 hContext,
        char                        *pSsid,
        PCOSA_DML_WIFI_AP_FULL      pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    if (!pSsid || !pEntry) return ANSC_STATUS_FAILURE;
    wifiDbgPrintf("%s pSsid = %s\n",__FUNCTION__, pSsid);

    CosaDmlWiFiApGetCfg(NULL, pSsid, &pEntry->Cfg);
    CosaDmlWiFiApGetInfo(NULL, pSsid, &pEntry->Info);
    CosaDmlGetApRadiusSettings(NULL,pSsid,&pEntry->RadiusSetting);  //zqiu: move from RadiusSettings_GetParamIntValue; RadiusSettings_GetParamBoolValue
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApSetValues
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulIndex,
        ULONG                       ulInstanceNumber,
        char*                       pAlias
    )
{
    wifiDbgPrintf("%s\n",__FUNCTION__);
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(ulIndex);
    UNREFERENCED_PARAMETER(ulInstanceNumber);
    UNREFERENCED_PARAMETER(pAlias);    
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApPushCfg
    (
        PCOSA_DML_WIFI_AP_CFG       pCfg
    )
{
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    ULONG                           uIndex         = 0;
#endif
wifiDbgPrintf("%s\n",__FUNCTION__);

    if (!pCfg)
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pCfg is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
        
    int wlanIndex = pCfg->InstanceNumber-1;

    if ( (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
        return ANSC_STATUS_FAILURE;
    }
    wifi_setApWmmEnable(wlanIndex,pCfg->WMMEnable);
    wifi_setApWmmUapsdEnable(wlanIndex, pCfg->UAPSDEnable);        

    {
	// Ic and Og policies set the same for first GA release
    // setting Ack policy so negate the NoAck value
	wifi_setApWmmOgAckPolicy(wlanIndex, 0, !pCfg->WmmNoAck);
	wifi_setApWmmOgAckPolicy(wlanIndex, 1, !pCfg->WmmNoAck);
	wifi_setApWmmOgAckPolicy(wlanIndex, 2, !pCfg->WmmNoAck);
	wifi_setApWmmOgAckPolicy(wlanIndex, 3, !pCfg->WmmNoAck);
    }
    wifi_setApMaxAssociatedDevices(wlanIndex, pCfg->BssMaxNumSta);
    
    wifi_setApIsolationEnable(wlanIndex, pCfg->IsolationEnable);

    wifi_pushSsidAdvertisementEnable(wlanIndex, pCfg->SSIDAdvertisementEnabled);

#if defined(ENABLE_FEATURE_MESHWIFI)
    {
        // notify mesh components that wifi SSID Advertise changed
        CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh of SSID Advertise changes\n",__FUNCTION__));
        v_secure_system("/usr/bin/sysevent set wifi_SSIDAdvertisementEnable 'RDK|%d|%s'", wlanIndex, (pCfg->SSIDAdvertisementEnabled?"true":"false"));
    }
#endif

#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    memcpy(&sWiFiDmlApRunningCfg[pCfg->InstanceNumber-1], pCfg, sizeof(COSA_DML_WIFI_AP_CFG));
#else
    GET_AP_INDEX(sWiFiDmlApRunningCfg, pCfg->InstanceNumber, uIndex);
    memcpy(&sWiFiDmlApRunningCfg[uIndex].Cfg, pCfg, sizeof(COSA_DML_WIFI_AP_CFG));
#endif

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApPushMacFilter
    (
        QUEUE_HEADER       *pMfQueue,
        ULONG                      wlanIndex
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pMacFiltLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)NULL; 
    PCOSA_DML_WIFI_AP_MAC_FILTER    pMacFilt            = (PCOSA_DML_WIFI_AP_MAC_FILTER        )NULL;
wifiDbgPrintf("%s\n",__FUNCTION__);

    if (!pMfQueue)
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pMfQueue is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
    if ( (wlanIndex >= WIFI_INDEX_MAX) )
    {
        return ANSC_STATUS_FAILURE;
    }
wifiDbgPrintf("%s : %d filters \n",__FUNCTION__, pMfQueue->Depth);
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : %d filters \n",__FUNCTION__, pMfQueue->Depth));
    int index;
    for (index = 0; index < pMfQueue->Depth; index++) {
        pMacFiltLinkObj = (PCOSA_CONTEXT_LINK_OBJECT) AnscSListGetEntryByIndex((PSLIST_HEADER)pMfQueue, index);
        if (pMacFiltLinkObj) {
            pMacFilt =  (PCOSA_DML_WIFI_AP_MAC_FILTER) pMacFiltLinkObj->hContext;
            wifi_addApAclDevice(wlanIndex, pMacFilt->MACAddress);
#if defined(ENABLE_FEATURE_MESHWIFI)
            {
                // notify mesh components that wifi SSID Advertise changed
                CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh to add device\n",__FUNCTION__));
                v_secure_system("/usr/bin/sysevent set wifi_addApAclDevice 'RDK|%lu|%s'", wlanIndex, pMacFilt->MACAddress);
            }
#endif
        }
    }
    
        
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_CFG       pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    ULONG                           uIndex         = 0;
#endif
    PCOSA_DML_WIFI_AP_CFG pStoredCfg = NULL;
wifiDbgPrintf("%s\n",__FUNCTION__);
	CcspWifiTrace(("RDK_LOG_WARN,WIFI %s\n",__FUNCTION__));
    if (!pCfg || !pSsid)
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pCfg is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    pStoredCfg = &sWiFiDmlApStoredCfg[pCfg->InstanceNumber-1].Cfg;
#else
    GET_AP_INDEX(sWiFiDmlApStoredCfg, pCfg->InstanceNumber, uIndex);
    pStoredCfg = &sWiFiDmlApStoredCfg[uIndex].Cfg;
#endif
        
    int wlanIndex = -1;
    int wlanRadioIndex = 0;
    int ret=0;

    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
	// Error could not find index
	CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : could not find index wlanIndex(-1)  \n",__FUNCTION__));
	return ANSC_STATUS_FAILURE;
    }
    wifi_getApRadioIndex(wlanIndex, &wlanRadioIndex);
    pCfg->InstanceNumber = wlanIndex+1;

    CosaDmlWiFiSetAccessPointPsmData(pCfg);

    // only set on SSID
    // wifi_setEnable(wlanIndex, pCfg->bEnabled);

    /* USGv2 Extensions */
    // static value for first GA release not settable
    pCfg->LongRetryLimit = 16;
    //pCfg->RetryLimit     = 16;
    wifi_setApRetryLimit(wlanIndex,pCfg->RetryLimit);

    // These should be pushed when the SSID is up
    //  They are currently set from ApGetCfg when it call GetAccessPointPsmData

    if (pCfg->WMMEnable != pStoredCfg->WMMEnable) {
        wifi_setApWmmEnable(wlanIndex,pCfg->WMMEnable);
        enable_reset_radio_flag(wlanIndex);
    }

    if (pCfg->UAPSDEnable != pStoredCfg->UAPSDEnable) {
        wifi_setApWmmUapsdEnable(wlanIndex, pCfg->UAPSDEnable);
        if(wlanIndex%2 == 0)/*UAPSDEnable Not supported by 5G Radio so avoid Radio reset for 5G*/
            enable_reset_radio_flag(wlanIndex);
    }

    if (pCfg->WmmNoAck != pStoredCfg->WmmNoAck) {
        // Ic and Og policies set the same for first GA release
        wifi_setApWmmOgAckPolicy(wlanIndex, 0, pCfg->WmmNoAck);
        wifi_setApWmmOgAckPolicy(wlanIndex, 1, pCfg->WmmNoAck);
        wifi_setApWmmOgAckPolicy(wlanIndex, 2, pCfg->WmmNoAck);
        wifi_setApWmmOgAckPolicy(wlanIndex, 3, pCfg->WmmNoAck);
        enable_reset_radio_flag(wlanIndex);
    }

    if (pCfg->IsolationEnable != pStoredCfg->IsolationEnable) {
            wifi_setApIsolationEnable(wlanIndex, pCfg->IsolationEnable);
    }

    if (pCfg->SSIDAdvertisementEnabled != pStoredCfg->SSIDAdvertisementEnabled) {
        wifi_setApSsidAdvertisementEnable(wlanIndex, pCfg->SSIDAdvertisementEnabled);
        if(wlanIndex%2 == 1)
        {
            enable_reset_radio_flag(wlanIndex);
        }
#if defined(ENABLE_FEATURE_MESHWIFI)
        {
            // notify mesh components that wifi SSID Advertise changed
            CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh of SSID Advertise changes\n",__FUNCTION__));
            v_secure_system("/usr/bin/sysevent set wifi_SSIDAdvertisementEnable 'RDK|%d|%s'", wlanIndex, (pCfg->SSIDAdvertisementEnabled?"true":"false"));
        }
#endif
    }

    if (pCfg->MaxAssociatedDevices != pStoredCfg->MaxAssociatedDevices) {
        wifi_setApMaxAssociatedDevices(wlanIndex, pCfg->MaxAssociatedDevices);
    }
        if (pCfg->ManagementFramePowerControl != pStoredCfg->ManagementFramePowerControl) {
	CcspWifiTrace(("RDK_LOG_INFO,X_RDKCENTRAL-COM_ManagementFramePowerControl:%d\n", pCfg->ManagementFramePowerControl));
        wifi_setApManagementFramePowerControl(wlanIndex, pCfg->ManagementFramePowerControl);
	CcspTraceWarning(("X_RDKCENTRAL-COM_ManagementFramePowerControl_Set:<%d>\n", pCfg->ManagementFramePowerControl));
        }
//>> zqiu	
    if (strcmp(pCfg->BeaconRate,pStoredCfg->BeaconRate)!=0) {
	CcspWifiTrace(("RDK_LOG_INFO,X_RDKCENTRAL-COM_BeaconRate:%s\n", pCfg->BeaconRate));
	
#ifdef _BEACONRATE_SUPPORT	
        ret=wifi_setApBeaconRate(wlanIndex, pCfg->BeaconRate);
	if(ret<0) {
	    CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : Wifi_setApBeaconRate is failed\n",__FUNCTION__));
	    return ANSC_STATUS_FAILURE;
	}
//RDKB-18000 Logging the changed value in wifihealth.txt
	CcspWifiTrace(("RDK_LOG_WARN,BEACON RATE CHANGED vAP%d %s to %s by TR-181 Object Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_BeaconRate\n",wlanIndex,pStoredCfg->BeaconRate,pCfg->BeaconRate,wlanIndex+1));
#endif		
    }
//<<
    if (pCfg->HighWatermarkThreshold != pStoredCfg->HighWatermarkThreshold) {
        wifi_setApAssociatedDevicesHighWatermarkThreshold(wlanIndex, pCfg->HighWatermarkThreshold);
    }
    // pCfg->MulticastRate = 123;
    // pCfg->BssCountStaAsCpe  = TRUE;

    if (pCfg->KickAssocDevices == TRUE) {
        CosaDmlWiFiApKickAssocDevices(pSsid);
        t2_event_d("WIFI_INFO_Kickoff_All_Clients", 1);
        pCfg->KickAssocDevices = FALSE;
    }

 /*   if (pCfg->InterworkingEnable != pStoredCfg->InterworkingEnable) {
        wifi_setInterworkingServiceEnable(wlanIndex, pCfg->InterworkingEnable);
    }*/
    if (pCfg->BSSTransitionActivated != pStoredCfg->BSSTransitionActivated) {
        if ( CosaDmlWifi_setBSSTransitionActivated(pCfg, wlanIndex) != ANSC_STATUS_SUCCESS) {
             pCfg->BSSTransitionActivated = false;
        } 
    }

#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    memcpy(&sWiFiDmlApStoredCfg[pCfg->InstanceNumber-1].Cfg, pCfg, sizeof(COSA_DML_WIFI_AP_CFG));
#else
    memcpy(pStoredCfg, pCfg, sizeof(COSA_DML_WIFI_AP_CFG));
#endif
	CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Returning Success \n",__FUNCTION__));
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApApplyCfg
    (
        PCOSA_DML_WIFI_AP_CFG       pCfg
    )
{
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    ULONG                           uIndex         = 0;
#endif
    PCOSA_DML_WIFI_AP_CFG pRunningCfg = NULL;
    int wlanIndex;
wifiDbgPrintf("%s\n",__FUNCTION__);

    if (!pCfg)
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pCfg is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
    wlanIndex = pCfg->InstanceNumber-1;
    if ( (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
        return ANSC_STATUS_FAILURE;
    }
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    wifiDbgPrintf("%s[%d] wlanIndex %d\n",__FUNCTION__, __LINE__, wlanIndex);

    pRunningCfg = &sWiFiDmlApRunningCfg[pCfg->InstanceNumber-1].Cfg ;
#else
    GET_AP_INDEX(sWiFiDmlApRunningCfg, pCfg->InstanceNumber, uIndex);
    pRunningCfg = &sWiFiDmlApRunningCfg[uIndex].Cfg;
#endif

    /* USGv2 Extensions */
    // static value for first GA release not settable
    // pCfg->LongRetryLimit = 16;
    // pCfg->RetryLimit     = 16;

    // These should be pushed when the SSID is up
    //  They are currently set from ApGetCfg when it call GetAccessPointPsmData
    if (pCfg->WMMEnable != pRunningCfg->WMMEnable) {
        wifi_setApWmmEnable(wlanIndex,pCfg->WMMEnable);
    }
    if (pCfg->UAPSDEnable != pRunningCfg->UAPSDEnable) {
        wifi_setApWmmUapsdEnable(wlanIndex, pCfg->UAPSDEnable);        
    }

    if (pCfg->WmmNoAck != pRunningCfg->WmmNoAck) {
        // Ic and Og policies set the same for first GA release
		wifi_setApWmmOgAckPolicy(wlanIndex, 0, !pCfg->WmmNoAck);
		wifi_setApWmmOgAckPolicy(wlanIndex, 1, !pCfg->WmmNoAck);
		wifi_setApWmmOgAckPolicy(wlanIndex, 2, !pCfg->WmmNoAck);
		wifi_setApWmmOgAckPolicy(wlanIndex, 3, !pCfg->WmmNoAck);
    }

    if (pCfg->BssMaxNumSta != pRunningCfg->BssMaxNumSta) {
        // Check to see the current # of associated devices exceeds the new limit
        // if so kick all the devices off
        ULONG devNum;
        wifi_getApNumDevicesAssociated(wlanIndex, &devNum);
        if (devNum > (ULONG)pCfg->BssMaxNumSta) {
            char ssidName[COSA_DML_WIFI_MAX_SSID_NAME_LEN];
            wifi_getApName(wlanIndex, ssidName);
            CosaDmlWiFiApKickAssocDevices(ssidName);
            t2_event_d("WIFI_INFO_Kickoff_All_Clients", 1);
        }
        wifi_setApMaxAssociatedDevices(wlanIndex, pCfg->BssMaxNumSta);
    }

    if (pCfg->IsolationEnable != pRunningCfg->IsolationEnable) {    
		wifi_setApIsolationEnable(wlanIndex, pCfg->IsolationEnable);
    }

    if (pCfg->SSIDAdvertisementEnabled != pRunningCfg->SSIDAdvertisementEnabled) {
        wifi_pushSsidAdvertisementEnable(wlanIndex, pCfg->SSIDAdvertisementEnabled);
        sWiFiDmlUpdatedAdvertisement[wlanIndex] = TRUE;
#if defined(ENABLE_FEATURE_MESHWIFI)
        {
            // notify mesh components that wifi SSID Advertise changed
            CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh of SSID Advertise changes\n",__FUNCTION__));
            v_secure_system("/usr/bin/sysevent set wifi_SSIDAdvertisementEnable 'RDK|%d|%s'", wlanIndex, (pCfg->SSIDAdvertisementEnabled?"true":"false"));
        }
#endif
    }

    // pCfg->MulticastRate = 123;
    // pCfg->BssCountStaAsCpe  = TRUE;

#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    memcpy(&sWiFiDmlApRunningCfg[pCfg->InstanceNumber-1], pCfg, sizeof(COSA_DML_WIFI_AP_CFG));
#else
    memcpy(pRunningCfg, pCfg, sizeof(COSA_DML_WIFI_AP_CFG));
#endif

    return ANSC_STATUS_SUCCESS;
}

/* CosaDmlWiFiApGetStatsEnable() */
ANSC_STATUS CosaDmlWiFiApGetStatsEnable(UINT InstanceNumber, BOOLEAN *pbValue)
{
    char* strValue = NULL;
    char recName[256] = {0};

    sprintf(recName, vAPStatsEnable, InstanceNumber);

    // Initialize the value as FALSE always
    *pbValue = FALSE;
    sWiFiDmlApStatsEnableCfg[InstanceNumber - 1] = FALSE;

    if (!g_wifidb_rfc) {
    if (CCSP_SUCCESS == PSM_Get_Record_Value2(bus_handle,
            g_Subsystem, recName, NULL, &strValue))
    {
        if (0 == strcmp(strValue, "true"))
        {
            *pbValue = TRUE;
            sWiFiDmlApStatsEnableCfg[InstanceNumber - 1] = TRUE;
        }

        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);

        return ANSC_STATUS_SUCCESS;
    }
    } else {
        struct schema_Wifi_VAP_Config  *pcfg= NULL;
        pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[InstanceNumber-1], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
        if (pcfg != NULL) {
            *pbValue = pcfg->vap_stats_enable;
            sWiFiDmlApStatsEnableCfg[InstanceNumber - 1] = pcfg->vap_stats_enable;
            return ANSC_STATUS_SUCCESS;
        }
    }

    return ANSC_STATUS_FAILURE;
}

/* CosaDmlWiFiApSetStatsEnable() */
ANSC_STATUS CosaDmlWiFiApSetStatsEnable(UINT InstanceNumber, BOOLEAN bValue)
{
    char recValue[16] = {0};
    char recName[256] = {0};

    snprintf(recName, sizeof(recName), vAPStatsEnable, InstanceNumber);
    snprintf(recValue, sizeof(recValue), "%s", (bValue ? "true" : "false"));
    if (CCSP_SUCCESS == PSM_Set_Record_Value2(bus_handle,
            g_Subsystem, recName, ccsp_string, recValue))
    {
        sWiFiDmlApStatsEnableCfg[InstanceNumber - 1] = bValue;
        wifi_stats_flag_change(InstanceNumber-1, bValue, 1);

        return ANSC_STATUS_SUCCESS;
    }
    if (g_wifidb_rfc) {
        struct schema_Wifi_VAP_Config  *pcfg= NULL;

        pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[InstanceNumber - 1], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
        if (pcfg != NULL) {
            pcfg->vap_stats_enable = bValue;
            if (wifi_ovsdb_update_table_entry(vap_names[InstanceNumber - 1],"vap_name",OCLM_STR,&table_Wifi_VAP_Config,pcfg,filter_vaps) <= 0) {
                CcspWifiTrace(("RDK_LOG_ERROR,%s: WIFI DB Failed to update vap config\n",__FUNCTION__ ));
                return ANSC_STATUS_FAILURE;
            }
            return ANSC_STATUS_SUCCESS;
        }
    }

    return ANSC_STATUS_FAILURE;
}

/* IsCosaDmlWiFiApStatsEnable() */
BOOLEAN IsCosaDmlWiFiApStatsEnable(UINT uvAPIndex)
{
    return ((sWiFiDmlApStatsEnableCfg[uvAPIndex]) ? TRUE : FALSE);
}
#if defined (FEATURE_SUPPORT_INTERWORKING)
ANSC_STATUS PSM_getInterworkingServiceCapability(INT apIndex, BOOL *output_bool)
{
    //Always true for supported APs
    if ((apIndex == 1) || (apIndex == 2) || (apIndex == 5) ||
        (apIndex == 6) || (apIndex == 9) || (apIndex == 10)) {

        *output_bool = 1;
    } else {
        *output_bool = 0;
    }
    return ANSC_STATUS_SUCCESS;
}

#endif

ANSC_STATUS
CosaDmlWiFiApGetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_CFG       pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    ULONG                           uIndex         = 0;
#endif
    BOOL enabled = FALSE;
    
    if (!pSsid)
    {
        return ANSC_STATUS_FAILURE;
    }
    
wifiDbgPrintf("%s pSsid = %s\n",__FUNCTION__, pSsid);

    if (!pCfg)
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pCfg is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
        
    int wlanIndex = -1;
    int wlanRadioIndex = 0;
    unsigned int RetryLimit=0;

    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
	CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pSsid = %s Couldn't find wlanIndex \n",__FUNCTION__, pSsid));
	t2_event_d("WIFI_ERROR_NoWlanIndex",1);
	// Error could not find index
	return ANSC_STATUS_FAILURE;
    }
    wifi_getApRadioIndex(wlanIndex, &wlanRadioIndex);
    pCfg->InstanceNumber = wlanIndex+1;
#if !defined(_INTEL_BUG_FIXES_)
    sprintf(pCfg->Alias,"AccessPoint%lu", pCfg->InstanceNumber);
#else
    sprintf(pCfg->Alias,"cpe-AccessPoint%lu", pCfg->InstanceNumber);
#endif

    wifi_getApEnable(wlanIndex, &enabled);

    pCfg->bEnabled = (enabled == TRUE) ? TRUE : FALSE;
    
    pCfg->WirelessManagementImplemented = (pCfg->bEnabled == TRUE) ? TRUE : FALSE;
    pCfg->BSSTransitionImplemented = (pCfg->bEnabled == TRUE) ? TRUE : FALSE;
  
    CosaDmlWiFiGetAccessPointPsmData(pCfg);

    /*Get the WMM related values from Hal*/
    wifi_getApWmmEnable(wlanIndex, &enabled);
    pCfg->WMMEnable = (enabled == TRUE) ? TRUE : FALSE;

    wifi_getApWmmUapsdEnable(wlanIndex, &enabled);
    pCfg->UAPSDEnable = (enabled == TRUE) ? TRUE : FALSE;

    wifi_getApWmmOgAckPolicy(wlanIndex, &enabled);
    pCfg->WmmNoAck = (enabled == TRUE)? 1 : 0;

   CcspTraceWarning(("X_RDKCENTRAL-COM_BSSTransitionActivated_Get:<%d>\n", pCfg->BSSTransitionActivated));

    /* USGv2 Extensions */
    // static value for first GA release not settable
    pCfg->LongRetryLimit = 16;
    //pCfg->RetryLimit     = 16;

    wifi_getApRetryLimit(wlanIndex,&RetryLimit);
    pCfg->RetryLimit = RetryLimit;
    sprintf(pCfg->SSID, "Device.WiFi.SSID.%d.", wlanIndex+1);

    wifi_getApSsidAdvertisementEnable(wlanIndex,  &enabled);
    pCfg->SSIDAdvertisementEnabled = (enabled == TRUE) ? TRUE : FALSE;
    wifi_getApManagementFramePowerControl(wlanIndex , &pCfg->ManagementFramePowerControl);
    CcspTraceWarning(("X_RDKCENTRAL-COM_ManagementFramePowerControl_Get:<%d>\n", pCfg->ManagementFramePowerControl));

//>> zqiu
#ifdef _BEACONRATE_SUPPORT
	wifi_getApBeaconRate(wlanIndex, pCfg->BeaconRate);
#endif
#if defined (FEATURE_SUPPORT_INTERWORKING)
    PSM_getInterworkingServiceCapability(wlanIndex+1, &enabled);
    pCfg->InterworkingCapability = (enabled == TRUE) ? TRUE : FALSE;
#endif
    pCfg->MulticastRate = 123;
    pCfg->BssCountStaAsCpe  = TRUE;


    // pCfg->BssUserStatus

#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    memcpy(&sWiFiDmlApStoredCfg[pCfg->InstanceNumber-1].Cfg, pCfg, sizeof(COSA_DML_WIFI_AP_CFG));
    memcpy(&sWiFiDmlApRunningCfg[pCfg->InstanceNumber-1].Cfg, pCfg, sizeof(COSA_DML_WIFI_AP_CFG));
#else
    GET_SSID_INDEX(sWiFiDmlSsidStoredCfg, pCfg->InstanceNumber, uIndex);
    memcpy(&sWiFiDmlApStoredCfg[uIndex].Cfg, pCfg, sizeof(COSA_DML_WIFI_AP_CFG));
    memcpy(&sWiFiDmlApRunningCfg[uIndex].Cfg, pCfg, sizeof(COSA_DML_WIFI_AP_CFG));
#endif
#if defined (FEATURE_SUPPORT_INTERWORKING)
    if ((pCfg->InstanceNumber == 1) || (pCfg->InstanceNumber == 2) || (pCfg->InstanceNumber == 5) ||
        (pCfg->InstanceNumber == 6) || (pCfg->InstanceNumber == 9) || (pCfg->InstanceNumber == 10)) {
        CosaDmlWiFi_getInterworkingElement(pCfg, (ULONG)wlanIndex);

        //Initialize ANQP Parameters
        CosaDmlWiFi_InitANQPConfig(pCfg);
        CosaDmlWiFi_InitHS2Config(pCfg);
    }
#endif
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApGetInfo
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_INFO      pInfo
    )
{
    UNREFERENCED_PARAMETER(hContext);
    int wRet = RETURN_OK;

    if (!pSsid)
    {
        return ANSC_STATUS_FAILURE;
    }

    CcspWifiTrace(("RDK_LOG_INFO,%s pSsid = %s\n",__FUNCTION__, pSsid));

    if (!pInfo)
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pCfg is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    int wlanIndex = 0;
    BOOL enabled = FALSE;
    wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pSsid = %s Couldn't find wlanIndex \n",__FUNCTION__, pSsid));
		t2_event_d("WIFI_ERROR_NoWlanIndex",1);
		// Error could not find index
		return ANSC_STATUS_FAILURE;
    }

    wifi_getApEnable(wlanIndex,&enabled);
    pInfo->Status = (enabled == TRUE) ? COSA_DML_WIFI_AP_STATUS_Enabled : COSA_DML_WIFI_AP_STATUS_Disabled;
    pInfo->BssUserStatus = (enabled == TRUE) ? 1 : 2;
	pInfo->WMMCapability = TRUE;        
	pInfo->UAPSDCapability = TRUE;

    return ANSC_STATUS_SUCCESS;
}


ANSC_STATUS
CosaDmlWiFiApAssociatedDevicesHighWatermarkGetVal 
   (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_CFG       pCfg
    )
{

    int wlanIndex = -1;
    UINT maxDevices=0,highWatermarkThreshold=0,highWatermarkThresholdReached=0,highWatermark=0;

    UNREFERENCED_PARAMETER(hContext);
    
    if (!pSsid)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    CcspWifiTrace(("RDK_LOG_INFO,%s pSsid = %s\n",__FUNCTION__, pSsid));


    if (!pCfg)
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pCfg is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pSsid = %s Couldn't find wlanIndex \n",__FUNCTION__, pSsid));
		t2_event_d("WIFI_ERROR_NoWlanIndex",1);
        // Error could not find index
        return ANSC_STATUS_FAILURE;
    }


    wifi_getApMaxAssociatedDevices(wlanIndex,&maxDevices);
	pCfg->MaxAssociatedDevices = maxDevices;

    wifi_getApAssociatedDevicesHighWatermarkThreshold(wlanIndex,&highWatermarkThreshold);
	pCfg->HighWatermarkThreshold = highWatermarkThreshold;

    wifi_getApAssociatedDevicesHighWatermarkThresholdReached(wlanIndex,&highWatermarkThresholdReached);
	pCfg->HighWatermarkThresholdReached = highWatermarkThresholdReached;

    wifi_getApAssociatedDevicesHighWatermark(wlanIndex,&highWatermark);
	pCfg->HighWatermark = highWatermark;

#if !defined(_XB7_PRODUCT_REQ_) && !defined(_PLATFORM_TURRIS_) && !defined(_HUB4_PRODUCT_REQ_)
    wifi_VAPTelemetry_t telemetry;
    wifi_getVAPTelemetry(wlanIndex, &telemetry);
    pCfg->TXOverflow = (ULONG)telemetry.txOverflow;
#endif

    return ANSC_STATUS_SUCCESS;
}
//>>zqiu
/*
ANSC_STATUS
CosaDmlGetHighWatermarkDate
    (
       ANSC_HANDLE                 hContext,
        char*                       pSsid,
       char                       *pDate
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    int wlanIndex = -1,ret;
    struct tm  ts;
    wifiDbgPrintf("%s pSsid = %s\n",__FUNCTION__, pSsid);
    char buf[80];
    ULONG dateInSecs=0;

    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pSsid = %s Couldn't find wlanIndex \n",__FUNCTION__, pSsid));
        // Error could not find index
        return ANSC_STATUS_FAILURE;
    }

    ret=wifi_getApAssociatedDevicesHighWatermarkDate(wlanIndex,&dateInSecs);
    if ( ret != 0 )
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : fail to getApAssociatedDevicesHighWatermarkDate \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    ts = *localtime(&dateInSecs);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ts);
    pDate=buf;
    return ANSC_STATUS_SUCCESS;
}
*/
ANSC_STATUS
CosaDmlGetApRadiusSettings
   (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_RadiusSetting       pRadiusSetting
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    int wlanIndex = -1;
    int result; 
    wifi_radius_setting_t radSettings;
    UNREFERENCED_PARAMETER(hContext);
 
    if (!pSsid)
    {
        return ANSC_STATUS_FAILURE;
    }
     
    wifiDbgPrintf("%s pSsid = %s\n",__FUNCTION__, pSsid);

    if (!pRadiusSetting)
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pRadiusSetting is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
    memset(pRadiusSetting,0,sizeof(COSA_DML_WIFI_RadiusSetting));

    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pSsid = %s Couldn't find wlanIndex \n",__FUNCTION__, pSsid));
		t2_event_d("WIFI_ERROR_NoWlanIndex",1);
        // Error could not find index
        return ANSC_STATUS_FAILURE;
    }
    result = wifi_getApSecurityRadiusSettings(wlanIndex, &radSettings);
    if (result == 0) {
        pRadiusSetting->iRadiusServerRetries                      	   = radSettings.RadiusServerRetries;
        pRadiusSetting->iRadiusServerRequestTimeout                	   = radSettings.RadiusServerRequestTimeout;
        pRadiusSetting->iPMKLifetime	                    		   = radSettings.PMKLifetime;
        pRadiusSetting->bPMKCaching			                   = radSettings.PMKCaching;
        pRadiusSetting->iPMKCacheInterval		                   = radSettings.PMKCacheInterval;
        pRadiusSetting->iMaxAuthenticationAttempts                         = radSettings.MaxAuthenticationAttempts;
        pRadiusSetting->iBlacklistTableTimeout                             = radSettings.BlacklistTableTimeout;
        pRadiusSetting->iIdentityRequestRetryInterval                      = radSettings.IdentityRequestRetryInterval;
        pRadiusSetting->iQuietPeriodAfterFailedAuthentication              = radSettings.QuietPeriodAfterFailedAuthentication;
    }
	return returnStatus;
}

ANSC_STATUS
CosaDmlSetApRadiusSettings
   (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_RadiusSetting       pRadiusSetting
    )
{
    int wlanIndex = -1;
    int result;
    wifi_radius_setting_t radSettings;
    UNREFERENCED_PARAMETER(hContext);
    
    if (!pSsid)
    {
        return ANSC_STATUS_FAILURE;
    }
     
    wifiDbgPrintf("%s pSsid = %s\n",__FUNCTION__, pSsid);

    if (!pRadiusSetting)
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pRadiusSetting is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
    memset(&radSettings,0,sizeof(wifi_radius_setting_t));

    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pSsid = %s Couldn't find wlanIndex \n",__FUNCTION__, pSsid));
		t2_event_d("WIFI_ERROR_NoWlanIndex",1);
        // Error could not find index
        return ANSC_STATUS_FAILURE;
    }

	radSettings.RadiusServerRetries  	=	pRadiusSetting->iRadiusServerRetries;
	radSettings.RadiusServerRequestTimeout  = 	pRadiusSetting->iRadiusServerRequestTimeout;
	radSettings.PMKLifetime			=	pRadiusSetting->iPMKLifetime;
	radSettings.PMKCaching			=	pRadiusSetting->bPMKCaching;
	radSettings.PMKCacheInterval		=	pRadiusSetting->iPMKCacheInterval;
	radSettings.MaxAuthenticationAttempts	=	pRadiusSetting->iMaxAuthenticationAttempts;
	radSettings.BlacklistTableTimeout	=	pRadiusSetting->iBlacklistTableTimeout;
	radSettings.IdentityRequestRetryInterval=	pRadiusSetting->iIdentityRequestRetryInterval;
	radSettings.QuietPeriodAfterFailedAuthentication=pRadiusSetting->iQuietPeriodAfterFailedAuthentication;
	result = wifi_setApSecurityRadiusSettings(wlanIndex, &radSettings);
    if (result != 0) {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : fail to  setApSecurityRadiusSettings \n",__FUNCTION__));
		return ANSC_STATUS_FAILURE;	  
	}
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApSecGetEntry
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APSEC_FULL   pEntry
    )
{
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    ULONG                           uIndex         = 0;
#endif
    int wlanIndex = -1;

    if (!pSsid)
    {
        return ANSC_STATUS_FAILURE;
    }

wifiDbgPrintf("%s pSsid = %s\n",__FUNCTION__, pSsid);

    if (!pEntry)
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pEntry is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pSsid = %s Couldn't find wlanIndex \n",__FUNCTION__, pSsid));
		t2_event_d("WIFI_ERROR_NoWlanIndex",1);
		// Error could not find index
		return ANSC_STATUS_FAILURE;
    }
#if defined(_XB6_PRODUCT_REQ_)
    pEntry->Info.ModesSupported = COSA_DML_WIFI_SECURITY_None | 
				  COSA_DML_WIFI_SECURITY_WPA2_Personal | 
				  COSA_DML_WIFI_SECURITY_WPA2_Enterprise |
				  COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise;
#elif defined(_HUB4_PRODUCT_REQ_)
    pEntry->Info.ModesSupported = COSA_DML_WIFI_SECURITY_None |
				  COSA_DML_WIFI_SECURITY_WPA2_Personal |
				  COSA_DML_WIFI_SECURITY_WPA_WPA2_Personal;
;
#else
    pEntry->Info.ModesSupported = COSA_DML_WIFI_SECURITY_None | COSA_DML_WIFI_SECURITY_WEP_64 | COSA_DML_WIFI_SECURITY_WEP_128 | 
				  //COSA_DML_WIFI_SECURITY_WPA_Personal | 
				  COSA_DML_WIFI_SECURITY_WPA2_Personal | 
				  COSA_DML_WIFI_SECURITY_WPA_WPA2_Personal | 
				  //COSA_DML_WIFI_SECURITY_WPA_Enterprise |
				  COSA_DML_WIFI_SECURITY_WPA2_Enterprise | COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise;
#endif
    CosaDmlWiFiApSecGetCfg((ANSC_HANDLE)hContext, pSsid, &pEntry->Cfg);

#ifdef CISCO_XB3_PLATFORM_CHANGES
    if (pEntry->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_64)
    {
	CosaDmlWiFi_GetWEPKey64ByIndex(wlanIndex+1, 0, &pEntry->WEPKey64Bit[0]);
	CosaDmlWiFi_GetWEPKey64ByIndex(wlanIndex+1, 1, &pEntry->WEPKey64Bit[1]);
	CosaDmlWiFi_GetWEPKey64ByIndex(wlanIndex+1, 2, &pEntry->WEPKey64Bit[2]);
	CosaDmlWiFi_GetWEPKey64ByIndex(wlanIndex+1, 3, &pEntry->WEPKey64Bit[3]);
    } else if (pEntry->Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_128)
    {
	CosaDmlWiFi_GetWEPKey128ByIndex(wlanIndex+1, 0, &pEntry->WEPKey128Bit[0]);
	CosaDmlWiFi_GetWEPKey128ByIndex(wlanIndex+1, 1, &pEntry->WEPKey128Bit[1]);
	CosaDmlWiFi_GetWEPKey128ByIndex(wlanIndex+1, 2, &pEntry->WEPKey128Bit[2]);
	CosaDmlWiFi_GetWEPKey128ByIndex(wlanIndex+1, 3, &pEntry->WEPKey128Bit[3]);
    }
#endif

#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    memcpy(&sWiFiDmlApSecurityStored[wlanIndex], pEntry, sizeof(COSA_DML_WIFI_APSEC_FULL));
    memcpy(&sWiFiDmlApSecurityRunning[wlanIndex], pEntry, sizeof(COSA_DML_WIFI_APSEC_FULL));
#else
    GET_AP_INDEX(sWiFiDmlApStoredCfg, wlanIndex+1, uIndex);
    memcpy(&sWiFiDmlApSecurityStored[uIndex], pEntry, sizeof(COSA_DML_WIFI_APSEC_FULL));
    memcpy(&sWiFiDmlApSecurityRunning[uIndex], pEntry, sizeof(COSA_DML_WIFI_APSEC_FULL));
#endif

    return ANSC_STATUS_SUCCESS;
}


ANSC_STATUS CosaDmlWiFiApSecLoadKeyPassphrase(ULONG instanceNumber, PCOSA_DML_WIFI_APSEC_CFG pCfg) {
    if(!g_newXH5Gpass)
	 return ANSC_STATUS_SUCCESS;
    g_newXH5Gpass=FALSE;
    wifi_getApSecurityPreSharedKey(instanceNumber-1, (char*)pCfg->PreSharedKey);
    wifi_getApSecurityKeyPassphrase(instanceNumber-1, (char*)pCfg->KeyPassphrase);
    return ANSC_STATUS_SUCCESS;
}
#if defined (MULTILAN_FEATURE)
/* Description:
 *      The API adds a new WiFi Access Point into the system.
 * Arguments:
 *      hContext        reserved.
 *      pEntry          Pointer of the service to be added.
 */
ANSC_STATUS
CosaDmlWiFiApAddEntry
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_FULL      pEntry
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pSsid);
    PCOSA_DML_WIFI_AP_CFG           pCfg           = &pEntry->Cfg;
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    ULONG                           uIndex         = 0;
#endif

    wifiDbgPrintf("%s\n",__FUNCTION__);

    if(!pEntry)
    {
        CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pEntry is NULL \n",__FUNCTION__));

        return ANSC_STATUS_FAILURE;
    }

    if (gSsidCount > WIFI_INDEX_MAX)
    {
        CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : Max AP count reached \n",__FUNCTION__));

        return ANSC_STATUS_FAILURE;
    }

    CosaDmlWiFiGetAccessPointPsmData(pCfg);

#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    AnscCopyMemory(&sWiFiDmlApStoredCfg[pCfg->InstanceNumber-1], pEntry, sizeof(COSA_DML_WIFI_AP_FULL));
    AnscCopyMemory(&sWiFiDmlApRunningCfg[pCfg->InstanceNumber-1], pEntry, sizeof(COSA_DML_WIFI_AP_FULL));
#else
    GET_SSID_INDEX(sWiFiDmlSsidStoredCfg, pCfg->InstanceNumber, uIndex);
    AnscCopyMemory(&sWiFiDmlApStoredCfg[uIndex], pEntry, sizeof(COSA_DML_WIFI_AP_FULL));
    AnscCopyMemory(&sWiFiDmlApRunningCfg[uIndex], pEntry, sizeof(COSA_DML_WIFI_AP_FULL));
#endif

    return ANSC_STATUS_SUCCESS;
}

#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
ANSC_STATUS
CosaDmlWiFiApDelEntry
    (
        ANSC_HANDLE                 hContext,
        ULONG                       ulInstanceNumber
    )
{
    ULONG                           uIndex       = 0;
    UNREFERENCED_PARAMETER(hContext);
    wifiDbgPrintf("%s\n",__FUNCTION__);

    GET_AP_INDEX(sWiFiDmlApStoredCfg, ulInstanceNumber, uIndex);

    for (; uIndex < (ULONG)(gSsidCount-1); uIndex++)
    {
        sWiFiDmlApStoredCfg[uIndex] = sWiFiDmlApStoredCfg[uIndex+1];
        sWiFiDmlApRunningCfg[uIndex] = sWiFiDmlApRunningCfg[uIndex+1];
    }

    AnscZeroMemory(&sWiFiDmlApStoredCfg[uIndex], sizeof(COSA_DML_WIFI_AP_FULL));
    AnscZeroMemory(&sWiFiDmlApRunningCfg[uIndex], sizeof(COSA_DML_WIFI_AP_FULL));

    return ANSC_STATUS_SUCCESS;
}
#endif // DMCLI_SUPPORT_TO_ADD_DELETE_VAP
#endif // MULTILAN_FEATURE

ANSC_STATUS
CosaDmlWiFiApSecGetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APSEC_CFG    pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
	int wlanIndex = -1;
#if !defined(_INTEL_BUG_FIXES_)
    char securityType[32];
    char authMode[32];
#else
    char securityType[128];
    char authMode[128];
#endif
    int retVal = 0;

    if (!pSsid)
    {
        return ANSC_STATUS_FAILURE;
    }

wifiDbgPrintf("%s pSsid = %s\n",__FUNCTION__, pSsid);

    if (!pCfg)
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pEntry is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
    
    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pSsid = %s Couldn't find wlanIndex \n",__FUNCTION__, pSsid));
		t2_event_d("WIFI_ERROR_NoWlanIndex",1);
		// Error could not find index
		return ANSC_STATUS_FAILURE;
    }

    wifi_getApBeaconType(wlanIndex, securityType);
    retVal = wifi_getApBasicAuthenticationMode(wlanIndex, authMode);
    wifiDbgPrintf("wifi_getApBasicAuthenticationMode wanIndex = %d return code = %d for auth mode = %s\n",wlanIndex,retVal,authMode);

#ifndef _XB6_PRODUCT_REQ_
    if (strncmp(securityType,"None", strlen("None")) == 0)
    {
		pCfg->ModeEnabled =  COSA_DML_WIFI_SECURITY_None; 
    } else if (strncmp(securityType,"Basic", strlen("Basic")) == 0 )   { 
		ULONG wepLen;
		char *strValue = NULL;
		char recName[256];
		int retPsmGet = CCSP_SUCCESS;

                if (!g_wifidb_rfc) {
		memset(recName, 0, sizeof(recName));
		snprintf(recName, sizeof(recName), WepKeyLength, wlanIndex+1);
		retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
		if (retPsmGet == CCSP_SUCCESS) {
			wepLen = _ansc_atoi(strValue);
                        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
		} else {
				// Default to 128
				wepLen = 128;
			PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "128" );
			}
                } else {
                    struct schema_Wifi_VAP_Config  *pcfg= NULL;
                    pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[wlanIndex], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
                    if (pcfg != NULL) {
                        wepLen = pcfg->wep_key_length;
                    } else {
                        wepLen = 128;
                    }
                }
		if (wepLen == 64)
		{ 
			pCfg->ModeEnabled = COSA_DML_WIFI_SECURITY_WEP_64; 
		} else if (wepLen == 128)
		{
			pCfg->ModeEnabled = COSA_DML_WIFI_SECURITY_WEP_128;
		}

    } else if (strncmp(securityType,"WPAand11i", strlen("WPAand11i")) == 0)
    {
        if(strncmp(authMode,"EAPAuthentication", strlen("EAPAuthentication")) == 0)
        {
            pCfg->ModeEnabled = COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise;
        }
        else
        {
	    pCfg->ModeEnabled = COSA_DML_WIFI_SECURITY_WPA_WPA2_Personal;
        }
    } else if (strncmp(securityType,"WPA", strlen("WPA")) == 0)
    {
        if(strncmp(authMode,"EAPAuthentication", strlen("EAPAuthentication")) == 0)
        {
            pCfg->ModeEnabled = COSA_DML_WIFI_SECURITY_WPA_Enterprise;
        }
        else
        {
	    pCfg->ModeEnabled = COSA_DML_WIFI_SECURITY_WPA_Personal;
        }
    } else if (strncmp(securityType,"11i", strlen("11i")) == 0)
    {
        if(strncmp(authMode,"EAPAuthentication", strlen("EAPAuthentication")) == 0)
        {
            pCfg->ModeEnabled = COSA_DML_WIFI_SECURITY_WPA2_Enterprise;
        }
        else
        {
	    pCfg->ModeEnabled = COSA_DML_WIFI_SECURITY_WPA2_Personal;
        }
    } else
    { 
	pCfg->ModeEnabled =  COSA_DML_WIFI_SECURITY_None; 
    }
#else
	if (strncmp(securityType,"None", strlen("None")) == 0)
    {
		pCfg->ModeEnabled =  COSA_DML_WIFI_SECURITY_None; 
    } else if (strncmp(securityType,"11i", strlen("11i")) == 0)
    {
        if(strncmp(authMode,"EAPAuthentication", strlen("EAPAuthentication")) == 0)
        {
            pCfg->ModeEnabled = COSA_DML_WIFI_SECURITY_WPA2_Enterprise;
        }
        else
        {
	    pCfg->ModeEnabled = COSA_DML_WIFI_SECURITY_WPA2_Personal;
        }
    }
    else if (strncmp(securityType,"WPAand11i", strlen("WPAand11i")) == 0)
    {
        if(strncmp(authMode,"EAPAuthentication", strlen("EAPAuthentication")) == 0)
        {
            pCfg->ModeEnabled = COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise;
        }
        else
        {
            pCfg->ModeEnabled = COSA_DML_WIFI_SECURITY_WPA_WPA2_Personal;
        }
    } else
    { 
	pCfg->ModeEnabled =  COSA_DML_WIFI_SECURITY_None; 
    }
#endif
    //>>Deprecated
    //wifi_getApWepKeyIndex(wlanIndex, (unsigned int *) &pCfg->DefaultKey);
	//<<
#ifdef CISCO_XB3_PLATFORM_CHANGES
    wifi_getApWepKeyIndex(wlanIndex, (ULONG *) &pCfg->DefaultKey);
#endif
    wifi_getApSecurityPreSharedKey(wlanIndex, (char*)pCfg->PreSharedKey);
    wifi_getApSecurityKeyPassphrase(wlanIndex, (char*)pCfg->KeyPassphrase);

    { 
#if !defined(_INTEL_BUG_FIXES_)
	char method[32];
#else
        char method[128];
#endif

	wifi_getApWpaEncryptionMode(wlanIndex, method);

	if (strncmp(method, "TKIPEncryption",strlen("TKIPEncryption")) == 0)
	{ 
	    pCfg->EncryptionMethod = COSA_DML_WIFI_AP_SEC_TKIP;
	} else if (strncmp(method, "AESEncryption",strlen("AESEncryption")) == 0)
	{
	    pCfg->EncryptionMethod = COSA_DML_WIFI_AP_SEC_AES;
	} 
	else if (strncmp(method, "TKIPandAESEncryption",strlen("TKIPandAESEncryption")) == 0)
	{
#ifndef _XB6_PRODUCT_REQ_
	    pCfg->EncryptionMethod = COSA_DML_WIFI_AP_SEC_AES_TKIP;
#else
    if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise)
    {
	    pCfg->EncryptionMethod = COSA_DML_WIFI_AP_SEC_AES_TKIP;
    }
#endif
	}
    }
    CosaDmlWiFi_GetApMFPConfigValue(wlanIndex, pCfg->MFPConfig);
    wifi_getApSecurityRadiusServer(wlanIndex, (char*)pCfg->RadiusServerIPAddr, (UINT *)&pCfg->RadiusServerPort, pCfg->RadiusSecret);
    wifi_getApSecuritySecondaryRadiusServer(wlanIndex, (char*)pCfg->SecondaryRadiusServerIPAddr, (UINT *)&pCfg->SecondaryRadiusServerPort, pCfg->SecondaryRadiusSecret);
    if (wlanIndex < 6)   //For VAPs 1-6
    {
        getDefaultPassphase(wlanIndex, (char*)pCfg->DefaultKeyPassphrase);
    }
 //wifi_getApSecurityRadiusServerIPAddr(wlanIndex,&pCfg->RadiusServerIPAddr); //bug
    //wifi_getApSecurityRadiusServerPort(wlanIndex, &pCfg->RadiusServerPort);
#if !defined (_COSA_BCM_MIPS_)&& !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_) && !defined(_INTEL_WAV_)
    wifi_getApSecurityWpaRekeyInterval(wlanIndex,  (unsigned int *) &pCfg->RekeyingInterval);
#endif 
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
    wifi_getApDASRadiusServer(wlanIndex, (char*)pCfg->RadiusDASIPAddr, (UINT *)&pCfg->RadiusDASPort, (char *)pCfg->RadiusDASSecret);
#endif
    //zqiu: TODO: set pCfg->RadiusReAuthInterval;
#ifdef DUAL_CORE_XB3
    wifi_eap_config_t eapcfg;
    if ((retVal = wifi_getEAP_Param(wlanIndex, &eapcfg)) == RETURN_OK) {
           pCfg->uiEAPOLKeyTimeout =  eapcfg.uiEAPOLKeyTimeout;
           pCfg->uiEAPOLKeyRetries =  eapcfg.uiEAPOLKeyRetries;
           pCfg->uiEAPIdentityRequestTimeout =  eapcfg.uiEAPIdentityRequestTimeout;
           pCfg->uiEAPIdentityRequestRetries =  eapcfg.uiEAPIdentityRequestRetries;
           pCfg->uiEAPRequestTimeout =  eapcfg.uiEAPRequestTimeout;
           pCfg->uiEAPRequestRetries =  eapcfg.uiEAPRequestRetries;
           CcspWifiTrace(("RDK_LOG_INFO,wifi_getEAP_Param success %d %d %d %d %d %d\n", pCfg->uiEAPOLKeyTimeout, pCfg->uiEAPOLKeyRetries, pCfg->uiEAPIdentityRequestTimeout, pCfg->uiEAPIdentityRequestRetries, pCfg->uiEAPRequestTimeout, pCfg->uiEAPRequestRetries));
    }
#endif

    return ANSC_STATUS_SUCCESS;
}

/* CosaDmlWiFiApIsSecmodeOpenForPrivateAP() */
BOOL CosaDmlWiFiApIsSecmodeOpenForPrivateAP( void )
{
	//Check whether security mode having open for private AP or not
	if (( sWiFiDmlApSecurityStored[0].Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_None )|| \
		( sWiFiDmlApSecurityStored[1].Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_None )
	    )
	{
          return TRUE;
	}
  return FALSE;
}

ANSC_STATUS
CosaDmlWiFiApSecSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APSEC_CFG    pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    ULONG                           uIndex         = 0;
#endif
    PCOSA_DML_WIFI_APSEC_CFG pStoredCfg = NULL;
    BOOLEAN bForceDisableFlag = FALSE;
    BOOL bsEnabled = FALSE;
wifiDbgPrintf("%s\n",__FUNCTION__);

    int wlanIndex = -1;
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
    int i = 0;
#endif

#if !defined(_INTEL_BUG_FIXES_)
    char securityType[32];
    char authMode[32];
#else
    char securityType[128];
    char authMode[128];
#endif
    BOOL bEnabled;
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
    BOOL bRadiusEnabled = FALSE;
#endif
    int rc = -1;
    if (!pCfg || !pSsid)
    {
        return ANSC_STATUS_FAILURE;
    }
    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex <0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
	// Error could not find index
	return ANSC_STATUS_FAILURE;
    }
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    pStoredCfg = &sWiFiDmlApSecurityStored[wlanIndex].Cfg;
#else
    GET_AP_INDEX(sWiFiDmlApStoredCfg, wlanIndex+1, uIndex);
    pStoredCfg = &sWiFiDmlApSecurityStored[uIndex].Cfg;
#endif

    if (pCfg->ModeEnabled != pStoredCfg->ModeEnabled) {
		
#ifndef _XB6_PRODUCT_REQ_
        if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_None) 
       {
           rc = strcpy_s(securityType,sizeof(securityType),"None");
	   if (rc != 0) {
               ERR_CHK(rc);
               return ANSC_STATUS_FAILURE;
	   }
           rc = strcpy_s(authMode,sizeof(authMode),"None");
	   if (rc != 0) {
               ERR_CHK(rc);
               return ANSC_STATUS_FAILURE;
	   }

            CcspWifiEventTrace(("RDK_LOG_NOTICE, Wifi security mode None is Enabled\n"));
            CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : Wifi security mode None is Enabled\n"));
        } 
		//>>Deprecated
		/*
		else if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_64 ||
                   pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_128 ) 
        {
            ULONG wepLen;
            char *strValue = NULL;
            char recName[256];
            int retPsmSet = CCSP_SUCCESS;

            strcpy(securityType,"Basic");
            strcpy(authMode,"None");
			CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CONFIG_CHANGED : %s setBasicEncryptionMode for %s \n",__FUNCTION__,pSsid));
            wifi_setApBasicEncryptionMode(wlanIndex, "WEPEncryption");

            memset(recName, 0, sizeof(recName));
            sprintf(recName, WepKeyLength, wlanIndex+1);
            if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_64) 
           {
                PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "64" );
                CcspWifiEventTrace(("RDK_LOG_NOTICE, Wifi security mode WEP-64 is Enabled\n"));
                CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CONFIG_CHANGED : Wifi security mode WEP-64 is Enabled\n"));
            } else if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_128)
            {
                PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "128" );
                CcspWifiEventTrace(("RDK_LOG_NOTICE, Wifi security mode WEP-128 is Enabled\n"));
                CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : Wifi security mode WEP-128 is Enabled\n"));
            }

        }
		*/
		else if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_WPA2_Personal) 
        {
           rc = strcpy_s(securityType,sizeof(securityType),"WPAand11i");
	   if (rc != 0) {
               ERR_CHK(rc);
               return ANSC_STATUS_FAILURE;
	   }
           rc = strcpy_s(authMode,sizeof(authMode),"PSKAuthentication");
	   if (rc != 0) {
               ERR_CHK(rc);
               return ANSC_STATUS_FAILURE;
	   }
            CcspWifiEventTrace(("RDK_LOG_NOTICE, Wifi security mode WPA-WPA2-Personal is Enabled\n"));
            CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CONFIG_CHANGED : Wifi security mode WPA-WPA2-Personal is Enabled\n"));
        } else if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_Personal) 
        {
           rc = strcpy_s(securityType,sizeof(securityType),"WPA");
	   if (rc != 0) {
               ERR_CHK(rc);
               return ANSC_STATUS_FAILURE;
	   }
           rc = strcpy_s(authMode,sizeof(authMode),"PSKAuthentication");
	   if (rc != 0) {
               ERR_CHK(rc);
               return ANSC_STATUS_FAILURE;
	   }
            CcspWifiEventTrace(("RDK_LOG_NOTICE, Wifi security mode WPA-Personal is Enabled\n"));
            CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : Wifi security mode WPA-Personal is Enabled\n"));
        } else if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Personal) 
        {
            rc = strcpy_s(securityType,sizeof(securityType),"11i");
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
	   }
            rc = strcpy_s(authMode,sizeof(authMode),"PSKAuthentication");
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
  	    }
            CcspWifiEventTrace(("RDK_LOG_NOTICE, Wifi security mode WPA2-Personal is Enabled\n"));
            CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : Wifi security mode WPA2-Personal is Enabled\n"));
		} else if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Enterprise) 
        {
			//zqiu: Add radius support
            rc = strcpy_s(securityType,sizeof(securityType),"11i");
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
	    }
            rc = strcpy_s(authMode,sizeof(authMode),"EAPAuthentication");
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
	    }
            CcspWifiEventTrace(("RDK_LOG_NOTICE, Wifi security mode WPA2_Enterprise is Enabled\n"));
            CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : Wifi security mode WPA2_Enterprise is Enabled\n"));
		} else if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise) 
        {
			//zqiu: Add Radius support
            rc = strcpy_s(securityType,sizeof(securityType),"WPAand11i");
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
	    }
            rc = strcpy_s(authMode,sizeof(authMode),"EAPAuthentication");
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
	    }
            CcspWifiEventTrace(("RDK_LOG_NOTICE, Wifi security mode WPA_WPA2_Enterprise is Enabled\n"));
            CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : Wifi security mode WPA_WPA2_Enterprise is Enabled\n"));
        } else
        {
            rc = strcpy_s(securityType,sizeof(securityType),"None");
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
	    }
            rc = strcpy_s(authMode,sizeof(authMode),"None");
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
	    }
            CcspWifiEventTrace(("RDK_LOG_NOTICE, Wifi security mode None is Enabled\n"));
            CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : Wifi security mode None is Enabled\n"));
        }
#else
		if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_None) 
       {
            rc = strcpy_s(securityType,sizeof(securityType),"None");
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
	    }
            rc = strcpy_s(authMode,sizeof(authMode),"None");
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
            }
            CcspWifiEventTrace(("RDK_LOG_NOTICE, Wifi security mode None is Enabled\n"));
            CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : Wifi security mode None is Enabled\n"));
        } else if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Personal) 
        {
            rc = strcpy_s(securityType,sizeof(securityType),"11i");
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
	    }
            rc = strcpy_s(authMode,sizeof(authMode),"PSKAuthentication");
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
	   }
            CcspWifiEventTrace(("RDK_LOG_NOTICE, Wifi security mode WPA2-Personal is Enabled\n"));
            CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : Wifi security mode WPA2-Personal is Enabled\n"));
	} else if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Enterprise) 
        {
			//zqiu: Add radius support
            rc = strcpy_s(securityType,sizeof(securityType),"11i");
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
	    }
            rc = strcpy_s(authMode,sizeof(authMode),"EAPAuthentication");
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
	    }
            CcspWifiEventTrace(("RDK_LOG_NOTICE, Wifi security mode WPA2_Enterprise is Enabled\n"));
            CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : Wifi security mode WPA2_Enterprise is Enabled\n"));
		} else
        {
            rc = strcpy_s(securityType,sizeof(securityType),"None");
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
	    }
            rc = strcpy_s(authMode,sizeof(authMode),"None");
	    if (rc != 0) {
                ERR_CHK(rc);
                return ANSC_STATUS_FAILURE;
	    }
            CcspWifiEventTrace(("RDK_LOG_NOTICE, Wifi security mode None is Enabled\n"));
            CcspWifiTrace(("RDK_LOG_WARN,RDKB_WIFI_CONFIG_CHANGED : Wifi security mode None is Enabled\n"));
        }
#endif
		if( ( 0 == wlanIndex ) || \
		    ( 1 == wlanIndex )
		   )
		{
			if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_None)
			{
		          wifi_setApWpsEnable(0, FALSE);
		          wifi_setApWpsEnable(1, FALSE);
		          sWiFiDmlApWpsStored[0].Cfg.bEnabled = FALSE;
			  sWiFiDmlApWpsStored[1].Cfg.bEnabled = FALSE;
			}
		}
        wifi_setApBeaconType(wlanIndex, securityType);
		CcspWifiTrace(("RDK_LOG_WARN,%s calling setBasicAuthenticationMode ssid : %s authmode : %s \n",__FUNCTION__,pSsid,authMode));
        wifi_setApBasicAuthenticationMode(wlanIndex, authMode);
#if defined(ENABLE_FEATURE_MESHWIFI)
        // Notify Mesh components of an AP config change
        MeshNotifySecurityChange(wlanIndex, pCfg);
#endif
#ifdef _LG_MV1_CELENO_
        wifi_getBandSteeringEnable_perSSID(wlanIndex/2,&bsEnabled);
        if(bsEnabled)
            enable_reset_both_radio_flag();
        else
            enable_reset_radio_flag(wlanIndex);
#endif
    }
	//>>Deprecated
    /*
    if (pCfg->DefaultKey != pStoredCfg->DefaultKey) {
		CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CONFIG_CHANGED : %s calling setApWepKeyIndex Index : %d DefaultKey  : %s \n",__FUNCTION__,wlanIndex,pCfg->DefaultKey));
        wifi_setApWepKeyIndex(wlanIndex, pCfg->DefaultKey);
    }
	*/
	//<<
    if (strcmp((char*)pCfg->PreSharedKey, (char*)pStoredCfg->PreSharedKey) != 0) {
        if (strlen((char*)pCfg->PreSharedKey) > 0) { 
		CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CONFIG_CHANGED : %s preshared key changed for index = %d   \n",__FUNCTION__,wlanIndex));
                if(wlanIndex == 0 || wlanIndex  == 1)

                {
                    int pair_index = (wlanIndex == 0)?1:0;
                    char pairSSIDkey[65] = {0};
                    int cmp=0;
                                        
                    if(wifi_getApSecurityPreSharedKey(pair_index,pairSSIDkey)==-1 || pairSSIDkey[0]==0) {
                    	wifi_getApSecurityKeyPassphrase(pair_index,pairSSIDkey);  //for xb6 case
                        cmp=strcmp((char*)pCfg->KeyPassphrase,pairSSIDkey);
                    } else {
                    	cmp=strcmp((char*)pCfg->PreSharedKey,pairSSIDkey);
                    }
                    
                    if( cmp != 0)
                    {
                        CcspWifiTrace(("RDK_LOG_WARN, Different passwords were configured on User Private SSID for 2.4 and 5 GHz radios.\n"));
                        t2_event_d("WIFI_INFO_PwdDiff", 1);
                    }
                    else
                    {
                        CcspWifiTrace(("RDK_LOG_WARN, Same password was configured on User Private SSID for 2.4 and 5 GHz radios.\n"));
                        t2_event_d("WIFI_INFO_PwdSame", 1);
                    }
                }

        wifi_setApSecurityPreSharedKey(wlanIndex, (char*)pCfg->PreSharedKey);
        }
    }

    if (strcmp((char*)pCfg->KeyPassphrase, (char*)pStoredCfg->KeyPassphrase) != 0) {
        if (strlen((char*)pCfg->KeyPassphrase) > 0) { 
		CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CONFIG_CHANGED : %s KeyPassphrase changed for index = %d   \n",__FUNCTION__,wlanIndex));
        /* when ForceDisableWiFiRadio feature is enabled all the radios are in disabled state.
           Hence we can't modify any radio or AP related params. Hence added a check to validate
           whether ForceDisableWiFiRadio feature is enabled or not.
         */
        if(ANSC_STATUS_FAILURE == CosaDmlWiFiGetCurrForceDisableWiFiRadio(&bForceDisableFlag))
        {
            CcspWifiTrace(("RDK_LOG_WARN, %s Failed to fetch ForceDisableWiFiRadio flag!!!\n",__FUNCTION__));
        }
        if(bForceDisableFlag == FALSE) {
             wifi_setApSecurityKeyPassphrase(wlanIndex, (char*)pCfg->KeyPassphrase);
             CcspWifiEventTrace(("RDK_LOG_NOTICE, KeyPassphrase changed \n "));
             CcspWifiTrace(("RDK_LOG_WARN, KeyPassphrase changed \n "));
#if defined(ENABLE_FEATURE_MESHWIFI)
             // Notify Mesh components of an AP config change
             MeshNotifySecurityChange(wlanIndex, pCfg);
#endif
#ifdef _LG_MV1_CELENO_
             wifi_getBandSteeringEnable_perSSID(wlanIndex/2,&bsEnabled);
             if(bsEnabled)
                 enable_reset_both_radio_flag();
             else
                 enable_reset_radio_flag(wlanIndex);
#endif
        } else {
             CcspWifiTrace(("RDK_LOG_WARN, WIFI_ATTEMPT_TO_CHANGE_CONFIG_WHEN_FORCE_DISABLED \n"));
        }
	CosaDmlWiFi_GetPreferPrivateData(&bEnabled);
	if (bEnabled == TRUE)
	{
		if(wlanIndex==0 || wlanIndex==1)
		{
			Delete_Hotspot_MacFilt_Entries();
		}
	}
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
	CosaDmlWiFiGetEnableRadiusGreylist(&bRadiusEnabled);
        if (bRadiusEnabled == TRUE)
        {
            if(wlanIndex==0 || wlanIndex==1)
            {
                for(i=0 ; i<HOTSPOT_NO_OF_INDEX ; i++)
                {
                    wifi_delApAclDevices(Hotspot_Index[i] - 1);
                }
           }
        }
#endif
    }
    }

    // WPA
    if ( pCfg->EncryptionMethod != pStoredCfg->EncryptionMethod &&
         pCfg->ModeEnabled >= COSA_DML_WIFI_SECURITY_WPA_Personal &&
         pCfg->ModeEnabled <= COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise )
    { 
#if !defined(_INTEL_BUG_FIXES_)
	char method[32];
#else
        char method[128];
#endif

        memset(method,'\0',sizeof(method));
	if ( pCfg->EncryptionMethod == COSA_DML_WIFI_AP_SEC_TKIP)
	{
            rc = strcpy_s(method,sizeof(method),"TKIPEncryption");
	    if ( rc != 0) {
                ERR_CHK(rc);
		return ANSC_STATUS_FAILURE;
            }

	} else if ( pCfg->EncryptionMethod == COSA_DML_WIFI_AP_SEC_AES)
	{
            rc = strcpy_s(method,sizeof(method),"AESEncryption");
	    if ( rc != 0) {
                ERR_CHK(rc);
		return ANSC_STATUS_FAILURE;
            }
	}
#ifndef _XB6_PRODUCT_REQ_
	else if ( pCfg->EncryptionMethod == COSA_DML_WIFI_AP_SEC_AES_TKIP)
	{
            rc = strcpy_s(method,sizeof(method),"TKIPandAESEncryption");
	    if ( rc != 0) {
                ERR_CHK(rc);
		return ANSC_STATUS_FAILURE;
            }
	}
#endif
		CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CONFIG_CHANGED :%s Encryption method changed ,calling setWpaEncryptionMode Index : %d mode : %s \n",__FUNCTION__,wlanIndex,method));
		wifi_setApWpaEncryptionMode(wlanIndex, method);
		enable_reset_radio_flag(wlanIndex);
    } 

#if !defined (_COSA_BCM_MIPS_)&& !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_) && !defined(_INTEL_WAV_)
    if ( pCfg->RekeyingInterval != pStoredCfg->RekeyingInterval) {
		CcspWifiTrace(("RDK_LOG_WARN,%s calling setWpaRekeyInterval  \n",__FUNCTION__));
        wifi_setApSecurityWpaRekeyInterval(wlanIndex,  pCfg->RekeyingInterval);
    }
#endif

    if ( strcmp((char*)pCfg->RadiusServerIPAddr, (char*)pStoredCfg->RadiusServerIPAddr) !=0 || 
		pCfg->RadiusServerPort != pStoredCfg->RadiusServerPort || 
		strcmp(pCfg->RadiusSecret, pStoredCfg->RadiusSecret) !=0) {
		CcspWifiTrace(("RDK_LOG_WARN,%s calling wifi_setApSecurityRadiusServer  \n",__FUNCTION__));        
		wifi_setApSecurityRadiusServer(wlanIndex, (char*)pCfg->RadiusServerIPAddr, pCfg->RadiusServerPort, pCfg->RadiusSecret);
    }

	if ( strcmp((char*)pCfg->SecondaryRadiusServerIPAddr, (char*)pStoredCfg->SecondaryRadiusServerIPAddr) !=0 || 
		pCfg->SecondaryRadiusServerPort != pStoredCfg->SecondaryRadiusServerPort || 
		strcmp(pCfg->SecondaryRadiusSecret, pStoredCfg->SecondaryRadiusSecret) !=0) {
		CcspWifiTrace(("RDK_LOG_WARN,%s calling wifi_setApSecurityRadiusServer  \n",__FUNCTION__));
		wifi_setApSecuritySecondaryRadiusServer(wlanIndex, (char*)pCfg->SecondaryRadiusServerIPAddr, pCfg->SecondaryRadiusServerPort, pCfg->SecondaryRadiusSecret);
	}
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
	if ( strcmp((char *)pCfg->RadiusDASIPAddr, (char *)pStoredCfg->RadiusDASIPAddr) !=0 ||
                pCfg->RadiusDASPort != pStoredCfg->RadiusDASPort ||
                strcmp(pCfg->RadiusDASSecret, pStoredCfg->RadiusDASSecret) !=0) {
                CcspWifiTrace(("RDK_LOG_WARN,%s calling wifi_setApDASRadiusServer  \n",__FUNCTION__));
                wifi_setApDASRadiusServer(wlanIndex, (char *)pCfg->RadiusDASIPAddr, pCfg->RadiusDASPort, pCfg->RadiusDASSecret);
	}
#endif
	if ( strcmp(pCfg->MFPConfig, pStoredCfg->MFPConfig) !=0 ) {
		CcspWifiTrace(("RDK_LOG_WARN,%s calling wifi_setApSecurityMFPConfig  \n",__FUNCTION__));
		if ( RETURN_OK == wifi_setApSecurityMFPConfig(wlanIndex, pCfg->MFPConfig)) {
                    CosaDmlWiFi_SetApMFPConfigValue(wlanIndex, pCfg->MFPConfig);
                }
		CcspWifiTrace(("RDK_LOG_INFO,\nMFPConfig = %s\n",pCfg->MFPConfig));
	}
	if( pCfg->bReset == TRUE )
	{
		/* Reset the value after do the operation */
		wifi_setApSecurityReset( wlanIndex );
		pCfg->bReset  = FALSE;		
        CcspWifiTrace(("RDK_LOG_WARN,%s WiFi security settings are reset to their factory default values \n ",__FUNCTION__));
	}

	//zqiu: TODO: set pCfg->RadiusReAuthInterval;     
    
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    memcpy(&sWiFiDmlApSecurityStored[wlanIndex].Cfg, pCfg, sizeof(COSA_DML_WIFI_APSEC_CFG));
#else
    memcpy(pStoredCfg, pCfg, sizeof(COSA_DML_WIFI_APSEC_CFG));
#endif

    return ANSC_STATUS_SUCCESS;
}

/* CosaDmlWiFiApSecsetMFPConfig() */
ANSC_STATUS CosaDmlWiFiApSecsetMFPConfig( int vAPIndex, CHAR *pMfpConfig )
{
	if ( RETURN_OK == wifi_setApSecurityMFPConfig( vAPIndex, pMfpConfig ) )
	{
                if ( ANSC_STATUS_SUCCESS == CosaDmlWiFi_SetApMFPConfigValue(vAPIndex, pMfpConfig))
                {
		     snprintf( sWiFiDmlApSecurityStored[vAPIndex].Cfg.MFPConfig, sizeof(sWiFiDmlApSecurityStored[vAPIndex].Cfg.MFPConfig), "%s", pMfpConfig );

		     CcspTraceInfo(("%s MFPConfig = [%d,%s]\n",__FUNCTION__, vAPIndex, sWiFiDmlApSecurityStored[vAPIndex].Cfg.MFPConfig ));
		     return ANSC_STATUS_SUCCESS;
                }
	}

	CcspTraceInfo(("%s Fail to set MFPConfig = [%d,%s]\n",__FUNCTION__, vAPIndex, ( pMfpConfig ) ?  pMfpConfig : "NULL" ));
	return ANSC_STATUS_FAILURE;
}

ANSC_STATUS
CosaDmlWiFiApAcctGetEntry
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APACCT_FULL   pEntry
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    int wlanIndex;

    wifiDbgPrintf("%s pSsid = %s\n",__FUNCTION__, pSsid);

    if (!pEntry)
    {
        CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pEntry is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    CosaDmlWiFiApAcctGetCfg((ANSC_HANDLE)hContext, pSsid, &pEntry->Cfg);
    wifi_getIndexFromName(pSsid, &wlanIndex);
    if (wlanIndex == -1)
    {
        CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pSsid = %s Couldn't find wlanIndex \n",__FUNCTION__, pSsid));
        return ANSC_STATUS_FAILURE;
    }

    memcpy(&sWiFiDmlApAcctStored[wlanIndex], pEntry, sizeof(COSA_DML_WIFI_APACCT_FULL));
    memcpy(&sWiFiDmlApAcctRunning[wlanIndex], pEntry, sizeof(COSA_DML_WIFI_APACCT_FULL));

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApAcctGetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APACCT_CFG    pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    int wlanIndex;

    wifiDbgPrintf("%s pSsid = %s\n",__FUNCTION__, pSsid);
    if (!pCfg)
    {
        CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pEntry is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    wifi_getIndexFromName(pSsid, &wlanIndex);
    if (wlanIndex == -1)
    {
        CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pSsid = %s Couldn't find wlanIndex \n",__FUNCTION__, pSsid));
        return ANSC_STATUS_FAILURE;
    }

    wifi_getRADIUSAcctEnable(wlanIndex, &pCfg->bEnabled);
    wifi_getApSecurityAcctServer(wlanIndex, pCfg->AcctServerIPAddr, &pCfg->AcctServerPort, pCfg->AcctSecret);
    wifi_getApSecuritySecondaryAcctServer(wlanIndex, pCfg->SecondaryAcctServerIPAddr, &pCfg->SecondaryAcctServerPort, pCfg->SecondaryAcctSecret);
    wifi_getApSecurityAcctInterimInterval(wlanIndex, &pCfg->InterimInterval);

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApAcctSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APACCT_CFG    pCfg
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    PCOSA_DML_WIFI_APACCT_CFG pStoredCfg = NULL;
    int wlanIndex = -1;

    wifiDbgPrintf("%s\n",__FUNCTION__);
    if (!pCfg)
    {
        CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pCfg is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    wifi_getIndexFromName(pSsid, &wlanIndex);
    if (wlanIndex == -1)
    {
        // Error could not find index
        CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : could not find index wlanIndex(-1)  \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    pStoredCfg = &sWiFiDmlApAcctStored[wlanIndex].Cfg;
    if ( pCfg->bEnabled != pStoredCfg->bEnabled )
    {
        if (RETURN_OK != wifi_setRADIUSAcctEnable(wlanIndex, pCfg->bEnabled))
        {
            CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : could not enable RADUIS account\n",__FUNCTION__));
            return ANSC_STATUS_FAILURE;
        }
    }

    if (strcmp(pCfg->AcctServerIPAddr, pStoredCfg->AcctServerIPAddr) !=0 ||
        pCfg->AcctServerPort != pStoredCfg->AcctServerPort ||
        strcmp(pCfg->AcctSecret, pStoredCfg->AcctSecret) !=0)
    {
        if (RETURN_OK != wifi_setApSecurityAcctServer(wlanIndex, pCfg->AcctServerIPAddr, pCfg->AcctServerPort, pCfg->AcctSecret) )
        {
            CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : could not set RADUIS account security server\n",__FUNCTION__));
            return ANSC_STATUS_FAILURE;
        }
    }

    if (strcmp(pCfg->SecondaryAcctServerIPAddr, pStoredCfg->SecondaryAcctServerIPAddr) !=0 ||
        pCfg->SecondaryAcctServerPort != pStoredCfg->SecondaryAcctServerPort ||
        strcmp(pCfg->SecondaryAcctSecret, pStoredCfg->SecondaryAcctSecret) !=0)
    {
        if (RETURN_OK != wifi_setApSecuritySecondaryAcctServer(wlanIndex, pCfg->SecondaryAcctServerIPAddr, pCfg->SecondaryAcctServerPort, pCfg->SecondaryAcctSecret))
        {
            CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : could not set RADUIS account security secondary server\n",__FUNCTION__));
            return ANSC_STATUS_FAILURE;
        }
    }

    if ( pCfg->InterimInterval != pStoredCfg->InterimInterval )
    {
        if (RETURN_OK != wifi_setApSecurityAcctInterimInterval( wlanIndex, pCfg->InterimInterval ))
        {
            CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : could not set RADUIS account InterimInterval\n",__FUNCTION__));
            return ANSC_STATUS_FAILURE;
        }
    }
    memcpy(&sWiFiDmlApAcctStored[wlanIndex].Cfg, pCfg, sizeof(COSA_DML_WIFI_APACCT_CFG));

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApAcctApplyCfg
    (
        PCOSA_DML_WIFI_APACCT_CFG    pCfg,
        ULONG                       index
    )
{
    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    PCOSA_DML_WIFI_APWPS_CFG    pRunningCfg = &sWiFiDmlApAcctRunning[index].Cfg;

    wifiDbgPrintf("%s[%d] wlanIndex %d\n",__FUNCTION__, __LINE__, index);
    if (!pCfg)
    {
        CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : pCfg is NULL \n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    if (memcmp(pCfg, pRunningCfg, sizeof(COSA_DML_WIFI_APACCT_CFG)) != 0) {
        sWiFiDmlRestartVap[index] = TRUE;
        sWiFiDmlRestartHostapd = TRUE;
        wifiDbgPrintf("%s %d sWiFiDmlRestartHostapd set to TRUE\n",__FUNCTION__, __LINE__);
    }

    memcpy(pRunningCfg, pCfg, sizeof(COSA_DML_WIFI_APACCT_CFG));

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApSecApplyWepCfg
(
PCOSA_DML_WIFI_APSEC_CFG    pCfg,
ULONG                                          instanceNumber
)
{
    wifiDbgPrintf("%s\n",__FUNCTION__);
    UNREFERENCED_PARAMETER(pCfg);
    UNREFERENCED_PARAMETER(instanceNumber);
//>> Deprecated
/*	
    int wlanIndex = instanceNumber-1;
    wifiDbgPrintf("%s[%d] wlanIndex %d\n",__FUNCTION__, __LINE__, wlanIndex);

    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }

    if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_64 || 
        pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_128 )
    {

        // Push Defualt Key to SSID as key 1.  This is to compensate for a Qualcomm bug
        wifi_pushWepKeyIndex( wlanIndex, 1);
        wifiDbgPrintf("%s[%d] wlanIndex %d DefualtKey %d \n",__FUNCTION__, __LINE__, wlanIndex, pCfg->DefaultKey);
		CcspWifiTrace(("RDK_LOG_WARN,%s : pushWepKey wlanIndex %d : DefualtKey %s :  \n",__FUNCTION__,wlanIndex,pCfg->DefaultKey));
        wifi_pushWepKey( wlanIndex, pCfg->DefaultKey);
        #if 0
        int i;
        for (i = 1; i <= 4; i++)
        {
        }
        wifi_setAuthMode(wlanIndex, 4);
        #endif

    }
*/
//<<
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApSecApplyCfg
(
PCOSA_DML_WIFI_APSEC_CFG    pCfg,
ULONG                                          instanceNumber
)
{
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    PCOSA_DML_WIFI_APSEC_CFG pRunningCfg = &sWiFiDmlApSecurityRunning[instanceNumber-1].Cfg;
#else
    PCOSA_DML_WIFI_APSEC_CFG pRunningCfg = NULL;
    ULONG                           uIndex         = 0;
#endif
    wifiDbgPrintf("%s\n",__FUNCTION__);

    int wlanIndex = instanceNumber-1;
    wifiDbgPrintf("%s[%d] wlanIndex %d\n",__FUNCTION__, __LINE__, wlanIndex);

    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }

#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    GET_AP_INDEX(sWiFiDmlApRunningCfg, instanceNumber, uIndex);
    pRunningCfg = &sWiFiDmlApSecurityRunning[uIndex].Cfg;
#endif
    // Make sure there is no entry in the /tmp/conf_finename
    wifi_removeApSecVaribles(wlanIndex);

    // Reset security to off 
    wifiDbgPrintf("%s %d Set encryptionOFF to reset security \n",__FUNCTION__, __LINE__);
    CcspWifiTrace(("RDK_LOG_WARN,%s : Set encryptionOFF to reset security \n",__FUNCTION__));
#if !defined(_XB7_PRODUCT_REQ_) && !defined(_XB8_PRODUCT_REQ_) && !defined(_CBR2_PRODUCT_REQ_)
    wifi_disableApEncryption(wlanIndex);
#endif

    // If the Running config has security = WPA or None hostapd must be restarted
    if ( (pRunningCfg->ModeEnabled >= COSA_DML_WIFI_SECURITY_WPA_Personal && 
          pRunningCfg->ModeEnabled <= COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise) ||
         (pRunningCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_None) )
    {
        wifiDbgPrintf("%s %d sWiFiDmlRestartHostapd set to TRUE\n",__FUNCTION__, __LINE__);
		CcspWifiTrace(("RDK_LOG_WARN,%s : sWiFiDmlRestartHostapd set to TRUE \n",__FUNCTION__));
        sWiFiDmlRestartHostapd = TRUE;
        sWiFiDmlRestartVap[wlanIndex] = TRUE;
    } else {
        // If the new config has security = WPA or None hostapd must be restarted
        if ( (pCfg->ModeEnabled >= COSA_DML_WIFI_SECURITY_WPA_Personal && 
              pCfg->ModeEnabled <= COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise) ||
             (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_None) )
        {
            wifiDbgPrintf("%s %d sWiFiDmlRestartHostapd set to TRUE\n",__FUNCTION__, __LINE__);
			CcspWifiTrace(("RDK_LOG_WARN,%s : sWiFiDmlRestartHostapd set to TRUE \n",__FUNCTION__));
            sWiFiDmlRestartHostapd = TRUE;
            sWiFiDmlRestartVap[wlanIndex] = TRUE; 
        }
    }

    if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_None) 
    {
#ifdef CISCO_XB3_PLATFORM_CHANGES
           BOOL wpsCfg = 0;
           wifi_getApWpsEnable(wlanIndex, &wpsCfg);
#else
           BOOL enableWps = FALSE;
           wifi_getApWpsEnable(wlanIndex, &enableWps);
#endif

#if defined(FEATURE_HOSTAP_AUTHENTICATOR)
       BOOL isHostapdAuthEnabled = FALSE;
       CosaDmlWiFiGetHostapdAuthenticatorEnable(&isHostapdAuthEnabled);

       if (isHostapdAuthEnabled)
       {
           CcspTraceWarning(("%s - %d parseConfiguation authenticator\n", __FUNCTION__, __LINE__));
           hapd_reset_ap_interface(wlanIndex);
           deinit_lib_hostapd(wlanIndex);
       }
       else
       {
           wifi_createHostApdConfig(wlanIndex, TRUE);
       }
#else //FEATURE_HOSTAP_AUTHENTICATOR
            // create WSC_ath*.conf file
            wifi_createHostApdConfig(wlanIndex, TRUE);
#endif //FEATURE_HOSTAP_AUTHENTICATOR

    } else if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_64 ||  pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_128 )
    {
        // Check the other primary SSID, if it is WPA and the current is going from WPA to WEP, turn off WPS
        if (wlanIndex < 2)
        {
            int checkIndex = (wlanIndex == 0) ? 1 : 0;
        
            // Only recreate hostapd file if Radio and SSID are enabled
            if ( sWiFiDmlRadioStoredCfg[checkIndex].bEnabled == TRUE && sWiFiDmlSsidStoredCfg[checkIndex].bEnabled )
            {
                // if the other Primary SSID (ath0/ath1) has security set to WPA or None with WPS enabled, WPS must be turned off
                if ( ( (sWiFiDmlApSecurityStored[checkIndex].Cfg.ModeEnabled >= COSA_DML_WIFI_SECURITY_WPA_Personal) ||
                       (sWiFiDmlApSecurityStored[checkIndex].Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_None) ) &&
                     (pRunningCfg->ModeEnabled >= COSA_DML_WIFI_SECURITY_WPA2_Personal) )
                {
#ifdef CISCO_XB3_PLATFORM_CHANGES
	            BOOL wpsCfg = 0;
        	    BOOL enableWps = FALSE;
            	    wifi_getApWpsEnable(checkIndex, &wpsCfg);
                    enableWps = (wpsCfg == 0) ? FALSE : TRUE;
#else
                    BOOL enableWps = FALSE;
                    wifi_getApWpsEnable(checkIndex, &enableWps);
#endif

                    if (enableWps == TRUE)
                    {
#if defined(FEATURE_HOSTAP_AUTHENTICATOR)
                        BOOL isHostapdAuthEnabled = FALSE;
                        CosaDmlWiFiGetHostapdAuthenticatorEnable(&isHostapdAuthEnabled);

                        if (!isHostapdAuthEnabled)
                        {
                            wifi_removeApSecVaribles(checkIndex);
                            wifi_createHostApdConfig(checkIndex, FALSE);
                        }
#else //FEATURE_HOSTAP_AUTHENTICATOR
                        wifi_removeApSecVaribles(checkIndex);
                        wifi_createHostApdConfig(checkIndex, FALSE);
#endif //FEATURE_HOSTAP_AUTHENTICATOR

                        wifiDbgPrintf("%s %d sWiFiDmlRestartHostapd set to TRUE\n",__FUNCTION__, __LINE__);
		    			CcspWifiTrace(("RDK_LOG_WARN,%s : sWiFiDmlRestartHostapd set to TRUE \n",__FUNCTION__));
                        sWiFiDmlRestartHostapd = TRUE;
                        sWiFiDmlRestartVap[checkIndex] = TRUE; 
                    }
                }
            }
        }
        
        sWiFiDmlPushWepKeys[wlanIndex] = TRUE;
        
        }
#ifdef _XB6_PRODUCT_REQ_
	else if ( pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Personal ||
               pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Enterprise )
#else
	else if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_Personal ||
               pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Personal ||
               pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_WPA2_Personal ||
               pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_Enterprise || 
               pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Enterprise ||
               pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise )
#endif
    {
        // WPA
        BOOL enableWps = FALSE;
       
#ifdef CISCO_XB3_PLATFORM_CHANGES
        BOOL wpsCfg = 0;
        wifi_getApWpsEnable(wlanIndex, &wpsCfg);
        enableWps = (wpsCfg == 0) ? FALSE : TRUE;     
            
#else
        wifi_getApWpsEnable(wlanIndex, &enableWps);
            
#endif 
        if (sWiFiDmlApStoredCfg[0].Cfg.SSIDAdvertisementEnabled == FALSE || 
            sWiFiDmlApStoredCfg[1].Cfg.SSIDAdvertisementEnabled == FALSE ||
            sWiFiDmlApSecurityStored[0].Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_64 ||
            sWiFiDmlApSecurityStored[0].Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_128 ||
            sWiFiDmlApSecurityStored[1].Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_64 ||
            sWiFiDmlApSecurityStored[1].Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_128)
        {
            enableWps = FALSE;
        }
#if defined(FEATURE_HOSTAP_AUTHENTICATOR)
        BOOL isHostapdAuthEnabled = FALSE;
        CosaDmlWiFiGetHostapdAuthenticatorEnable(&isHostapdAuthEnabled);

        if (!isHostapdAuthEnabled)
        {
            wifi_createHostApdConfig(wlanIndex, enableWps);
        }
#else //FEATURE_HOSTAP_AUTHENTICATOR
        wifi_createHostApdConfig(wlanIndex, enableWps);
#endif //FEATURE_HOSTAP_AUTHENTICATOR

       // Check the other primary SSID, if it is WPA recreate the config file
        if (wlanIndex < 2)
        {
            int checkIndex = (wlanIndex == 0) ? 1 : 0;

            // Only recreate hostapd file if Radio and SSID are enabled
            if ( sWiFiDmlRadioStoredCfg[checkIndex].bEnabled == TRUE && sWiFiDmlSsidStoredCfg[checkIndex].bEnabled )
            {
                // If the other SSID is running WPA recreate the config file
                if ( sWiFiDmlApSecurityStored[checkIndex].Cfg.ModeEnabled >= COSA_DML_WIFI_SECURITY_WPA_Personal ) 
                {
#ifdef CISCO_XB3_PLATFORM_CHANGES
                   wifi_getApWpsEnable(wlanIndex, &wpsCfg);
                   enableWps = (wpsCfg == 0) ? FALSE : TRUE;
            
#else
                    wifi_getApWpsEnable(wlanIndex, &enableWps);
            
#endif
                
                    if (sWiFiDmlApStoredCfg[0].Cfg.SSIDAdvertisementEnabled == FALSE || 
                        sWiFiDmlApStoredCfg[1].Cfg.SSIDAdvertisementEnabled == FALSE) 
                    { 
                        enableWps = FALSE;
                    }
                    printf("%s: recreating sec file enableWps = %s \n" , __FUNCTION__, ( enableWps == FALSE ) ? "FALSE" : "TRUE");
                    wifi_removeApSecVaribles(checkIndex);
                    wifi_createHostApdConfig(checkIndex, enableWps);
                }
            }
        }

    }

#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    memcpy(&sWiFiDmlApSecurityRunning[wlanIndex].Cfg, pCfg, sizeof(COSA_DML_WIFI_APSEC_CFG));
#else
    memcpy(pRunningCfg, pCfg, sizeof(COSA_DML_WIFI_APSEC_CFG));
#endif
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApSecPushCfg
    (
        PCOSA_DML_WIFI_APSEC_CFG    pCfg,
        ULONG                                          instanceNumber
    )
{
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    ULONG                           uIndex         = 0;
#endif
wifiDbgPrintf("%s\n",__FUNCTION__);

#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
    BOOL bEnabled;
#endif
    int wlanIndex = instanceNumber-1;

    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }
    
    // Make sure there is no entry in the /tmp/conf_finename
    wifi_removeApSecVaribles(wlanIndex);

    if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_None)
   {
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
       CosaDmlWiFiGetEnableRadiusGreylist(&bEnabled);
#endif
#ifdef CISCO_XB3_PLATFORM_CHANGES
        BOOL wpsCfg = 0;
        BOOL enableWps = FALSE;
 
        wifi_getApWpsEnable(wlanIndex, &wpsCfg);
        enableWps = (wpsCfg == 0) ? FALSE : TRUE;
#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
        if (bEnabled == TRUE || enableWps == TRUE )
        {
            sWiFiDmlRestartHostapd = TRUE;
            wifiDbgPrintf("%s %d sWiFiDmlRestartHostapd set to TRUE\n",__FUNCTION__, __LINE__);
            // create WSC_ath*.conf file
            wifi_createHostApdConfig(wlanIndex, enableWps);
        } 
#else
	 if (enableWps == TRUE )
        {
            sWiFiDmlRestartHostapd = TRUE;
            wifiDbgPrintf("%s %d sWiFiDmlRestartHostapd set to TRUE\n",__FUNCTION__, __LINE__);
            // create WSC_ath*.conf file
            wifi_createHostApdConfig(wlanIndex, TRUE);
        }

#endif
            
#else
         BOOL enableWps = FALSE;
         wifi_getApWpsEnable(wlanIndex, &enableWps);

#if defined (FEATURE_SUPPORT_RADIUSGREYLIST)
        if (bEnabled == TRUE || enableWps == TRUE )
        {
            sWiFiDmlRestartHostapd = TRUE;
            wifiDbgPrintf("%s %d sWiFiDmlRestartHostapd set to TRUE\n",__FUNCTION__, __LINE__);
            // create WSC_ath*.conf file
            wifi_createHostApdConfig(wlanIndex, enableWps);
        }
#else
         if (enableWps == TRUE)
         {
             sWiFiDmlRestartVap[wlanIndex] = TRUE;
             sWiFiDmlRestartHostapd = TRUE;
             wifiDbgPrintf("%s %d sWiFiDmlRestartHostapd set to TRUE\n",__FUNCTION__, __LINE__);
             // create WSC_ath*.conf file
             wifi_createHostApdConfig(wlanIndex, TRUE);
         } 
#endif            
#endif
    } else if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_64 || 
                   pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_128 ) { 

        // Check the other primary SSID, if it is WPA and the current is going from WPA to WEP, turn off WPS
        if (wlanIndex < 2)
        {
            int checkIndex = (wlanIndex == 0) ? 1 : 0;

            // Only recreate hostapd file if Radio and SSID are enabled
            if ( sWiFiDmlRadioStoredCfg[checkIndex].bEnabled == TRUE && sWiFiDmlSsidStoredCfg[checkIndex].bEnabled )
            {
                // if the other Primary SSID (ath0/ath1) has security set to WPA or None with WPS enabled, WPS must be turned off
                if ( (sWiFiDmlApSecurityStored[checkIndex].Cfg.ModeEnabled >= COSA_DML_WIFI_SECURITY_WPA_Personal) ||
                       (sWiFiDmlApSecurityStored[checkIndex].Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_None) ) 
                {
#ifdef CISCO_XB3_PLATFORM_CHANGES
                     BOOL wpsCfg = 0;
                     BOOL enableWps = FALSE;
                     wifi_getApWpsEnable(checkIndex, &wpsCfg);
                     enableWps = (wpsCfg == 0) ? FALSE : TRUE;
#else
                     BOOL enableWps = FALSE;
                     wifi_getApWpsEnable(checkIndex, &enableWps);
            
#endif                
                    if (enableWps == TRUE)
                    {
                        wifi_removeApSecVaribles(checkIndex);
                        wifi_disableApEncryption(checkIndex);

                        // Only create sec file for WPA, secath.  Can't run None/WPS with WEP on another SSID
                        if (sWiFiDmlApSecurityStored[checkIndex].Cfg.ModeEnabled >= COSA_DML_WIFI_SECURITY_WPA_Personal)
                        {
                            wifi_createHostApdConfig(checkIndex, FALSE);
                        }

                        wifiDbgPrintf("%s %d sWiFiDmlRestartHostapd set to TRUE\n",__FUNCTION__, __LINE__);
                        sWiFiDmlRestartVap[checkIndex] = TRUE;
                        sWiFiDmlRestartHostapd = TRUE;
                    }
                }
            }
        }

        sWiFiDmlPushWepKeys[wlanIndex] = TRUE;

    } 
#ifdef _XB6_PRODUCT_REQ_
	else  if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Personal || 
                pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Enterprise )
#else
	else  if (pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_Personal ||
                pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Personal ||
                pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_WPA2_Personal ||
                pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_Enterprise || 
                pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA2_Enterprise ||
                pCfg->ModeEnabled == COSA_DML_WIFI_SECURITY_WPA_WPA2_Enterprise )
#endif
    { 
        // WPA
#ifdef CISCO_XB3_PLATFORM_CHANGES

             BOOL wpsCfg = 0;
             BOOL enableWps = FALSE;
             wifi_getApWpsEnable(wlanIndex, &wpsCfg);
             enableWps = (wpsCfg == 0) ? FALSE : TRUE;
            
#else
             BOOL enableWps = FALSE;
             wifi_getApWpsEnable(wlanIndex, &enableWps);
            
#endif        
        if (sWiFiDmlApStoredCfg[0].Cfg.SSIDAdvertisementEnabled == FALSE || 
            sWiFiDmlApStoredCfg[1].Cfg.SSIDAdvertisementEnabled == FALSE ||
            sWiFiDmlApSecurityStored[0].Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_64 ||
            sWiFiDmlApSecurityStored[0].Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_128 ||
            sWiFiDmlApSecurityStored[1].Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_64 ||
            sWiFiDmlApSecurityStored[1].Cfg.ModeEnabled == COSA_DML_WIFI_SECURITY_WEP_128) {
            enableWps = FALSE;
        }
#if defined(FEATURE_HOSTAP_AUTHENTICATOR)
        BOOL isHostapdAuthEnabled = FALSE;
        CosaDmlWiFiGetHostapdAuthenticatorEnable(&isHostapdAuthEnabled);

        if (!isHostapdAuthEnabled)
        {
            wifi_createHostApdConfig(wlanIndex, enableWps);
            sWiFiDmlRestartHostapd = TRUE;
            wifiDbgPrintf("%s %d sWiFiDmlRestartHostapd set to TRUE\n",__FUNCTION__, __LINE__);
        }
#else //FEATURE_HOSTAP_AUTHENTICATOR
        wifi_createHostApdConfig(wlanIndex, enableWps);
        sWiFiDmlRestartVap[wlanIndex] = TRUE; 
        sWiFiDmlRestartHostapd = TRUE;
wifiDbgPrintf("%s %d sWiFiDmlRestartHostapd set to TRUE\n",__FUNCTION__, __LINE__);
#endif //FEATURE_HOSTAP_AUTHENTICATOR
    }

#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    memcpy(&sWiFiDmlApSecurityRunning[wlanIndex].Cfg, pCfg, sizeof(COSA_DML_WIFI_APSEC_CFG));
#else
    GET_AP_INDEX(sWiFiDmlApRunningCfg, instanceNumber, uIndex);
    memcpy(&sWiFiDmlApSecurityRunning[uIndex].Cfg, pCfg, sizeof(COSA_DML_WIFI_APSEC_CFG));
#endif

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApEapAuthCfg
    (
        ANSC_HANDLE                     hContext,
        char*                           pSsid,
        PCOSA_DML_WIFI_APSEC_CFG        pCfg
    )
{
    PCOSA_DML_WIFI_APSEC_CFG pStoredCfg = NULL;
    int wRet, wlanIndex = -1;
    int retval = RETURN_OK;
    UNREFERENCED_PARAMETER(hContext);
    wifiDbgPrintf("%s\n",__FUNCTION__);
    
    if (!pCfg || !pSsid)
    {
        CcspTraceInfo(("%s pCfg is NULL \n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex <0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
        // Error could not find index
        return ANSC_STATUS_FAILURE;
    }

    pStoredCfg = &sWiFiDmlApSecurityStored[wlanIndex].Cfg;

    if (pStoredCfg == NULL) {
       CcspTraceInfo(("%s pStoredCfg is NULL \n", __FUNCTION__));
       return ANSC_STATUS_FAILURE;
    }
#ifdef DUAL_CORE_XB3
    if (pCfg->uiEAPOLKeyTimeout != pStoredCfg->uiEAPOLKeyTimeout) {
        retval = wifi_setEAP_Param(wlanIndex, pCfg->uiEAPOLKeyTimeout, "rdkb_eapol_key_timeout");
        sWiFiDmlApSecurityStored[wlanIndex].Cfg.uiEAPOLKeyTimeout = pCfg->uiEAPOLKeyTimeout;
    }

    if (pCfg->uiEAPOLKeyRetries != pStoredCfg->uiEAPOLKeyRetries) {
        retval = wifi_setEAP_Param(wlanIndex, pCfg->uiEAPOLKeyRetries, "rdkb_eapol_key_retries");
        sWiFiDmlApSecurityStored[wlanIndex].Cfg.uiEAPOLKeyRetries = pCfg->uiEAPOLKeyRetries;
    }

    if (pCfg->uiEAPIdentityRequestTimeout != pStoredCfg->uiEAPIdentityRequestTimeout) {
        retval = wifi_setEAP_Param(wlanIndex, pCfg->uiEAPIdentityRequestTimeout, "rdkb_eapidentity_request_timeout");
        sWiFiDmlApSecurityStored[wlanIndex].Cfg.uiEAPIdentityRequestTimeout = pCfg->uiEAPIdentityRequestTimeout;
    }

    if (pCfg->uiEAPIdentityRequestRetries != pStoredCfg->uiEAPIdentityRequestRetries) {
        retval = wifi_setEAP_Param(wlanIndex, pCfg->uiEAPIdentityRequestRetries, "rdkb_eapidentity_request_retries");
        sWiFiDmlApSecurityStored[wlanIndex].Cfg.uiEAPIdentityRequestRetries = pCfg->uiEAPIdentityRequestRetries;
    }

    if (pCfg->uiEAPRequestTimeout != pStoredCfg->uiEAPRequestTimeout) {
        retval = wifi_setEAP_Param(wlanIndex, pCfg->uiEAPRequestTimeout, "rdkb_eap_request_timeout");
        sWiFiDmlApSecurityStored[wlanIndex].Cfg.uiEAPRequestTimeout = pCfg->uiEAPRequestTimeout;
    }

    if (pCfg->uiEAPRequestRetries != pStoredCfg->uiEAPRequestRetries) {
        retval = wifi_setEAP_Param(wlanIndex, pCfg->uiEAPRequestRetries, "rdkb_eap_request_retries");
        sWiFiDmlApSecurityStored[wlanIndex].Cfg.uiEAPRequestRetries = pCfg->uiEAPRequestRetries;
    }
#endif

    if (retval == RETURN_OK ) {
        CcspTraceInfo(("%s wifi_set success\n", __FUNCTION__));
    } else {
        CcspTraceInfo(("%s wifi_set failed\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApWpsGetEntry
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APWPS_FULL   pEntry
    )
{
    ANSC_STATUS               returnStatus = ANSC_STATUS_SUCCESS;
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    ULONG                           uIndex         = 0;
#endif
    int wlanIndex = -1;
    
    if (!pEntry || !pSsid)
    {
        return ANSC_STATUS_FAILURE;
    }
    PCOSA_DML_WIFI_APWPS_CFG  pCfg = &pEntry->Cfg;
    PCOSA_DML_WIFI_APWPS_INFO pInfo = &pEntry->Info;
  
    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
        // Error could not find index
        return ANSC_STATUS_FAILURE;
    }

#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    GET_AP_INDEX(sWiFiDmlApStoredCfg, wlanIndex+1, uIndex);
#endif
    returnStatus = CosaDmlWiFiApWpsGetCfg ( hContext, pSsid, pCfg );

    if (returnStatus == ANSC_STATUS_SUCCESS) {
        returnStatus = CosaDmlWiFiApWpsGetInfo ( hContext, pSsid, pInfo );
    }
    
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    memcpy(&sWiFiDmlApWpsRunning[wlanIndex], pEntry, sizeof(COSA_DML_WIFI_APWPS_FULL));
    memcpy(&sWiFiDmlApWpsStored[wlanIndex], pEntry, sizeof(COSA_DML_WIFI_APWPS_FULL));
#else
    memcpy(&sWiFiDmlApWpsRunning[uIndex], pEntry, sizeof(COSA_DML_WIFI_APWPS_FULL));
    memcpy(&sWiFiDmlApWpsStored[uIndex], pEntry, sizeof(COSA_DML_WIFI_APWPS_FULL));
#endif
    return returnStatus;
}

ANSC_STATUS
CosaDmlWiFiApWpsApplyCfg
    (
        PCOSA_DML_WIFI_APWPS_CFG    pCfg,
        ULONG                       index
    )
{
    PCOSA_DML_WIFI_APWPS_CFG    pRunningCfg = &sWiFiDmlApWpsRunning[index].Cfg;
wifiDbgPrintf("%s\n",__FUNCTION__);

    wifiDbgPrintf("%s[%d] wlanIndex %lu\n",__FUNCTION__, __LINE__, index);
    if (!pCfg)
    {
        return ANSC_STATUS_FAILURE;
    }

    if (memcmp(pCfg, pRunningCfg, sizeof(COSA_DML_WIFI_APWPS_CFG)) != 0) {
        sWiFiDmlRestartVap[index] = TRUE;
        sWiFiDmlRestartHostapd = TRUE;
wifiDbgPrintf("%s %d sWiFiDmlRestartHostapd set to TRUE\n",__FUNCTION__, __LINE__);
    }
wifiDbgPrintf("%s %d\n",__FUNCTION__, __LINE__);

    memcpy(pRunningCfg, pCfg, sizeof(COSA_DML_WIFI_APWPS_CFG));
wifiDbgPrintf("%s %d\n",__FUNCTION__, __LINE__);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApWpsSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APWPS_CFG    pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    ULONG                           uIndex         = 0;
#endif
    PCOSA_DML_WIFI_APWPS_CFG pStoredCfg = NULL;
    int wlanIndex = -1;
    char recName[256];
    char strValue[32];
    int retPsmSet = CCSP_SUCCESS;

wifiDbgPrintf("%s\n",__FUNCTION__);

    if (!pCfg || !pSsid)
    {
        return ANSC_STATUS_FAILURE;
    }


    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ((wRet != RETURN_OK) || wlanIndex < 0 || wlanIndex >= WIFI_INDEX_MAX) 
    {
        // Error could not find index
        return ANSC_STATUS_FAILURE;
    }
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    pStoredCfg = &sWiFiDmlApWpsStored[wlanIndex].Cfg;
#else
    GET_AP_INDEX(sWiFiDmlApStoredCfg, wlanIndex+1, uIndex);
    pStoredCfg = &sWiFiDmlApWpsStored[uIndex].Cfg;
#endif

    if (pCfg->bEnabled != pStoredCfg->bEnabled)
    {
        ULONG pin;
        wifi_getApWpsDevicePIN(wlanIndex,&pin);

//#if defined(_COSA_BCM_MIPS_)|| defined(_COSA_BCM_ARM_)
//        wifi_setApWpsEnable(wlanIndex, pCfg->bEnabled);
//#else
#ifdef CISCO_XB3_PLATFORM_CHANGES
        if (pCfg->bEnabled == TRUE && pin != 0)
        {
            wifi_setApWpsEnable(wlanIndex, 2);
        } else
        {
            wifi_setApWpsEnable(wlanIndex, pCfg->bEnabled);
        }
#else
            wifi_setApWpsEnable(wlanIndex, pCfg->bEnabled);
            enable_reset_radio_flag(wlanIndex);
        
#endif
//#endif
    }

    if (pCfg->ConfigMethodsEnabled != pStoredCfg->ConfigMethodsEnabled)
    {
	// Label and Display should always be set.  The currently DML enum does not include them, but they must be 
	// set on the Radio
	if ( pCfg->ConfigMethodsEnabled == COSA_DML_WIFI_WPS_METHOD_PushButton ) {
	    wifi_setApWpsConfigMethodsEnabled(wlanIndex,"PushButton");
	} else  if (pCfg->ConfigMethodsEnabled == COSA_DML_WIFI_WPS_METHOD_Pin) {
	    wifi_setApWpsConfigMethodsEnabled(wlanIndex,"Keypad,Label,Display");
	} else if ( pCfg->ConfigMethodsEnabled == (COSA_DML_WIFI_WPS_METHOD_PushButton|COSA_DML_WIFI_WPS_METHOD_Pin) ) {
	    wifi_setApWpsConfigMethodsEnabled(wlanIndex,"PushButton,Keypad,Label,Display");
	} 
	enable_reset_radio_flag(wlanIndex);
    } 
    
    if (pCfg->WpsPushButton != pStoredCfg->WpsPushButton) {

        snprintf(recName, sizeof(recName) - 1, WpsPushButton, wlanIndex+1);
        snprintf(strValue, sizeof(strValue) - 1,"%d", pCfg->WpsPushButton);
        printf("%s: Setting %s to %d(%s)\n", __FUNCTION__, recName, pCfg->WpsPushButton, strValue);
        retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue);
        if (retPsmSet != CCSP_SUCCESS) {
            wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting %s \n",__FUNCTION__, retPsmSet, recName); 
        }
        if (g_wifidb_rfc) {
            struct schema_Wifi_VAP_Config  *pcfg= NULL;
            pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[wlanIndex], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
            if (pcfg != NULL) {
                pcfg->wps_push_button = pCfg->WpsPushButton;
                if (wifi_ovsdb_update_table_entry(vap_names[wlanIndex],"vap_name",OCLM_STR,&table_Wifi_VAP_Config,pcfg,filter_vaps) <= 0) {
                    CcspWifiTrace(("RDK_LOG_ERROR,%s: WIFI DB Failed to update vap config\n",__FUNCTION__ ));
                }
            }
        }
    }

    // looks like hostapd_cli allows for settings a timeout when the pin is set
    // would need to expand or create a new API that could handle both.
    // Either ClientPin or ActivatePushButton should be set
    if (strlen(pCfg->X_LGI_COM_ClientPin) > 0) {
		wifi_setApWpsEnrolleePin(wlanIndex,pCfg->X_LGI_COM_ClientPin);
    } else if (pCfg->X_LGI_COM_ActivatePushButton == TRUE) {
        wifi_setApWpsButtonPush(wlanIndex);
    } else if (pCfg->X_LGI_COM_CancelSession == TRUE) {
        wifi_cancelApWPS(wlanIndex);
    }

    // reset Pin and Activation
    memset(pCfg->X_LGI_COM_ClientPin, 0, sizeof(pCfg->X_LGI_COM_ClientPin));
    pCfg->X_LGI_COM_ActivatePushButton = FALSE;
    pCfg->X_LGI_COM_CancelSession = FALSE;
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    memcpy(&sWiFiDmlApWpsStored[wlanIndex].Cfg, pCfg, sizeof(COSA_DML_WIFI_APWPS_CFG));
#else
    memcpy(pStoredCfg, pCfg, sizeof(COSA_DML_WIFI_APWPS_CFG));
#endif

    return ANSC_STATUS_SUCCESS;

}

ANSC_STATUS
    CosaDmlWiFiApWpsGetCfg
(
 ANSC_HANDLE                 hContext,
 char*                       pSsid,
 PCOSA_DML_WIFI_APWPS_CFG    pCfg
 )
{
    UNREFERENCED_PARAMETER(hContext);
    int wlanIndex = -1;
    char recName[256];
    char *strValue = NULL;
    int retPsmGet = CCSP_SUCCESS;

    wifiDbgPrintf("%s\n",__FUNCTION__);
    
    if (!pCfg || !pSsid) {
        return ANSC_STATUS_FAILURE;
    }

    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex <0) || (wlanIndex >= WIFI_INDEX_MAX) ) {
        // Error could not find index
        return ANSC_STATUS_FAILURE;
    }

#ifdef CISCO_XB3_PLATFORM_CHANGES
     BOOL wpsEnabled = 0;
     wifi_getApWpsEnable(wlanIndex, &wpsEnabled);
     pCfg->bEnabled = (wpsEnabled == 0) ? FALSE : TRUE;
            
#else
     wifi_getApWpsEnable(wlanIndex, &pCfg->bEnabled);
            
#endif
    if (!g_wifidb_rfc) {
        snprintf(recName, sizeof(recName), WpsPushButton, wlanIndex+1);
        retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
        if (retPsmGet == CCSP_SUCCESS) {
            pCfg->WpsPushButton = atoi(strValue);
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        } else {
            pCfg->WpsPushButton = 1;  // Use as default value
        }
    } else {
        struct schema_Wifi_VAP_Config  *pcfg= NULL;
        pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[wlanIndex], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
        if (pcfg != NULL) {
            pCfg->WpsPushButton  = pcfg->wps_push_button;
        } else {
            pCfg->WpsPushButton = 1;
        }
    } 
    pCfg->ConfigMethodsEnabled = 0;
    char methodsEnabled[64];

    wifi_getApWpsConfigMethodsEnabled(wlanIndex,methodsEnabled);
    if (strstr(methodsEnabled,"PushButton") != NULL) {
        pCfg->ConfigMethodsEnabled |= COSA_DML_WIFI_WPS_METHOD_PushButton;
    } 
    if (strstr(methodsEnabled,"Keypad") != NULL) {
        pCfg->ConfigMethodsEnabled |= COSA_DML_WIFI_WPS_METHOD_Pin;
    } 
    
    /* USGv2 Extensions */
    // These may be write only parameters
    memset(pCfg->X_LGI_COM_ClientPin, 0, sizeof(pCfg->X_LGI_COM_ClientPin));
    pCfg->X_LGI_COM_ActivatePushButton = FALSE;
    pCfg->X_LGI_COM_CancelSession = FALSE;
    
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApWpsSetInfo
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APWPS_INFO    pInfo
    )
{
    UNREFERENCED_PARAMETER(hContext);
    int wlanIndex = -1;
    wifiDbgPrintf("%s\n",__FUNCTION__);

    if (!pInfo || !pSsid)
    {
        return ANSC_STATUS_FAILURE;
    }

    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex <0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
        // Error could not find index
        return ANSC_STATUS_FAILURE;
    }

    // Read Only parameter
    // pInfo->ConfigMethodsSupported

    unsigned int pin = _ansc_atoi(pInfo->X_LGI_COM_Pin);
    wifi_setApWpsDevicePIN(wlanIndex, pin);

#if !defined(_COSA_BCM_MIPS_)&& !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
    // Already set WPS enabled in WpsSetCfg, but 
    //   if config==TRUE set again to configured(2).
    if ( pInfo->X_Comcast_com_Configured == TRUE ) {
#ifdef CISCO_XB3_PLATFORM_CHANGES
            wifi_setApWpsEnable(wlanIndex, 2);
#else
            wifi_setApWpsEnable(wlanIndex, TRUE);
#endif
    }
#endif

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApWpsGetInfo
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_APWPS_INFO   pInfo
    )
{
    UNREFERENCED_PARAMETER(hContext);
    int wlanIndex = -1;
    char  configState[32];
    ULONG pin;
    wifiDbgPrintf("%s\n",__FUNCTION__);

    if (!pInfo || !pSsid)
    {
        return ANSC_STATUS_FAILURE;
    }

    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex <0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
        // Error could not find index
        return ANSC_STATUS_FAILURE;
    }

    pInfo->ConfigMethodsSupported = COSA_DML_WIFI_WPS_METHOD_PushButton | COSA_DML_WIFI_WPS_METHOD_Pin;

    wifi_getApWpsDevicePIN(wlanIndex, &pin);

    sprintf(pInfo->X_LGI_COM_Pin, "%08lu", pin);

    wifi_getApWpsConfigurationState(wlanIndex, configState);
    if (strstr(configState,"Not configured") != NULL) {
        pInfo->X_Comcast_com_Configured = FALSE;
    } else {
        pInfo->X_Comcast_com_Configured = TRUE;
    }
    
    return ANSC_STATUS_SUCCESS;
}
/* Description:
 *	This routine is to retrieve the complete list of currently associated WiFi devices, 
 *	which is a dynamic table.
 * Arguments:
 *	pName   		Indicate which SSID to operate on.
 *	pulCount		To receive the actual number of entries.
 * Return:
 * The pointer to the array of WiFi associated devices, allocated by callee. 
 * If no entry is found, NULL is returned.
 */
PCOSA_DML_WIFI_AP_ASSOC_DEVICE
CosaDmlWiFiApGetAssocDevices
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PULONG                      pulCount
    )
{
    int wlanIndex = -1;	//???
    int radioIndex = 0;
    BOOL enabled=FALSE; 
    wifi_associated_dev2_t *wifi_associated_dev_array=NULL, *ps=NULL; //LGI ADD
    ULLONG handle = 0; // LGI ADD
	COSA_DML_WIFI_AP_ASSOC_DEVICE *pWifiApDev=NULL, *pd=NULL; 
	ULONG i=0;
	UINT array_size=0;
    UNREFERENCED_PARAMETER(hContext);
    
    if (!pSsid)
        return NULL;
    
    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex <0) || (wlanIndex >= WIFI_INDEX_MAX) )
		return NULL;
    
    wifi_getApEnable(wlanIndex, &enabled);
    if (enabled == FALSE) 
		return NULL; 

	//hal would allocate the array
	wRet = wifi_getApAssociatedDeviceDiagnosticResult2(wlanIndex, &wifi_associated_dev_array, &array_size); //LGI ADD
	if((wRet == RETURN_OK) && wifi_associated_dev_array && array_size>0) {
		*pulCount=array_size;
		//zqiu: TODO: to search the MAC in exsting pWifiApDev Array to find the match, and count Disassociations/AuthenticationFailures and Active
		pWifiApDev=(PCOSA_DML_WIFI_AP_ASSOC_DEVICE)malloc(sizeof(COSA_DML_WIFI_AP_ASSOC_DEVICE)*array_size);
		for(i=0, ps=wifi_associated_dev_array, pd=pWifiApDev; i<array_size; i++, ps++, pd++) {
			memcpy(pd->MacAddress, ps->cli_MACAddress, sizeof(UCHAR)*6);
			pd->AuthenticationState 	= ps->cli_AuthenticationState;
			pd->LastDataDownlinkRate 	= ps->cli_LastDataDownlinkRate;
			pd->LastDataUplinkRate 		= ps->cli_LastDataUplinkRate;
			pd->SignalStrength 			= ps->cli_SignalStrength;
			pd->Retransmissions 		= ps->cli_Retransmissions;
			pd->Active 					= ps->cli_Active;	//???
			memcpy(pd->OperatingStandard, ps->cli_OperatingStandard, sizeof(char)*64);
			memcpy(pd->OperatingChannelBandwidth, ps->cli_OperatingChannelBandwidth, sizeof(char)*64);
			pd->SNR 					= ps->cli_SNR;
			memcpy(pd->InterferenceSources, ps->cli_InterferenceSources, sizeof(char)*64);
			pd->DataFramesSentAck 		= ps->cli_DataFramesSentAck;
			pd->DataFramesSentNoAck 	= ps->cli_DataFramesSentNoAck;
			pd->BytesSent 				= ps->cli_BytesSent;
			pd->BytesReceived 			= ps->cli_BytesReceived;
#if 0
                        pd->PacketsSent                         = ps->cli_PacketsSent;
                        pd->PacketsReceived                     = ps->cli_PacketsReceived;
                        pd->ErrorsSent                          = ps->cli_ErrorsSent;
                        pd->RetransCount                        = ps->cli_RetransCount;
                        pd->FailedRetransCount                  = ps->cli_FailedRetransCount;
                        pd->RetryCount                          = ps->cli_RetryCount;
                        pd->MultipleRetryCount                  = ps->cli_MultipleRetryCount;
#endif
			pd->RSSI		 		= ps->cli_RSSI;
			pd->MinRSSI 				= ps->cli_MinRSSI;
			pd->MaxRSSI 				= ps->cli_MaxRSSI;
			pd->Disassociations			= 0;	//???
			pd->AuthenticationFailures	= 0;	//???
#if 0
			pd->maxUplinkRate   = ps->cli_MaxDownlinkRate;
			pd->maxDownlinkRate = ps->cli_MaxUplinkRate;
#endif
		}
		free(wifi_associated_dev_array);
		return (PCOSA_DML_WIFI_AP_ASSOC_DEVICE)pWifiApDev; 
	} else {
        if (wifi_associated_dev_array != NULL) {
            // the count is greater than 0, but we have corrupted data in the structure
            CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s DiagnosticResult2 data is corrupted - dropping\n",__FUNCTION__));
            free(wifi_associated_dev_array);
        }
    }	
    
    return NULL;
}

/* Description:
 *	This routine is to retrieve the complete list of currently associated WiFi devices
 *	and kick them to force them to disassociate.
 * Arguments:
 *	pName   		Indicate which SSID to operate on.
 * Return:
 * Status
 */
ANSC_STATUS
CosaDmlWiFiApKickAssocDevices
    (
        char*                       pSsid
    )
{
    if (!pSsid)
    {
        return ANSC_STATUS_FAILURE;
    }
    
wifiDbgPrintf("%s SSID %s\n",__FUNCTION__, pSsid);

    ANSC_STATUS                     returnStatus   = ANSC_STATUS_SUCCESS;
    ULONG                           index             = 0;
    ULONG                           ulCount           = 0;
    /*For example we have 5 AssocDevices*/
    int wlanIndex = -1;
    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
	// Error could not find index
        return ANSC_STATUS_FAILURE;
    }

    wifi_getApNumDevicesAssociated(wlanIndex, &ulCount);
    if (ulCount > 0)
    {
		for (index = ulCount; index > 0; index--)
		{ 
			wifi_device_t wlanDevice;

			wifi_getAssociatedDeviceDetail(wlanIndex, index, &wlanDevice);
			wifi_kickAssociatedDevice(wlanIndex, &wlanDevice);
		}
#if defined(ENABLE_FEATURE_MESHWIFI)
        {
            // notify mesh components that wifi SSID Advertise changed
            CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh to kick off all devices\n",__FUNCTION__));
            v_secure_system("/usr/bin/sysevent set wifi_kickAllApAssociatedDevice 'RDK|%d'", wlanIndex);
        }
#endif
    }
    return returnStatus;
}

ANSC_STATUS
CosaDmlWiFiApMfGetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_MF_CFG    pCfg
    )
{
    UNREFERENCED_PARAMETER(hContext);
    wifiDbgPrintf("%s\n",__FUNCTION__);
    // R3 requirement 
    int mode = 0;
    int wlanIndex = -1;
    char recName[256];
    int retPsmGet = CCSP_SUCCESS;
    char *strValue = NULL;

    if (!pCfg || !pSsid)
    {
        return ANSC_STATUS_FAILURE;
    }

    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ( (wRet != RETURN_OK) || (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
        // Error could not find index
        return ANSC_STATUS_FAILURE;
    }

#if defined(_XF3_PRODUCT_REQ_)    
    if (wlanIndex == 4 || wlanIndex == 5 || wlanIndex == 8 || wlanIndex == 9) {
        CcspWifiTrace(("RDK_LOG_INFO,%s WIFI Macfilter mode set not needed for Xfinity vaps\n",__FUNCTION__));
        return ANSC_STATUS_SUCCESS;
    }
#endif

    if (!g_wifidb_rfc) {
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), MacFilterMode, wlanIndex+1);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        mode = _ansc_atoi(strValue);
        wifi_setApMacAddressControlMode(wlanIndex,mode);
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    } else {
        struct schema_Wifi_VAP_Config  *pcfg= NULL;
        pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[wlanIndex], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
        if (pcfg != NULL) {
            if (pcfg->mac_filter_enabled && (pcfg->mac_filter_mode == wifi_mac_filter_mode_black_list)) {
                mode = 2;
            } else if (pcfg->mac_filter_enabled && (pcfg->mac_filter_mode == wifi_mac_filter_mode_white_list)) {
                mode = 1;
            } else {
                mode = 0;
            }
            wifi_setApMacAddressControlMode(wlanIndex,mode);
        }
    }
    if (mode == 0) 
    {
        pCfg->bEnabled = FALSE;
        pCfg->FilterAsBlackList = FALSE;
    } else if (mode == 1) {
        pCfg->bEnabled = TRUE;
        pCfg->FilterAsBlackList = FALSE;
    } else if (mode == 2) {
        pCfg->bEnabled = TRUE;
        pCfg->FilterAsBlackList = TRUE;
    }

    sWiFiDmlApMfCfg[wlanIndex] = pCfg;
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApMfSetCfg
    (
        ANSC_HANDLE                 hContext,
        char*                       pSsid,
        PCOSA_DML_WIFI_AP_MF_CFG    pCfg
    )
{
    wifiDbgPrintf("%s\n",__FUNCTION__);
    UNREFERENCED_PARAMETER(hContext);
    int wlanIndex = -1;
    char recName[256];

    if (!pCfg || !pSsid)
    {
        return ANSC_STATUS_FAILURE;
    }

    int wRet = wifi_getIndexFromName(pSsid, &wlanIndex);
    if ((wRet != RETURN_OK) || (wlanIndex < 0) || (wlanIndex >= WIFI_INDEX_MAX) )
    {
        // Error could not find index
        return ANSC_STATUS_FAILURE;
    }

    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), MacFilterMode, wlanIndex+1);

    if ( pCfg->bEnabled == FALSE )
    {
	wifi_setApMacAddressControlMode(wlanIndex, 0);
        PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "0");
    } else if ( pCfg->FilterAsBlackList == FALSE ) {
	wifi_setApMacAddressControlMode(wlanIndex, 1);
        PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "1");
    } else if ( pCfg->FilterAsBlackList == TRUE ) {
	wifi_setApMacAddressControlMode(wlanIndex, 2);
        PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, "2");
    }

    if (g_wifidb_rfc) {
        struct schema_Wifi_VAP_Config  *pcfg= NULL;
        pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[wlanIndex], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
        if (pcfg != NULL) {
            if (pCfg->bEnabled == FALSE) {
                 pcfg->mac_filter_enabled = FALSE;
            } else if ( pCfg->FilterAsBlackList == FALSE ) {
                 pcfg->mac_filter_enabled = TRUE; 
                 pcfg->mac_filter_enabled = wifi_mac_filter_mode_white_list;
            } else if ( pCfg->FilterAsBlackList == TRUE ) {
                 pcfg->mac_filter_enabled = TRUE;
                 pcfg->mac_filter_mode = wifi_mac_filter_mode_black_list;
            }
            if (wifi_ovsdb_update_table_entry(vap_names[wlanIndex],"vap_name",OCLM_STR,&table_Wifi_VAP_Config,pcfg,filter_vaps) <= 0) {
                CcspWifiTrace(("RDK_LOG_ERROR,%s: WIFI DB Failed to update vap config\n",__FUNCTION__ ));
            }
        }
    }
#if defined(ENABLE_FEATURE_MESHWIFI)
    {
        // notify mesh components that wifi SMAc Filter Mode changed
        CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh Mac filter mode changed\n",__FUNCTION__));
        v_secure_system("/usr/bin/sysevent set wifi_MacAddressControlMode 'RDK|%d|%s|%s'", wlanIndex, (pCfg->bEnabled?"true":"false"), (pCfg->FilterAsBlackList?"true":"false"));
    }
#endif

#if defined(_COSA_BCM_MIPS_) //|| defined(_COSA_BCM_ARM_)
    // special case for Broadcom radios
    wifi_initRadio((wlanIndex%2==0?0:1));

#endif

    if ( pCfg->bEnabled == TRUE ) {
        wifi_kickApAclAssociatedDevices(wlanIndex, pCfg->FilterAsBlackList);
    }

#if defined(_COSA_BCM_MIPS_) || defined(_INTEL_WAV_)//|| defined(_COSA_BCM_ARM_)
    // special case for Broadcom radios
    wifi_initRadio((wlanIndex%2==0?0:1));

#endif
	if ( pCfg->bEnabled == TRUE )
	{
		BOOL enable = FALSE; 
#if defined(_ENABLE_BAND_STEERING_)
		if( ( 0 == wlanIndex ) || \
			( 1 == wlanIndex )
		  )
		{
			//To get Band Steering enable status
			wifi_getBandSteeringEnable( &enable );
			
			/* 
			  * When bandsteering already enabled then need to disable band steering 
			  * when private ACL is going to on case
			  */
			if( enable )
			{
				pthread_t tid; 
                                pthread_attr_t attr;
                                pthread_attr_t *attrp = NULL;

                                attrp = &attr;
                                pthread_attr_init(&attr);
                                pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
				pthread_create( &tid, 
								attrp, 
								CosaDmlWiFi_DisableBandSteeringBasedonACLThread, 
								NULL ); 
                                if(attrp != NULL)
                                    pthread_attr_destroy( attrp );
			}
		}
#endif /* _ENABLE_BAND_STEERING_ */
	}

#if (defined(_COSA_BCM_ARM_) && defined(_XB7_PRODUCT_REQ_)) || defined(_XB8_PRODUCT_REQ_) || defined(_CBR2_PRODUCT_REQ_)
    /* Converting brcm patch to code and this code will be removed as part of Hal Version 3 changes */
    fprintf(stderr, "%s: calling wifi_apply()...\n", __func__);
    wifi_apply();
#endif

    return ANSC_STATUS_SUCCESS;
}

/* CosaDmlWiFi_DisableBandSteeringBasedonACLThread() */
void *CosaDmlWiFi_DisableBandSteeringBasedonACLThread( void *input )  
{
	CCSP_MESSAGE_BUS_INFO *bus_info = (CCSP_MESSAGE_BUS_INFO *)bus_handle;
	parameterValStruct_t   param_val[ 1 ]    = {{ "Device.WiFi.X_RDKCENTRAL-COM_BandSteering.Enable", "false", ccsp_boolean }};
	char				   component[ 256 ]  = "eRT.com.cisco.spvtg.ccsp.wifi";
	char				   bus[256]		     = "/com/cisco/spvtg/ccsp/wifi";
	char*				   faultParam 	     = NULL;
	int 				   ret			     = 0;
	UNREFERENCED_PARAMETER(input);

	CcspWifiTrace(("RDK_LOG_WARN, %s-%d [Disable BS due to ACL is on] \n",__FUNCTION__,__LINE__));
	
	ret = CcspBaseIf_setParameterValues(  bus_handle,
										  component,
										  bus,
										  0,
										  0,
										  param_val,
										  1,
										  TRUE,
										  &faultParam
										  );
			
	if( ( ret != CCSP_SUCCESS ) && \
		( faultParam )
	  )
	{
		CcspWifiTrace(("RDK_LOG_WARN, %s-%d Failed to disable BS\n",__FUNCTION__,__LINE__));
		bus_info->freefunc( faultParam );
	}
	return NULL;
}

ANSC_STATUS
CosaDmlWiFiApMfPushCfg
(
PCOSA_DML_WIFI_AP_MF_CFG    pCfg,
ULONG                                           wlanIndex
)
{
    wifiDbgPrintf("%s\n",__FUNCTION__);
    if (!pCfg) return ANSC_STATUS_FAILURE;

    if ( pCfg->bEnabled == FALSE ) {
        wifi_setApMacAddressControlMode(wlanIndex, 0);
    } else if ( pCfg->FilterAsBlackList == FALSE ) {
        wifi_setApMacAddressControlMode(wlanIndex, 1);
    } else if ( pCfg->FilterAsBlackList == TRUE ) {
        wifi_setApMacAddressControlMode(wlanIndex, 2);
    }

#if defined(ENABLE_FEATURE_MESHWIFI)
    {
        // notify mesh components that wifi SMAc Filter Mode changed
        CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh Mac filter mode changed\n",__FUNCTION__));
        v_secure_system("/usr/bin/sysevent set wifi_MacAddressControlMode 'RDK|%lu|%s|%s'", wlanIndex, (pCfg->bEnabled?"true":"false"), (pCfg->FilterAsBlackList?"true":"false"));
    }
#endif
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApSetMFQueue
    (
        QUEUE_HEADER *mfQueue,
        ULONG                  apIns
    )
{
    if ( (apIns >= 1) && (apIns <= WIFI_INDEX_MAX) )
    {
wifiDbgPrintf("%s apIns = %lu \n",__FUNCTION__, apIns );
    sWiFiDmlApMfQueue[apIns-1] = mfQueue;
    }
    return ANSC_STATUS_SUCCESS;
}

//>> Deprecated

ANSC_STATUS
CosaDmlWiFi_GetWEPKey64ByIndex(ULONG apIns, ULONG keyIdx, PCOSA_DML_WEPKEY_64BIT pWepKey)
{

    wifiDbgPrintf("%s apIns = %lu, keyIdx = %lu\n",__FUNCTION__, apIns, keyIdx);
#ifdef CISCO_XB3_PLATFORM_CHANGES
    wifi_getWepKey(apIns-1, keyIdx+1, pWepKey->WEPKey);
#else
    UNREFERENCED_PARAMETER(pWepKey);
#endif
    
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetWEPKey64ByIndex(ULONG apIns, ULONG keyIdx, PCOSA_DML_WEPKEY_64BIT pWepKey)
{
    wifiDbgPrintf("%s apIns = %lu, keyIdx = %lu\n",__FUNCTION__, apIns, keyIdx);
    UNREFERENCED_PARAMETER(pWepKey);
    // for downgrade compatibility set both index 0 & 1 for keyIdx 1 
    // Comcast 1.3 release uses keyIdx 0 maps to driver index 0, this is used by driver and the on GUI
    // all four keys are set the same.  If a box is downgraded from 1.6 to 1.3 and the required WEP key will be set
   //if (keyIdx == 0)
   //{
   //    wifi_setWepKey(apIns-1, keyIdx, pWepKey->WEPKey);
   //}
    
    //wifi_setWepKey(apIns-1, keyIdx+1, pWepKey->WEPKey);
    //sWiFiDmlWepChg[apIns-1] = TRUE;

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_GetWEPKey128ByIndex(ULONG apIns, ULONG keyIdx, PCOSA_DML_WEPKEY_128BIT pWepKey)
{
    wifiDbgPrintf("%s apIns = %lu, keyIdx = %lu\n",__FUNCTION__, apIns, keyIdx);
#ifdef CISCO_XB3_PLATFORM_CHANGES
    wifi_getWepKey(apIns-1, keyIdx+1, pWepKey->WEPKey);
#else
    UNREFERENCED_PARAMETER(pWepKey);
#endif

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetWEPKey128ByIndex(ULONG apIns, ULONG keyIdx, PCOSA_DML_WEPKEY_128BIT pWepKey)
{
    wifiDbgPrintf("%s apIns = %lu, keyIdx = %lu\n",__FUNCTION__, apIns, keyIdx);
    UNREFERENCED_PARAMETER(pWepKey);    
    //wifi_setWepKey(apIns-1, keyIdx+1, pWepKey->WEPKey);
    //sWiFiDmlWepChg[apIns-1] = TRUE;

    return ANSC_STATUS_SUCCESS;
}

int CosaDmlWiFi_IsValidMacAddr(const char* mac)
{
    int i = 0;
    int s = 0;

    while (*mac) 
    {
        if (isxdigit(*mac)) 
        {
            i++;
        } 
        else if (*mac == ':')
        {
            if (i == 0 || i / 2 - 1 != s)
                break;
            ++s;
        }
        else
        {
            s = -1;
        }
        ++mac;
    }
    return (i == 12 && (s == 5 || s == 0));
}

#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)

ANSC_STATUS
CosaDmlWiFi_setDppVersion(ULONG apIns, ULONG version){
    int retPsmSet = CCSP_SUCCESS;
    char recName[256];
    char value[2]={0};

    sprintf(value,"%li",version);
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), DppVersion, apIns);

    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, value);
    if (retPsmSet == CCSP_SUCCESS) {
        return ANSC_STATUS_SUCCESS;
    }else{
        CcspTraceError(("%s:%d: PSM Set Failed: %s\n", __func__, __LINE__,recName));
        return ANSC_STATUS_FAILURE;
    }
}

ANSC_STATUS
CosaDmlWiFi_setDppReconfig(ULONG apIns,char* ParamName,char *value ){
    int retPsmSet = CCSP_SUCCESS;
    char recName[256];
    memset(recName, 0, sizeof(recName));

    if (strcmp(ParamName, "PrivateSigningKey") == 0){
        snprintf(recName, sizeof(recName), DppPrivateSigningKey, apIns);
    }else if (strcmp(ParamName, "PrivateReconfigAccessKey") == 0){
        snprintf(recName, sizeof(recName), DppPrivateReconfigAccessKey, apIns);
    }else{
        CcspTraceError(("%s:%d: Invalid Config: %s\n", __func__, __LINE__,recName));
        return ANSC_STATUS_FAILURE;
    }
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, value);
    if (retPsmSet == CCSP_SUCCESS) {
        return ANSC_STATUS_SUCCESS;
    }else{
        CcspTraceError(("%s:%d: PSM Set Failed: %s\n", __func__, __LINE__,recName));
        return ANSC_STATUS_FAILURE;
    }
}

ANSC_STATUS
CosaDmlWiFi_setDppValue(ULONG apIns, ULONG staIndex,char* ParamName,char *value ){
    int retPsmSet = CCSP_SUCCESS;
    char recName[256];
    memset(recName, 0, sizeof(recName));

    if (strcmp(ParamName, "ClientMac") == 0){
        sprintf(recName, DppClientMac, apIns,staIndex);
    }else if (strcmp(ParamName, "InitiatorBootstrapSubjectPublicKeyInfo") == 0){ 
        sprintf(recName, DppInitPubKeyInfo, apIns,staIndex);
    }else if (strcmp(ParamName, "ResponderBootstrapSubjectPublicKeyInfo") == 0){ 
        sprintf(recName, DppRespPubKeyInfo, apIns,staIndex);
    }else if (strcmp(ParamName, "Channels") == 0){ 
        sprintf(recName, DppChannels, apIns,staIndex);
    }else if (strcmp(ParamName, "MaxRetryCount") == 0){ 
        sprintf(recName, DppMaxRetryCnt, apIns,staIndex);
    }else if (strcmp(ParamName, "Activate") == 0){ 
        sprintf(recName, DppActivate, apIns,staIndex);
    }else if (strcmp(ParamName, "ActivationStatus") == 0){ 
        sprintf(recName, DppActivationStatus, apIns,staIndex);
    }else if (strcmp(ParamName, "EnrolleeResponderStatus") == 0){ 
        sprintf(recName, DppEnrolleeRespStatus, apIns,staIndex);
    }else if (strcmp(ParamName, "KeyManagement") == 0){ 
        sprintf(recName, DppEnrolleeKeyManagement, apIns,staIndex);
    }else {
        return ANSC_STATUS_FAILURE; 
    }

    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, value);
    if (retPsmSet == CCSP_SUCCESS) {
        return ANSC_STATUS_SUCCESS;
    }else{
        CcspTraceError(("%s:%d: PSM Set Failed: %s\n", __func__, __LINE__,recName));
        return ANSC_STATUS_FAILURE; 
    }
}

void CosaDmlWifi_getDppConfigFromPSM(PANSC_HANDLE phContext){

    PCOSA_DATAMODEL_WIFI            pWiFi     = (PCOSA_DATAMODEL_WIFI) phContext;
    if(!pWiFi){
        CcspTraceError(("%s:%d: Wifi Instance is NULL\n", __func__, __LINE__));
        return;
    }
    char recName[256];
    char *strValue=NULL;
    int apIns, staIndex;
    PCOSA_DML_WIFI_DPP_STA_CFG pWifiDppSta = NULL; 

    PSINGLE_LINK_ENTRY          pAPLink     = NULL;
    PCOSA_CONTEXT_LINK_OBJECT   pAPLinkObj  = NULL;
    PCOSA_DML_WIFI_AP           pWiFiAP     = NULL;
    PCOSA_DML_WIFI_DPP_CFG      pWifiDpp    = NULL;
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
        if (!pWiFiAP)
            continue;
        apIns = pWiFiAP->AP.Cfg.InstanceNumber;        

        pWifiDpp  = (PCOSA_DML_WIFI_DPP_CFG)&pWiFiAP->DPP;
        memset(recName, 0, sizeof(recName));
        sprintf(recName,DppVersion, apIns);
        if(CCSP_SUCCESS == PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue)){
            pWifiDpp->Version = (UCHAR)_ansc_atoi(strValue);
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        }else{
            pWifiDpp->Version = 2;//Set Default to 2 unless if already set to Version 1
        }

        memset(recName, 0, sizeof(recName));
        sprintf(recName,DppPrivateSigningKey, apIns);
        if(CCSP_SUCCESS == PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue)){
            AnscCopyString(pWifiDpp->Recfg.PrivateSigningKey, strValue);
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        }
        memset(recName, 0, sizeof(recName));
        sprintf(recName,DppPrivateReconfigAccessKey, apIns);
        if(CCSP_SUCCESS == PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue)){
            AnscCopyString(pWifiDpp->Recfg.PrivateReconfigAccessKey, strValue);
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        }

        /* for each Device.WiFi.AccessPoint.{i}.X_RDKCENTRAL-COM_DPP.STA.{i}. */
        for (staIndex = 1; staIndex <= COSA_DML_WIFI_DPP_STA_MAX; staIndex++)
        {
            pWifiDppSta = (ANSC_HANDLE)&pWiFiAP->DPP.Cfg[staIndex-1];
            if(!pWifiDppSta)
                continue;           
 
            memset(recName, 0, sizeof(recName));
            sprintf(recName, DppClientMac, apIns,staIndex);
            if(CCSP_SUCCESS == PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue)){
                AnscCopyString(pWifiDppSta->ClientMac, strValue);
                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
            }
            else{
                continue;
            }

            memset(recName, 0, sizeof(recName));
            sprintf(recName, DppInitPubKeyInfo, apIns,staIndex);
            if(CCSP_SUCCESS == PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue)){
                AnscCopyString(pWifiDppSta->InitiatorBootstrapSubjectPublicKeyInfo, strValue);
                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
            }

            memset(recName, 0, sizeof(recName));
            sprintf(recName, DppRespPubKeyInfo, apIns,staIndex);
            if(CCSP_SUCCESS == PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue)){
                AnscCopyString(pWifiDppSta->ResponderBootstrapSubjectPublicKeyInfo, strValue);
                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
            }

            memset(recName, 0, sizeof(recName));
            sprintf(recName, DppChannels, apIns,staIndex);
            if(CCSP_SUCCESS == PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue)){
                CosaDmlWiFi_StringToChannelsList(strValue, pWifiDppSta);
                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
            }

            memset(recName, 0, sizeof(recName));
            sprintf(recName, DppMaxRetryCnt, apIns,staIndex);
            if(CCSP_SUCCESS == PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue)){
                pWifiDppSta->MaxRetryCount = _ansc_atoi(strValue);
                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
            }
           
            //Always Initialize to false
            pWifiDppSta->Activate = FALSE;

            memset(recName, 0, sizeof(recName));
            sprintf(recName, DppActivationStatus, apIns,staIndex);
            if(CCSP_SUCCESS == PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue)){
                AnscCopyString((char*)pWifiDppSta->ActivationStatus, strValue);
                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
            }

            memset(recName, 0, sizeof(recName));
            sprintf(recName, DppEnrolleeRespStatus, apIns,staIndex);
            if(CCSP_SUCCESS == PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue)){
                AnscCopyString((char*)pWifiDppSta->EnrolleeResponderStatus, strValue);
                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
            }
            
            memset(recName, 0, sizeof(recName));
			sprintf(recName, DppEnrolleeKeyManagement, apIns,staIndex);
            if(CCSP_SUCCESS == PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue)){
                AnscCopyString((char*)pWifiDppSta->Cred.KeyManagement, strValue);
                ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
            }
        }
    }

#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
	CosaDmlWiFi_initEasyConnect(pWiFi);
#endif // !defined(_HUB4_PRODUCT_REQ_)
}

ANSC_STATUS
CosaDmlWiFi_startDPP(PCOSA_DML_WIFI_AP pWiFiAP, ULONG staIndex)
{

#if !defined(_BWG_PRODUCT_REQ_)
#if !defined(_XF3_PRODUCT_REQ_) && !defined(_CBR_PRODUCT_REQ_) && !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_) && !defined(_PLATFORM_TURRIS_) && !defined(_PLATFORM_RASPBERRYPI_)
    if (start_device_provisioning(pWiFiAP, staIndex)  == RETURN_OK) {
       CcspTraceError(("%s:%d: DPP Authentication Request Frame send success\n", __func__, __LINE__));
       return ANSC_STATUS_SUCCESS;
    } else {
        CcspTraceError(("%s:%d: DPP Authentication Request Frame send failed\n", __func__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }
    return ANSC_STATUS_FAILURE;
#else //!defined(_XF3_PRODUCT_REQ_) && !defined(_CBR_PRODUCT_REQ_) && !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_) && !defined(_PLATFORM_RASPBERRYPI_)
    UNREFERENCED_PARAMETER(pWiFiAP);
    UNREFERENCED_PARAMETER(staIndex);
#endif
#else //!defined(_BWG_PRODUCT_REQ_)
    UNREFERENCED_PARAMETER(pWiFiAP);
    UNREFERENCED_PARAMETER(staIndex);
#endif
    return ANSC_STATUS_SUCCESS;
}
#endif // !defined(_HUB4_PRODUCT_REQ_)

#define MAX_MAC_FILT                64

static int                          g_macFiltCnt[MAX_VAP] = { 0 };
//static COSA_DML_WIFI_AP_MAC_FILTER  g_macFiltTab[MAX_MAC_FILT]; // = { { 1, "MacFilterTable1", "00:1a:2b:aa:bb:cc" }, };
pthread_mutex_t MacFilt_CountMutex = PTHREAD_MUTEX_INITIALIZER;

ULONG
CosaDmlMacFilt_GetNumberOfEntries(ULONG apIns)
{
    char *strValue = NULL;
    char recName[256];
    int retPsmGet = CCSP_SUCCESS;
    int retPsmSet = CCSP_SUCCESS;
    int count = 0;
    
    if ( (apIns < 1) || (apIns > WIFI_INDEX_MAX) )
    {
	return 0;
    }
wifiDbgPrintf("%s apIns = %lu\n",__FUNCTION__, apIns);
    pthread_mutex_lock(&MacFilt_CountMutex);
    g_macFiltCnt[apIns-1] = 0;

    memset(recName, 0, sizeof(recName));
    sprintf(recName, MacFilterList, apIns);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {

        if (strlen(strValue) > 0) {
			char tmpMacFilterList[512] = { 0 };
            char *start = tmpMacFilterList;
            char *end = NULL;

			snprintf( tmpMacFilterList, sizeof( tmpMacFilterList ) - 1, "%s", strValue);
			end = tmpMacFilterList + strlen(tmpMacFilterList);

            if ((end = strstr(tmpMacFilterList, ":" ))) {
                *end = 0;

                g_macFiltCnt[apIns-1] = _ansc_atoi(start);
            }
        }
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    } else  if (retPsmGet == CCSP_CR_ERR_INVALID_PARAM){
        char *macFilter = "0:"; 

        g_macFiltCnt[apIns-1] = 0;

        // Init to empty list on factory fresh
        retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, macFilter);
        if (retPsmSet != CCSP_SUCCESS) {
            wifiDbgPrintf("%s PSM_Set_Record_Value2 returned error %d while setting recName %s\n",__FUNCTION__, retPsmSet, recName);
    }
    }
    count = g_macFiltCnt[apIns-1];
pthread_mutex_unlock(&MacFilt_CountMutex);
wifiDbgPrintf("%s apIns = %lu count = %d\n",__FUNCTION__, apIns, count);
    return count;
}

ANSC_STATUS
CosaDmlMacFilt_GetMacInstanceNumber(ULONG apIns, ULONG index, PCOSA_DML_WIFI_AP_MAC_FILTER pMacFilt)
{
wifiDbgPrintf("%s\n",__FUNCTION__);
    char recName[256];
    char *strValue;
    char *macFilterList;
    int retPsmGet = CCSP_SUCCESS;
    ULONG i = 0;

    if (!pMacFilt) return ANSC_STATUS_FAILURE;

    pMacFilt->InstanceNumber = 0;

    memset(recName, 0, sizeof(recName));
    sprintf(recName, MacFilterList, apIns);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
		if ((macFilterList = strstr(strValue, ":" ))) {
			macFilterList += 1;
			while (i < index  && macFilterList != NULL ) {
                i++;
                if ((macFilterList = strstr(macFilterList,","))) {
                    if(macFilterList == NULL)
                    {
                        break;
                    }
                    macFilterList += 1;
                }
            } 
            if (i == index && macFilterList != NULL) {
                pMacFilt->InstanceNumber = _ansc_atoi(macFilterList);
            } 
        }
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    } else {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : get %s Fail \n",__FUNCTION__, recName));
        return ANSC_STATUS_FAILURE;
    }
    return ANSC_STATUS_SUCCESS;
}


ANSC_STATUS
CosaDmlMacFilt_GetEntryByIndex(ULONG apIns, ULONG index, PCOSA_DML_WIFI_AP_MAC_FILTER pMacFilt)
{
wifiDbgPrintf("%s\n",__FUNCTION__);
    char recName[256];
    char *devName;
    char *devMac;
    int retPsmGet = CCSP_SUCCESS;

    if (!pMacFilt || (apIns < 1) || (apIns > WIFI_INDEX_MAX)) return ANSC_STATUS_FAILURE;

    if (index >= (ULONG)g_macFiltCnt[apIns-1])
        return ANSC_STATUS_FAILURE;

    if ( CosaDmlMacFilt_GetMacInstanceNumber(apIns, index, pMacFilt) == ANSC_STATUS_FAILURE)
    {
        return ANSC_STATUS_FAILURE;
    }

    snprintf(pMacFilt->Alias,sizeof(pMacFilt->Alias), "MacFilter%lu", pMacFilt->InstanceNumber);

    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), MacFilter, apIns, pMacFilt->InstanceNumber);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &devMac);
    if (retPsmGet == CCSP_SUCCESS) {
	sprintf(pMacFilt->MACAddress,"%s",devMac);
	wifi_addApAclDevice(apIns-1,devMac);
	wifiDbgPrintf("%s called wifi_addApAclDevice index = %lu mac %s \n",__FUNCTION__, apIns-1,devMac);
	CcspWifiTrace(("RDK_LOG_WARN,%s : called wifi_addApAclDevice index = %lu mac %s \n",__FUNCTION__,apIns-1,devMac));
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(devMac);
    }

    memset(recName, 0, sizeof(recName));
    sprintf(recName, MacFilterDevice, apIns, pMacFilt->InstanceNumber);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &devName);
    if (retPsmGet == CCSP_SUCCESS) {
	sprintf(pMacFilt->DeviceName,"%s",devName);
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(devName);
    }

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlMacFilt_SetValues(ULONG apIns, ULONG index, ULONG ins, char *Alias)
{
    UNREFERENCED_PARAMETER(apIns);
    UNREFERENCED_PARAMETER(index);
    UNREFERENCED_PARAMETER(ins);
    UNREFERENCED_PARAMETER(Alias);
    wifiDbgPrintf("%s\n",__FUNCTION__);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlMacFilt_AddEntry(ULONG apIns, PCOSA_DML_WIFI_AP_MAC_FILTER pMacFilt)
{
wifiDbgPrintf("%s\n",__FUNCTION__);
    char recName[256];
    char macFilterList[256];
    int retPsmGet = CCSP_SUCCESS;
    int retPsmSet = CCSP_SUCCESS;
    char *strValue = NULL;
	BOOL enabled=FALSE;

    if (!pMacFilt || (apIns < 1) || (apIns > WIFI_INDEX_MAX)) return ANSC_STATUS_FAILURE;

        if (g_macFiltCnt[apIns-1] >= MAX_MAC_FILT)
        {
              CcspTraceWarning(("Mac Filter max limit is reached ,returning failure\n"));
              return ANSC_STATUS_FAILURE;
        }
	pthread_mutex_lock(&MacFilt_CountMutex);
	wifi_getApEnable(apIns-1, &enabled);
	if (enabled) { 		 			
		CcspTraceWarning(("Mac Filter Entry count:%d\n", g_macFiltCnt[apIns-1]));
		int rc = wifi_addApAclDevice(apIns-1,pMacFilt->MACAddress);
		if (rc != 0) {
			wifiDbgPrintf("%s apIns = %lu wifi_addApAclDevice failed for %s\n",__FUNCTION__, apIns,(char *) pMacFilt->MACAddress);
			CcspWifiTrace(("RDK_LOG_ERROR,%s : apIns = %lu wifi_addApAclDevice failed for %s \n",__FUNCTION__, apIns,(char *) pMacFilt->MACAddress));
			//zqiu: need to continue to save to PSM
			//return ANSC_STATUS_FAILURE;
		} else {
#if defined(ENABLE_FEATURE_MESHWIFI)
            {
                // notify mesh components that wifi SSID Advertise changed
                CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh to add device\n",__FUNCTION__));
                v_secure_system("/usr/bin/sysevent set wifi_addApAclDevice 'RDK|%lu|%s'", apIns-1, pMacFilt->MACAddress);
            }
#endif
		}

//		wifi_getApAclDeviceNum(apIns-1, &g_macFiltCnt[apIns-1]);
	}

    snprintf(pMacFilt->Alias,sizeof(pMacFilt->Alias),"MacFilter%lu", pMacFilt->InstanceNumber);

    // Add Mac to Non-Vol PSM
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), MacFilter, apIns, pMacFilt->InstanceNumber);
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, pMacFilt->MACAddress);
    if (retPsmSet != CCSP_SUCCESS) {
	wifiDbgPrintf("%s Error %d adding mac = %s \n", __FUNCTION__, retPsmSet, pMacFilt->MACAddress);
	CcspWifiTrace(("RDK_LOG_ERROR,%s : %d adding mac = %s\n",__FUNCTION__, retPsmSet, pMacFilt->MACAddress));
	pthread_mutex_unlock(&MacFilt_CountMutex);
        return ANSC_STATUS_FAILURE;
    }

    // Add Mac to Non-Vol PSM
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), MacFilterDevice, apIns, pMacFilt->InstanceNumber);
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, pMacFilt->DeviceName);
    if (retPsmSet != CCSP_SUCCESS) {
	wifiDbgPrintf("%s Error %d adding mac device name = %s \n", __FUNCTION__, retPsmSet, pMacFilt->DeviceName);
	CcspWifiTrace(("RDK_LOG_ERROR,%s : %d adding mac device name = %s \n",__FUNCTION__, retPsmSet, pMacFilt->DeviceName));
	pthread_mutex_unlock(&MacFilt_CountMutex);
        return ANSC_STATUS_FAILURE;
    }

    memset(macFilterList, 0, sizeof(macFilterList));
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), MacFilterList, apIns);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
		char tmpMacFilterList[512] = { 0 };
        int numFilters = 0;
        char *start = tmpMacFilterList;
        char *macs = NULL;

		snprintf( tmpMacFilterList, sizeof( tmpMacFilterList ) - 1, "%s", strValue);
		CcspWifiTrace(("RDK_LOG_INFO,%s : <MF-PSMGet> MacFilterList %s\n",__FUNCTION__, (strValue) ? strValue : "NULL"	));

        if (strlen(start) > 0) {
	    numFilters = _ansc_atoi(start);

            if (numFilters == 0) {
                snprintf(macFilterList,sizeof(macFilterList),"%d:%lu", numFilters+1,pMacFilt->InstanceNumber);
            } else {
               if ((macs = strstr(tmpMacFilterList, ":" ))) {
					//macs should not be empty colon (:). It should be :5 or :5,6
			   		if( strlen(macs) > 1 )	
		   			{
						snprintf(macFilterList,sizeof(macFilterList),"%d%s,%lu", numFilters+1,macs,pMacFilt->InstanceNumber);
		   			}
					else
					{
						((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
						pthread_mutex_unlock(&MacFilt_CountMutex);
						CcspWifiTrace(("RDK_LOG_ERROR,%s : Error adding MacFilterList %s\n",__FUNCTION__, macFilterList));
						return ANSC_STATUS_FAILURE;
					}
               }  else {
		   // else illformed string.  If there are instances should have a :
                   snprintf(macFilterList,sizeof(macFilterList),"%d:%lu", numFilters+1,pMacFilt->InstanceNumber);
               }
            }
        }
        
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    } else {
	snprintf(macFilterList,sizeof(macFilterList),"1:%lu",pMacFilt->InstanceNumber);
    }

    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), MacFilterList, apIns);
	CcspWifiTrace(("RDK_LOG_INFO,%s: <MF-PSMSet> MacFilterList %s \n",__FUNCTION__,macFilterList ));
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, macFilterList);
    if (retPsmSet != CCSP_SUCCESS) {
	wifiDbgPrintf("%s PSM error adding MacFilterList  mac %d \n", __FUNCTION__, retPsmSet);
	CcspWifiTrace(("RDK_LOG_ERROR,%s : PSM error adding MacFilterList  mac %d \n",__FUNCTION__, retPsmSet));
	pthread_mutex_unlock(&MacFilt_CountMutex);
	return ANSC_STATUS_FAILURE;
    }

    // Notify WiFiExtender that MacFilter has changed
    {

		g_macFiltCnt[apIns-1]++;
	/* Note:When mac filter mode change gets called before adding mac in the list, kick mac does not work. 
Added api call to kick mac, once entry is added in the list*/
		wifi_kickApAclAssociatedDevices(apIns-1, TRUE);
    }
pthread_mutex_unlock(&MacFilt_CountMutex);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlMacFilt_DelEntry(ULONG apIns, ULONG macFiltIns)
{
    if ( (apIns < 1) || (apIns > WIFI_INDEX_MAX) )
    {
	return ANSC_STATUS_FAILURE;
    }

wifiDbgPrintf("%s apIns = %lu macFiltIns = %lu g_macFiltCnt = %d\n",__FUNCTION__, apIns, macFiltIns, g_macFiltCnt[apIns-1]);
    char recName[256];
    int retPsmGet = CCSP_SUCCESS;
    int retPsmSet = CCSP_SUCCESS;
    char *macAddress = NULL;
    char *macFilterList = NULL;
	pthread_mutex_lock(&MacFilt_CountMutex);
    // Add Mac to Non-Vol PSM
    memset(recName, 0, sizeof(recName));
    snprintf(recName, sizeof(recName), MacFilter, apIns, macFiltIns);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &macAddress);
    if (retPsmGet == CCSP_SUCCESS) {	
	//Note: Since wifi_kickApAclAssociatedDevices was getting called after wifi_delApAclDevice kick was not happening for the removed entry. 
	//Hence calling kick before removing the entry.  
	wifi_kickApAclAssociatedDevices(apIns-1, TRUE);
	wifi_delApAclDevice(apIns-1,macAddress);
#if defined(ENABLE_FEATURE_MESHWIFI)
    {
        // notify mesh components that wifi SSID Advertise changed
        CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh to delete device\n",__FUNCTION__));
        v_secure_system("/usr/bin/sysevent set wifi_delApAclDevice 'RDK|%lu|%s'", apIns-1, macAddress);
    }
#endif
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(macAddress);

	if ( g_macFiltCnt[apIns-1] > 0 )
	    g_macFiltCnt[apIns-1]--;

        memset(recName, 0, sizeof(recName));
	snprintf(recName, sizeof(recName), MacFilter, apIns, macFiltIns);
	PSM_Del_Record(bus_handle,g_Subsystem,recName);

        memset(recName, 0, sizeof(recName));
	snprintf(recName, sizeof(recName), MacFilterDevice, apIns, macFiltIns);
	PSM_Del_Record(bus_handle,g_Subsystem, recName);

        // Remove from MacFilterList
	memset(recName, 0, sizeof(recName));
	snprintf(recName, sizeof(recName), MacFilterList, apIns);
	retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &macFilterList);
	if (retPsmGet == CCSP_SUCCESS) {
			char tmpMacFilterList[512] = { 0 };
            char inst[32];
            char *mac = NULL;
            char *macs = NULL;
            char *nextMac = NULL;
            char *prev = NULL;

		snprintf( tmpMacFilterList, sizeof( tmpMacFilterList ) - 1, "%s", macFilterList);
		CcspWifiTrace(("RDK_LOG_INFO,%s: <MF-PSMGet> MacFilterList %s \n",__FUNCTION__,macFilterList ));

	    macs = strstr(tmpMacFilterList,":");
            if (macs) {
		snprintf(inst,sizeof(inst),"%lu",macFiltIns);
		mac = strstr(macs,inst);
            }
            if (mac) {
                // Not the only or last mac in list
                if ((nextMac=strstr(mac,","))) {
                    nextMac += 1;
                    snprintf(mac,sizeof(*mac),"%s",nextMac);
                } else {
                    prev = mac - 1;
                    if (strstr(prev,":")) {
			*mac = 0;
                    } else {
			*prev = 0;
                    }
                }

                mac=strstr(tmpMacFilterList,":");
                if (mac) {
                    char newMacList[256];
		    snprintf(newMacList, sizeof(newMacList)-1, "%d%s",g_macFiltCnt[apIns-1],mac);
			CcspWifiTrace(("RDK_LOG_INFO,%s: <MF-PSMSet> MacFilterList %s \n",__FUNCTION__,newMacList ));
	            retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, newMacList);
		    if (retPsmSet != CCSP_SUCCESS) {
			wifiDbgPrintf("%s PSM error %d while setting MacFilterList %s \n", __FUNCTION__, retPsmSet, newMacList);
			CcspWifiTrace(("RDK_LOG_ERROR,%s : PSM error %d while setting MacFilterList %s \n",__FUNCTION__, retPsmSet, newMacList));
			((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(macFilterList);
			pthread_mutex_unlock(&MacFilt_CountMutex);
			return ANSC_STATUS_FAILURE;
		    }
                }
            }
          ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(macFilterList);
        }
        
    } else {
    	pthread_mutex_unlock(&MacFilt_CountMutex);
        return ANSC_STATUS_FAILURE;
    }
	pthread_mutex_unlock(&MacFilt_CountMutex);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlMacFilt_GetConf(ULONG apIns, ULONG macFiltIns, PCOSA_DML_WIFI_AP_MAC_FILTER pMacFilt)
{
wifiDbgPrintf("%s\n",__FUNCTION__);

    if (!pMacFilt) return ANSC_STATUS_FAILURE;

    CosaDmlMacFilt_GetEntryByIndex(apIns, macFiltIns, pMacFilt);

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlMacFilt_SetConf(ULONG apIns, ULONG macFiltIns, PCOSA_DML_WIFI_AP_MAC_FILTER pMacFilt)
{
    wifiDbgPrintf("%s\n",__FUNCTION__);
    char recName[256];
    int retPsmSet = CCSP_SUCCESS;
    UNREFERENCED_PARAMETER(macFiltIns);
    if (!pMacFilt) return ANSC_STATUS_FAILURE;

    // Add Mac to Non-Vol PSM
    memset(recName, 0, sizeof(recName));
    sprintf(recName, MacFilter, apIns, pMacFilt->InstanceNumber);
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, pMacFilt->MACAddress);
    if (retPsmSet != CCSP_SUCCESS) {
	wifiDbgPrintf("%s Error %d adding mac = %s \n", __FUNCTION__, retPsmSet, pMacFilt->MACAddress);
	CcspWifiTrace(("RDK_LOG_ERROR,%s :adding mac = %s\n",__FUNCTION__, pMacFilt->MACAddress));
        return ANSC_STATUS_FAILURE;
    }
	CcspWifiTrace(("RDK_LOG_INFO,%s :adding mac = %s\n",__FUNCTION__, pMacFilt->MACAddress));

    // Add Mac Device Name to Non-Vol PSM
    memset(recName, 0, sizeof(recName));
    sprintf(recName, MacFilterDevice, apIns, pMacFilt->InstanceNumber);
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, pMacFilt->DeviceName);
    if (retPsmSet != CCSP_SUCCESS) {
	wifiDbgPrintf("%s Error %d adding mac device name = %s \n", __FUNCTION__, retPsmSet, pMacFilt->DeviceName);
	CcspWifiTrace(("RDK_LOG_ERROR,%s :adding mac device name = %s \n",__FUNCTION__, pMacFilt->DeviceName));
        return ANSC_STATUS_FAILURE;
    }
	CcspWifiTrace(("RDK_LOG_INFO,%s :adding mac device name = %s \n",__FUNCTION__, pMacFilt->DeviceName));
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS CosaDmlWifi_setBSSTransitionActivated(PCOSA_DML_WIFI_AP_CFG pCfg, ULONG apIns)
{
    int retPsmSet;
    char strValue[32]={0};
    char recName[256]={0};

    if (pCfg->BSSTransitionImplemented != TRUE || pCfg->WirelessManagementImplemented != TRUE) {
         CcspTraceWarning(("%s: BSSTransitionImplemented or WirelessManagementImplemented not supported\n", __FUNCTION__));
         return ANSC_STATUS_FAILURE;
    }
#if !defined(_HUB4_PRODUCT_REQ_) || defined(HUB4_WLDM_SUPPORT)
    CcspTraceWarning(("%s: wifi_setBSSTransitionActivation apIns:%lu  BSSTransitionActivated:%d\n", __FUNCTION__, apIns, pCfg->BSSTransitionActivated));
    if (wifi_setBSSTransitionActivation(apIns, pCfg->BSSTransitionActivated) != RETURN_OK)
    {
        CcspTraceWarning(("%s: wifi_setBSSTransitionActivation Failed\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
#endif/*!defined(_HUB4_PRODUCT_REQ_) || defined(HUB4_WLDM_SUPPORT)*/
    snprintf(recName, sizeof(recName), BSSTransitionActivated, apIns+1);
    if (pCfg->BSSTransitionActivated)
    {
       snprintf(strValue,sizeof(strValue),"%s","true");
    }
    else 
    {
      snprintf(strValue,sizeof(strValue),"%s","false");
    }
    retPsmSet = PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue); 

    if (retPsmSet != CCSP_SUCCESS) {
        CcspTraceWarning(("%s: PSM_Set_Record_Value2 returned error %d\n",__FUNCTION__, retPsmSet));
        return ANSC_STATUS_FAILURE;
    }

    if (g_wifidb_rfc) {
        struct schema_Wifi_VAP_Config  *pcfg= NULL;

        pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[apIns], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
        if (pcfg != NULL) {
            pcfg->bss_transition_activated = pCfg->BSSTransitionActivated;
            if (wifi_ovsdb_update_table_entry(vap_names[apIns],"vap_name",OCLM_STR,&table_Wifi_VAP_Config,pcfg,filter_vaps) <= 0) {
                CcspWifiTrace(("RDK_LOG_ERROR,%s: WIFI DB Failed to update vap config\n",__FUNCTION__ ));
            }
        }
    }
    return ANSC_STATUS_SUCCESS;
}

#ifndef NELEMS
#define NELEMS(arr)         (sizeof(arr) / sizeof((arr)[0]))
#endif 

/*
 * @buf [OUT], buffer to save config file
 * @size [IN-OUT], buffer size as input and config file size as output
 */
ANSC_STATUS 
CosaDmlWiFi_GetConfigFile(void *buf, int *size)
{
const char *wifi_cfgs[] = {
#if defined (_XB6_PRODUCT_REQ_) && !(defined (_XB7_PRODUCT_REQ_) && (defined (_COSA_BCM_ARM_ )))
#if defined(_INTEL_WAV_)
        "/nvram/etc/config/wireless",
#else
        "/nvram/config/wireless",
#endif
#elif defined(_COSA_BCM_MIPS_) || defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_)// For TCCBR we use _COSA_BCM_ARM_ (TCCBR-3935)
        "/data/nvram",
#else
        "/nvram/etc/ath/.configData",
#endif
    };

    struct pack_hdr *hdr;

    if (!buf || !size) {
        CcspTraceError(("%s: bad parameter\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    if ((hdr = pack_files((char **)wifi_cfgs, NELEMS(wifi_cfgs))) == NULL) {
        CcspTraceError(("%s: pack_files error\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    dump_pack_hdr(hdr);

    if (*size < hdr->totsize) {
        CcspTraceError(("%s: buffer too small: %d, need %d\n", __FUNCTION__, *size, (int)hdr->totsize));
        free(hdr); /*RDKB-6907, CID-33234, free unused resource before exit*/
        return ANSC_STATUS_FAILURE;
    }

    *size = hdr->totsize;
    memcpy(buf, hdr, hdr->totsize);
    free(hdr); /*RDKB-6907, CID-33234, free unused resource before exit*/
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS 
CosaDmlWiFi_SetConfigFile(const void *buf, int size)
{
    const struct pack_hdr *hdr = buf;

    if (!buf || size != hdr->totsize) {
        wifiDbgPrintf("%s: bad parameter\n", __FUNCTION__);
        return ANSC_STATUS_FAILURE;
    }

    wifiDbgPrintf("%s: Remove Generated files\n", __FUNCTION__);
    v_secure_system("rm -rf /nvram/etc/wpa2/WSC_ath*");
    v_secure_system("rm -rf /tmp/secath*");

    wifiDbgPrintf("%s: unpack files\n", __FUNCTION__);
    if (unpack_files(hdr) != 0) {
        wifiDbgPrintf("%s: unpack_files error\n", __FUNCTION__);
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
}

#if defined (FEATURE_SUPPORT_WEBCONFIG)
ANSC_STATUS
CosaDmlWiFi_setWebConfig(char *webconfstr, int size,uint8_t ssid)
{
    ANSC_STATUS ret = ANSC_STATUS_FAILURE;
    
    if (webconfstr != NULL) {
        if (ssid == WIFI_SSID_CONFIG) {
            ret = wifi_vapBlobSet(webconfstr);
        } else {
            ret = wifi_WebConfigSet(webconfstr, size,ssid);
        }
    }
    return ret;
}
#endif

ANSC_STATUS 
CosaDmlWiFi_RadioUpTime(ULONG *TimeInSecs, int radioIndex)
{
    if (!TimeInSecs) return ANSC_STATUS_FAILURE;
    wifi_getRadioUpTime(radioIndex, TimeInSecs);
    if (*TimeInSecs  == 0) {
        wifiDbgPrintf("%s: error : Radion is not enable \n", __FUNCTION__);
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s :error : Radion is not enable\n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
	return ANSC_STATUS_SUCCESS;
}
ANSC_STATUS 
CosaDmlWiFi_getRadioCarrierSenseThresholdRange(INT radioIndex, INT *output)
{
    int ret = 0;
    ret = wifi_getRadioCarrierSenseThresholdRange(radioIndex,output);
	 
    if (ret != 0) {
		CcspWifiTrace(("RDK_LOG_ERROR,%s :wifi_getRadioCarrierSenseThresholdRange returned fail response\n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
	return ANSC_STATUS_SUCCESS;
}
ANSC_STATUS 
CosaDmlWiFi_getRadioCarrierSenseThresholdInUse(INT radioIndex, INT *output)
{
    int ret = 0;
    ret = wifi_getRadioCarrierSenseThresholdInUse(radioIndex,output);
    if (ret != 0) {
		CcspWifiTrace(("RDK_LOG_ERROR,%s :wifi_getRadioCarrierSenseThresholdInUse returned fail response\n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
	return ANSC_STATUS_SUCCESS;
}
ANSC_STATUS 
CosaDmlWiFi_setRadioCarrierSenseThresholdInUse(INT radioIndex, INT threshold)
{
    int ret = 0;
    ret = wifi_setRadioCarrierSenseThresholdInUse(radioIndex,threshold);
    if (ret != 0) {
		CcspWifiTrace(("RDK_LOG_ERROR,%s :wifi_setRadioCarrierSenseThresholdInUse returned fail response\n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS 
CosaDmlWiFi_getRadioBeaconPeriod(INT radioIndex, UINT *output)
{
    int ret = 0;
    ret = wifi_getRadioBeaconPeriod(radioIndex,output);
    if (ret != 0) {
		CcspWifiTrace(("RDK_LOG_ERROR,%s :wifi_getRadioBeaconPeriod returned fail response\n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
	return ANSC_STATUS_SUCCESS;
}
ANSC_STATUS 
CosaDmlWiFi_setRadioBeaconPeriod(INT radioIndex, UINT BeaconPeriod)
{
    int ret = 0;
    ret = wifi_setRadioBeaconPeriod(radioIndex,BeaconPeriod);
    if (ret != 0) {
		CcspWifiTrace(("RDK_LOG_ERROR,%s :wifi_setRadioBeaconPeriod returned fail response\n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
	return ANSC_STATUS_SUCCESS;
}

// LGI ADD - START
ANSC_STATUS
CosaDmlWiFi_getWpsStatus(INT apIndex, CHAR *output)
{
    int ret = 0;
    ret = wifi_getWpsStatus(apIndex,output);

    if (ret != 0) {
        CcspWifiTrace(("RDK_LOG_ERROR,\n%s :wifi_getWpsStatus returned fail response\n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
} // LGI ADD - END


ANSC_STATUS 
CosaDmlWiFi_getChanUtilThreshold(INT radioInstance, PUINT ChanUtilThreshold)
{
	char *strValue= NULL;
        char recName[256]={0};
	int   retPsmGet  = CCSP_SUCCESS;

        if (!g_wifidb_rfc) {
	memset(recName, 0, sizeof(recName));
	sprintf(recName, SetChanUtilThreshold, radioInstance);
	retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
	if (retPsmGet == CCSP_SUCCESS) {
        *ChanUtilThreshold =  _ansc_atoi(strValue);
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        } 
        } else {
            struct schema_Wifi_Radio_Config  *pcfg= NULL;
            char radio_name[16] = {0};
            if (convert_radio_to_name(radioInstance,radio_name) == 0) {
                pcfg = (struct schema_Wifi_Radio_Config  *) wifi_db_get_table_entry(radio_name, "radio_name",&table_Wifi_Radio_Config,OCLM_STR);
                if (pcfg != NULL) {
                    *ChanUtilThreshold = pcfg->chan_util_threshold;
                }
            }
        }
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_getChanUtilSelfHealEnable(INT radioInstance, ULONG *enable) {

	char *strValue= NULL;
        char recName[256]={0};
	int   retPsmGet  = CCSP_SUCCESS;

    if (!g_wifidb_rfc) {
	memset(recName, 0, sizeof(recName));
	sprintf(recName, SetChanUtilSelfHealEnable, radioInstance);
	retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
	if (retPsmGet == CCSP_SUCCESS) {
        *enable =  _ansc_atoi(strValue);
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        }
    } else {
        struct schema_Wifi_Radio_Config  *pcfg= NULL;
        char radio_name[16] = {0};
        if (convert_radio_to_name(radioInstance,radio_name) == 0) {
            pcfg = (struct schema_Wifi_Radio_Config  *) wifi_db_get_table_entry(radio_name, "radio_name",&table_Wifi_Radio_Config,OCLM_STR);
            if (pcfg != NULL) {
                *enable = pcfg->chan_util_selfheal_enable;
            }
        }
    } 
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS 
CosaDmlWiFi_setChanUtilThreshold(INT radioInstance, UINT ChanUtilThreshold)
{
	char strValue[10]={0};
        char recName[256]={0};
	wifiDbgPrintf("%s\n",__FUNCTION__);
       sprintf(recName, SetChanUtilThreshold, radioInstance);
   	sprintf(strValue,"%d",ChanUtilThreshold);
       PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue); 

    if (g_wifidb_rfc) {
        struct schema_Wifi_Radio_Config  *pcfg= NULL;
        char radio_name[16] = {0};
        if (convert_radio_to_name(radioInstance,radio_name) == 0) {
            pcfg = (struct schema_Wifi_Radio_Config  *) wifi_db_get_table_entry(radio_name, "radio_name",&table_Wifi_Radio_Config,OCLM_STR);
            if (pcfg != NULL) {
                pcfg->chan_util_threshold = ChanUtilThreshold;
                if (wifi_ovsdb_update_table_entry(radio_name,"radio_name",OCLM_STR,&table_Wifi_Radio_Config,pcfg,filter_radio) <= 0) {
                    CcspTraceError(("%s: WIFI DB Failed to update WIFI DB Radio config\n",__FUNCTION__));
                }
            }
        }
    }
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_setChanUtilSelfHealEnable(INT radioInstance, UINT enable) {
	char strValue[10]={0};
        char recName[256]={0};

	wifiDbgPrintf("%s\n",__FUNCTION__);
       sprintf(recName, SetChanUtilSelfHealEnable, radioInstance);
   	sprintf(strValue,"%d",enable);
       PSM_Set_Record_Value2(bus_handle,g_Subsystem, recName, ccsp_string, strValue); 

    if (g_wifidb_rfc) {
        struct schema_Wifi_Radio_Config  *pcfg= NULL;
        char radio_name[16] = {0};
        if (convert_radio_to_name(radioInstance,radio_name) == 0) {
            pcfg = (struct schema_Wifi_Radio_Config  *) wifi_db_get_table_entry(radio_name, "radio_name",&table_Wifi_Radio_Config,OCLM_STR);
            if (pcfg != NULL) {
                pcfg->chan_util_selfheal_enable = enable;
                if (wifi_ovsdb_update_table_entry(radio_name,"radio_name",OCLM_STR,&table_Wifi_Radio_Config,pcfg,filter_radio) <= 0) {
                    CcspTraceError(("%s: WIFI DB Failed to update WIFI DB Radio config\n",__FUNCTION__));
                }
            }
        }
    }
	return ANSC_STATUS_SUCCESS;
}


ANSC_STATUS
ChannelUtil_SelfHeal_Notification(char *event, char *data) 
{
#define COSA_DML_WIFI_SELF_HEAL_DISABLE    0
#define COSA_DML_WIFI_SELF_HEAL_RESET  2
#define COSA_DML_WIFI_SELF_HEAL_BEST_EFFORT    1

#define SELF_HEAL_ACTION_RADIO_RESET 1000

   unsigned int radioIndex, channel;
   ULONG selfHealType = 0;

   if (strncmp(event, "wifi_ChannelUtilHeal", strlen("wifi_ChannelUtilHeal")) != 0) {
       return ANSC_STATUS_FAILURE;
   }
               
   sscanf(data, "%d %d", &radioIndex, &channel);
   CosaDmlWiFi_getChanUtilSelfHealEnable(radioIndex, &selfHealType);   

   if (selfHealType != COSA_DML_WIFI_SELF_HEAL_DISABLE) {
       if (selfHealType == COSA_DML_WIFI_SELF_HEAL_RESET) {
           CcspWifiTrace(("RDK_LOG_INFO,%s:%d:Resetting WiFi radio:%d\n",__func__, __LINE__, radioIndex)); // reset radio
       } else if (selfHealType == COSA_DML_WIFI_SELF_HEAL_BEST_EFFORT) {

           if (channel == SELF_HEAL_ACTION_RADIO_RESET) {
               CcspWifiTrace(("RDK_LOG_INFO,%s:%d:Resetting WiFi radio:%d in best effort\n",__func__, __LINE__, radioIndex)); // reset radio

           } else {
               CcspWifiTrace(("RDK_LOG_INFO,%s:%d:Channel change to %d on WiFi radio:%d in best effort\n",__func__, __LINE__, channel, radioIndex));
           }
       }

   }

   return ANSC_STATUS_SUCCESS;
}

//zqiu: for RDKB-3346
/*
ANSC_STATUS 
CosaDmlWiFi_getRadioBasicDataTransmitRates(INT radioIndex, ULONG *output)
{
    int ret = 0;
	char sTransmitRates[128]="";
	ret = wifi_getRadioBasicDataTransmitRates(radioIndex,sTransmitRates);
    if (ret != 0) {
		CcspWifiTrace(("RDK_LOG_ERROR,\n%s :wifi_getRadioBasicDataTransmitRates returned fail response\n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
	if(AnscEqualString(sTransmitRates, "Default", TRUE))
	{
		*output = 1;
	}
	else if(AnscEqualString(sTransmitRates, "1-2Mbps", TRUE))
	{
		*output = 2;
	}
	else if(AnscEqualString(sTransmitRates, "All", TRUE))
	{
		*output = 3;
	}
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS 
CosaDmlWiFi_setRadioBasicDataTransmitRates(INT radioIndex,ULONG val)
{
    int ret = 0;
	char sTransmitRates[128] = "Default";
	if(val == 2)
		strcpy(sTransmitRates,"1-2Mbps");
	else if(val == 3)
		strcpy(sTransmitRates,"All");
    ret = wifi_setRadioBasicDataTransmitRates(radioIndex,sTransmitRates);
    if (ret != 0) {
		CcspWifiTrace(("RDK_LOG_ERROR,\n%s :wifi_setRadioBasicDataTransmitRates returned fail response\n",__FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }
	return ANSC_STATUS_SUCCESS;
}
*/


ANSC_STATUS 
CosaDmlWiFi_getRadioStatsRadioStatisticsMeasuringRate(INT radioInstanceNumber, INT *output)
{
    int ret = 0;
	char record[256]="";
	char *strValue=NULL;
	if (!output) return ANSC_STATUS_FAILURE;

    if (!g_wifidb_rfc) {
	sprintf(record, MeasuringRateRd, radioInstanceNumber);
	ret = PSM_Get_Record_Value2(bus_handle,g_Subsystem, record, NULL, &strValue);
    if (ret != CCSP_SUCCESS) {
		CcspWifiTrace(("RDK_LOG_ERROR,%s : get %s fail\n",__FUNCTION__, record));
		return ANSC_STATUS_FAILURE;
	}
	*output = _ansc_atoi(strValue);        
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    } else {
        struct schema_Wifi_Radio_Config  *pcfg= NULL;
        char radio_name[16] = {0};
        if (convert_radio_to_name(radioInstanceNumber,radio_name) == 0) {
            pcfg = (struct schema_Wifi_Radio_Config  *) wifi_db_get_table_entry(radio_name, "radio_name",&table_Wifi_Radio_Config,OCLM_STR);
            if (pcfg != NULL) {
                *output = pcfg->radio_stats_measuring_rate;
            } else {
                CcspWifiTrace(("RDK_LOG_ERROR,%s WIFI DB Raio config get fail\n",__FUNCTION__));
                return ANSC_STATUS_FAILURE;
            }
        }
    }
    return ANSC_STATUS_SUCCESS;
	//return wifi_getRadioStatsRadioStatisticsMeasuringRate(radioInstanceNumber-1,output);
    
}

ANSC_STATUS 
CosaDmlWiFi_setRadioStatsRadioStatisticsMeasuringRate(INT radioInstanceNumber, INT rate)
{
    //int ret = 0;
	char record[256]="";
	char verString[32]="";
	
	//if ( wifi_setRadioStatsRadioStatisticsMeasuringRate(radioInstanceNumber-1,rate) != 0) 
    //    return ANSC_STATUS_FAILURE;
	
	sprintf(record, MeasuringRateRd, radioInstanceNumber);
	sprintf(verString, "%d",rate);
	PSM_Set_Record_Value2(bus_handle,g_Subsystem, record, ccsp_string, verString);

    if (g_wifidb_rfc) {
        struct schema_Wifi_Radio_Config  *pcfg= NULL;
        char radio_name[16] = {0};
        if (convert_radio_to_name(radioInstanceNumber,radio_name) == 0) {
            pcfg = (struct schema_Wifi_Radio_Config  *) wifi_db_get_table_entry(radio_name, "radio_name",&table_Wifi_Radio_Config,OCLM_STR);
            if (pcfg != NULL) {
               CcspWifiTrace(("RDK_LOG_INFO,%s:%d: Current measuring rate %d\n",__func__, __LINE__,pcfg->radio_stats_measuring_rate));
               pcfg->radio_stats_measuring_rate = rate;
               int retval = wifi_ovsdb_update_table_entry(radio_name,"radio_name",OCLM_STR,&table_Wifi_Radio_Config,pcfg,filter_radio);
               CcspWifiTrace(("RDK_LOG_INFO,%s:%d: updated wifidb retval %d\n",__func__, __LINE__,retval));
            }
        }
    }
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS 
CosaDmlWiFi_getRadioStatsRadioStatisticsMeasuringInterval(INT radioInstanceNumber, INT *output)
{
	int ret = 0;
	char record[256]="";
	char *strValue=NULL;
	if (!output) return ANSC_STATUS_FAILURE;

    if (!g_wifidb_rfc) {
	sprintf(record, MeasuringIntervalRd, radioInstanceNumber);
	ret = PSM_Get_Record_Value2(bus_handle,g_Subsystem, record, NULL, &strValue);
    if (ret != CCSP_SUCCESS) {
		CcspWifiTrace(("RDK_LOG_ERROR,%s : get %s fail\n",__FUNCTION__, record));
		return ANSC_STATUS_FAILURE;
	}
	*output = _ansc_atoi(strValue);        
	((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    } else {
        struct schema_Wifi_Radio_Config  *pcfg= NULL;
        char radio_name[16] = {0};
        if (convert_radio_to_name(radioInstanceNumber,radio_name) == 0) {
            pcfg = (struct schema_Wifi_Radio_Config  *) wifi_db_get_table_entry(radio_name, "radio_name",&table_Wifi_Radio_Config,OCLM_STR);
            if (pcfg != NULL) {
                *output = pcfg->radio_stats_measuring_interval;
            } else {
                CcspWifiTrace(("RDK_LOG_ERROR,%s WIFI DB Radio config get fail\n",__FUNCTION__));
                return ANSC_STATUS_FAILURE;
            }
        }
    }
    return ANSC_STATUS_SUCCESS;
   //return wifi_getRadioStatsRadioStatisticsMeasuringInterval(radioInstanceNumber-1,output);   
}

ANSC_STATUS 
CosaDmlWiFi_setRadioStatsRadioStatisticsMeasuringInterval(INT radioInstanceNumber, INT rate)
{
    //int ret = 0;
	char record[256]="";
	char verString[32];
    //if (wifi_setRadioStatsRadioStatisticsMeasuringInterval(radioInstanceNumber-1,rate) != 0) 
    //    return ANSC_STATUS_FAILURE;
	
	sprintf(record, MeasuringIntervalRd, radioInstanceNumber);
    sprintf(verString, "%d",rate);
	PSM_Set_Record_Value2(bus_handle,g_Subsystem, record, ccsp_string, verString);
    
    if (g_wifidb_rfc) {
        struct schema_Wifi_Radio_Config  *pcfg= NULL;
        char radio_name[16] = {0};
        if (convert_radio_to_name(radioInstanceNumber,radio_name) == 0) {
            pcfg = (struct schema_Wifi_Radio_Config  *) wifi_db_get_table_entry(radio_name, "radio_name",&table_Wifi_Radio_Config,OCLM_STR);
            if (pcfg != NULL) {
                pcfg->radio_stats_measuring_interval = rate;
                if (wifi_ovsdb_update_table_entry(radio_name,"radio_name",OCLM_STR,&table_Wifi_Radio_Config,pcfg,filter_radio) <= 0) {
                    CcspTraceError(("%s: WIFI DB Failed to update WIFI DB Radio config\n",__FUNCTION__));
                }
            }
        }
    }
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS 
CosaDmlWiFi_resetRadioStats(PCOSA_DML_WIFI_RADIO_STATS    pWifiStats)
{
    if (!pWifiStats) return ANSC_STATUS_FAILURE;
    memset(pWifiStats->RslInfo.ReceivedSignalLevel, 0, sizeof(INT)*RSL_MAX);
	pWifiStats->RslInfo.Count=pWifiStats->RadioStatisticsMeasuringInterval/pWifiStats->RadioStatisticsMeasuringRate;
	pWifiStats->RslInfo.StartIndex=0;
	
	pWifiStats->StatisticsStartTime=AnscGetTickInSeconds();
    
	return ANSC_STATUS_SUCCESS;
}



ANSC_STATUS 
CosaDmlWiFi_getRadioStatsReceivedSignalLevel(INT radioInstanceNumber, INT *iRsl)
{
	wifi_getRadioStatsReceivedSignalLevel(radioInstanceNumber-1,0,iRsl);
	return ANSC_STATUS_SUCCESS;
}



ANSC_STATUS
CosaDmlWiFiRadioStatsSet
(
int     InstanceNumber,
PCOSA_DML_WIFI_RADIO_STATS    pWifiRadioStats        
)
{
	wifi_radioTrafficStatsMeasure_t measure;

    wifiDbgPrintf("%s Config changes  \n",__FUNCTION__);
    int  radioIndex;
    
	CcspWifiTrace(("RDK_LOG_WARN,%s\n",__FUNCTION__));
    if (!pWifiRadioStats )
    {
        return ANSC_STATUS_FAILURE;
    }

    
    radioIndex = InstanceNumber-1;

    //CosaDmlWiFiSetRadioPsmData(pWifiRadioStats, wlanIndex, pCfg->InstanceNumber);
	if(pWifiRadioStats->RadioStatisticsEnable) {
		measure.radio_RadioStatisticsMeasuringRate=pWifiRadioStats->RadioStatisticsMeasuringRate;
		measure.radio_RadioStatisticsMeasuringInterval=pWifiRadioStats->RadioStatisticsMeasuringInterval;
		CosaDmlWiFi_resetRadioStats(pWifiRadioStats);
		wifi_setRadioTrafficStatsMeasure(radioIndex, &measure); 
		
	}
	wifi_setRadioTrafficStatsRadioStatisticsEnable(radioIndex, pWifiRadioStats->RadioStatisticsEnable);

	return ANSC_STATUS_SUCCESS;
}


pthread_mutex_t sNeighborScanThreadMutex = PTHREAD_MUTEX_INITIALIZER;




//CosaDmlWiFi_doNeighbouringScan ( PCOSA_DML_NEIGHTBOURING_WIFI_RESULT *ppNeighScanResult, unsigned int *pResCount ) 
void * CosaDmlWiFi_doNeighbouringScanThread (void *input) 
{
    if (!input) return NULL ;
	PCOSA_DML_NEIGHTBOURING_WIFI_DIAG_CFG pNeighScan=input;
    PCOSA_DML_NEIGHTBOURING_WIFI_RESULT tmp_2, tmp_5;
	wifi_neighbor_ap2_t *wifiNeighbour_2=NULL, *wifiNeighbour_5=NULL;
	unsigned int count_2=0, count_5=0;
    INT n2Status = RETURN_ERR, n5Status = RETURN_ERR;
    
	n2Status = wifi_getNeighboringWiFiDiagnosticResult2(0, &wifiNeighbour_2,&count_2);	
	n5Status = wifi_getNeighboringWiFiDiagnosticResult2(1, &wifiNeighbour_5,&count_5);	
		

fprintf(stderr, "-- %s %d count_2=%d count_5=%d\n", __func__, __LINE__,  count_2, count_5);	
	printf("%s Calling pthread_mutex_lock for sNeighborScanThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 
    pthread_mutex_lock(&sNeighborScanThreadMutex);
    printf("%s Called pthread_mutex_lock for sNeighborScanThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 
    pNeighScan->ResultCount=0;
	
	if(count_2 > 0 && n2Status == RETURN_OK) {
		tmp_2=pNeighScan->pResult_2;
		pNeighScan->pResult_2=(PCOSA_DML_NEIGHTBOURING_WIFI_RESULT)wifiNeighbour_2;
		pNeighScan->ResultCount_2=count_2;
		if(tmp_2) {
			free(tmp_2);
                        tmp_2 = NULL;
        }	
    } else if (wifiNeighbour_2 != NULL) {
        // the count is greater than 0, but we have corrupted data in the structure
        CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s 2.4GHz NeighborScan data is corrupted - dropping\n",__FUNCTION__));
        if(wifiNeighbour_2)
        {
            free(wifiNeighbour_2);
            wifiNeighbour_2 = NULL;
        }
    }
	if(count_5 > 0 && n5Status == RETURN_OK) {
		tmp_5=pNeighScan->pResult_5;
		pNeighScan->pResult_5=(PCOSA_DML_NEIGHTBOURING_WIFI_RESULT)wifiNeighbour_5;
		pNeighScan->ResultCount_5=count_5;
		if(tmp_5) { 
			free(tmp_5);
                        tmp_5 = NULL;
        }	
	} else if (wifiNeighbour_5 != NULL) {
        // the count is greater than 0, but we have corrupted data in the structure
        CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s 5GHz NeighborScan data is corrupted - dropping\n",__FUNCTION__));
        if(wifiNeighbour_5)
        {
            free (wifiNeighbour_5);
            wifiNeighbour_5 = NULL;
        }
    }
    pNeighScan->ResultCount=pNeighScan->ResultCount_2+pNeighScan->ResultCount_5;
    pNeighScan->ResultCount=(pNeighScan->ResultCount<=250)?pNeighScan->ResultCount:250;

	AnscCopyString(pNeighScan->DiagnosticsState, "Completed");
	
	printf("%s Calling pthread_mutex_unlock for sNeighborScanThreadMutex  %d \n",__FUNCTION__ , __LINE__ ); 
    pthread_mutex_unlock(&sNeighborScanThreadMutex);
    printf("%s Called pthread_mutex_unlock for sNeighborScanThreadMutex  %d \n",__FUNCTION__ , __LINE__ );  
    return NULL;
}

ANSC_STATUS 
CosaDmlWiFi_doNeighbouringScan ( PCOSA_DML_NEIGHTBOURING_WIFI_DIAG_CFG pNeighScan)
{
    if (!pNeighScan) return ANSC_STATUS_FAILURE;
	fprintf(stderr, "-- %s1Y %d\n", __func__, __LINE__);
	wifiDbgPrintf("%s\n",__FUNCTION__);
	pthread_t tid; 
	AnscCopyString(pNeighScan->DiagnosticsState, "Requested");
        pthread_attr_t attr;
        pthread_attr_t *attrp = NULL;

        attrp = &attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
        if(pthread_create(&tid,attrp,CosaDmlWiFi_doNeighbouringScanThread, (void*)pNeighScan))  {
                if(attrp != NULL)
                    pthread_attr_destroy( attrp );
		AnscCopyString(pNeighScan->DiagnosticsState, "Error");
		return ANSC_STATUS_FAILURE;
	}
        if(attrp != NULL)
            pthread_attr_destroy( attrp );
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS 
CosaDmlWiFi_RadioGetResetCount(INT radioIndex, ULONG *output)
{
	CcspWifiTrace(("RDK_LOG_INFO,**** CosaDmlWiFi_RadioGetResetCoun : Entry **** \n"));
	wifi_getRadioResetCount(radioIndex,output);

	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS 
CosaDmlWiFi_GetBandSteeringOptions(PCOSA_DML_WIFI_BANDSTEERING_OPTION  pBandSteeringOption)
{
	if( NULL != pBandSteeringOption )
	{
		BOOL  support = FALSE,
			  enable  = FALSE;
		CHAR apgroup[COSA_DML_WIFI_MAX_BAND_STEERING_APGROUP_STR_LEN] = "1,2";

#if defined(_ENABLE_BAND_STEERING_)
		//To get Band Steering enable status
		wifi_getBandSteeringEnable( &enable );

		/*
		  * Check whether BS enable. If enable then we have to consider some cases for ACL
		  */
		if( TRUE == enable )
		{
			char *strValue	 = NULL;
			char  recName[ 256 ];
			int   retPsmGet  = CCSP_SUCCESS,
				  mode_24G	 = 0,
				  mode_5G	 = 0;
                        struct schema_Wifi_VAP_Config  *pcfg= NULL;
			//MacFilter mode for private 2.4G
                        if (!g_wifidb_rfc) {
			memset ( recName, 0, sizeof( recName ) );
			sprintf( recName, MacFilterMode, 1 );
			retPsmGet = PSM_Get_Record_Value2( bus_handle, g_Subsystem, recName, NULL, &strValue );
			if ( CCSP_SUCCESS == retPsmGet ) 
			{
				mode_24G = _ansc_atoi( strValue );
				((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
			}
			} else {
                            pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[0], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
                            if (pcfg != NULL) {
                                if (pcfg->mac_filter_enabled && pcfg->mac_filter_mode == wifi_mac_filter_mode_black_list) {
                                    mode_24G = 2;
                                } else if (pcfg->mac_filter_enabled && pcfg->mac_filter_mode == wifi_mac_filter_mode_white_list) {
                                    mode_24G = 1;
                                } else {
                                    mode_24G = 0;
                                }
                            }
                        }
			//MacFilter mode for private 5G
                        if (!g_wifidb_rfc) {
			memset ( recName, 0, sizeof( recName ) );
			sprintf( recName, MacFilterMode, 2 );
			retPsmGet = PSM_Get_Record_Value2( bus_handle, g_Subsystem, recName, NULL, &strValue );
			if ( CCSP_SUCCESS == retPsmGet ) 
			{
				mode_5G = _ansc_atoi( strValue );
				((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
			}
		       	} else {
                            pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[1], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
                            if (pcfg != NULL) {
                                if (pcfg->mac_filter_enabled && pcfg->mac_filter_mode == wifi_mac_filter_mode_black_list) {
                                    mode_5G = 2;
                                } else if (pcfg->mac_filter_enabled && pcfg->mac_filter_mode == wifi_mac_filter_mode_white_list) {
                                    mode_5G = 1;
                                } else {
                                    mode_5G = 0;
                                }
                            }
                        }

			/*
			  * Any one of private wifi ACL mode enabled then don't allow to enable BS
			  */
			if( ( 0 != mode_24G ) || \
				( 0 != mode_5G ) 
			  )
			{
				wifi_setBandSteeringEnable( FALSE );
				enable = FALSE;
			}
		}
#endif
		pBandSteeringOption->bEnable 	= enable;			

#if defined(_ENABLE_BAND_STEERING_)
		//To get Band Steering Capability
		wifi_getBandSteeringCapability( &support );
#endif
		pBandSteeringOption->bCapability = support;

#if defined(_ENABLE_BAND_STEERING_)
		//To get Band Steering ApGroup
		wifi_getBandSteeringApGroup( apgroup );
#endif
		AnscCopyString(pBandSteeringOption->APGroup, apgroup);
	}

	return ANSC_STATUS_SUCCESS;
}


ANSC_STATUS 
CosaDmlWiFi_GetBandSteeringLog(CHAR *BandHistory, INT TotalNoOfChars)
{
	INT    record_index=0;
	ULONG  SteeringTime = 0;     
	INT    SourceSSIDIndex = 0, 
		   DestSSIDIndex = 0, 
		   SteeringReason = 0;
	CHAR  ClientMAC[ 48 ] = {0};
	CHAR  band_history_for_one_record[ 96 ] = {0};

   //Records is hardcoded now. This can be changed according to requirement.		  
    int NumOfRecords = 10;		  	  
    int ret = 0;		  
    if (!BandHistory) return ANSC_STATUS_FAILURE;

	// To take BandSteering History for 10 records 
	memset( BandHistory, 0, TotalNoOfChars );

#if defined(_ENABLE_BAND_STEERING_)
	while( !ret)
	{
		SteeringTime    = 0; 
		SourceSSIDIndex = 0; 
		DestSSIDIndex   = 0; 
		SteeringReason  = 0;

		memset( ClientMAC, 0, sizeof( ClientMAC ) );
		memset( band_history_for_one_record, 0, sizeof( band_history_for_one_record ) );		
		
		//Steering history
		ret = wifi_getBandSteeringLog( record_index, 
								 &SteeringTime, 
								 ClientMAC, 
								 &SourceSSIDIndex, 
								 &DestSSIDIndex, 
								 &SteeringReason );
				
				
		//Entry not fund		
		if (ret != 0) {
			//return ANSC_STATUS_SUCCESS;
                        break;
		}						 
		++record_index;						 
				 
	}
        --record_index;
	//strcat( BandHistory, "\n");
	while(record_index >=0 && NumOfRecords >0)
	{
		ret = wifi_getBandSteeringLog( record_index, 
								 &SteeringTime, 
								 ClientMAC, 
								 &SourceSSIDIndex, 
								 &DestSSIDIndex, 
								 &SteeringReason );
				
				
		//Entry not fund		
		if (ret != 0) {
			//return ANSC_STATUS_SUCCESS;
                        break;
		}						 
		snprintf( band_history_for_one_record, sizeof( band_history_for_one_record )-1,
				 "\n%lu|%s|%d|%d|%d",
				 SteeringTime,
				 ClientMAC,
				 SourceSSIDIndex,
				 DestSSIDIndex,
				 SteeringReason );
		if((strlen(BandHistory)+strlen(band_history_for_one_record)) < (ULONG)TotalNoOfChars)
		strcat( BandHistory, band_history_for_one_record);
                --NumOfRecords;
                --record_index;
	}
#endif	
	return ANSC_STATUS_SUCCESS;
}

#if defined(_ENABLE_BAND_STEERING_)
ANSC_STATUS 
CosaDmlWiFi_GetBandSteeringLog_2()
{
	INT    record_index;
	ULONG  SteeringTime = 0;     
	INT    SourceSSIDIndex = 0, 
		   DestSSIDIndex = 0, 
		   SteeringReason = 0;
	CHAR  ClientMAC[ 48] = {0};
	CHAR  band_history_for_one_record[512] = {0};
        CHAR buf[96];
        struct tm tmlocal;
    	struct timeval timestamp;
    	int ret = 0;		  

    	record_index=0;
    	while(!ret)	
    	{
		SteeringTime    = 0; 
		SourceSSIDIndex = 0; 
		DestSSIDIndex   = 0; 
		SteeringReason  = 0;

		memset( ClientMAC, 0, sizeof( ClientMAC ) );
		memset( band_history_for_one_record, 0, sizeof( band_history_for_one_record ) );		
		
	//Steering history
		ret = wifi_getBandSteeringLog( record_index, 
					&SteeringTime, 
					ClientMAC, 
					&SourceSSIDIndex, 
					&DestSSIDIndex, 
					&SteeringReason );
				
	
        	if(ret !=0) return ANSC_STATUS_SUCCESS;

                memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf), "%ld", SteeringTime);
    		strptime(buf, "%s", &tmlocal);
    		strftime(buf, sizeof(buf), "%b %d %H:%M %Y", &tmlocal);
    		gettimeofday(&timestamp, NULL);
     		ULONG currentTime= (ULONG)timestamp.tv_sec;
    		if(currentTime <= (SteeringTime+BandsteerLoggingInterval))
    		{
			snprintf( band_history_for_one_record, sizeof(band_history_for_one_record),
				 "Time %s | MAC %s | From Band %d | To Band %d | Reason %d\n",
				 buf,
				 ClientMAC,
				 SourceSSIDIndex,
				 DestSSIDIndex,
				 SteeringReason);
			fprintf(stderr, "steer time is%s \n", buf);
			fprintf(stderr, "steer record: %s \n", band_history_for_one_record);
			CcspWifiTrace(("RDK_LOG_INFO, WIFI :SteerRecord: %s \n",band_history_for_one_record));
     		}
		record_index++;
	}
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS 
CosaDmlWiFi_GetBandSteeringLog_3()
{
	CHAR   pOutput_string_2[48] = {0};
        CHAR   pOutput_string_5[48] = {0};
        CHAR   tmp[96];
        CHAR   tmp1[96];
    	int ret = 0;		  
	char *buf=NULL;
	char *buf2=NULL;
	char *pos_2 = NULL;
	char *pos_5 = NULL;
        char *pos_stadb = NULL;
    	char *pos_end = NULL;
        int counter=0;
	CHAR   bandsteering_MAC_1[512] = {0};
	CHAR   bandsteering_RSSI_1[256] = {0};
	CHAR   bandsteering_MAC_2[512] = {0};
	CHAR   bandsteering_RSSI_2[256] = {0};
	CHAR   band_mac[16] ={0};
        CHAR   rssi[96]     ={0};
        CHAR   band_mac_one_record[128]     ={0};
        wifi_associated_dev_t *wifi_associated_dev_array = NULL;
        UINT array_size=0;
	BOOL enable;

	ret=wifi_getBandSteeringEnable( &enable );
        
	if(!ret && enable)
        {

		if(syscfg_executecmd(__func__,"nc 127.0.0.1 7787 <<< \"bandmon s \" ", &buf))
		{
			if(buf) 
			{
				free(buf);
				buf = NULL;
			}
			return ANSC_STATUS_SUCCESS;
		}

		if (buf != NULL)
		{
			pos_2 = buf;
			if((pos_2=strstr(pos_2,"Utilization 2.4 GHz: "))!=NULL)
			{
				pos_2 +=21;
				while(*pos_2==' ') pos_2=pos_2+1;
				if (( pos_end = strchr(pos_2, '%')))
				{
					memset(pOutput_string_2, 0 , sizeof(pOutput_string_2));
					if (sizeof(pOutput_string_2) >= (ULONG)(pos_end-pos_2)) 
					{ 
						memcpy(pOutput_string_2, pos_2, pos_end - pos_2);
						pOutput_string_2[pos_end-pos_2]='\0';
						
					}
				}
			}
			else
			{
				memset(pOutput_string_2, 0 , sizeof(pOutput_string_2));
			}
			pos_5 = buf;
			if((pos_5=strstr(pos_5,"Utilization 5 GHz: "))!=NULL)
			{
				pos_5 +=19;
				while(*pos_5==' ') pos_5=pos_5+1;
				pos_end=NULL;
				if (( pos_end = strchr(pos_5, '%')))
				{
					memset(pOutput_string_5, 0 , sizeof(pOutput_string_5));
					if (sizeof(pOutput_string_5) >= (ULONG)(pos_end-pos_5))  
					{
						memcpy(pOutput_string_5, pos_5, pos_end - pos_5);
						pOutput_string_5[pos_end-pos_5]='\0';
					}
				}
			}
			else
			{
				memset(pOutput_string_5, 0 , sizeof(pOutput_string_5));
			}

			if(buf)
			{
				free(buf);
				buf=NULL;
			}
		}

		fprintf(stderr, "2.4 Ghz Band Utilization: %s %% \n", pOutput_string_2);
		fprintf(stderr, "5 Ghz Band Utilization: %s %% \n", pOutput_string_5);
		CcspWifiTrace(("RDK_LOG_WARN, WIFI WIFI_BandUtilization_2.4_GHz:%s\n",pOutput_string_2));
		CcspWifiTrace(("RDK_LOG_WARN, WIFI WIFI_BandUtilization_5_GHz:%s\n",pOutput_string_5));
		
		if(syscfg_executecmd(__func__,"nc 127.0.0.1 7787 <<< \"stadb s in \" ", &buf2))
		{
			if(buf2) 
			{
				free(buf2);
				buf2 = NULL;
			}
			return ANSC_STATUS_SUCCESS;
		}
		 
		pos_stadb = buf2;
		while(pos_stadb!=NULL && counter<10 && (strlen(pos_stadb) >=126)) 
		{
			if((pos_stadb=strstr(pos_stadb,":"))!=NULL)
			{
				pos_stadb =pos_stadb -2;
				memset(tmp, 0 , sizeof(tmp));
				if(strlen(pos_stadb)>=126)
				{
					memcpy(tmp, pos_stadb, 17);
					pos_stadb=pos_stadb+93;
					memset(tmp1, 0 , sizeof(tmp1));
					memcpy(tmp1, pos_stadb, 16);
				}
				if((pos_end=strstr(tmp1,"2   (")) || (pos_end=strstr(tmp1,"5   (")))
				{
					CcspWifiTrace(("RDK_LOG_WARN,WIFI MAC %s Associated to: band(connection time) %s \n",tmp,tmp1));
					fprintf(stderr, "MAC %s Associated to: band(connection time) %s \n",tmp,tmp1);
				}
				counter++;
			}
			else break;
		}
		if(buf2)
		{
			free(buf2);
			buf2=NULL;
		}
        }
		
        memset(bandsteering_MAC_1, 0 , sizeof(bandsteering_MAC_1));
        memset(bandsteering_RSSI_1, 0 , sizeof(bandsteering_RSSI_1));
        memset(band_mac_one_record, 0 , sizeof(band_mac_one_record));
	
        ret = wifi_getApAssociatedDeviceDiagnosticResult(0, &wifi_associated_dev_array, &array_size);
        if (!ret && wifi_associated_dev_array && array_size > 0)
        {
		unsigned int i;
		int j;
		wifi_associated_dev_t *ps = NULL;
                ps =(wifi_associated_dev_t *)wifi_associated_dev_array;
		for (i = 0; i < array_size; i++, ps++)
		{
        		memset(band_mac_one_record, 0 , sizeof(band_mac_one_record));
			memset(band_mac, 0 , sizeof(band_mac));
	     		for (j = 0; j < 6; j++)
			{
				snprintf( band_mac, sizeof(band_mac),
					 "%02x",ps->cli_MACAddress[j]);
				if((strlen(band_mac_one_record)+strlen(band_mac))<128)
				{
					if(strlen(band_mac_one_record)) strcat( band_mac_one_record, ":");
					strcat( band_mac_one_record, band_mac);
				}
	    		}
			if((strlen(bandsteering_MAC_1)+strlen(band_mac_one_record))<512)
			{
				if(strlen(bandsteering_MAC_1)) strcat( bandsteering_MAC_1, ",");
				strcat( bandsteering_MAC_1, band_mac_one_record);
			}
			snprintf( rssi, sizeof(rssi),
				"%d",ps->cli_RSSI);
			if((strlen(bandsteering_RSSI_1)+strlen(rssi))<256)
			{
				if(strlen(bandsteering_RSSI_1)) strcat( bandsteering_RSSI_1, ",");
				strcat( bandsteering_RSSI_1, rssi);
			}
		}

		if (array_size)
		{
			CcspWifiTrace(("RDK_LOG_WARN,WIFI_MAC_1:%s\n",bandsteering_MAC_1));
                        t2_event_s("2GclientMac_split", bandsteering_MAC_1);
			CcspWifiTrace(("RDK_LOG_WARN,WIFI_RSSI_1:%s\n",bandsteering_RSSI_1));
                        t2_event_s("2GRSSI_split", bandsteering_RSSI_1);
		}
        }
                 
     	if(wifi_associated_dev_array)
	{
		free(wifi_associated_dev_array);
		wifi_associated_dev_array = NULL;
	}
        array_size=1;
     	memset(bandsteering_MAC_2, 0 , sizeof(bandsteering_MAC_2));
     	memset(bandsteering_RSSI_2, 0 , sizeof(bandsteering_RSSI_2));
     	memset(band_mac_one_record, 0 , sizeof(band_mac_one_record));
    	ret = wifi_getApAssociatedDeviceDiagnosticResult(1, &wifi_associated_dev_array, &array_size);
    	if (!ret && wifi_associated_dev_array && array_size > 0)
    	{
        	unsigned int i; 
        	int j;
        	wifi_associated_dev_t *ps = NULL;
                ps=(wifi_associated_dev_t *)wifi_associated_dev_array;
        	for (i = 0; i < array_size; i++, ps++)
        	{
        		memset(band_mac_one_record, 0 , sizeof(band_mac_one_record));
                	memset(band_mac, 0 , sizeof(band_mac));
             		for (j = 0; j < 6; j++)
			{
				snprintf( band_mac, sizeof(band_mac),
					 "%02x",ps->cli_MACAddress[j]);
				if((strlen(band_mac_one_record)+strlen(band_mac))<128)
				{
					if(strlen(band_mac_one_record)) strcat( band_mac_one_record, ":");
					strcat( band_mac_one_record, band_mac);
				}
			}
			if((strlen(bandsteering_MAC_2)+strlen(band_mac_one_record))<512)
			{
				if(strlen(bandsteering_MAC_2)) strcat( bandsteering_MAC_2, ",");
				strcat( bandsteering_MAC_2, band_mac_one_record);
			}
			snprintf( rssi, sizeof(rssi),
				 "%d",ps->cli_RSSI);
			if((strlen(bandsteering_RSSI_2)+strlen(rssi))<256)
			{
				if(strlen(bandsteering_RSSI_2)) strcat( bandsteering_RSSI_2, ",");
				strcat( bandsteering_RSSI_2, rssi);
			}
		}

		if (array_size)
		{
			CcspWifiTrace(("RDK_LOG_WARN,WIFI_MAC_2:%s\n",bandsteering_MAC_2));
			t2_event_s("5GclientMac_split", bandsteering_MAC_2);
                        CcspWifiTrace(("RDK_LOG_WARN,WIFI_RSSI_2:%s\n",bandsteering_RSSI_2));
		        t2_event_s("5GRSSI_split", bandsteering_RSSI_2);
                }
     	}
     	if(wifi_associated_dev_array)
	{
		free(wifi_associated_dev_array);
		wifi_associated_dev_array = NULL;
	}
	return ANSC_STATUS_SUCCESS;
}
#endif

ANSC_STATUS 
CosaDmlWiFi_SetBandSteeringOptions(PCOSA_DML_WIFI_BANDSTEERING_OPTION  pBandSteeringOption)
{
#if defined(_ENABLE_BAND_STEERING_)
    if (!pBandSteeringOption) return ANSC_STATUS_FAILURE;
	
	/*
	  * Check whether call coming for BS enable. If enable then we have to 
	  * consider some cases for ACL
	  */
	if( TRUE == pBandSteeringOption->bEnable )
	{
		char *strValue	 = NULL;
		char  recName[ 256 ];
		int   retPsmGet  = CCSP_SUCCESS,
			  mode_24G	 = 0,
			  mode_5G	 = 0;
                struct schema_Wifi_VAP_Config  *pcfg= NULL;

                if (!g_wifidb_rfc) {
		//MacFilter mode for private 2.4G
		memset ( recName, 0, sizeof( recName ) );
		sprintf( recName, MacFilterMode, 1 );
		retPsmGet = PSM_Get_Record_Value2( bus_handle, g_Subsystem, recName, NULL, &strValue );
		if ( CCSP_SUCCESS == retPsmGet ) 
		{
			mode_24G = _ansc_atoi( strValue );
			((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
		}
		} else {
                    pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[0], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
                    if (pcfg != NULL) {
                        if (pcfg->mac_filter_enabled && pcfg->mac_filter_mode == wifi_mac_filter_mode_black_list) {
                            mode_24G = 2;
                        } else if (pcfg->mac_filter_enabled && pcfg->mac_filter_mode == wifi_mac_filter_mode_white_list) {
                            mode_24G = 1;
                        } else {
                            mode_24G = 0;
                        }
                    }
                }

                if (!g_wifidb_rfc) {
		//MacFilter mode for private 5G
		memset ( recName, 0, sizeof( recName ) );
		sprintf( recName, MacFilterMode, 2 );
		retPsmGet = PSM_Get_Record_Value2( bus_handle, g_Subsystem, recName, NULL, &strValue );
		if ( CCSP_SUCCESS == retPsmGet ) 
		{
			mode_5G = _ansc_atoi( strValue );
			((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc( strValue );
		}
		} else {
                    pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[1], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
                    if (pcfg != NULL) {
                        if (pcfg->mac_filter_enabled && pcfg->mac_filter_mode == wifi_mac_filter_mode_black_list) {
                            mode_5G = 2;
                        } else if (pcfg->mac_filter_enabled && pcfg->mac_filter_mode == wifi_mac_filter_mode_white_list) {
                            mode_5G = 1;
                        } else {
                            mode_5G = 0;
                        }
                    }
                }
		/*
		  * Any one of private wifi ACL mode enabled then don't allow to enable BS
		  */
		if( ( 0 != mode_24G ) || \
			( 0 != mode_5G ) 
		  )
		{
			pBandSteeringOption->bEnable = FALSE;
			return ANSC_STATUS_FAILURE;
		}
	}

	//To turn on/off Band steering
  
  wifi_setBandSteeringApGroup( pBandSteeringOption->APGroup );
  CcspWifiTrace(("RDK_LOG_INFO,BS_VAPPAIR_SUPERSET: '%s'\n",pBandSteeringOption->APGroup));
#if defined(_PLATFORM_RASPBERRYPI_) || defined(_PLATFORM_TURRIS_)
  if (wifi_setBandSteeringEnable( pBandSteeringOption->bEnable ) != RETURN_OK){
	pBandSteeringOption->bEnable = FALSE;
	return ANSC_STATUS_FAILURE;
  }
#else
  wifi_setBandSteeringEnable( pBandSteeringOption->bEnable );
#endif

#endif
	return ANSC_STATUS_SUCCESS;
}

#if defined(_ENABLE_BAND_STEERING_)
#if defined (_PLATFORM_RASPBERRYPI_) || defined(_PLATFORM_TURRIS_)
void *_Band_Switch( void *arg)
{
	pthread_detach(pthread_self());
	BOOL enable = FALSE;
	int radioIndex=(INT)arg;
	char interface_name[MAX_BUF_SIZE] = {0};
        char HConf_file[MAX_BUF_SIZE]={'\0'};
        char freqBand[10] = {0};

	wifi_getBandSteeringEnable( &enable );

	if (enable == TRUE){
		if((radioIndex == 0) || (radioIndex == 1)){
			sprintf(HConf_file,"%s%d%s","/nvram/hostapd",radioIndex,".conf");
			GetInterfaceName(interface_name,HConf_file);
			wifi_getRadioSupportedFrequencyBands(radioIndex, freqBand);
		}
		while(1){
			wifi_switchBand(interface_name,radioIndex,freqBand);
			sleep(20);
		}
	}
	pthread_exit(NULL);
}

void _wifi_eventCapture(void){
	pthread_detach(pthread_self());
	v_secure_system("iw event -f > /tmp/event_count.txt &");
	//pthread_exit(NULL);
}
#endif
#endif

ANSC_STATUS 
CosaDmlWiFi_GetBandSteeringSettings(int radioIndex, PCOSA_DML_WIFI_BANDSTEERING_SETTINGS pBandSteeringSettings)
{
	if( NULL != pBandSteeringSettings )
	{
		INT  PrThreshold   = 0,
			 RssiThreshold = 0,
			 BuThreshold   = 0,
			 OvrLdInactiveTime = 0,
			 IdlInactiveTime = 0;

	
		//to read the band steering physical modulation rate threshold parameters
		#if defined(_ENABLE_BAND_STEERING_)
		wifi_getBandSteeringPhyRateThreshold( radioIndex, &PrThreshold );
		#endif
		pBandSteeringSettings->PhyRateThreshold = PrThreshold;
		
		//to read the band steering band steering RSSIThreshold parameters
		#if defined(_ENABLE_BAND_STEERING_)
		wifi_getBandSteeringRSSIThreshold( radioIndex, &RssiThreshold );
		#endif
		pBandSteeringSettings->RSSIThreshold    = RssiThreshold;

		//to read the band steering BandUtilizationThreshold parameters 
		#if defined(_ENABLE_BAND_STEERING_)
		wifi_getBandSteeringBandUtilizationThreshold( radioIndex, &BuThreshold );
		#endif
		pBandSteeringSettings->UtilizationThreshold    = BuThreshold;

		//to read the band steering OverloadInactiveTime  parameters
		#if defined(_ENABLE_BAND_STEERING_)
		wifi_getBandSteeringOverloadInactiveTime( radioIndex, &OvrLdInactiveTime );
		#endif
		pBandSteeringSettings->OverloadInactiveTime    = OvrLdInactiveTime;

		//to read the band steering IdlInactiveTime parameters
		#if defined(_ENABLE_BAND_STEERING_)
		wifi_getBandSteeringIdleInactiveTime( radioIndex, &IdlInactiveTime );
		#endif
		pBandSteeringSettings->IdleInactiveTime    = IdlInactiveTime;

		// Take copy default band steering settings 
		memcpy( &sWiFiDmlBandSteeringStoredSettinngs[ radioIndex ], 
				pBandSteeringSettings, 
				sizeof( COSA_DML_WIFI_BANDSTEERING_SETTINGS ) );
#if defined(_ENABLE_BAND_STEERING_)
#if defined (_PLATFORM_RASPBERRYPI_) || defined(_PLATFORM_TURRIS_)
        	pthread_t bandSwitch,eventCount;
        	pthread_create(&bandSwitch, NULL, &_Band_Switch, (void*)radioIndex);
        	pthread_create(&eventCount, NULL, &_wifi_eventCapture, NULL);
#endif
#endif
	}
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetBandSteeringSettings(int radioIndex, PCOSA_DML_WIFI_BANDSTEERING_SETTINGS pBandSteeringSettings)
{
	int ret=0;
	if( NULL != pBandSteeringSettings )
	{
		BOOLEAN bChanged = FALSE;
		
		//to set the band steering physical modulation rate threshold parameters
		if( pBandSteeringSettings->PhyRateThreshold != sWiFiDmlBandSteeringStoredSettinngs[ radioIndex ].PhyRateThreshold ) 
		{
			#if defined(_ENABLE_BAND_STEERING_)
			ret=wifi_setBandSteeringPhyRateThreshold( radioIndex, pBandSteeringSettings->PhyRateThreshold );
                        if(ret) CcspWifiTrace(("RDK_LOG_INFO, WIFI :Phyrate setting failed \n" ));
			#endif
			bChanged = TRUE;
		}
		
		//to set the band steering band steering RSSIThreshold parameters
		if( pBandSteeringSettings->RSSIThreshold != sWiFiDmlBandSteeringStoredSettinngs[ radioIndex ].RSSIThreshold ) 
		{
			
			#if defined(_ENABLE_BAND_STEERING_)
			ret=wifi_setBandSteeringRSSIThreshold( radioIndex, pBandSteeringSettings->RSSIThreshold );
                        if(ret) CcspWifiTrace(("RDK_LOG_INFO, WIFI RSSI setting failed  \n"));
			#endif
			bChanged = TRUE;			
		}

		//to set the band steering BandUtilizationThreshold parameters 
		if( pBandSteeringSettings->UtilizationThreshold != sWiFiDmlBandSteeringStoredSettinngs[ radioIndex ].UtilizationThreshold ) 
		{
			#if defined(_ENABLE_BAND_STEERING_)
			ret=wifi_setBandSteeringBandUtilizationThreshold( radioIndex, pBandSteeringSettings->UtilizationThreshold);
                        if(ret) CcspWifiTrace(("RDK_LOG_INFO, WIFI :Utilization setting failed \n"));
			#endif
			bChanged = TRUE;
		}

		//to set the band steering OverloadInactiveTime parameters
		if( pBandSteeringSettings->OverloadInactiveTime != sWiFiDmlBandSteeringStoredSettinngs[ radioIndex ].OverloadInactiveTime )
		{
			#if defined(_ENABLE_BAND_STEERING_)
			ret=wifi_setBandSteeringOverloadInactiveTime( radioIndex, pBandSteeringSettings->OverloadInactiveTime);
			if(ret) CcspWifiTrace(("RDK_LOG_INFO, WIFI :OverloadInactiveTime setting failed \n"));
			#endif
			bChanged = TRUE;
		}

		//to set the band steering IdleInactiveTime parameters
		if( pBandSteeringSettings->IdleInactiveTime != sWiFiDmlBandSteeringStoredSettinngs[ radioIndex ].IdleInactiveTime )
		{
			#if defined(_ENABLE_BAND_STEERING_)
			ret=wifi_setBandSteeringIdleInactiveTime( radioIndex, pBandSteeringSettings->IdleInactiveTime);
			if(ret) CcspWifiTrace(("RDK_LOG_INFO, WIFI :IdleInactiveTime setting failed \n"));
			#endif
			bChanged = TRUE;
		}

		if( bChanged )
		{
			// Take copy of band steering settings from current values
			memcpy( &sWiFiDmlBandSteeringStoredSettinngs[ radioIndex ], 
					pBandSteeringSettings, 
					sizeof( COSA_DML_WIFI_BANDSTEERING_SETTINGS ) );
		}
	}
	return ANSC_STATUS_SUCCESS;
}


ANSC_STATUS 
CosaDmlWiFi_GetATMOptions(PCOSA_DML_WIFI_ATM  pATM) {	
	if( NULL == pATM )
		return ANSC_STATUS_FAILURE;
	
#ifdef _ATM_HAL_ 		
	wifi_getATMCapable(&pATM->Capable);
	wifi_getATMEnable(&pATM->Enable);
#endif
	return ANSC_STATUS_SUCCESS;
}



ANSC_STATUS
CosaWifiRegGetATMInfo( ANSC_HANDLE   hThisObject){
	PCOSA_DML_WIFI_ATM        		pATM    = (PCOSA_DML_WIFI_ATM     )hThisObject;
	unsigned int g=0;
	int s=0;
	UINT percent=25;
	UCHAR buf[256]={0};
	char *token=NULL, *dev=NULL;
        int rc = -1 ;
	pATM->grpCount=ATM_GROUP_COUNT;
	for(g=0; g<pATM->grpCount; g++) {
		snprintf(pATM->APGroup[g].APList, COSA_DML_WIFI_ATM_MAX_APLIST_STR_LEN, "%d,%d", g*2+1, g*2+2);

#ifdef _ATM_HAL_
		wifi_getApATMAirTimePercent(g*2, &percent);
		wifi_getApATMSta(g*2, buf, sizeof(buf)); 
#endif
		
		if(percent<=100)
			pATM->APGroup[g].AirTimePercent=percent;
		
		//"$MAC $ATM_percent|$MAC $ATM_percent|$MAC $ATM_percent"
		token = strtok((char*)buf, "|");
		while(token != NULL) {
			dev=strchr(token, ' ');
			if(dev) {
				*dev=0; 
				dev+=1;
                                /*CID:135362 BUFFER_SIZE_WARNING*/
				rc = strcpy_s(pATM->APGroup[g].StaList[s].MACAddress, sizeof(pATM->APGroup[g].StaList[s].MACAddress), token);
	                        if ( rc != 0) {
                                     ERR_CHK(rc);
                                     return ANSC_STATUS_FAILURE;
                                }
                                pATM->APGroup[g].StaList[s].MACAddress[sizeof(pATM->APGroup[g].StaList[s].MACAddress) -1] = '\0';
				pATM->APGroup[g].StaList[s].AirTimePercent=_ansc_atoi(dev); 
				pATM->APGroup[g].StaList[s].pAPList=pATM->APGroup[g].APList;
				s++;
			}
			token = strtok(NULL, "|");		
		}
		
	}
fprintf(stderr, "---- %s ???load from PSM\n", __func__);
//??? load from PSM
	
	return ANSC_STATUS_SUCCESS;
}


ANSC_STATUS
CosaDmlWiFi_SetATMEnable(PCOSA_DML_WIFI_ATM pATM, BOOL bValue) {
	if( NULL == pATM )
		return ANSC_STATUS_FAILURE;
		
	pATM->Enable=bValue;
#ifdef _ATM_HAL_ 	
	wifi_setATMEnable(bValue);
#endif
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetATMAirTimePercent(char *APList, UINT AirTimePercent) {
	char str[128];
	char *token=NULL;
	int apIndex=-1;
	int rc = -1;

	rc = strcpy_s(str, 127, APList);
	if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
	token = strtok(str, ",");
    while(token != NULL) {
		apIndex = _ansc_atoi(token)-1; 
		if(apIndex>=0) {
fprintf(stderr, "---- %s %s %d %d\n", __func__, "wifi_setApATMAirTimePercent", apIndex-1, AirTimePercent);			
#ifdef _ATM_HAL_ 		
			wifi_setApATMAirTimePercent(apIndex-1, AirTimePercent);
#endif			
		}		
        token = strtok(NULL, ",");		
    }
	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFi_SetATMSta(char *APList, char *MACAddress, UINT AirTimePercent) {
	char str[128];
	char *token=NULL;
	int apIndex=-1;
	int rc = -1;
	rc = strcpy_s(str, 127, APList);
	if (rc != 0) {
            ERR_CHK(rc);
            return ANSC_STATUS_FAILURE;
        }
	token = strtok(str, ",");
    while(token != NULL) {
		apIndex = _ansc_atoi(token)-1; 
		if(apIndex>=0) {
#ifdef _ATM_HAL_ 		
			wifi_setApATMSta(apIndex-1, (unsigned char*)MACAddress, AirTimePercent);
#endif			
		}		
        token = strtok(NULL, ",");		
    }

#ifndef _ATM_HAL_ 
    UNREFERENCED_PARAMETER(MACAddress);
    UNREFERENCED_PARAMETER(AirTimePercent);
#endif	
	return ANSC_STATUS_SUCCESS;

}

//zqiu >>

int init_client_socket(int *client_fd){
    int sockfd;
    if (!client_fd) return ANSC_STATUS_FAILURE;
#ifdef DUAL_CORE_XB3
	struct sockaddr_in serv_addr;
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) 
	{
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI-CLIENT <%s> <%d> : ERROR opening socket \n",__FUNCTION__, __LINE__));
		return -1;
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(5001);
	char RemoteIP[128]="";
	readRemoteIP(RemoteIP, 128,"ARM_ARPING_IP"); 
	if (RemoteIP[0] != 0 && strlen(RemoteIP) > 0) 
	{
		if(inet_pton(AF_INET,RemoteIP, &(serv_addr.sin_addr))<=0)
		{
		  close(sockfd); /*RDKB-13101 & CID :- 33747*/
		  CcspWifiTrace(("RDK_LOG_ERROR,WIFI-CLIENT <%s> <%d> : inet_pton error occured \n",__FUNCTION__, __LINE__));
		  return -1;
		}
	} 
#else
	#define WIFI_SERVER_FILE_NAME  "/tmp/wifi.sock"
	struct sockaddr_un serv_addr;
        int rc = -1;
	sockfd = socket(PF_UNIX,SOCK_STREAM,0);
	if(sockfd < 0 )
		return -1;
	serv_addr.sun_family=AF_UNIX;
	rc = strcpy_s(serv_addr.sun_path,sizeof(serv_addr.sun_path), WIFI_SERVER_FILE_NAME);
	if (rc != 0) {
            ERR_CHK(rc);
            return -1;
        }
#endif
	if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0)
    {
        close(sockfd);
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI-CLIENT <%s> <%d> : Error in connecting socket \n",__FUNCTION__, __LINE__));
        return -1;  
    }
    *client_fd = sockfd;
    return 0;

}

int send_to_socket(void *buff, int buff_size)
{
    int ret;
    int fd;

    ret = init_client_socket(&fd);
    if(ret != 0){		
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI-CLIENT <%s> <%d> : init_client_socket error \n",__FUNCTION__, __LINE__));
        return -1;
    }

    ret = write(fd, buff, buff_size);
	if (ret < 0) 
	{		
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI-CLIENT <%s> <%d> : ERROR writing to socket \n",__FUNCTION__, __LINE__));
	}

    close(fd);
    return 0;
}


#ifdef USE_NOTIFY_COMPONENT
extern void *bus_handle;

void Send_Associated_Device_Notification(int i,ULONG old_val, ULONG new_val)
{

	char  str[512] = {0};
	parameterValStruct_t notif_val[1];
	char param_name[256] = "Device.NotifyComponent.SetNotifi_ParamName";
	char compo[256] = "eRT.com.cisco.spvtg.ccsp.notifycomponent";
	char bus[256] = "/com/cisco/spvtg/ccsp/notifycomponent";
	char* faultParam = NULL;
	int ret = 0;	

	
	sprintf(str,"Device.WiFi.AccessPoint.%d.AssociatedDeviceNumberOfEntries,%d,%lu,%lu,%d",i,0,new_val,old_val,ccsp_unsignedInt);
	notif_val[0].parameterName =  param_name ;
	notif_val[0].parameterValue = str;
	notif_val[0].type = ccsp_string;

	ret = CcspBaseIf_setParameterValues(
		  bus_handle,
		  compo,
		  bus,
		  0,
		  0,
		  notif_val,
		  1,
		  TRUE,
		  &faultParam
		  );

	if(ret != CCSP_SUCCESS)
	{
		if ( faultParam ) 
		{
			CCSP_MESSAGE_BUS_INFO *bus_info = (CCSP_MESSAGE_BUS_INFO *)bus_handle;
			bus_info->freefunc(faultParam);
		}
		CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CNNECTED_CLIENT : Sending Notification Fail \n"));
	}

}

//zqiu:
void Send_Notification_for_hotspot(char *mac, BOOL add, int ssidIndex, int rssi) {
	int ret;
	
	char objName[256]="Device.X_COMCAST-COM_GRE.Hotspot.ClientChange";
	char objValue[256]={0};
    if (!mac) return;
	parameterValStruct_t  value[1] = {{ objName, objValue, ccsp_string}};
	
	char dst_pathname_cr[64]  =  {0};
	componentStruct_t **        ppComponents = NULL;
	int size =0;
	
	CCSP_MESSAGE_BUS_INFO *bus_info = (CCSP_MESSAGE_BUS_INFO *)bus_handle;
	char* faultParam = NULL;

	snprintf(objValue, sizeof(objValue), "%d|%d|%d|%s", (int)add, ssidIndex, rssi, mac);
	fprintf(stderr, "--  try to set %s=%s\n", objName, objValue);
	
	snprintf(dst_pathname_cr, sizeof(dst_pathname_cr), "%s%s", g_Subsystem, CCSP_DBUS_INTERFACE_CR);
	ret = CcspBaseIf_discComponentSupportingNamespace(
				bus_handle,
				dst_pathname_cr,
				objName,
				g_Subsystem,        /* prefix */
				&ppComponents,
				&size);
	if ( ret != CCSP_SUCCESS ) {
		fprintf(stderr, "Error:'%s' is not exist\n", objName);
		return;
	}	

	ret = CcspBaseIf_setParameterValues(
				bus_handle,
				ppComponents[0]->componentName,
				ppComponents[0]->dbusPath,
				0, 0x0,   /* session id and write id */
				value,
				1,
				TRUE,   /* no commit */
				&faultParam
			);

	if (ret != CCSP_SUCCESS && faultParam) {
		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s Failed to SetValue for param '%s' and ret val is %d\n",__FUNCTION__,faultParam,ret));
		fprintf(stderr, "Error:Failed to SetValue for param '%s'\n", faultParam);
		bus_info->freefunc(faultParam);
	}
	free_componentStruct_t(bus_handle, 1, ppComponents);
	return;
}

int sMac_to_cMac(char *sMac, unsigned char *cMac) {
	unsigned int iMAC[6];
	int i=0;
	if (!sMac || !cMac) return 0;
	sscanf(sMac, "%x:%x:%x:%x:%x:%x", &iMAC[0], &iMAC[1], &iMAC[2], &iMAC[3], &iMAC[4], &iMAC[5]);
	for(i=0;i<6;i++) 
		cMac[i] = (unsigned char)iMAC[i];
	
	return 0;
}

int cMac_to_sMac(unsigned char *cMac, char *sMac) {
	if (!sMac || !cMac) return 0;
	snprintf(sMac, 32, "%02X:%02X:%02X:%02X:%02X:%02X", cMac[0],cMac[1],cMac[2],cMac[3],cMac[4],cMac[5]);
	return 0;
}

BOOL wifi_is_mac_in_macfilter(int apIns, char *mac) {
	BOOL found=FALSE;
	if (!mac) return false;

	PCOSA_DATAMODEL_WIFI            pMyObject       = (PCOSA_DATAMODEL_WIFI) g_pCosaBEManager->hWifi;

	PSINGLE_LINK_ENTRY pSLinkEntryAp = AnscQueueGetEntryByIndex( &pMyObject->AccessPointQueue, apIns-1);

	if ( pSLinkEntryAp )
	{
		PCOSA_CONTEXT_LINK_OBJECT pLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntryAp);
		if( pLinkObj )
		{
			PCOSA_DML_WIFI_AP                       pWifiAp            = (PCOSA_DML_WIFI_AP)pLinkObj->hContext;
			PCOSA_DML_WIFI_AP_FULL          pWiFiApFull         = (PCOSA_DML_WIFI_AP_FULL)&pWifiAp->AP;
			PSINGLE_LINK_ENTRY          pAPLink     = NULL;
			PCOSA_CONTEXT_LINK_OBJECT   pAPLinkObj  = NULL;

			for (   pAPLink = AnscSListGetFirstEntry(&pWiFiApFull->MacFilterList);
				   pAPLink != NULL;
				   pAPLink = AnscSListGetNextEntry(pAPLink)
				)
			{
				pAPLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pAPLink);
				if (!pAPLinkObj)
					continue;

				PCOSA_DML_WIFI_AP_MAC_FILTER    pMacFilt        = (PCOSA_DML_WIFI_AP_MAC_FILTER)pAPLinkObj->hContext;
				if(strcasecmp(mac, pMacFilt->MACAddress)==0)
				{
					found=TRUE;
					break;
				}
			}
		}
	}

	return found;
}

void Hotspot_APIsolation_Set(int apIns) {

    char *strValue = NULL;
    char recName[256];
    int retPsmGet = CCSP_SUCCESS;
    BOOL enabled = FALSE;

    wifi_getApEnable(apIns-1, &enabled);

    if (enabled == FALSE) {
        CcspWifiTrace(("RDK_LOG_INFO,%s: wifi_getApEnable %d, %d \n", __FUNCTION__, apIns, enabled))
        return;
    }

    if (!g_wifidb_rfc) {
    memset(recName, 0, sizeof(recName));
    sprintf(recName, ApIsolationEnable, apIns);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if (retPsmGet == CCSP_SUCCESS) {
        BOOL enable = atoi(strValue);
        wifi_setApIsolationEnable(apIns-1, enable);
        CcspWifiTrace(("RDK_LOG_INFO,%s: wifi_setApIsolationEnable %d, %d \n", __FUNCTION__, apIns-1, enable));
	    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    } else {
        struct schema_Wifi_VAP_Config  *pcfg= NULL;
        pcfg = (struct schema_Wifi_VAP_Config  *) wifi_db_get_table_entry(vap_names[apIns-1], "vap_name",&table_Wifi_VAP_Config,OCLM_STR);
        if (pcfg != NULL) {
            wifi_setApIsolationEnable(apIns-1,pcfg->isolation_enabled);
            CcspWifiTrace(("RDK_LOG_INFO,%s: wifi_setApIsolationEnable %d, %d \n", __FUNCTION__, apIns-1, pcfg->isolation_enabled));
        }
    }
}

void Load_Hotspot_APIsolation_Settings(void)
{
	int i;
	for(i=0 ; i<HOTSPOT_NO_OF_INDEX ; i++)
	{
		Hotspot_APIsolation_Set(Hotspot_Index[i]);
	}
}

void Hotspot_MacFilter_UpdateEntry(int apIns) {

	int retPsmGet = CCSP_SUCCESS;
	char recName[256];
	char *macAddress = NULL;
	int i;

	unsigned int 		InstNumCount    = 0;
	unsigned int*		pInstNumList    = NULL;

	memset(recName, 0, sizeof(recName));
	sprintf(recName, "Device.WiFi.AccessPoint.%d.X_CISCO_COM_MacFilterTable.", apIns);
	CcspWifiTrace(("RDK_LOG_INFO, %s-%d :ApIndex=%d\n",__FUNCTION__,__LINE__,apIns));

/*
This call gets instances of table and total count.
*/
	if(CcspBaseIf_GetNextLevelInstances
	(
		bus_handle,
		WIFI_COMP,
		WIFI_BUS,
		recName,
		&InstNumCount,
		&pInstNumList
	) == CCSP_SUCCESS)
	{

		/*
		This loop checks if mac address is matching with the new mac to be added.
		*/
		for(i=InstNumCount-1; i>=0; i--)
		{
			memset(recName, 0, sizeof(recName));
			sprintf(recName, MacFilter, apIns, pInstNumList[i]);

			retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &macAddress);
			if (retPsmGet == CCSP_SUCCESS)
			{

                wifi_addApAclDevice(apIns-1, macAddress);
				CcspWifiTrace(("RDK_LOG_INFO, Hotspot_MacFilter_UpdateEntry:Mac=%s\n",macAddress));
				((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(macAddress);

			}
		}

		if(pInstNumList)
			free(pInstNumList);

        }
	else
	{
		printf("MAC_FILTER : CcspBaseIf_GetNextLevelInstances failed \n");
	}

}

void *Update_Hotspot_MacFilt_Entries_Thread_Func(void *pArg)
{
    int i=0;
    BOOL check = true;
    struct timespec time_to_wait;
    struct timeval tv_now;
    int rc;
    UNREFERENCED_PARAMETER(pArg);
    do{
        pthread_mutex_lock(&macfilter);
        /* For the false case, this can complete after 5 min. But if ResetRadio
         * called 2x immediately, then it could take upto 11 minutes for the 
         * 2nd thread to finish. wifi_reset() takes about 5 min to complete.
         */
        gettimeofday(&tv_now, NULL);
        time_to_wait.tv_nsec = 0;
        time_to_wait.tv_sec = tv_now.tv_sec + 900;  //15 min.
        rc = pthread_cond_timedwait(&reset_done, &macfilter, &time_to_wait);
        if (rc == ETIMEDOUT)    // timed out
        {
            CcspWifiTrace(("RDK_LOG_ERROR, %s Timed out. Hotspot_MacFilter_UpdateEntry() not called.\n",__FUNCTION__));
        }
        else if (rc != 0)   // some other error.
        {
            CcspWifiTrace(("RDK_LOG_ERROR, %s cond_timedwait error=%d. Hotspot_MacFilter_UpdateEntry() not called.\n",__FUNCTION__, rc));
        }
        else    // got the signal
        {
            CcspWifiTrace(("RDK_LOG_INFO, Hotspot_MacFilter_UpdateEntry() called.\n"));
            for(i=0; i<HOTSPOT_NO_OF_INDEX ; i++)
            {
                Hotspot_MacFilter_UpdateEntry(Hotspot_Index[i]);
            }
        }
        check = false;
        pthread_mutex_unlock(&macfilter);
    }while(check);
    return NULL;
}

void Update_Hotspot_MacFilt_Entries(BOOL signal_thread)
{
	pthread_t Update_Hotspot_MacFilt_Entries_Thread;
	int res;
    pthread_attr_t attr;
    pthread_attr_t *attrp = NULL;

    attrp = &attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
    CcspWifiTrace(("RDK_LOG_INFO, %s Create thread(signal=%d).\n",__FUNCTION__, signal_thread));
    res = pthread_create(&Update_Hotspot_MacFilt_Entries_Thread, attrp, Update_Hotspot_MacFilt_Entries_Thread_Func, NULL);
    if(res != 0) {
        CcspTraceError(("%s MAC_FILTER : Create Update_Hotspot_MacFilt_Entries_Thread_Func failed for %d \n",__FUNCTION__,res));
    }
    if(signal_thread)
        pthread_cond_signal(&reset_done);
    if(attrp != NULL)
        pthread_attr_destroy( attrp );
}

void Hotspot_MacFilter_AddEntry(char *mac)
{

    char table_name[512] = {0};
    char param_name[HOTSPOT_NO_OF_INDEX*2] [512] = {{0}};
    int i, j , table_index[HOTSPOT_NO_OF_INDEX]={0};
    parameterValStruct_t param[HOTSPOT_NO_OF_INDEX*2];

    char *param_value=HOTSPOT_DEVICE_NAME;
    char* faultParam = NULL;
    int ret = CCSP_FAILURE;

    if ((!mac) || (strlen(mac) <=0))
    {
	    return;
    }

    if (!Validate_mac(mac))
    {
        return;
    }
	/*
	This Loop checks if mac is already present in the table. If it is present it will continue for next index.
	In case , if mac is not present or add entry fails, table_index[i] will be 0.
	In success case, it will add table entry but values will be blank.
	*/
	for(i=0 ; i<HOTSPOT_NO_OF_INDEX ; i++)
	{
		if(wifi_is_mac_in_macfilter(Hotspot_Index[i], mac))
		{
			continue;
		}
    		sprintf(table_name,"Device.WiFi.AccessPoint.%d.X_CISCO_COM_MacFilterTable.",Hotspot_Index[i]);
                char recName[100];
                int retPsmGet = CCSP_SUCCESS;
                char *strValue = NULL;
                sprintf(recName,"eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.MacFilterList",Hotspot_Index[i]);
                retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
                if (retPsmGet == CCSP_SUCCESS && strValue != NULL) {
                    char strValue2[256];
		    int rc = -1;
                    rc = strcpy_s(strValue2, sizeof(strValue2), strValue);
                    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
		    if (rc != 0) {
                        ERR_CHK(rc);
                        return;
                    }
                    strValue2[sizeof(strValue2) - 1] = '\0';
                    char *tot_ent = strtok(strValue2, ":");
                    int entry_count = atoi(tot_ent);
                    if (entry_count >= 64)
                    {
                        CcspTraceWarning(("%s MAC_FILTER : Access point = %d  Failed to add entry as MAC Filter is Full!! \n",__FUNCTION__,Hotspot_Index[i]));
                        continue;
                    }
                    else
                    {
                                table_index[i] = Cosa_AddEntry(WIFI_COMP,WIFI_BUS,table_name);
                        if( table_index[i] == 0)
                        {
                            CcspTraceError(("%s MAC_FILTER : Access point = %d  Add table failed\n",__FUNCTION__,Hotspot_Index[i]));
                        }
                    }
                }
	}

/*
This loop check for non zero table_index and fill the parameter values for them.
*/
	for(i=0 , j= 0 ; j<HOTSPOT_NO_OF_INDEX ; j++)
	{
		if( table_index[j] != 0)
		{
		        sprintf( param_name[i], "Device.WiFi.AccessPoint.%d.X_CISCO_COM_MacFilterTable.%d.MACAddress", Hotspot_Index[j],table_index[j] );
		        param[i].parameterName =  param_name[i] ;
		        param[i].parameterValue = mac;
		        param[i].type = ccsp_string;
		        i++;

		        sprintf( param_name[i], "Device.WiFi.AccessPoint.%d.X_CISCO_COM_MacFilterTable.%d.DeviceName", Hotspot_Index[j],table_index[j] );
		        param[i].parameterName =  param_name[i] ;
		        param[i].parameterValue = param_value;
		        param[i].type = ccsp_string;
		        i++;
		}
	}

/*
This is a dbus bulk set call for all parameters of table at one go
*/
	if(i > 0)
	{
	    ret = CcspBaseIf_setParameterValues(
	          bus_handle,
	          WIFI_COMP,
	          WIFI_BUS,
	          0,
	          0,
	          param,
	          i,
	          TRUE,
	          &faultParam
	          );

	        if(ret != CCSP_SUCCESS)
	        {
				if ( faultParam ) 
				{
					CCSP_MESSAGE_BUS_INFO *bus_info = (CCSP_MESSAGE_BUS_INFO *)bus_handle;

					CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s Failed to SetValue for param '%s' and ret val is %d\n",__FUNCTION__,faultParam,ret));
					fprintf(stderr, "Error:Failed to SetValue for param '%s'\n", faultParam);
					bus_info->freefunc(faultParam);
				}

			CcspTraceError(("%s MAC_FILTER : CcspBaseIf_setParameterValues failed, Deleting Entries \n",__FUNCTION__));
            for(j= 0 ; j<HOTSPOT_NO_OF_INDEX ; j++)
			{
				if( table_index[j] != 0)
				{
					sprintf(table_name,"Device.WiFi.AccessPoint.%d.X_CISCO_COM_MacFilterTable.%d.",Hotspot_Index[j],table_index[j]);
					ret = Cosa_DelEntry(WIFI_COMP,WIFI_BUS,table_name);

					if(ret != CCSP_SUCCESS)
					{
						CcspTraceError(("%s MAC_FILTER : Cosa_DelEntry failed for %s \n",__FUNCTION__,table_name));
					}
				}
            }
	        }
	}
}

void Hotspot_Macfilter_sync(char *mac) {
    if (!mac) return;
	fprintf(stderr, "---- %s %d\n", __func__, __LINE__);
	Hotspot_MacFilter_AddEntry(mac);
}

#if defined(_PLATFORM_RASPBERRYPI_) || defined(_PLATFORM_TURRIS_)
typedef struct _wifi_disassociation_details
{
        CHAR  wifi_mac[64];
        INT   wifi_state;
        INT   wifi_SignalStrength;

}wifi_disassociation_details_t;
void update_wifi_inactive_AssociatedDeviceInfo(char *filename)
{
        PCOSA_DML_WIFI_AP_ASSOC_DEVICE assoc_devices = NULL;
        LM_wifi_hosts_t hosts;
        char ssid[256]= {0},assoc_device[256] = {0};
        ULONG count = 0;
        int j = 0,i = 0;
        int rc = -1;
        memset(&hosts,0,sizeof(LM_wifi_hosts_t));
        memset(assoc_device,0,sizeof(assoc_device));
        memset(ssid,0,sizeof(ssid));

        if(strcmp(filename,"/tmp/AllAssociated_Devices_2G.txt") == 0)
                i = 1;
        else
                i = 2;
        wifi_disassociation_details_t *w_disassoc_clients=NULL;
        wifi_getApInactiveAssociatedDeviceDiagnosticResult(filename,&w_disassoc_clients, &count);
        hosts.count = count;
        for(j=0 ; j<count ; j++)
        {
                _ansc_snprintf(ssid,sizeof(ssid),"Device.WiFi.SSID.%lu",i);
                _ansc_snprintf(assoc_device,sizeof(assoc_device),"Device.WiFi.AccessPoint.%d.AssociatedDevice.%d",i,j+1);

                rc = strcpy_s(hosts.host[j].AssociatedDevice,sizeof(hosts.host[j].AssociatedDevice),assoc_device);
	        if (rc != 0) {
                    ERR_CHK(rc);
                    return;
                }
                rc = strcpy_s(hosts.host[j].phyAddr,sizeof(hosts.host[j].phyAddr),w_disassoc_clients[j].wifi_mac);
	        if (rc != 0) {
                    ERR_CHK(rc);
                    return;
                }
                rc = strcpy_s(hosts.host[j].ssid,sizeof(hosts.host[j].ssid),ssid);
	        if (rc != 0) {
                    ERR_CHK(rc);
                    return;
                }
                hosts.host[j].Status = w_disassoc_clients[j].wifi_state;
                hosts.host[j].RSSI = w_disassoc_clients[j].wifi_SignalStrength;
                printf("*** phyAddr : %s,Status : %d : RSSI %d\n",hosts.host[j].phyAddr,hosts.host[j].Status,hosts.host[j].RSSI);
        }
        if(hosts.count)
                CosaDMLWiFi_Send_FullHostDetails_To_LMLite( &hosts );
        if(w_disassoc_clients)
                free(w_disassoc_clients);
}
#endif


void *Wifi_Hosts_Sync_Func(void *pt, int index, wifi_associated_dev_t *associated_dev, BOOL bCallForFullSync, BOOL bCallFromDisConnCB )
{

	ULONG i;
	unsigned int j;
	char ssid[256]= {0};
	char mac_id[256] = {0},rec_mac_id[256] = {0};
	char assoc_device[256] = {0};
	ULONG count = 0;
	PCOSA_DML_WIFI_AP_ASSOC_DEVICE assoc_devices = NULL;
	LM_wifi_hosts_t hosts;
	static ULONG backup_count[4]={0};
	BOOL enabled=FALSE;
        int rc = -1;
#if defined(_INTEL_BUG_FIXES_)
	char *output_buf = NULL;
	char partOfAddress[3];
#else
    UNREFERENCED_PARAMETER(pt);
#endif
	CcspWifiTrace(("RDK_LOG_INFO, %s-%d \n",__FUNCTION__,__LINE__));
 
	memset(&hosts,0,sizeof(LM_wifi_hosts_t));
	memset(assoc_device,0,sizeof(assoc_device));
	memset(ssid,0,sizeof(ssid));
	memset(rec_mac_id,0,sizeof(rec_mac_id));
	memset(mac_id,0,sizeof(mac_id));
	
		
	if(bCallForFullSync == 0)  // Single device callback notification
	{
		if (!associated_dev) return NULL;
		
			//No need to check AP enable during disconnection. Becasue needs to send host details to LMLite during SSID disable case.
			if( FALSE == bCallFromDisConnCB )
			{
				wifi_getApEnable(index-1, &enabled);
				if (enabled == FALSE) 
					return NULL;
			} 

#if !defined(_COSA_BCM_MIPS_)
			wifi_getApName(index-1, ssid);
#else
			_ansc_sprintf(ssid,"ath%d",index-1);
#endif
			
			count = 0;

#if !defined(_INTEL_BUG_FIXES_)
			assoc_devices = CosaDmlWiFiApGetAssocDevices(NULL, ssid , &count);
#else
			// Get the num of associated devices
			wifi_getApNumDevicesAssociated(index-1, &count);  /* override 'count' value */

			if (count > 254 /*MAX_STA_SUPPORT*/)
			{
				CcspWifiTrace(("RDK_LOG_ERROR, (%s) count is %lu ==> exit\n",__FUNCTION__, count));
				return NULL;
			}

			if (count > 0)
			{
				if ( (output_buf = (char *)malloc(count * 18 /*MAC_ADDR_STR_LEN*/)) == NULL )
				{
					CcspWifiTrace(("RDK_LOG_ERROR, (%s) output_buf malloc ERROR\n",__FUNCTION__));
					return NULL;
				}

				// Get all associated devies
				if (wifi_getApAssociatedDevice(index-1, output_buf, count * 18 /*MAC_ADDR_STR_LEN*/) == RETURN_ERR)
				{
					CcspWifiTrace(("RDK_LOG_ERROR, (%s) wlan_getApAssociatedDevice returned ERROR\n",__FUNCTION__));
					AnscFreeMemory(output_buf);
					return NULL;
				}

				if ( (assoc_devices = (PCOSA_DML_WIFI_AP_ASSOC_DEVICE)malloc(sizeof(COSA_DML_WIFI_AP_ASSOC_DEVICE) * count)) == NULL )
				{
					CcspWifiTrace(("RDK_LOG_ERROR, %s assoc_devices malloc ERROR\n",__FUNCTION__));
					AnscFreeMemory(output_buf);
					return NULL;
				}

				// Get the mac of all associated devices
				for (i=0; i < count; i++)
				{
					for (j=0; j < 6; j++)
					{
						snprintf(partOfAddress, 3, "%s", &output_buf[i*18 + j*3]);
						assoc_devices[i].MacAddress[j] = (char)strtol(partOfAddress, NULL, 16);
					}
				}
			}
#endif
			CcspWifiTrace(("RDK_LOG_WARN, backup_count[%d]=%lu, count=%lu\n", index-1, backup_count[index-1], count));

			if(backup_count[index-1] != count) // Notification for AssociatedDeviceNumberOfEntries
			{
				Send_Associated_Device_Notification(index,backup_count[index-1],count);
				backup_count[index-1] = count;
			}


			_ansc_snprintf
			(
				rec_mac_id, sizeof(rec_mac_id),
				"%02X:%02X:%02X:%02X:%02X:%02X",
				associated_dev->cli_MACAddress[0],
				associated_dev->cli_MACAddress[1],
				associated_dev->cli_MACAddress[2],
				associated_dev->cli_MACAddress[3],
				associated_dev->cli_MACAddress[4],
				associated_dev->cli_MACAddress[5]
			);
			rec_mac_id[17] = '\0';
			_ansc_snprintf(ssid,sizeof(ssid),"Device.WiFi.SSID.%d",index);


#if !defined(_INTEL_BUG_FIXES_)
                for(j = 0; j < count ; j++)
                {
                        //CcspWifiTrace(("RDK_LOG_WARN,WIFI-CLIENT <%s> <%d> : j = %d \n",__FUNCTION__, __LINE__ , j));
                        _ansc_snprintf
                        (
                                mac_id, sizeof(mac_id),
                                "%02X:%02X:%02X:%02X:%02X:%02X",
                                assoc_devices[j].MacAddress[0],
                                assoc_devices[j].MacAddress[1],
                                assoc_devices[j].MacAddress[2],
                                assoc_devices[j].MacAddress[3],
                                assoc_devices[j].MacAddress[4],
                                assoc_devices[j].MacAddress[5]
                        );
                        mac_id[17] = '\0';
                        if( 0 == strcmp( rec_mac_id, mac_id ) )
                                {
                                        _ansc_snprintf(assoc_device,sizeof(assoc_device),"Device.WiFi.AccessPoint.%d.AssociatedDevice.%d",index,j+1);
                                        break;
                                }

                }

                rc = strcpy_s((char*)hosts.host[0].phyAddr,sizeof(hosts.host[0].phyAddr),rec_mac_id);
		if (rc != 0) {
                    ERR_CHK(rc);
                    return NULL;
                }
                rc = strcpy_s((char*)hosts.host[0].ssid,sizeof(hosts.host[0].ssid),ssid);
		if (rc != 0) {
                    ERR_CHK(rc);
                    return NULL;
                }
                hosts.host[0].RSSI = associated_dev->cli_SignalStrength;
                hosts.host[0].phyAddr[17] = '\0';

                if(associated_dev->cli_Active) // Online Clients Private, XHS
                {
                        hosts.host[0].Status = TRUE;
                        if(0 == strlen(assoc_device)) // if clients switch to other ssid and not listing in CosaDmlWiFiApGetAssocDevices
                        {
                                _ansc_snprintf(assoc_device,sizeof(assoc_device),"Device.WiFi.AccessPoint.%d.AssociatedDevice.%lu",index,count+1);

                                }
                        rc = strcpy_s((char*)hosts.host[0].AssociatedDevice, sizeof(hosts.host[0].AssociatedDevice), assoc_device);
			if (rc != 0) {
                            ERR_CHK(rc);
                            return NULL;
                        }
                }
                else // Offline Clients Private, XHS.. AssociatedDevice should be null
                {
                        hosts.host[0].Status = FALSE;
                }
                CosaDMLWiFi_Send_ReceivedHostDetails_To_LMLite( &(hosts.host[0]) );

#else
		char *expMacAdd=(char *)pt;	
		if(NULL == expMacAdd) { // Association event
	
			for(j = 0; j < count ; j++) // Send the info of all the associated devices to LMLite
			{
				CcspWifiTrace(("RDK_LOG_INFO,WIFI-CLIENT <%s> <%d> : j = %d \n",__FUNCTION__, __LINE__ , j));
				_ansc_snprintf
				    (
				mac_id, sizeof(mac_id),
				"%02X:%02X:%02X:%02X:%02X:%02X",
				assoc_devices[j].MacAddress[0],
				assoc_devices[j].MacAddress[1],
				assoc_devices[j].MacAddress[2],
				assoc_devices[j].MacAddress[3],
				assoc_devices[j].MacAddress[4],
				assoc_devices[j].MacAddress[5]
				     );
	
				mac_id[17] = '\0';		

				mac_id[17] = '\0';

				_ansc_snprintf(assoc_device,sizeof(assoc_device),"Device.WiFi.AccessPoint.%d.AssociatedDevice.%d",index, j+1);

                        	if( 0 == strcmp( rec_mac_id, mac_id ) ) {
					// Get the RSSI from the client which triggers the callback
					hosts.host[0].RSSI = associated_dev->cli_SignalStrength;
				}

				rc = strcpy_s((char*)hosts.host[0].phyAddr, sizeof(hosts.host[0].phyAddr), mac_id);
                  		if (rc != 0) {
                                    ERR_CHK(rc);
                                    return NULL;
                                }
				rc = strcpy_s((char*)hosts.host[0].ssid, sizeof(hosts.host[0].ssid), ssid);
                  		if (rc != 0) {
                                    ERR_CHK(rc);
                                    return NULL;
                                }
				hosts.host[0].phyAddr[17] = '\0';

				hosts.host[0].Status = TRUE;
				rc = strcpy_s((char*)hosts.host[0].AssociatedDevice, sizeof(hosts.host[0].AssociatedDevice), assoc_device);
                  		if (rc != 0) {
                                    ERR_CHK(rc);
                                    return NULL;
                                }


				
				CosaDMLWiFi_Send_ReceivedHostDetails_To_LMLite( &(hosts.host[0]) );
			}
		}
		else { // Disassociation event
			_ansc_snprintf
			(
			rec_mac_id, sizeof(rec_mac_id),
				"%02X:%02X:%02X:%02X:%02X:%02X",
				associated_dev->cli_MACAddress[0],
				associated_dev->cli_MACAddress[1],
				associated_dev->cli_MACAddress[2],					
				associated_dev->cli_MACAddress[3],					
				associated_dev->cli_MACAddress[4],					
				associated_dev->cli_MACAddress[5]
			);
			rec_mac_id[17] = '\0';	
	
			CcspWifiTrace(("RDK_LOG_WARN, send association event for %s\n", rec_mac_id));

                        rc = strcpy_s((char*)hosts.host[0].phyAddr, sizeof(hosts.host[0].phyAddr), rec_mac_id);
              		if (rc != 0) {
                            ERR_CHK(rc);
                            return NULL;
                        }
			rc = strcpy_s((char*)hosts.host[0].ssid, sizeof(hosts.host[0].ssid), ssid);
              		if (rc != 0) {
                            ERR_CHK(rc);
                            return NULL;
                        }
			hosts.host[0].phyAddr[17] = '\0';
			
			hosts.host[0].Status = FALSE;
			hosts.host[0].RSSI = 0;

			// Send the disassociated device info to LMLite
			CosaDMLWiFi_Send_ReceivedHostDetails_To_LMLite( &(hosts.host[0]) );
	
		}
#endif

	}
	else  // Group notification - on request from LMLite
	{
		for(i = 1; i <=4 ; i++)
		{
			//zqiu:
			//_ansc_sprintf(ssid,"ath%d",i-1);
			wifi_getApEnable(i-1, &enabled);
			if (enabled == FALSE) 
				continue; 
#if !defined(_COSA_BCM_MIPS_)
			wifi_getApName(i-1, ssid);
#else
			_ansc_snprintf(ssid,sizeof(ssid),"ath%lu",i-1);
#endif
			count = 0;

#if !defined(_INTEL_BUG_FIXES_)
			assoc_devices = CosaDmlWiFiApGetAssocDevices(NULL, ssid , &count);
#else
			// Get the num of associated devices
			wifi_getApNumDevicesAssociated(index-1, &count);  /* override 'count' value */
			if (count > 254 /*MAX_STA_SUPPORT*/)
			{
				CcspWifiTrace(("RDK_LOG_ERROR, %s count is %lu ==> exit\n",__FUNCTION__, count));
				return NULL;
			}

			if (count > 0)
			{
				if ( (output_buf = (char *)malloc(count * 18 /*MAC_ADDR_STR_LEN*/)) == NULL )
				{
					CcspWifiTrace(("RDK_LOG_ERROR, %s output_buf malloc ERROR\n",__FUNCTION__));
					return NULL;
				}

				// Get the mac addr of all associated devies
				if (wifi_getApAssociatedDevice(index-1, output_buf, count * 18 /*MAC_ADDR_STR_LEN*/) == RETURN_ERR)
				{
					CcspWifiTrace(("RDK_LOG_ERROR, %s wlan_getApAssociatedDevice returned ERROR\n",__FUNCTION__));
					AnscFreeMemory(output_buf);
					return NULL;
				}

				if ( (assoc_devices = (PCOSA_DML_WIFI_AP_ASSOC_DEVICE)malloc(sizeof(COSA_DML_WIFI_AP_ASSOC_DEVICE) * count)) == NULL )
				{
					CcspWifiTrace(("RDK_LOG_ERROR, %s assoc_devices malloc ERROR\n",__FUNCTION__));
					AnscFreeMemory(output_buf);
					return NULL;
				}

				// Find the specific associated client which triggers the callback
				for (i=0; i < count; i++)
				{
					for (j=0; j < 6; j++)
					{
						snprintf(partOfAddress, 3, "%s", &output_buf[i*18 + j*3]);
						assoc_devices[i].MacAddress[j] = (char)strtol(partOfAddress, NULL, 16);
					}
				}
			}

#endif

			if(backup_count[i-1] != count)
			{
				Send_Associated_Device_Notification(i,backup_count[i-1],count);
				backup_count[i-1] = count;
			}

			for(j = 0; j < count ; j++)
			{
				//CcspWifiTrace(("RDK_LOG_WARN,WIFI-CLIENT <%s> <%d> : j = %d \n",__FUNCTION__, __LINE__ , j));
	                _ansc_snprintf( mac_id, sizeof(mac_id),
	                "%02X:%02X:%02X:%02X:%02X:%02X",
	                assoc_devices[j].MacAddress[0],
	                assoc_devices[j].MacAddress[1],
	                assoc_devices[j].MacAddress[2],
	                assoc_devices[j].MacAddress[3],
	                assoc_devices[j].MacAddress[4],
	                assoc_devices[j].MacAddress[5]
	            );

				_ansc_snprintf(ssid,sizeof(ssid),"Device.WiFi.SSID.%lu",i);
				_ansc_snprintf(assoc_device,sizeof(assoc_device),"Device.WiFi.AccessPoint.%lu.AssociatedDevice.%d",i,j+1);

				mac_id[17] = '\0';
				rc = strcpy_s((char*)hosts.host[hosts.count].AssociatedDevice, sizeof(hosts.host[hosts.count].AssociatedDevice),assoc_device);
              		        if (rc != 0) {
                                    ERR_CHK(rc);
                                    return NULL;
                                }
				rc = strcpy_s((char*)hosts.host[hosts.count].phyAddr, sizeof(hosts.host[hosts.count].phyAddr), mac_id);
              		        if (rc != 0) {
                                    ERR_CHK(rc);
                                    return NULL;
                                }
				rc = strcpy_s((char*)hosts.host[hosts.count].ssid, sizeof(hosts.host[hosts.count].ssid), ssid);
              		        if (rc != 0) {
                                    ERR_CHK(rc);
                                    return NULL;
                                }
				hosts.host[hosts.count].RSSI = assoc_devices[j].SignalStrength;
				hosts.host[hosts.count].Status = TRUE;
				hosts.host[hosts.count].phyAddr[17] = '\0';
    			(hosts.count)++;
			}
			if(assoc_devices) { /*RDKB-13101 & CID:-33716*/
                                AnscFreeMemory(assoc_devices);
				assoc_devices = NULL;
			}
	
		}

		CcspWifiTrace(("RDK_LOG_WARN, Total Hosts Count is %d\n",hosts.count));
#if !defined(_PLATFORM_RASPBERRYPI_)
		if(hosts.count)
		CosaDMLWiFi_Send_FullHostDetails_To_LMLite( &hosts );
#endif
#if defined(_PLATFORM_RASPBERRYPI_) || defined(_PLATFORM_TURRIS_)
		if(hosts.count >= 0)
		{
			update_wifi_inactive_AssociatedDeviceInfo("/tmp/AllAssociated_Devices_2G.txt");
			update_wifi_inactive_AssociatedDeviceInfo("/tmp/AllAssociated_Devices_5G.txt");
		}
#endif
	}

#if defined(_INTEL_BUG_FIXES_)
	if (output_buf)
        {
		AnscFreeMemory(output_buf);
                output_buf = NULL;
        }

#endif	
	//zqiu:
	if(assoc_devices) {
		AnscFreeMemory(assoc_devices);
        	assoc_devices = NULL;
	}

	return NULL;
}

/* CosaDMLWiFi_Send_ReceivedHostDetails_To_LMLite */
void CosaDMLWiFi_Send_ReceivedHostDetails_To_LMLite(LM_wifi_host_t   *phost)
{
	BOOL bProcessFurther = TRUE;
	
	/* Validate received param. If it is not valid then dont proceed further */
	if( NULL == phost )
	{
		CcspWifiTrace(("RDK_LOG_WARN, %s-%d Recv Param NULL \n",__FUNCTION__,__LINE__));
		bProcessFurther = FALSE;
	}

	if( bProcessFurther )
	{
		/* 
		  * If physical address not having any valid data then no need to 
		  * send corresponding host details to Lm-Lite
		  */
		if( '\0' != phost->phyAddr[ 0 ] )
		{
			parameterValStruct_t notif_val[1];
			char				 param_name[256] = "Device.Hosts.X_RDKCENTRAL-COM_LMHost_Sync_From_WiFi";
			char				 component[256]  = "eRT.com.cisco.spvtg.ccsp.lmlite";
			char				 bus[256]		 = "/com/cisco/spvtg/ccsp/lmlite";
			char				 str[2048]		 = {0};
			char*				 faultParam 	 = NULL;
			int 				 ret			 = 0;	
			
			/* 
			* Group Received Associated Params as below,
			* MAC_Address,AssociatedDevice_Alias,SSID_Alias,RSSI_Signal_Strength,Status
			*/
			snprintf(str, sizeof(str), "%s,%s,%s,%d,%d",
										(char*)phost->phyAddr,
										('\0' != phost->AssociatedDevice[ 0 ]) ? (char*)phost->AssociatedDevice : "NULL",
										('\0' != phost->ssid[ 0 ]) ? (char*)phost->ssid : "NULL",
										phost->RSSI,
										phost->Status);
			
			CcspWifiTrace(("RDK_LOG_WARN, %s-%d [%s] \n",__FUNCTION__,__LINE__,(char*)str));
			
			notif_val[0].parameterName	= param_name;
			notif_val[0].parameterValue = str;
			notif_val[0].type			= ccsp_string;
			
			ret = CcspBaseIf_setParameterValues(  bus_handle,
												  component,
												  bus,
												  0,
												  0,
												  notif_val,
												  1,
												  TRUE,
												  &faultParam
												  );
			
			if(ret != CCSP_SUCCESS)
			{
				if ( faultParam ) 
				{
					CCSP_MESSAGE_BUS_INFO *bus_info = (CCSP_MESSAGE_BUS_INFO *)bus_handle;
					bus_info->freefunc(faultParam);
				}

				CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CNNECTED_CLIENT : Sending Notification Fail \n"));
			}
		}
		else
		{
			CcspWifiTrace(("RDK_LOG_WARN, Sending Notification Fail Bcoz NULL MAC Address \n"));
		}
	}
}

/* CosaDMLWiFi_Send_HostDetails_To_LMLite */
void CosaDMLWiFi_Send_FullHostDetails_To_LMLite(LM_wifi_hosts_t *phosts)
{
	BOOL bProcessFurther = TRUE;
	CcspWifiTrace(("RDK_LOG_WARN, %s-%d \n",__FUNCTION__,__LINE__));
	
	/* Validate received param. If it is not valid then dont proceed further */
	if( NULL == phosts )
	{
		CcspWifiTrace(("RDK_LOG_WARN, %s-%d Recv Param NULL \n",__FUNCTION__,__LINE__));
		bProcessFurther = FALSE;
	}

	if( bProcessFurther )
	{
		parameterValStruct_t notif_val[1];
		char				 param_name[256] = "Device.Hosts.X_RDKCENTRAL-COM_LMHost_Sync_From_WiFi";
		char				 component[256]  = "eRT.com.cisco.spvtg.ccsp.lmlite";
		char				 bus[256]		 = "/com/cisco/spvtg/ccsp/lmlite";
		char				 str[2048]		 = {0};
		char*				 faultParam 	 = NULL;
		int 				 ret			 = 0, 
							 i;	
		
		for(i =0; i < phosts->count ; i++)
		{
			/* 
			  * If physical address not having any valid data then no need to 
			  * send corresponding host details to Lm-Lite
			  */
			if( '\0' != phosts->host[i].phyAddr[ 0 ] )
			{
				/* 
				* Group Received Associated Params as below,
				* MAC_Address,AssociatedDevice_Alias,SSID_Alias,RSSI_Signal_Strength,Status
				*/
				snprintf(str, sizeof(str), "%s,%s,%s,%d,%d",
											(char*)phosts->host[i].phyAddr,
											('\0' != phosts->host[i].AssociatedDevice[ 0 ]) ? (char*)phosts->host[i].AssociatedDevice : "NULL",
											('\0' != phosts->host[i].ssid[ 0 ]) ? (char*)phosts->host[i].ssid : "NULL",
											phosts->host[i].RSSI,
											phosts->host[i].Status);
				
				CcspWifiTrace(("RDK_LOG_WARN, %s-%d [%s] \n",__FUNCTION__,__LINE__,(char*)str));
				
				notif_val[0].parameterName	= param_name;
				notif_val[0].parameterValue = str;
				notif_val[0].type			= ccsp_string;
				
				ret = CcspBaseIf_setParameterValues(  bus_handle,
													  component,
													  bus,
													  0,
													  0,
													  notif_val,
													  1,
													  TRUE,
													  &faultParam
													  );
				
				if(ret != CCSP_SUCCESS)
				{
					if ( faultParam ) 
					{
						CCSP_MESSAGE_BUS_INFO *bus_info = (CCSP_MESSAGE_BUS_INFO *)bus_handle;
						bus_info->freefunc(faultParam);
					}

					CcspWifiTrace(("RDK_LOG_WARN, RDKB_WIFI_CNNECTED_CLIENT : Sending Notification Fail \n"));
				}
			}
			else
			{
				CcspWifiTrace(("RDK_LOG_WARN, Sending Notification Fail Bcoz NULL MAC Address \n"));
			}
		}
	}
}

static inline char *to_sta_key    (mac_addr_t mac, sta_key_t key) {
    snprintf(key, STA_KEY_LEN, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return (char *)key;
}

//dispatch the notification here
INT CosaDmlWiFi_AssociatedDevice_callback(INT apIndex, wifi_associated_dev_t *associated_dev) {    
	char mac[32]={0};
	BOOL bEnabled;
        sta_key_t key = {0};
	if(!associated_dev)
		return -1;
	
	cMac_to_sMac(associated_dev->cli_MACAddress, mac);	

        if(client_fast_reconnect(apIndex, to_sta_key(associated_dev->cli_MACAddress, key))) {
                return -1;
        }
	if(apIndex==0 || apIndex==1) {	//for private network
		if(associated_dev->cli_Active == 1) 
		{
			Wifi_Hosts_Sync_Func(NULL,(apIndex+1), associated_dev, 0, 0);		
			if ( ANSC_STATUS_SUCCESS == CosaDmlWiFi_GetPreferPrivateData(&bEnabled) )
			{
				if (bEnabled == TRUE)
				{
					Hotspot_Macfilter_sync(mac);
				}
			}
		}
		else 				
		{
			Wifi_Hosts_Sync_Func((void *)mac, (apIndex+1), associated_dev, 0, 0);		
		}
	} else if (apIndex==4 || apIndex==5 || apIndex==8 || apIndex==9 || apIndex==15) { //for hotspot
    	Send_Notification_for_hotspot(mac, associated_dev->cli_Active, apIndex+1, associated_dev->cli_SignalStrength);
	} else if (apIndex==2 || apIndex==3 ) { //XHS

                if(associated_dev->cli_Active == 1)
                {
                        Wifi_Hosts_Sync_Func(NULL,(apIndex+1), associated_dev, 0, 0);
                }
                else
                {
                       Wifi_Hosts_Sync_Func((void *)mac,(apIndex+1), associated_dev, 0, 0);
                }	
	} else if (apIndex==6 || apIndex==7 ||  apIndex==10 || apIndex==11 ) { //L&F
	      CcspWifiTrace(("RDK_LOG_INFO, RDKB_WIFI_NOTIFY: Connected to:%s%s clientMac:%s\n",apIndex%2?"5.0":"2.4",apIndex<10?"_LNF_PSK_SSID":"_LNF_EAP_SSID",mac));
	} else if (apIndex==14) { //guest
          
	} else {
		//unused ssid
	}	
	return 0;
}

INT CosaDmlWiFi_DisAssociatedDevice_callback(INT apIndex, char *mac, int reason) {    
        wifi_associated_dev_t associated_dev;
        char macAddr[32]={0};
        BOOL bEnabled;
        sta_key_t key = {0};

        memset(&associated_dev, 0, sizeof(wifi_associated_dev_t));
	sMac_to_cMac(mac, associated_dev.cli_MACAddress);
        associated_dev.cli_Active = reason;
        
	cMac_to_sMac(associated_dev.cli_MACAddress, macAddr);

        if(client_fast_redeauth(apIndex, to_sta_key(associated_dev.cli_MACAddress, key))) {
                return -1;
        }
        if(apIndex==0 || apIndex==1) {  //for private network
                if(associated_dev.cli_Active == 1)
                {
                        Wifi_Hosts_Sync_Func(NULL,(apIndex+1), &associated_dev, 0, 1);
                        if ( ANSC_STATUS_SUCCESS == CosaDmlWiFi_GetPreferPrivateData(&bEnabled) )
                        {
                                if (bEnabled == TRUE)
                                {
                                        Hotspot_Macfilter_sync(macAddr);
                                }
                        }
                }
                else
                {
                        Wifi_Hosts_Sync_Func((void *)macAddr, (apIndex+1), &associated_dev, 0, 1);
                }
        } else if (apIndex==4 || apIndex==5 || apIndex==8 || apIndex==9) { //for hotspot
                Send_Notification_for_hotspot(macAddr, associated_dev.cli_Active, apIndex+1, associated_dev.cli_SignalStrength);
        } else if (apIndex==2 || apIndex==3 ) { //XHS

                if(associated_dev.cli_Active == 1)
                {
                        Wifi_Hosts_Sync_Func(NULL,(apIndex+1), &associated_dev, 0, 1);
                }
                else
                {
                       Wifi_Hosts_Sync_Func((void *)macAddr,(apIndex+1), &associated_dev, 0, 1);
                }
        } else if (apIndex==6 || apIndex==7 ||  apIndex==10 || apIndex==11 ) { //L&F
                CcspWifiTrace(("RDK_LOG_INFO, RDKB_WIFI_NOTIFY: connectedTo:%s%s clientMac:%s\n",apIndex%2?"5.0":"2.4",apIndex<10?"_LNF_PSK_SSID":"_LNF_EAP_SSID",macAddr));
        } else if (apIndex==14 || apIndex==15 ) { //guest

        } else {
                //unused ssid
        }
        return 0;
}

#endif //USE_NOTIFY_COMPONENT

int CosaDml_print_uptime( char *log  ) {

#if defined(_COSA_INTEL_USG_ATOM_)
    char RemoteIP[128]="";
    readRemoteIP(RemoteIP, 128,"ARM_ARPING_IP");
    if (RemoteIP[0] != 0 && strlen(RemoteIP) > 0) {
        v_secure_system("/usr/bin/rpcclient %s \"print_uptime %s\" &", RemoteIP, log);
    }
    //>>zqiu: for AXB6
    else {
    v_secure_system("print_uptime \"%s\"", log);
    }
    //<<
#else
    v_secure_system("print_uptime \"%s\"", log);
#endif
	return 0;
}

void *updateBootLogTime() {

/*For other platforms, boot to wifi uptime is printed in waitforbrlan1 thread
  for CBR brlan1 is not applicable,so handling here*/

#if defined(_CBR_PRODUCT_REQ_)
    if ( access( "/var/tmp/boot_to_private_wifi" , F_OK ) != 0 )
    {
        int count 					= 0;
        CHAR SSID1_CUR[COSA_DML_WIFI_MAX_SSID_NAME_LEN] = {0};
	CHAR SSID2_CUR[COSA_DML_WIFI_MAX_SSID_NAME_LEN] = {0};
	CHAR output_AP0[ 16 ]  			        = { 0 },
             output_AP1[ 16 ]  			        = { 0 };
        BOOL enabled_24		       			= FALSE;
	BOOL enabled_5	 				= FALSE;
	int  uptime					= 0;
	int  wRet_24					= RETURN_OK;
	int  wRet_5					= RETURN_OK;
        BOOL skip_24					= FALSE;
	BOOL skip_5					= FALSE;
	BOOL brdcstd_24					= FALSE;
	BOOL brdcstd_5					= FALSE;
	BOOL radio_24_actv				= FALSE;
	BOOL radio_5_actv				= FALSE;
	
	/* In current implementation if we disable radio we will bring down 
	   the interface, but SSID enable status still holds true,so need
	   a separate handling based on radio status*/		

	/* Radio Enable staus for 2.4G*/	
	wRet_24 = wifi_getRadioEnable(0, &radio_24_actv); 
    	if ( (wRet_24 != RETURN_OK) )
	{
                CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : Radio 0 Couldn't find Status\n",__FUNCTION__));
		skip_24 = TRUE;
	}
	else
	{
		if(radio_24_actv == TRUE)
		{
			wRet_24 = wifi_getApEnable(0,&enabled_24);
    			if ( (wRet_24 != RETURN_OK) )
			{
                		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : Index 0 Couldn't find AP enable status\n",__FUNCTION__));
				skip_24 = TRUE;
			}
			else
			{
				wRet_24 = wifi_getSSIDName(0,SSID1_CUR);
    				if ( (wRet_24 != RETURN_OK) )
				{	
                			CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : Index 0 Couldn't find SSID Name\n",__FUNCTION__));
				  /* ?? prerequisities not met, for now skip 2.4 broadcast print*/
					skip_24 = TRUE;
				}
							
			}
		}
	}

	wRet_5 = wifi_getRadioEnable(1, &radio_5_actv);
    	if ( (wRet_5 != RETURN_OK) )
	{
                CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : Radio 1 Couldn't find Status\n",__FUNCTION__));
		skip_5 = TRUE;
	}
	else
	{
		if(radio_5_actv == TRUE)
		{
			wRet_5 = wifi_getApEnable(1,&enabled_5);
    			if ( (wRet_5 != RETURN_OK) )
			{
                		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : Index 1 Couldn't find AP enable status\n",__FUNCTION__));
				skip_5 = TRUE;
			}
			else
			{
				wRet_5 = wifi_getSSIDName(1,SSID2_CUR);
    				if ( (wRet_5 != RETURN_OK) )
				{
                			CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : Index 1 Couldn't find SSID Name\n",__FUNCTION__));
				 /* ?? Prerequsities not met, for no skip 5 broadcast print*/
					skip_5 = TRUE;
				}
				
			}
		}
	}

	count = 0;
        do
        {
	    /* if either radio's are disabled or SSID's are disabled or preconditions not met skip the loop*/	
	    if(((radio_24_actv == FALSE) && (radio_5_actv == FALSE)) ||
			((enabled_24 == FALSE) && (enabled_5 == FALSE)) || 
			((skip_24 == TRUE) && (skip_5 == TRUE)))
	    {
		CcspWifiTrace(("RDK_LOG_WARN,WIFI %s : Both SSID or Radio's disabled,Skip wifi boot time log\n",__FUNCTION__));
		break;
            }		  	   		
            sleep (10);
            count++;

            /* Private WiFi, if any one or both are enabled */
	    /* 1) if both SSID enabled , wait for both SSID to be come up
	       2) if either one of SSID disabled, skip the disabled SSID*/ 	
			
            if((enabled_24 == TRUE) || (enabled_5 == TRUE)) 
	    {
		
		if((skip_24 == FALSE) && (enabled_24 == TRUE) && (brdcstd_24 == FALSE))	
		{
						
			wRet_24 = wifi_getApStatus( 0  , output_AP0 );
    			if ( (wRet_24 != RETURN_OK) )
			{
                		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : Index 0 Couldn't find AP status\n",__FUNCTION__));
				skip_24 = TRUE;
			}
			else
			{
				if((0 == strcmp( output_AP0 ,"Up")) && (brdcstd_24 == FALSE))   
            			{
					get_uptime(&uptime);
					CcspWifiTrace(("RDK_LOG_WARN,Wifi_Broadcast_complete:%d\n",uptime));
					t2_event_d("bootuptime_WifiBroadcasted_split", uptime);
                                        CcspTraceInfo(("Wifi_Name_Broadcasted:%s\n",SSID1_CUR));
					brdcstd_24 = TRUE;
            			}
			}
		}
	
		if((skip_5 == FALSE) && (enabled_5 == TRUE) && (brdcstd_5 == FALSE))
		{	
			wRet_5 = wifi_getApStatus( 1  , output_AP1 );
    			if ( (wRet_5 != RETURN_OK) )
			{ 
                		CcspWifiTrace(("RDK_LOG_ERROR,WIFI %s : Index 1 Couldn't find AP status\n",__FUNCTION__));
				skip_5 = TRUE;
			}
			else
			{
				if(0 == strcmp( output_AP1 ,"Up") && (brdcstd_5 == FALSE))
				{
					get_uptime(&uptime);
					CcspWifiTrace(("RDK_LOG_WARN,Wifi_Broadcast_complete:%d\n",uptime));
					t2_event_d("bootuptime_WifiBroadcasted_split", uptime);
                                        CcspTraceInfo(("Wifi_Name_Broadcasted:%s\n",SSID2_CUR));
					brdcstd_5 = TRUE;
				}
       		 	}
		} 

                /* 2.4Ghz can come up at different time and 5 Ghz at different time. So break the loop
		   if both are finished*/
		if(( (skip_24 == FALSE) && (skip_5 == FALSE) ) && 
			( (enabled_24 == TRUE)  && (enabled_5 == TRUE) ))
                {
                    if(brdcstd_24 == TRUE && brdcstd_5 == TRUE)
                    {
                        v_secure_system( "touch /var/tmp/boot_to_private_wifi");
                        break;
                    }
                }
                else if((skip_24 == FALSE) && 
			((enabled_24 == TRUE)  && (brdcstd_24 == TRUE)))
                {
                    v_secure_system( "touch /var/tmp/boot_to_private_wifi");
                    break;
                }
                else if((skip_5 == FALSE) && 
			((enabled_5 == TRUE) && (brdcstd_5 == TRUE)))
                {
                    v_secure_system( "touch /var/tmp/boot_to_private_wifi");
                    break;
                }
                   	
	    } 				
        } while (count <= 100);
	
    	count = 0;	
    }

#endif /* _CBR_PRODUCT_REQ */


#if !defined(_CBR_PRODUCT_REQ_) && !defined(_HUB4_PRODUCT_REQ_) && !defined(_PLATFORM_RASPBERRYPI_)/* TCCBR-4030*/
    if ( access( "/var/tmp/boot_to_LnF_SSID" , F_OK ) != 0 )
    {
        int count = 0;
        CHAR output_AP6[ 16 ]  = { 0 },
             output_AP7[ 16 ]  = { 0 },
             output_AP10[ 16 ] = { 0 },
             output_AP11[ 16 ] = { 0 };
        do
        {
            sleep (10);
            count++;
            //L&F
            wifi_getApStatus( 6  , output_AP6 );
            wifi_getApStatus( 7  , output_AP7 );
            wifi_getApStatus( 10 , output_AP10 );
            wifi_getApStatus( 11 , output_AP11 );

            CcspTraceWarning(("%s-%d LnF SSID 6:%s 7:%s 10:%s 11:%s\n",
                        __FUNCTION__,
                        __LINE__,
                        output_AP6,
                        output_AP7,
                        output_AP10,
                        output_AP11 ));

            if(( 0 == strcmp( output_AP6 ,"Up" ) ) || \
                    ( 0 == strcmp( output_AP7 ,"Up" ) ) || \
                    ( 0 == strcmp( output_AP10 ,"Up" ) ) || \
                    ( 0 == strcmp( output_AP11 ,"Up" ) )
              )
            {
                struct sysinfo l_sSysInfo;
    		sysinfo(&l_sSysInfo);
    		char uptime[16] = {0};
    		snprintf(uptime, sizeof(uptime), "%ld", l_sSysInfo.uptime);
                print_uptime("boot_to_LnF_SSID_uptime", NULL, uptime);
                v_secure_system( "touch /var/tmp/boot_to_LnF_SSID");
                break;
            }

        } while (count <= 100);
    }
#endif /*TCCBR-4030*/

#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_INTEL_BUG_FIXES_)
    if ( access( "/var/tmp/xfinityready" , F_OK ) != 0 )
    {
        int count = 0;
        CHAR output_AP4[ 16 ]  = { 0 },
             output_AP5[ 16 ]  = { 0 },
             output_AP8[ 16 ] = { 0 },
             output_AP9[ 16 ] = { 0 };
        do
        {
            sleep (10);
            count++;
	    //Xfinity
            wifi_getApStatus( 4  , output_AP4 );
            wifi_getApStatus( 5  , output_AP5 );
            wifi_getApStatus( 8 , output_AP8 );
            wifi_getApStatus( 9 , output_AP9 );

            CcspTraceWarning(("%s-%d Xfinity SSID 4:%s 5:%s 8:%s 9:%s\n",
                                    __FUNCTION__,
                                    __LINE__,
                                    output_AP4,
                                    output_AP5,
                                    output_AP8,
                                    output_AP9 ));

            if(( 0 == strcmp( output_AP4 ,"Up" ) ) || \
                ( 0 == strcmp( output_AP5 ,"Up" ) ) || \
                ( 0 == strcmp( output_AP8 ,"Up" ) ) || \
                ( 0 == strcmp( output_AP9 ,"Up" ) )
              )
            {
               struct sysinfo l_sSysInfo;
               sysinfo(&l_sSysInfo);
               char uptime[16] = {0};
               snprintf(uptime, sizeof(uptime), "%ld", l_sSysInfo.uptime);
               print_uptime("boot_to_xfinity_wifi_uptime", NULL, uptime);
               v_secure_system( "touch /var/tmp/xfinityready");
               break;
            }
        } while (count <= 100);
    }
#endif /* * !_HUB4_PRODUCT_REQ_ */

    pthread_exit(NULL);
}


ANSC_STATUS txRateStrToUint(char *inputStr, UINT *pTxRate)
{
    char *token;
    bool isRateInvalid = TRUE;
    UINT seqCounter = 0;
    char tmpInputString[128] = {0};

    if ((inputStr == NULL) || (pTxRate == NULL))
    {
        CcspWifiTrace(("RDK_LOG_ERROR, %s Invalid Argument\n", __FUNCTION__));
        return ANSC_STATUS_FAILURE;
    }

    snprintf(tmpInputString, sizeof(tmpInputString), "%s", inputStr);

    token = strtok(tmpInputString, ",");
    while (token != NULL)
    {
        isRateInvalid = TRUE;
        for (seqCounter = 0; seqCounter < ARRAY_SZ(wifiDataTxRateMap); seqCounter++)
        {
            if (AnscEqualString(token, wifiDataTxRateMap[seqCounter].DataTxRateStr, TRUE))
            {
                *pTxRate |= wifiDataTxRateMap[seqCounter].DataTxRateEnum;
                //ccspWifiDbgPrint(CCSP_WIFI_TRACE, "%s Token : %s txRate : %d\n", __FUNCTION__, token, *pTxRate);
                isRateInvalid = FALSE;
            }
        }

        if (isRateInvalid == TRUE)
        {
            CcspWifiTrace(("RDK_LOG_ERROR, %s Invalid txrate Token : %s\n", __FUNCTION__, token));
            return ANSC_STATUS_FAILURE;
        }

        token = strtok(NULL, ",");
    }
    return ANSC_STATUS_SUCCESS;
}


INT m_wifi_init() {

    system("print_uptime \"wifi_hal-init_sequence-started\"");

#if defined(_XB6_PRODUCT_REQ_) 
    CcspWifiTrace(("%s Starting Mesh Stop\n",__FUNCTION__));
    v_secure_system("sysevent set wifi_init start");
#endif

	INT ret=wifi_init();
	//Print bootup time when LnF SSID came up from bootup

#if defined(ENABLE_FEATURE_MESHWIFI)
#if defined(_XB6_PRODUCT_REQ_) 
    CcspWifiTrace(("%s Starting Mesh Start\n",__FUNCTION__));
    v_secure_system("sysevent set wifi_init stop");
#else
#if defined(_XF3_PRODUCT_REQ_)
    if (!g_mesh_script_executed) {
        v_secure_system("/usr/ccsp/wifi/mesh_aclmac.sh allow; /usr/ccsp/wifi/mesh_setip.sh; ");
        g_mesh_script_executed = TRUE;
    }
#else
        v_secure_system("/usr/ccsp/wifi/mesh_aclmac.sh allow; /usr/ccsp/wifi/mesh_setip.sh; ");
#endif
        // notify mesh components that wifi init was performed.
        CcspWifiTrace(("RDK_LOG_INFO,WIFI %s : Notify Mesh of wifi_init\n",__FUNCTION__));
	v_secure_system("/usr/bin/sysevent set wifi_init true");
#endif
#endif
   //bootLogTime();
   //updateBootLogTime();

	return ret;
}
//zqiu <<

#if defined(_HUB4_PRODUCT_REQ_)
/* CosaDmlWiFiSetParamValuesForWFA() */
ANSC_STATUS CosaDmlWiFiSetParamValuesForWFA( char *ParamaterName, char *Value, char *ParamType )
{
    CCSP_MESSAGE_BUS_INFO *bus_info              = (CCSP_MESSAGE_BUS_INFO *)bus_handle;
    parameterValStruct_t   param_val[1]          = { 0 };
    char                   component[256]        = "eRT.com.cisco.spvtg.ccsp.wifi";
    char                   bus[256]              = "/com/cisco/spvtg/ccsp/wifi",
	                   acparameterName[256]  = { 0 },
			   acparameterValue[128] = { 0 };
    char                   *faultParam           = NULL;
    int                    ret                   = 0;

    //copy name
    snprintf( acparameterName, sizeof(acparameterName), "%s", ParamaterName );
    param_val[0].parameterName  = acparameterName;

    //copy value
    sprintf( acparameterValue, "%s", Value );
    param_val[0].parameterValue = acparameterValue;

    //Check type
    if( 0 == strcmp( ParamType , "boolean" ) )
    {
      param_val[0].type           = ccsp_boolean;
    }
    else if( 0 == strcmp( ParamType , "string" )  )
    {
	param_val[0].type         = ccsp_string;
    }
    else
    {
	return ANSC_STATUS_FAILURE;
    }

    ret = CcspBaseIf_setParameterValues(
            bus_handle,
            component,
            bus,
            0,
            0,
            (void*)&param_val,
            1,
            TRUE,
            &faultParam
            );

    if( ( ret != CCSP_SUCCESS ) && ( faultParam != NULL ) ) 
    {
        CcspTraceError(("%s-%d Failed to set %s\n",__FUNCTION__,__LINE__,ParamaterName));
        bus_info->freefunc( faultParam );
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
}

void*
CosaDmlWiFiUpdateWiFiConfigurationsForWFACaseThread
    (
        void *arg
    )
{
    int iLoopCount = 0;
    UNREFERENCED_PARAMETER(arg);
    //Sync both private SSID, Security and WPS configurations
    for( iLoopCount = 0; iLoopCount < 2 ; iLoopCount++ )
    { 
       char acTmpSSID[COSA_DML_WIFI_MAX_SSID_NAME_LEN] = { 0 },
            acPassphrase[65]                           = { 0 },
	    acSecurityMode[64]                         = { 0 },
	    acEncryptionMode[16]                       = { 0 };

       char acParamName[256]      = { 0 },
            acTempSecMode[64]     = { 0 },
            acTempEncryptMode[16] = { 0 };

       int  retSSID       = -1,
            retVAP        = -1,
            retSecMode    = -1,
	    retEncrptMode = -1;

       //Get Current SSID Name
       retSSID = wifi_getSSIDName( iLoopCount, acTmpSSID );
  
       //Set SSID Name
       CcspTraceInfo(("%s %d SSID-ret:%d Index:%d\n",__FUNCTION__,__LINE__,retSSID,iLoopCount));
       if( ( 0 == retSSID ) && ( '\0' != acTmpSSID[ 0 ] ) )
       {
          snprintf( acParamName, sizeof(acParamName), "Device.WiFi.SSID.%d.SSID", iLoopCount + 1 );
          CosaDmlWiFiSetParamValuesForWFA( acParamName, acTmpSSID, "string" );
          CcspTraceInfo(("%s %d SSID:%s Index:%d\n",__FUNCTION__,__LINE__,acTmpSSID, iLoopCount));
       }

       //Get Keypassphrase
       retVAP = wifi_getApSecurityKeyPassphrase( iLoopCount, acPassphrase );

       //Set PassPhrase
       CcspTraceInfo(("%s %d Passphrase-ret:%d Index:%d\n",__FUNCTION__,__LINE__,retVAP,iLoopCount));
       if( ( 0 == retVAP ) && ( '\0' != acPassphrase[ 0 ] ) )
       {
           memset( acParamName, 0, sizeof(acParamName) );
           snprintf( acParamName, sizeof(acParamName), "Device.WiFi.AccessPoint.%d.Security.X_COMCAST-COM_KeyPassphrase", iLoopCount + 1 );
           CosaDmlWiFiSetParamValuesForWFA( acParamName, acPassphrase, "string" );
           CcspTraceInfo(("%s %d Passphrase:%s Index:%d\n",__FUNCTION__,__LINE__,acPassphrase, iLoopCount));
       }

       //Get Secuity Mode
       retSecMode = wifi_getApBeaconType( iLoopCount, acSecurityMode );

       //Set Security Mode
       CcspTraceInfo(("%s %d SecurityMode-ret:%d Index:%d\n",__FUNCTION__,__LINE__,retSecMode,iLoopCount));
       if( ( 0 == retSecMode ) && ( '\0' != acSecurityMode[ 0 ] ) )
       {
           memset( acParamName, 0, sizeof(acParamName) );
           snprintf( acParamName, sizeof(acParamName), "Device.WiFi.AccessPoint.%d.Security.ModeEnabled", iLoopCount + 1 );

           if( 0 == strncmp(acSecurityMode,"WPAand11i", strlen("WPAand11i")))
           {
              snprintf( acTempSecMode, sizeof(acTempSecMode), "%s", "WPA-WPA2-Personal" );
           }
           else if( 0 == strncmp(acSecurityMode,"11i", strlen("11i")))
           {
              snprintf( acTempSecMode, sizeof(acTempSecMode), "%s", "WPA2-Personal" );
           }
           else
           {
              snprintf( acTempSecMode, sizeof(acTempSecMode), "%s", "None" );
           }

           CosaDmlWiFiSetParamValuesForWFA( acParamName, acTempSecMode, "string" );
           CcspTraceInfo(("%s %d SecurityMode:%s Index:%d\n",__FUNCTION__,__LINE__,acTempSecMode, iLoopCount));
       }

       //Get Encryption Mode
       retEncrptMode = wifi_getApWpaEncryptionMode( iLoopCount, acEncryptionMode );

       //Set Encryption Mode
       CcspTraceInfo(("%s %d EncryptionMode-ret:%d Index:%d\n",__FUNCTION__,__LINE__,retEncrptMode,iLoopCount));
       if( ( 0 == retEncrptMode ) && ( '\0' != acEncryptionMode[ 0 ] ) )
       {
           memset( acParamName, 0, sizeof(acParamName) );
           snprintf( acParamName, sizeof(acParamName), "Device.WiFi.AccessPoint.%d.Security.X_CISCO_COM_EncryptionMethod", iLoopCount + 1 );

           if (strncmp(acEncryptionMode, "TKIPEncryption",strlen("TKIPEncryption")) == 0)
           {
               snprintf( acTempEncryptMode, sizeof(acTempEncryptMode), "%s", "TKIP" );
           }
           else if (strncmp(acEncryptionMode, "AESEncryption",strlen("AESEncryption")) == 0)
           {
	       snprintf( acTempEncryptMode, sizeof(acTempEncryptMode), "%s", "AES" );
           }
           else if (strncmp(acEncryptionMode, "TKIPandAESEncryption",strlen("TKIPandAESEncryption")) == 0)
           {
               snprintf( acTempEncryptMode, sizeof(acTempEncryptMode), "%s", "AES+TKIP" );
           }

           CosaDmlWiFiSetParamValuesForWFA( acParamName, acTempEncryptMode, "string" );
           CcspTraceInfo(("%s %d EncryptionMode:%s Index:%d\n",__FUNCTION__,__LINE__,acTempEncryptMode, iLoopCount));
       }
   }
   return NULL;
}
 
INT CosaDmlWiFiWFA_Notification(char *event, char *data, int *IsEventProcessed) 
{
   int ret = 0;

   *IsEventProcessed = FALSE;

   //Needs to update SSID, Passphrase, WPS values
   if(strcmp(event, "sync_wpssec_config")==0)
   {
	char *token    = NULL;

	//Event Processed so no need to process further
	*IsEventProcessed = TRUE;

	//APUP|apIndex
	if((token = strtok(data+5, "|"))==NULL) 
	{
            CcspTraceError(("%s %d Bad event data format\n",__FUNCTION__,__LINE__));
            return ANSC_STATUS_FAILURE;
        }     
	  
	if( 0 == strcmp(token, "true") )
	{
           pthread_t WFA_Config_RefreshThread;
           int res;
           pthread_attr_t attr;

	   //Proceed further for update
	   CcspTraceInfo(("%s Started Process %s notification as thread!\n",__FUNCTION__, event));

           pthread_attr_init(&attr);
           pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
           res = pthread_create(&WFA_Config_RefreshThread, &attr, CosaDmlWiFiUpdateWiFiConfigurationsForWFACaseThread, NULL);
           pthread_attr_destroy( &attr );
           if(res != 0) 
           {
               CcspTraceError(("Create %s failed for %d\n",__FUNCTION__,res));
           }
	}	
   }

   return ret;
}
#endif /* _HUB4_PRODUCT_REQ_  */

#if defined(ENABLE_FEATURE_MESHWIFI)
static BOOL WiFiSysEventHandlerStarted=FALSE;
static int sysevent_fd = 0;
static token_t sysEtoken;
static async_id_t async_id[4];
#if defined(_HUB4_PRODUCT_REQ_)
    static async_id_t async_id_wpssec;
#endif

enum {SYS_EVENT_ERROR=-1, SYS_EVENT_OK, SYS_EVENT_TIMEOUT, SYS_EVENT_HANDLE_EXIT, SYS_EVENT_RECEIVED=0x10};

static INT Mesh_Notification(char *event, char *data)
{
        char *token=NULL;
        int ret = 0;
 
        int apIndex=-1;
        int radioIndex=-1;
        char ssidName[COSA_DML_WIFI_MAX_SSID_NAME_LEN]="";
        int channel=0;
        char passphrase[128]="";
	int rc = -1;
        PCOSA_DATAMODEL_WIFI pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
        PSINGLE_LINK_ENTRY pSLinkEntry = NULL;
        PCOSA_DML_WIFI_SSID  pWifiSsid = NULL;
        PCOSA_DML_WIFI_AP      pWifiAp = NULL;

 
        if(!pMyObject) {
            CcspTraceError(("%s Data Model object is NULL!\n",__FUNCTION__));
            return -1;
        }
        if(strncmp(data, "MESH|", 5)!=0) {
            // CcspTraceInfo(("%s Ignore non-mesh notification!\n",__FUNCTION__));
            return -1;  //non mesh notification
        }

        CcspTraceInfo(("%s Received event %s with data = %s\n",__FUNCTION__,event,data));

        if(strcmp(event, "wifi_SSIDName")==0) {
                //MESH|apIndex|ssidName
                if((token = strtok(data+5, "|"))==NULL) {
                    CcspTraceError(("%s Bad event data format\n",__FUNCTION__));
                    return -1;
                }
                sscanf(token, "%d", &apIndex);
                if(apIndex<0 || apIndex>16) {
                    CcspTraceError(("apIndex error:%d\n", apIndex));
                    return -1;
                }
                if((token = strtok(NULL, "|"))==NULL) {
                    CcspTraceError(("%s Bad event data format\n",__FUNCTION__));
                    return -1;
                }
                /*CID:135462 BUFFER_SIZE_WARNING*/
                rc = strcpy_s(ssidName, sizeof(ssidName), token);
		if (rc != 0) {
                    ERR_CHK(rc);
                    return -1;
                }
                ssidName[sizeof(ssidName)-1] ='\0';

                if((pSLinkEntry = AnscQueueGetEntryByIndex(&pMyObject->SsidQueue, apIndex))==NULL) {
                    CcspTraceError(("%s Data Model object not found!\n",__FUNCTION__));
                    return -1;
                }
                if((pWifiSsid=ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry)->hContext)==NULL) {
                    CcspTraceError(("%s Error linking Data Model object!\n",__FUNCTION__));
                    return -1;
                }
                rc = strcpy_s(pWifiSsid->SSID.Cfg.SSID, COSA_DML_WIFI_MAX_SSID_NAME_LEN, ssidName);
		if (rc != 0) {
                    ERR_CHK(rc);
                    return -1;
                }
                return 0;
 
        } else if (strcmp(event, "wifi_RadioChannel")==0) {
                //MESH|radioIndex|channel
                if((token = strtok(data+5, "|"))==NULL) {
                    CcspTraceError(("%s Bad event data format\n",__FUNCTION__));
                    return -1;
                }
                sscanf(token, "%d", &radioIndex);
                if(radioIndex<0 || radioIndex>2) {
                        CcspTraceError(("radioIndex error:%d\n", radioIndex));
                        return -1;
                }
                if((token = strtok(NULL, "|"))==NULL) {
                    CcspTraceError(("%s Bad event data format\n",__FUNCTION__));
                    return -1;
                }
                sscanf(token, "%d", &channel);
                if(channel<0 || channel>165) {
                        CcspTraceError(("channel error:%d\n", channel));
                        return -1;
                }
 
                return 0;
        } else if (strcmp(event, "wifi_ApSecurity")==0) {
                //MESH|apIndex|passphrase|secMode|encMode
                if((token = strtok(data+5, "|"))==NULL) {
                    CcspTraceError(("%s Bad event data format\n",__FUNCTION__));
                    return -1;
                }
                sscanf(token, "%d", &apIndex);
                if(apIndex<0 || apIndex>16) {
                    CcspTraceError(("apIndex error:%d\n", apIndex));
                    return -1;
                }
                if((token = strtok(NULL, "|"))==NULL) {
                    CcspTraceError(("%s Bad event data format\n",__FUNCTION__));
                    return -1;
                }
                rc = strcpy_s(passphrase, sizeof(passphrase), token);
		if (rc != 0) {
                    ERR_CHK(rc);
                    return -1;
                }

                if((pSLinkEntry = AnscQueueGetEntryByIndex(&pMyObject->AccessPointQueue, apIndex))==NULL) {
                   CcspTraceError(("%s Data Model object not found!\n",__FUNCTION__));
                   return -1;
                }
                if((pWifiAp=ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry)->hContext)==NULL) {
                    CcspTraceError(("%s Error linking Data Model object!\n",__FUNCTION__));
                    return -1;
                }
                rc = strcpy_s((char*)pWifiAp->SEC.Cfg.PreSharedKey, 65, passphrase);
		if (rc != 0) {
                    ERR_CHK(rc);
                    return -1;
                }
                rc = strcpy_s((char*)pWifiAp->SEC.Cfg.KeyPassphrase, 65, passphrase);
		if (rc != 0) {
                    ERR_CHK(rc);
                    return -1;
                }
                return 0;
        }
 
        CcspTraceWarning(("unmatch event: %s\n", event));
        return ret;
}

/*
 * Initialize sysevnt
 *   return 0 if success and -1 if failure.
 */
static int wifi_sysevent_init(void)
{
    int rc;

    CcspWifiTrace(("RDK_LOG_INFO,wifi_sysevent_init\n"));

    sysevent_fd = sysevent_open("127.0.0.1", SE_SERVER_WELL_KNOWN_PORT, SE_VERSION, "wifi_agent", &sysEtoken);
    if (!sysevent_fd) {
        return(SYS_EVENT_ERROR);
    }

    /*you can register the event as you want*/

	//register wifi_SSIDName event
    sysevent_set_options(sysevent_fd, sysEtoken, "wifi_SSIDName", TUPLE_FLAG_EVENT);
    rc = sysevent_setnotification(sysevent_fd, sysEtoken, "wifi_SSIDName", &async_id[0]);
    if (rc) {
       return(SYS_EVENT_ERROR);
    }

	//register wifi_RadioChannel event
    sysevent_set_options(sysevent_fd, sysEtoken, "wifi_RadioChannel", TUPLE_FLAG_EVENT);
    rc = sysevent_setnotification(sysevent_fd, sysEtoken, "wifi_RadioChannel", &async_id[1]);
    if (rc) {
       return(SYS_EVENT_ERROR);
    }

	//register wifi_ApSecurity event
    sysevent_set_options(sysevent_fd, sysEtoken, "wifi_ApSecurity", TUPLE_FLAG_EVENT);
    rc = sysevent_setnotification(sysevent_fd, sysEtoken, "wifi_ApSecurity", &async_id[2]);
    if (rc) {
       return(SYS_EVENT_ERROR);
    }

   	//register wifi_ChannelUtilHeal event
    sysevent_set_options(sysevent_fd, sysEtoken, "wifi_ChannelUtilHeal", TUPLE_FLAG_EVENT);
    rc = sysevent_setnotification(sysevent_fd, sysEtoken, "wifi_ChannelUtilHeal", &async_id[3]);
    if (rc) {
       return(SYS_EVENT_ERROR);
    }

#if defined(_HUB4_PRODUCT_REQ_)
        //register  event
    sysevent_set_options(sysevent_fd, sysEtoken, "sync_wpssec_config", TUPLE_FLAG_EVENT);
    rc = sysevent_setnotification(sysevent_fd, sysEtoken, "sync_wpssec_config", &async_id_wpssec);
    if (rc) {
       return(SYS_EVENT_ERROR);
    }
#endif /* _HUB4_PRODUCT_REQ_ */

    CcspWifiTrace(("RDK_LOG_INFO,wifi_sysevent_init - Exit\n"));
    return(SYS_EVENT_OK);
}

/*
 * Listen sysevent notification message
 */
static int wifi_sysvent_listener(void)
{
    int     ret = SYS_EVENT_TIMEOUT;

    char name[128];
    char val[256];
    int namelen = sizeof(name);
    int vallen	= sizeof(val);
    int err;
    async_id_t getnotification_asyncid;
    int  IsEventProcessed = 0;

    CcspWifiTrace(("RDK_LOG_INFO,wifi_sysvent_listener created\n"));

    err = sysevent_getnotification(sysevent_fd, sysEtoken, name, &namelen,  val, &vallen, &getnotification_asyncid); 
    if (err)
    {
        CcspTraceError(("sysevent_getnotification failed with error: %d\n", err));
    }
    else
    {
        CcspTraceWarning(("received notification event %s\n", name));

#if defined(_HUB4_PRODUCT_REQ_)
	//Process WFA Notification
	CosaDmlWiFiWFA_Notification( name, val, &IsEventProcessed );
#endif /* _HUB4_PRODUCT_REQ_ */

	if( 0 == IsEventProcessed )
	{
       	    Mesh_Notification(name,val);
	    ChannelUtil_SelfHeal_Notification(name, val);
	}

	ret = SYS_EVENT_RECEIVED;
    }

    return ret;
}

/*
 * Close sysevent
 */
static int wifi_sysvent_close(void)
{
    /* we are done with this notification, so unregister it using async_id provided earlier */
    sysevent_rmnotification(sysevent_fd, sysEtoken, async_id[0]);
    sysevent_rmnotification(sysevent_fd, sysEtoken, async_id[1]);
    sysevent_rmnotification(sysevent_fd, sysEtoken, async_id[2]);
	sysevent_rmnotification(sysevent_fd, sysEtoken, async_id[3]);

#if defined(_HUB4_PRODUCT_REQ_)
    sysevent_rmnotification(sysevent_fd, sysEtoken, async_id_wpssec);
#endif /* _HUB4_PRODUCT_REQ_ */

    /* close this session with syseventd */
    sysevent_close(sysevent_fd, sysEtoken);

    return (SYS_EVENT_OK);
}

/*
 * check the initialized sysevent status (happened or not happened),
 * if the event happened, call the functions registered for the events previously
 */
static int wifi_check_sysevent_status(int fd, token_t token)
{
    char evtValue[256] = {0};
    int  returnStatus = ANSC_STATUS_SUCCESS;

	CcspWifiTrace(("RDK_LOG_INFO,wifi_check_sysevent_status\n"));

    /*wifi_SSIDName event*/
    if( 0 == sysevent_get(fd, token, "wifi_SSIDName", evtValue, sizeof(evtValue)) && '\0' != evtValue[0] )
    {
		Mesh_Notification("wifi_SSIDName",evtValue);
    }

    /*wifi_RadioChannel event*/
    if( 0 == sysevent_get(fd, token, "wifi_RadioChannel", evtValue, sizeof(evtValue)) && '\0' != evtValue[0] )
    {
		Mesh_Notification("wifi_RadioChannel",evtValue);
    }

    /*wifi_ApSecurity event*/
    if( 0 == sysevent_get(fd, token, "wifi_ApSecurity", evtValue, sizeof(evtValue)) && '\0' != evtValue[0] )
    {
		Mesh_Notification("wifi_ApSecurity",evtValue);
    }

    return returnStatus;
}


/*
 * The sysevent handler thread.
 */
static void *wifi_sysevent_handler_th(void *arg)
{
    int ret = SYS_EVENT_ERROR;
    UNREFERENCED_PARAMETER(arg);
	CcspWifiTrace(("RDK_LOG_INFO,wifi_sysevent_handler_th created\n"));

    while(SYS_EVENT_ERROR == wifi_sysevent_init())
    {
        CcspWifiTrace(("RDK_LOG_INFO,%s: sysevent init failed!\n", __FUNCTION__));
        sleep(1);
    }

    /*first check the events status*/
    wifi_check_sysevent_status(sysevent_fd, sysEtoken);

    while(1)
    {
        ret = wifi_sysvent_listener();
        switch (ret)
        {
            case SYS_EVENT_RECEIVED:
                break;
            default :
                CcspWifiTrace(("RDK_LOG_INFO,The received event status is not expected!\n"));
                break;
        }

        if (SYS_EVENT_HANDLE_EXIT == ret) //end this event handling loop
            break;

        sleep(2);
    }

    wifi_sysvent_close();

    return NULL;
}


/*
 * Create a thread to handle the sysevent asynchronously
 */
static void wifi_handle_sysevent_async(void)
{
    int err;
    pthread_t event_handle_thread;

    if(WiFiSysEventHandlerStarted)
        return;
    else
        WiFiSysEventHandlerStarted = TRUE;

    CcspWifiTrace(("RDK_LOG_INFO,wifi_handle_sysevent_async...\n"));

    pthread_attr_t attr;
    pthread_attr_t *attrp = NULL;

    attrp = &attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
    err = pthread_create(&event_handle_thread, attrp, wifi_sysevent_handler_th, NULL);
    if(attrp != NULL)
        pthread_attr_destroy( attrp );
    if(0 != err)
    {
        CcspWifiTrace(("RDK_LOG_INFO,%s: create the event handle thread error!\n", __FUNCTION__));
    }
}

#endif //ENABLE_FEATURE_MESHWIFI

#endif
//
// Fetch the mesh enable value. We can't call dmcli here since this function is called as part of wifi_init.
// Mesh does not start until CcspWifi has completed initialization.
//
BOOL is_mesh_enabled()
{
    int ret = FALSE;
    char cmd[] = "/usr/bin/syscfg get mesh_enable"; // This should probably change to a PSM value
    char *retBuf = NULL;

    syscfg_executecmd(__FUNCTION__, cmd, &retBuf);

    // The return value should be either NULL (mesh has never been enabled), "true" or "false"
    if (retBuf != NULL && strncmp(retBuf, "true", 4) == 0) {
        ret = TRUE;
    }

    if (retBuf != NULL) {
        free(retBuf);
    }

    return ret;
}

ANSC_STATUS CosaDmlWiFi_startHealthMonitorThread(void)
{
  static BOOL monitor_running = false;

  if (monitor_running == true) {
		fprintf(stderr, "-- %s %d CosaDmlWiFi_startHealthMonitorThread already running\n", __func__, __LINE__);
        return ANSC_STATUS_SUCCESS;
  }

  if ((init_wifi_monitor() < 0)) {
      	fprintf(stderr, "-- %s %d CosaDmlWiFi_startHealthMonitorThread fail\n", __func__, __LINE__);
        return ANSC_STATUS_FAILURE;
  }

  monitor_running = true;
	
#if defined (FEATURE_SUPPORT_PASSPOINT)
  wifi_anqpStartReceivingTestFrame();
#endif 
  
  return ANSC_STATUS_SUCCESS;
}

#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
ANSC_STATUS CosaDmlWiFi_initEasyConnect(PCOSA_DATAMODEL_WIFI pWifiDataModel)
{
#if !defined(_BWG_PRODUCT_REQ_)
#if !defined(_XF3_PRODUCT_REQ_) && !defined(_CBR_PRODUCT_REQ_) && !defined(_HUB4_PRODUCT_REQ_) && !defined(_PLATFORM_RASPBERRYPI_) && !defined(_PLATFORM_TURRIS_)
    if ((init_easy_connect(pWifiDataModel) < 0)) {
        fprintf(stderr, "-- %s %d CosaDmlWiFi_startEasyConnect fail\n", __func__, __LINE__);
        return ANSC_STATUS_FAILURE;
    }
#endif// !defined(_XF3_PRODUCT_REQ_) && !defined(_CBR_PRODUCT_REQ_) && !defined(_HUB4_PRODUCT_REQ_) && !defined(_PLATFORM_RASPBERRYPI_) && !defined(_PLATFORM_TURRIS_)
#endif// !defined(_BWG_PRODUCT_REQ_)
    UNREFERENCED_PARAMETER(pWifiDataModel);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS CosaDmlWiFi_startEasyConnect(unsigned int apIndex, char *staMac, const char *iPubKeyInfoB64, const char *rPubKeyInfoB64, unsigned int channel)
{
    UNREFERENCED_PARAMETER(apIndex);
    UNREFERENCED_PARAMETER(staMac);
    UNREFERENCED_PARAMETER(iPubKeyInfoB64);
    UNREFERENCED_PARAMETER(rPubKeyInfoB64);
    UNREFERENCED_PARAMETER(channel);
    return ANSC_STATUS_SUCCESS;
}
#endif // !defined(_HUB4_PRODUCT_REQ_)

#if defined (FEATURE_SUPPORT_WEBCONFIG)
void CosaDmlWiFiWebConfigFrameworkInit()
{

    if ((init_web_config() < 0)) {
        fprintf(stderr, "-- %s %d Init WiFi Web Config  fail\n", __func__, __LINE__);
        return;
    }
    printf("webconfig init success\n");
    return;
}
#endif

#if defined(_COSA_BCM_MIPS_) || defined(_XB6_PRODUCT_REQ_) || defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_)
#define PARTNER_ID_LEN 64
void FillParamUpdateSource(cJSON *partnerObj, char *key, char *paramUpdateSource)
{
    cJSON *paramObj = cJSON_GetObjectItem( partnerObj, key);
    if ( paramObj != NULL )
    {
        char *valuestr = NULL;
        cJSON *paramObjVal = cJSON_GetObjectItem(paramObj, "UpdateSource");
        if (paramObjVal)
            valuestr = paramObjVal->valuestring;
        if (valuestr != NULL)
        {
            AnscCopyString(paramUpdateSource, valuestr);
            valuestr = NULL;
        }
        else
        {
            CcspTraceWarning(("%s - %s UpdateSource is NULL\n", __FUNCTION__, key ));
        }
    }
    else
    {
        CcspTraceWarning(("%s - %s Object is NULL\n", __FUNCTION__, key ));
    }
}

void FillPartnerIDJournal
    (
        cJSON *json ,
        char *partnerID ,
        PCOSA_DATAMODEL_RDKB_WIFIREGION  pwifiregion
    )
{
                cJSON *partnerObj = cJSON_GetObjectItem( json, partnerID );
                if( partnerObj != NULL)
                {
                      FillParamUpdateSource(partnerObj, "Device.WiFi.X_RDKCENTRAL-COM_Syndication.WiFiRegion.Code", pwifiregion->Code.UpdateSource);
                }
                else
                {
                      CcspTraceWarning(("%s - PARTNER ID OBJECT Value is NULL\n", __FUNCTION__ ));
                }
}

//Get the UpdateSource info from /nvram/bootstrap.json. This is needed to know for override precedence rules in set handlers
ANSC_STATUS
CosaWiFiInitializeParmUpdateSource
    (
        PCOSA_DATAMODEL_RDKB_WIFIREGION  pwifiregion
    )
{
        char *data = NULL;
        cJSON *json = NULL;
        FILE *fileRead = NULL;
        char PartnerID[PARTNER_ID_LEN] = {0};
        int len;
        if (!pwifiregion)
        {
                CcspTraceWarning(("%s-%d : NULL param\n" , __FUNCTION__, __LINE__ ));
                return ANSC_STATUS_FAILURE;
        }

         fileRead = fopen( BOOTSTRAP_INFO_FILE, "r" );
         if( fileRead == NULL )
         {
                 CcspTraceWarning(("%s-%d : Error in opening JSON file\n" , __FUNCTION__, __LINE__ ));
                 return ANSC_STATUS_FAILURE;
         }
         /*CID: 135365 Time of check time of use*/
         
         fseek( fileRead, 0, SEEK_END );
         len = ftell( fileRead );
         /*CID: 104478 Argument cannot be negative*/
         if (len <0) {
             CcspTraceWarning(("%s-%d : File size reads negative \n", __FUNCTION__, __LINE__));
             fclose( fileRead );
             return ANSC_STATUS_FAILURE;
         }
         fseek( fileRead, 0, SEEK_SET );
         data = ( char* )malloc( sizeof(char) * (len + 1) );
         if (data != NULL)
         {
                memset( data, 0, ( sizeof(char) * (len + 1) ));
                /*CID: 104475 Ignoring number of bytes read*/
                if(1 != fread( data, len, 1, fileRead )) {
                   fclose( fileRead );
                   return ANSC_STATUS_FAILURE;
                }
                /*CID:135575 String not null terminated*/
                data[len] = '\0';
         }
         else
         {
                 CcspTraceWarning(("%s-%d : Memory allocation failed \n", __FUNCTION__, __LINE__));
                 fclose( fileRead );
                 return ANSC_STATUS_FAILURE;
         }

         fclose( fileRead );

         if ( data == NULL )
         {
                CcspTraceWarning(("%s-%d : fileRead failed \n", __FUNCTION__, __LINE__));
                return ANSC_STATUS_FAILURE;
         }
         else if ( strlen(data) != 0)
         {
                 json = cJSON_Parse( data );
                 if( !json )
                 {
                         CcspTraceWarning((  "%s : json file parser error : [%d]\n", __FUNCTION__,__LINE__));
                         free(data);
                         return ANSC_STATUS_FAILURE;
                 }
                 else
                 {
                         if( CCSP_SUCCESS == getPartnerId(PartnerID) )
                         {
                                if ( PartnerID[0] != '\0' )
                                {
                                        CcspTraceWarning(("%s : Partner = %s \n", __FUNCTION__, PartnerID));
                                        FillPartnerIDJournal(json, PartnerID, pwifiregion);
                                }
                                else
                                {
                                        CcspTraceWarning(( "Reading Deafult PartnerID Values \n" ));
                                        strcpy(PartnerID, "comcast");
                                        FillPartnerIDJournal(json, PartnerID, pwifiregion);
                                }
                        }
                        else{
                                CcspTraceWarning(("Failed to get Partner ID\n"));
                        }
                        cJSON_Delete(json);
                }
                free(data);
                data = NULL;
         }
         else
         {
                CcspTraceWarning(("BOOTSTRAP_INFO_FILE %s is empty\n", BOOTSTRAP_INFO_FILE));
                /*CID: 104438 Resource leak*/
                free(data);
                data = NULL;
                return ANSC_STATUS_FAILURE;
         }
         return ANSC_STATUS_SUCCESS;
}


static int writeToJson(char *data, char *file)
{
    FILE *fp;
    fp = fopen(file, "w");
    if (fp == NULL)
    {
        CcspTraceWarning(("%s : %d Failed to open file %s\n", __FUNCTION__,__LINE__,file));
        return -1;
    }

    fwrite(data, strlen(data), 1, fp);
    fclose(fp);
    return 0;
}

ANSC_STATUS UpdateJsonParamLegacy
	(
		char*                       pKey,
		char*			PartnerId,
		char*			pValue
    )
{
	cJSON *partnerObj = NULL;
	cJSON *json = NULL;
	FILE *fileRead = NULL;
	char * cJsonOut = NULL;
	char* data = NULL;
	 int len ;
	 int configUpdateStatus = -1;
	 fileRead = fopen( PARTNERS_INFO_FILE, "r" );
	 if( fileRead == NULL ) 
	 {
		 CcspTraceWarning(("%s-%d : Error in opening JSON file\n" , __FUNCTION__, __LINE__ ));
		 return ANSC_STATUS_FAILURE;
	 }
	 
	 fseek( fileRead, 0, SEEK_END );
	 len = ftell( fileRead );
         /*CID: 55623 Argument cannot be negative*/
         if (len < 0) {
              CcspTraceWarning(("%s-%d : FileRead Negative \n", __FUNCTION__, __LINE__));
              fclose( fileRead );
              return ANSC_STATUS_FAILURE;
         }
	 fseek( fileRead, 0, SEEK_SET );
	 data = ( char* )malloc( sizeof(char) * (len + 1) );
	 if (data != NULL) 
	 {
		memset( data, 0, ( sizeof(char) * (len + 1) ));
                /*CID: 70535 Ignoring number of bytes read*/
	 	if(1 != fread( data, len, 1, fileRead )) {
                   fclose( fileRead );
                   return ANSC_STATUS_FAILURE;
                }
                /*CID: 135238 String not null terminated*/
                data[len] ='\0';
	 } 
	 else 
	 {
		 CcspTraceWarning(("%s-%d : Memory allocation failed \n", __FUNCTION__, __LINE__));
		 fclose( fileRead );
		 return ANSC_STATUS_FAILURE;
	 }
	 
	 fclose( fileRead );
	 if ( data == NULL )
	 {
		CcspTraceWarning(("%s-%d : fileRead failed \n", __FUNCTION__, __LINE__));
		return ANSC_STATUS_FAILURE;
	 }
	 else if ( strlen(data) != 0)
	 {
		 json = cJSON_Parse( data );
		 if( !json ) 
		 {
			 CcspTraceWarning((  "%s : json file parser error : [%d]\n", __FUNCTION__,__LINE__));
			 free(data);
			 return ANSC_STATUS_FAILURE;
		 } 
		 else
		 {
			 partnerObj = cJSON_GetObjectItem( json, PartnerId );
			 if ( NULL != partnerObj)
			 {
				 if (NULL != cJSON_GetObjectItem( partnerObj, pKey) )
				 {
					 cJSON_ReplaceItemInObject(partnerObj, pKey, cJSON_CreateString(pValue));
					 cJsonOut = cJSON_Print(json);
					 CcspTraceWarning(( "Updated json content is %s\n", cJsonOut));
					 configUpdateStatus = writeToJson(cJsonOut, PARTNERS_INFO_FILE);
					 if ( !configUpdateStatus)
					 {
						 CcspTraceWarning(( "Updated Value for %s partner\n",PartnerId));
						 CcspTraceWarning(( "Param:%s - Value:%s\n",pKey,pValue));
					 }
					 else
				 	{
						 CcspTraceWarning(( "Failed to update value for %s partner\n",PartnerId));
						 CcspTraceWarning(( "Param:%s\n",pKey));
			 			 cJSON_Delete(json);
						 return ANSC_STATUS_FAILURE;						
				 	}
				 }
				else
			 	{
			 		CcspTraceWarning(("%s - OBJECT  Value is NULL %s\n", pKey,__FUNCTION__ ));
			 		cJSON_Delete(json);
			 		return ANSC_STATUS_FAILURE;
			 	}
			 
			 }
			 else
			 {
			 	CcspTraceWarning(("%s - PARTNER ID OBJECT Value is NULL\n", __FUNCTION__ ));
			 	cJSON_Delete(json);
			 	return ANSC_STATUS_FAILURE;
			 }
			cJSON_Delete(json);
		 }
	  }
	  else
	  {
		CcspTraceWarning(("PARTNERS_INFO_FILE %s is empty\n", PARTNERS_INFO_FILE));
                /*CID: 65542 Resource leak*/
                free(data);
		return ANSC_STATUS_FAILURE;
	  }
	 return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS UpdateJsonParam
        (
                char*                       pKey,
                char*                   PartnerId,
                char*                   pValue,
                char*                   pSource,
                char*                   pCurrentTime
    )
{
        cJSON *partnerObj = NULL;
        cJSON *json = NULL;
        FILE *fileRead = NULL;
        char * cJsonOut = NULL;
        char* data = NULL;
         int len ;
         int configUpdateStatus = -1;
         fileRead = fopen( BOOTSTRAP_INFO_FILE, "r" );
         if( fileRead == NULL )
         {
                 CcspTraceWarning(("%s-%d : Error in opening JSON file\n" , __FUNCTION__, __LINE__ ));
                 return ANSC_STATUS_FAILURE;
         }

         fseek( fileRead, 0, SEEK_END );
         len = ftell( fileRead );
         /*CID: 56120 Argument cannot be negative*/
         if (len < 0) {
             CcspTraceWarning(("%s-%d : fileRead negative \n", __FUNCTION__, __LINE__));
             fclose( fileRead );
             return ANSC_STATUS_FAILURE;
         }
         fseek( fileRead, 0, SEEK_SET );
         data = ( char* )malloc( sizeof(char) * (len + 1) );
         if (data != NULL)
         {
                memset( data, 0, ( sizeof(char) * (len + 1) ));
                /*CID: 70144 Ignoring number of bytes read*/
                if( 1 != fread( data, len, 1, fileRead )) {
                       fclose( fileRead );
                       return ANSC_STATUS_FAILURE;
                }
                /*CID: 135285 String not null terminated*/
                data[len] ='\0';
         }
         else
         {
                 CcspTraceWarning(("%s-%d : Memory allocation failed \n", __FUNCTION__, __LINE__));
                 fclose( fileRead );
                 return ANSC_STATUS_FAILURE;
         }

         fclose( fileRead );
         if ( data == NULL )
         {
                CcspTraceWarning(("%s-%d : fileRead failed \n", __FUNCTION__, __LINE__));
                return ANSC_STATUS_FAILURE;
         }
         else if ( strlen(data) != 0)
         {
                 json = cJSON_Parse( data );
                 if( !json )
                 {
                         CcspTraceWarning((  "%s : json file parser error : [%d]\n", __FUNCTION__,__LINE__));
                         free(data);
                         return ANSC_STATUS_FAILURE;
                 }
                 else
                 {
                         partnerObj = cJSON_GetObjectItem( json, PartnerId );
                         if ( NULL != partnerObj)
                         {
                                 cJSON *paramObj = cJSON_GetObjectItem( partnerObj, pKey);
                                 if (NULL != paramObj )
                                 {
                                         cJSON_ReplaceItemInObject(paramObj, "ActiveValue", cJSON_CreateString(pValue));
                                         cJSON_ReplaceItemInObject(paramObj, "UpdateTime", cJSON_CreateString(pCurrentTime));
                                         cJSON_ReplaceItemInObject(paramObj, "UpdateSource", cJSON_CreateString(pSource));

                                         cJsonOut = cJSON_Print(json);
                                         CcspTraceWarning(( "Updated json content is %s\n", cJsonOut));
                                         configUpdateStatus = writeToJson(cJsonOut, BOOTSTRAP_INFO_FILE);
                                         if ( !configUpdateStatus)
                                         {
                                                 CcspTraceWarning(( "Bootstrap config update: %s, %s, %s, %s \n", pKey, pValue, PartnerId, pSource));
                                         }
                                         else
                                        {
                                                 CcspTraceWarning(( "Failed to update value for %s partner\n",PartnerId));
                                                 CcspTraceWarning(( "Param:%s\n",pKey));
                                                 cJSON_Delete(json);
                                                 return ANSC_STATUS_FAILURE;
                                        }
                                 }
                                else
                                {
                                        CcspTraceWarning(("%s - OBJECT  Value is NULL %s\n", pKey,__FUNCTION__ ));
                                        cJSON_Delete(json);
                                        return ANSC_STATUS_FAILURE;
                                }

                         }
                         else
                         {
                                CcspTraceWarning(("%s - PARTNER ID OBJECT Value is NULL\n", __FUNCTION__ ));
                                cJSON_Delete(json);
                                return ANSC_STATUS_FAILURE;
                         }
                        cJSON_Delete(json);
                 }
          }
          else
          {
                CcspTraceWarning(("BOOTSTRAP_INFO_FILE %s is empty\n", BOOTSTRAP_INFO_FILE));
                /*CID: 72622 Resource leak*/
                free(data);
                return ANSC_STATUS_FAILURE;
          }

          //Also update in the legacy file /nvram/partners_defaults.json for firmware roll over purposes.
          UpdateJsonParamLegacy(pKey, PartnerId, pValue);

         return ANSC_STATUS_SUCCESS;
}
#endif //#if defined(_COSA_BCM_MIPS_) || defined(_XB6_PRODUCT_REQ_) || defined(_COSA_BCM_ARM_)

BOOL CosaDmlWiFi_ValidateEasyConnectSingleChannelString(UINT apIndex, const char *pString)
{
//TODO for triband
    if ((apIndex == 0) && (atoi(pString) >= 1) && (atoi(pString) <= 11)) {
        return true;
    }
    if ((apIndex == 1) && (atoi(pString) >= 36) && (atoi(pString) < 52)) {
        return true;
    }
    if ((apIndex == 1) && (atoi(pString) >= 136) && (atoi(pString) <= 165)) {
        return true;
    }
    return false;
}

#if !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_)
void CosaDmlWiFi_AllPossibleEasyConnectChannels(UINT apIndex, PCOSA_DML_WIFI_DPP_STA_CFG pWifiDppSta)
{
#if !defined(_BWG_PRODUCT_REQ_)
#if !defined(_XF3_PRODUCT_REQ_) && !defined(_CBR_PRODUCT_REQ_) && !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_) && !defined(_PLATFORM_TURRIS_) && !defined(_PLATFORM_RASPBERRYPI_)
    wifi_easy_connect_best_enrollee_channels_t *channels;
    unsigned int i, tmp;
    ULONG op_channel = 0;
    channels = get_easy_connect_best_enrollee_channels(apIndex);
    pWifiDppSta->NumChannels = channels->num;
    for (i = 0; i < channels->num; i++) {
        pWifiDppSta->Channels[i] = channels->channels[i];
    }
    if (wifi_getRadioChannel(apIndex%2, &op_channel) != RETURN_OK) {
        return;
    }
    for (i = 0; i < pWifiDppSta->NumChannels; i++) {
        if (op_channel == pWifiDppSta->Channels[i]) {
            //swap
            tmp = pWifiDppSta->Channels[0];
            pWifiDppSta->Channels[0] = op_channel;
            pWifiDppSta->Channels[i] = tmp;
            break;
        }
    }
#endif //#if !defined(_XF3_PRODUCT_REQ_) && !defined(_CBR_PRODUCT_REQ_) && !defined(_HUB4_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_) && !defined(_PLATFORM_TURRIS_) && !defined(_PLATFORM_RASPBERRYPI_)
#endif //#if !defined(_BWG_PRODUCT_REQ_)
    UNREFERENCED_PARAMETER(apIndex);
    UNREFERENCED_PARAMETER(pWifiDppSta);
}

ANSC_STATUS CosaDmlWiFi_ParseEasyConnectEnrolleeChannels(UINT apIndex, PCOSA_DML_WIFI_DPP_STA_CFG pWifiDppSta, const char *pString)
{
    char tmpStr[256] = {0x0};
    char *ptr, *tmp;
    unsigned int i, j = 0;
    CcspWifiTrace(("RDK_LOG_WARN, %s:%d\n",__FUNCTION__,__LINE__));
    if (strcmp(pString, "") == 0) {
        // Empty String
        CosaDmlWiFi_AllPossibleEasyConnectChannels(apIndex, pWifiDppSta);
    } else {
        memset(tmpStr, 0, sizeof(tmpStr));
        for (i = 0; i < strlen(pString) && i < sizeof(tmpStr); i++) {
            if (((pString[i] >= '0') && (pString[i] <='9')) || pString[i] == ',') {
                tmpStr[j] = pString[i];
                j++;
            }
        }
        // there is only one channel
        if ((ptr = strchr(tmpStr, ',')) == NULL) {
            if (CosaDmlWiFi_ValidateEasyConnectSingleChannelString(apIndex, tmpStr) == true) {
                pWifiDppSta->NumChannels = 1;
                pWifiDppSta->Channels[0] = atoi(tmpStr);
            } else {
                CosaDmlWiFi_AllPossibleEasyConnectChannels(apIndex, pWifiDppSta);
            }
        } else {
            // there are comma separated channels
            ptr = tmpStr;
            tmp = tmpStr;
            pWifiDppSta->NumChannels = 0;
            while ((ptr = strchr(tmp, ',')) != NULL && (pWifiDppSta->NumChannels < 32)) {
                *ptr = 0;
                ptr++;
                if (CosaDmlWiFi_ValidateEasyConnectSingleChannelString(apIndex, tmp) == true) {
                    pWifiDppSta->Channels[pWifiDppSta->NumChannels] = atoi(tmp);
                    pWifiDppSta->NumChannels += 1;
                }
                tmp = ptr;
            }
            if (CosaDmlWiFi_ValidateEasyConnectSingleChannelString(apIndex, tmp) == true && (pWifiDppSta->NumChannels < 32)) {
                pWifiDppSta->Channels[pWifiDppSta->NumChannels] = atoi(tmp);
                pWifiDppSta->NumChannels += 1;
            }
        }
    }
    return ANSC_STATUS_SUCCESS;
}

char *CosaDmlWiFi_ChannelsListToString(PCOSA_DML_WIFI_DPP_STA_CFG pWifiDppSta, char *string)
{
    char tmpStr[8];
    unsigned int i;
    for (i = 0; i < pWifiDppSta->NumChannels; i++) {
        snprintf(tmpStr, sizeof(tmpStr), "%d,", pWifiDppSta->Channels[i]);
        strcat(string, tmpStr);
    }
    if (pWifiDppSta->NumChannels > 0) {
        string[strlen(string) - 1] = 0;
    }
    return string;
}

static void CosaDmlWiFi_StringToChannelsList(char *psmString, PCOSA_DML_WIFI_DPP_STA_CFG pWifiDppSta)
{
    char *tmp, *ptr;
    char string[256];
    int rc = -1;
    pWifiDppSta->NumChannels = 0;

    if ((psmString == NULL) || (strlen(psmString) == 0) || (*psmString == ' ')) {
	return;
    }

    // if psm string is not empty, it is guaranteed to be of the form ch1,ch2,ch3,...
    CcspWifiTrace(("RDK_LOG_WARN, %s-%d, Channels string in psm:%s\n",__FUNCTION__,__LINE__, psmString));
    
    memset(string, 0, sizeof(string));
    rc = strcpy_s(string,  sizeof(string), psmString);
    if (rc != 0) {
        ERR_CHK(rc);
    }
    ptr = string;
    tmp = string;
    while ((ptr = strchr(tmp, ',')) != NULL  && (pWifiDppSta->NumChannels < 32) ) {
        *ptr = 0;
        ptr++;
        pWifiDppSta->Channels[pWifiDppSta->NumChannels] = atoi(tmp);
        pWifiDppSta->NumChannels += 1;
        tmp = ptr;
    }

    pWifiDppSta->Channels[pWifiDppSta->NumChannels] = atoi(tmp);
    pWifiDppSta->NumChannels += 1;
}
#endif // !defined(_HUB4_PRODUCT_REQ_)

BOOL validateDefReportingPeriod(ULONG period)
{
    unsigned int i;

    for (i=0; i < (ARRAY_SZ(INSTClientReprotingPeriods)); i++) {
        if (INSTClientReprotingPeriods[i] == period)
            return TRUE;
    }
    return FALSE;
}

ANSC_STATUS CosaDmlWiFi_InstantMeasurementsEnable(PCOSA_DML_WIFI_AP_ASSOC_DEVICE pWifiApDev, BOOL enable)
{
	monitor_enable_instant_msmt(pWifiApDev->MacAddress, enable);
	return ANSC_STATUS_SUCCESS;
}

BOOL CosaDmlWiFi_IsInstantMeasurementsEnable()
{
	return monitor_is_instant_msmt_enabled();	
}

/* Active Measurement SET/GET calls */
/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFi_IsActiveMeasurementEnable                         */
/*                                                                               */
/* DESCRIPTION   : This function returns the status of the Active Measurement    */
/*                                                                               */
/* INPUT         : NONE                                                          */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : ENABLE / DISABLE                                              */
/*                                                                               */
/*********************************************************************************/

BOOL CosaDmlWiFi_IsActiveMeasurementEnable()
{
    return monitor_is_active_msmt_enabled();
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFi_ActiveMsmtEnable                                  */
/*                                                                               */
/* DESCRIPTION   : This function calls the set function to enable/ disable       */
/*                 Active Measurement.                                           */
/*                                                                               */
/* INPUT         : pHarvester - Pointer to the harvester structure               */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/

ANSC_STATUS CosaDmlWiFi_ActiveMsmtEnable(PCOSA_DML_WIFI_HARVESTER pHarvester)
{
    if (pHarvester == NULL){
        CcspWifiTrace(("RDK_LOG_WARN, %s-%d Recv Param NULL \n",__FUNCTION__,__LINE__));
        return ANSC_STATUS_FAILURE;
    }

    SetActiveMsmtEnable(pHarvester->bActiveMsmtEnabled);
    return ANSC_STATUS_SUCCESS;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFi_ActiveMsmtPktSize                                 */
/*                                                                               */
/* DESCRIPTION   : This function calls the set function to set the Packet size   */
/*                 for Active Measurement.                                       */
/*                                                                               */
/* INPUT         : pHarvester - Pointer to the harvester structure               */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/

ANSC_STATUS CosaDmlWiFi_ActiveMsmtPktSize(PCOSA_DML_WIFI_HARVESTER pHarvester)
{
    if (pHarvester == NULL){
        CcspWifiTrace(("RDK_LOG_WARN, %s-%d Recv Param NULL \n",__FUNCTION__,__LINE__));
        return ANSC_STATUS_FAILURE;
    }

    SetActiveMsmtPktSize(pHarvester->uActiveMsmtPktSize);
    return ANSC_STATUS_SUCCESS;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFi_ActiveMsmtSampleDuration                          */
/*                                                                               */
/* DESCRIPTION   : This function calls the set function to set the sample        */
/*                 interval for Active Measurement.                              */
/*                                                                               */
/* INPUT         : pHarvester - Pointer to the harvester structure               */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/

ANSC_STATUS CosaDmlWiFi_ActiveMsmtSampleDuration(PCOSA_DML_WIFI_HARVESTER pHarvester)
{
    if (pHarvester == NULL){
        CcspWifiTrace(("RDK_LOG_WARN, %s-%d Recv Param NULL \n",__FUNCTION__,__LINE__));
        return ANSC_STATUS_FAILURE;
    }

    SetActiveMsmtSampleDuration(pHarvester->uActiveMsmtSampleDuration);
    return ANSC_STATUS_SUCCESS;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFi_ActiveMsmtNumberOfSamples                         */
/*                                                                               */
/* DESCRIPTION   : This function calls the set function to set the count of      */
/*                 sample for Active Measurement.                                */
/*                                                                               */
/* INPUT         : pHarvester - Pointer to the harvester structure               */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/

ANSC_STATUS CosaDmlWiFi_ActiveMsmtNumberOfSamples(PCOSA_DML_WIFI_HARVESTER pHarvester)
{
    if (pHarvester == NULL){
        CcspWifiTrace(("RDK_LOG_WARN, %s-%d Recv Param NULL \n",__FUNCTION__,__LINE__));
        return ANSC_STATUS_FAILURE;
    }

    SetActiveMsmtNumberOfSamples(pHarvester->uActiveMsmtNumberOfSamples);
    return ANSC_STATUS_SUCCESS;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFiClient_ResetActiveMsmtStep                         */
/*                                                                               */
/* DESCRIPTION   : This function reset the step information in the DML layer     */
/*                                                                               */
/* INPUT         : pHarvester - Pointer to the harvester structure               */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/

ANSC_STATUS CosaDmlWiFiClient_ResetActiveMsmtStep (PCOSA_DML_WIFI_HARVESTER pHarvester)
{
    INT stepCount = 0;
    if (pHarvester == NULL){
        CcspWifiTrace(("RDK_LOG_WARN, %s-%d Recv Param NULL \n",__FUNCTION__,__LINE__));
        return ANSC_STATUS_FAILURE;
    }
    for (stepCount = 0; stepCount < ACTIVE_MSMT_STEP_COUNT; stepCount++)
    {
        pHarvester->Step.StepCfg[stepCount].StepId = 0;
        memset(pHarvester->Step.StepCfg[stepCount].SourceMac, '\0',MAC_ADDRESS_LENGTH);
        memset(pHarvester->Step.StepCfg[stepCount].DestMac, '\0',MAC_ADDRESS_LENGTH);
    }
    return ANSC_STATUS_SUCCESS;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFiClient_SetActiveMsmtPlanId                         */
/*                                                                               */
/* DESCRIPTION   : This function calls the set function to set the Plan ID       */
/*                 for Active Measurement.                                       */
/*                                                                               */
/* INPUT         : pHarvester - Pointer to the harvester structure               */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/

ANSC_STATUS CosaDmlWiFiClient_SetActiveMsmtPlanId (PCOSA_DML_WIFI_HARVESTER pHarvester)
{
    if (pHarvester == NULL){
        CcspWifiTrace(("RDK_LOG_WARN, %s-%d Recv Param NULL \n",__FUNCTION__,__LINE__));
        return ANSC_STATUS_FAILURE;
    }

    SetActiveMsmtPlanID((char*)pHarvester->ActiveMsmtPlanID);
    return ANSC_STATUS_SUCCESS;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlWiFiClient_SetActiveMsmtStepId                         */
/*                                                                               */
/* DESCRIPTION   : This function calls the set function to set the Step ID       */
/*                 for Active Measurement.                                       */
/*                                                                               */
/* INPUT         : StepId - Active Msmt Step ID                                  */
/*                 StepIns - Step Instance Number                                */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/

ANSC_STATUS CosaDmlWiFiClient_SetActiveMsmtStepId (UINT StepId, ULONG StepIns)
{
    CcspWifiTrace(("RDK_LOG_WARN, %s-%d Changed the Step ID to %u for instance : %lu\n",__FUNCTION__,__LINE__,StepId,StepIns));
    SetActiveMsmtStepID(StepId, StepIns);
    return ANSC_STATUS_SUCCESS;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlActiveMsmt_Step_SetSrcMac                              */
/*                                                                               */
/* DESCRIPTION   : This function calls the set function to set the Step SrcMac   */
/*                 for Active Measurement.                                       */
/*                                                                               */
/* INPUT         : SrcMac - Active Msmt Step Source Mac                          */
/*                 StepIns - Step Instance Number                                */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/

ANSC_STATUS CosaDmlActiveMsmt_Step_SetSrcMac (char *SrcMac, ULONG StepIns)
{
    CcspWifiTrace(("RDK_LOG_WARN, %s-%d Changed the Step Id SrcMac to  %s \n",__FUNCTION__,__LINE__,SrcMac));
    SetActiveMsmtStepSrcMac(SrcMac, StepIns);
    return ANSC_STATUS_SUCCESS;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : CosaDmlActiveMsmt_Step_SetDestMac                             */
/*                                                                               */
/* DESCRIPTION   : This function calls the set function to set the Step Dest Mac */
/*                 for Active Measurement.                                       */
/*                                                                               */
/* INPUT         : DestMac - Active Msmt Step Destination Mac                    */
/*                 StepIns - Step Instance Number                                */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/

ANSC_STATUS CosaDmlActiveMsmt_Step_SetDestMac (char *DestMac, ULONG StepIns)
{
    CcspWifiTrace(("RDK_LOG_WARN, %s-%d Changed the Step Id DestMac to  %s \n",__FUNCTION__,__LINE__,DestMac));
    SetActiveMsmtStepDstMac(DestMac, StepIns);
    return ANSC_STATUS_SUCCESS;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : GetActiveMsmtStepInsNum                                       */
/*                                                                               */
/* DESCRIPTION   : This function gets the instance number for the active msmt    */
/*                 Step configuration                                            */
/*                                                                               */
/* INPUT         : pStepCfg - Step configuration                                 */
/*                 StepIns    - Instance number return value                     */
/*                                                                               */
/* OUTPUT        : Instance Number                                               */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/

ANSC_STATUS
GetActiveMsmtStepInsNum(PCOSA_DML_WIFI_ACTIVE_MSMT_STEP_CFG pStepCfg, ULONG *StepIns)
{
    PCOSA_DATAMODEL_WIFI    pMyObject   = (PCOSA_DATAMODEL_WIFI  )g_pCosaBEManager->hWifi;
    PCOSA_DML_WIFI_HARVESTER pHarvester = (PCOSA_DML_WIFI_HARVESTER)pMyObject->pHarvester;
    PCOSA_DML_WIFI_ACTIVE_MSMT_STEP_FULL pStepFull = (PCOSA_DML_WIFI_ACTIVE_MSMT_STEP_FULL) &pHarvester->Step;

    int nIndex;

    for (nIndex = 0; nIndex < ACTIVE_MSMT_STEP_COUNT; nIndex++)
    {
        if ((ANSC_HANDLE)pStepCfg == ((ANSC_HANDLE)&pStepFull->StepCfg[nIndex]))
        {
            *StepIns = nIndex;
            CcspWifiTrace(("RDK_LOG_WARN, %s-%d Instance number is : %d \n",__FUNCTION__,__LINE__,nIndex));
            return ANSC_STATUS_SUCCESS;
        }
    }

    CcspTraceError(("%s:%d : failed to get the instance number\n",__func__, __LINE__));
    return ANSC_STATUS_FAILURE;
}
/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : ValidateActiveMsmtPlanID                                      */
/*                                                                               */
/* DESCRIPTION   : This function checks whether plan ID has been set by          */
/*                 comparing with a dummy variable                               */
/*                                                                               */
/* INPUT         : pPlanId - Plan Identifier                                     */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : ANSC_STATUS_SUCCESS / ANSC_STATUS_FAILURE                     */
/*                                                                               */
/*********************************************************************************/
ANSC_STATUS
ValidateActiveMsmtPlanID(UCHAR *pPlanId)
{
    CHAR CheckStr[PLAN_ID_LEN] = {0};
    if ((strncmp((char*)pPlanId, CheckStr, PLAN_ID_LEN)) == 0)
    {
        CcspTraceError(("%s:%d : Plan ID is not configured\n",__func__, __LINE__));
        return ANSC_STATUS_FAILURE;
    }
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS CosaDmlWiFiClient_InstantMeasurementsEnable(PCOSA_DML_WIFI_HARVESTER pHarvester)
{
    mac_addr_t macAddr;

    if (pHarvester == NULL){
	CcspWifiTrace(("RDK_LOG_WARN, %s-%d Recv Param NULL \n",__FUNCTION__,__LINE__));
        return ANSC_STATUS_FAILURE;
    }

    if (Validate_InstClientMac(pHarvester->MacAddress )){
        str_to_mac_bytes(pHarvester->MacAddress, macAddr);
    }else{
	CcspWifiTrace(("RDK_LOG_WARN, %s-%d Invalid MAC address \n",__FUNCTION__,__LINE__));
        return ANSC_STATUS_FAILURE;
    }

    monitor_enable_instant_msmt(macAddr, pHarvester->bINSTClientEnabled);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS CosaDmlWiFiClient_InstantMeasurementsReportingPeriod(ULONG reportingPeriod)
{
    instant_msmt_reporting_period(reportingPeriod);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS CosaDmlWiFiClient_InstantMeasurementsMacAddress(char *macAddress)
{
    instant_msmt_macAddr(macAddress);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS CosaDmlWiFiClient_InstantMeasurementsOverrideTTL(ULONG overrideTTL)
{
    instant_msmt_ttl(overrideTTL);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS CosaDmlWiFiClient_InstantMeasurementsDefReportingPeriod(ULONG defPeriod)
{
    instant_msmt_def_period(defPeriod);
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS CosaDmlWiFi_ApplyRoamingConsortiumElement(PCOSA_DML_WIFI_AP_CFG pCfg)
{   
    wifi_roamingConsortiumElement_t  elem;
    
    memset(&elem, 0, sizeof(elem));
   
    elem.wifiRoamingConsortiumCount = pCfg->IEEE80211uCfg.RoamCfg.iWIFIRoamingConsortiumCount;
    memcpy(&elem.wifiRoamingConsortiumOui, &pCfg->IEEE80211uCfg.RoamCfg.iWIFIRoamingConsortiumOui, sizeof(elem.wifiRoamingConsortiumOui));
    memcpy(&elem.wifiRoamingConsortiumLen, &pCfg->IEEE80211uCfg.RoamCfg.iWIFIRoamingConsortiumLen, sizeof(elem.wifiRoamingConsortiumLen));
#if defined (FEATURE_SUPPORT_PASSPOINT)
    if ((wifi_pushApRoamingConsortiumElement(pCfg->InstanceNumber - 1, &elem)) != RETURN_OK)
    {  
       CcspWifiTrace(("RDK_LOG_ERROR,wifi_pushApRoamingConsortiumElement returns Error\n"));
       return ANSC_STATUS_FAILURE;
    }
#endif
    return ANSC_STATUS_SUCCESS;
}

#if defined (FEATURE_SUPPORT_INTERWORKING)

void CosaDmlWiFiPsmDelInterworkingEntry()
{
    char recName[256];
    char *strValue = NULL;
    int retPsmGet;
    int apIns;
    for (apIns = 1; apIns <= 16; apIns++) {
 
        memset(recName, 0, 256);
        snprintf(recName, sizeof(recName), InterworkingServiceCapability, apIns);
        retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
        if (retPsmGet == CCSP_SUCCESS) {
            PSM_Del_Record(bus_handle,g_Subsystem, recName);
	    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);

            memset(recName, 0, 256);
            snprintf(recName, sizeof(recName), InterworkingServiceEnable, apIns);
            PSM_Del_Record(bus_handle,g_Subsystem, recName);

            //ASRA
            memset(recName, 0, 256);
            snprintf(recName, sizeof(recName), InterworkingASRAEnable, apIns);
            PSM_Del_Record(bus_handle,g_Subsystem, recName);

            //InternetAvailable
            memset(recName, 0, 256);
            snprintf(recName, sizeof(recName), SetInterworkingInternetAvailable, apIns);
            PSM_Del_Record(bus_handle,g_Subsystem, recName);

            //VenueOption Present
            memset(recName, 0, 256);
            snprintf(recName, sizeof(recName), SetInterworkingVenueOptionPresent, apIns);
            PSM_Del_Record(bus_handle,g_Subsystem, recName);

            //ESR
            memset(recName, 0, 256);
            snprintf(recName, sizeof(recName), InterworkingESREnable, apIns);
            PSM_Del_Record(bus_handle,g_Subsystem, recName);

            //UESA
            memset(recName, 0, 256);
            snprintf(recName, sizeof(recName), InterworkingUESAEnable, apIns);
            PSM_Del_Record(bus_handle,g_Subsystem, recName);

            //HESSOptionPresent
            memset(recName, 0, 256);
            snprintf(recName, sizeof(recName), SetInterworkingHESSID, apIns);
            PSM_Del_Record(bus_handle,g_Subsystem, recName);
            memset(recName, 0, 256);
            snprintf(recName, sizeof(recName), InterworkingHESSOptionPresentEnable, apIns);
            PSM_Del_Record(bus_handle,g_Subsystem, recName);

            //AccessNetworkType
            memset(recName, 0, 256);
            snprintf(recName, sizeof(recName), InterworkingAccessNetworkType, apIns);
            PSM_Del_Record(bus_handle,g_Subsystem, recName);

            //VenueGroup
            memset(recName, 0, 256);
            snprintf(recName, sizeof(recName), SetInterworkingVenueGroup, apIns);
            PSM_Del_Record(bus_handle,g_Subsystem, recName);

            //VenueType
            memset(recName, 0, 256);
            snprintf(recName, sizeof(recName), SetInterworkingVenueType, apIns);
            PSM_Del_Record(bus_handle,g_Subsystem, recName);

            //GAS PauseForServerResponse
            memset(recName, 0, 256);
            snprintf(recName, sizeof(recName), "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.AccessPoint.%d.X_RDKCENTRAL-COM_GASConfiguration.1.PauseForServerResponse", apIns);
            PSM_Del_Record(bus_handle,g_Subsystem, recName);
        }
    }
}

ANSC_STATUS CosaDmlWiFi_ApplyInterworkingElement(PCOSA_DML_WIFI_AP_CFG pCfg)
{
    wifi_InterworkingElement_t  elem;

    memset(&elem, 0, sizeof(elem));

    elem.interworkingEnabled = pCfg->InterworkingEnable;
    elem.accessNetworkType = pCfg->IEEE80211uCfg.IntwrkCfg.iAccessNetworkType;
    elem.internetAvailable = pCfg->IEEE80211uCfg.IntwrkCfg.iInternetAvailable;
    elem.asra = pCfg->IEEE80211uCfg.IntwrkCfg.iASRA;
    elem.esr = pCfg->IEEE80211uCfg.IntwrkCfg.iESR;
    elem.uesa = pCfg->IEEE80211uCfg.IntwrkCfg.iUESA;
    elem.venueOptionPresent = pCfg->IEEE80211uCfg.IntwrkCfg.iVenueOptionPresent;
    elem.venueType = pCfg->IEEE80211uCfg.IntwrkCfg.iVenueType;
    elem.venueGroup = pCfg->IEEE80211uCfg.IntwrkCfg.iVenueGroup;
    elem.hessOptionPresent = pCfg->IEEE80211uCfg.IntwrkCfg.iHESSOptionPresent;
    strcpy(elem.hessid, pCfg->IEEE80211uCfg.IntwrkCfg.iHESSID);

    if ((wifi_pushApInterworkingElement(pCfg->InstanceNumber - 1, &elem)) != RETURN_OK)
    {
       CcspWifiTrace(("RDK_LOG_ERROR,wifi_pushApInterworkingElement returns Error\n"));
       return ANSC_STATUS_FAILURE;
    }
    //Update Venue Group and Type in ANQP Configuration
    CosaDmlWiFi_UpdateANQPVenueInfo(pCfg);

#if defined(ENABLE_FEATURE_MESHWIFI)
    //Update OVS DB
    char *vap_name[] = {"private_ssid_2g", "private_ssid_5g", "iot_ssid_2g", "iot_ssid_5g", "hotspot_open_2g", "hotspot_open_5g", "lnf_psk_2g", "lnf_psk_5g", "hotspot_secure_2g", "hotspot_secure_5g"};
    update_ovsdb_interworking(vap_name[pCfg->InstanceNumber - 1],&elem);
#endif

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS CosaDmlWiFi_setInterworkingElement(PCOSA_DML_WIFI_AP_CFG pCfg)
{
    if ((CosaDmlWiFi_ApplyInterworkingElement(pCfg)) != ANSC_STATUS_SUCCESS)
    {
       CcspWifiTrace(("RDK_LOG_ERROR,CosaDmlWiFi_ApplyInterworkingElement returns Error\n"));
       return ANSC_STATUS_FAILURE;
    }

#if defined(ENABLE_FEATURE_MESHWIFI)
    return ANSC_STATUS_SUCCESS;
#else
    return CosaDmlWiFi_WriteInterworkingConfig(pCfg);
#endif
}


ANSC_STATUS CosaDmlWiFi_getInterworkingElement(PCOSA_DML_WIFI_AP_CFG pCfg, ULONG apIns)
{
    UNREFERENCED_PARAMETER(apIns);
    char *strValue = NULL; 
    char recName[256]={0};
    int retPsmGet = 0;

    //Check RFC status. In case of failure or disabled, set default value and exit.
    snprintf(recName, sizeof(recName), "%s" ,InterworkingRFCEnable);
    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
    if ((retPsmGet != CCSP_SUCCESS) || (strValue == NULL) || (!atoi(strValue))) 
    {
        if(strValue) {
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        }

        CcspTraceDebug(("(%s), InterworkingRFCEnable PSM get Error or Disabled. Setting Default\n", __func__));
        return CosaDmlWiFi_DefaultInterworkingConfig(pCfg);
    }

    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);

    CosaDmlWiFi_InitInterworkingElement(pCfg);

    //Internet Availability
    memset(recName, 0, 256);
    {
        /* Is_Internet_available_thread WAN ping logic causes CISCOXB3-6254, so removed that and as per CSV team's suggestion that WAN link status is always UP, 
           made sysint entries i.e. PSM value for internet (Device.WiFi.AccessPoint.{i}.X_RDKCENTRAL-COM_InterworkingElement.Internet) as TRUE for all vaps statically 
           except Xfinity Hotspot vaps, where its internet is based on runtime Tunnel interface status */
        if ( (pCfg->InstanceNumber == 5) || (pCfg->InstanceNumber == 6) || (pCfg->InstanceNumber == 9) || (pCfg->InstanceNumber == 10) )	//Xfinity hotspot vaps
        {
            int iTun = 0;
            static char *Tunnl_status = "dmsb.hotspot.tunnel.1.Enable";

            /*get Tunnel status for xfinity ssids*/
            retPsmGet = PSM_Get_Record_Value2(bus_handle, g_Subsystem, Tunnl_status, NULL, &strValue);
            if ((retPsmGet != CCSP_SUCCESS) || (strValue == NULL)) {
                CcspTraceError(("(%s), InternetAvailable PSM get Error !!!\n", __func__));
                return ANSC_STATUS_FAILURE;
            }
            iTun = atoi(strValue);
            /*set Tunnel status as internet status for xfinity ssids*/
            pCfg->IEEE80211uCfg.IntwrkCfg.iInternetAvailable = iTun;

            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        } else {      /* Other than Xfinity SSIDs */
            /*Set Internet status for non-xfinity ssids statically configured as true i.e.WAN link status is UP*/
            pCfg->IEEE80211uCfg.IntwrkCfg.iInternetAvailable = 1;
        }
    }

    if(pCfg->InterworkingEnable){//Push settigns to HAL if interworking is enabled
        char  errorCode[128];
        memset(errorCode,0,sizeof(errorCode));
        ULONG len;
        if (InterworkingElement_Validate(pCfg, errorCode, &len) == FALSE) {
            CcspTraceError(("(%s)Interworking Validate Error on (%s) !!!\n", __func__,errorCode));
            return ANSC_STATUS_FAILURE;
        }
        if ((CosaDmlWiFi_ApplyInterworkingElement(pCfg)) != ANSC_STATUS_SUCCESS)
        {
           CcspWifiTrace(("RDK_LOG_ERROR,CosaDmlWiFi_ApplyInterworkingElement returns Error\n"));
           return ANSC_STATUS_FAILURE;
        }
    }

    return ANSC_STATUS_SUCCESS;
}

#endif

#if defined (_HUB4_PRODUCT_REQ_)
//LMLite 
#define LMLITE_DBUS_PATH                     "/com/cisco/spvtg/ccsp/lmlite"
#define LMLITE_COMPONENT_NAME                "eRT.com.cisco.spvtg.ccsp.lmlite"
#define LMLITE_HOSTS_NOE_PARAM_NAME          "Device.Hosts.HostNumberOfEntries"
#define LMLITE_LAYER1_IF_PARAM_NAME          "Device.Hosts.Host.%d.Layer1Interface"
#define LMLITE_ACTIVE_PARAM_NAME             "Device.Hosts.Host.%d.Active"
#define LMLITE_PHY_ADDR_PARAM_NAME           "Device.Hosts.Host.%d.PhysAddress"

/* * CosaDmlWiFi_GetParamValues() */
ANSC_STATUS CosaDmlWiFi_GetParamValues( char *pComponent, char *pBus, char *pParamName, char *pReturnVal )
{
    parameterValStruct_t   **retVal;
    char                    *ParamName[ 1 ];
    int                    ret               = 0,
                           nval;

    //Assign address for get parameter name
    ParamName[0] = pParamName;

    ret = CcspBaseIf_getParameterValues(
                                    bus_handle,
                                    pComponent,
                                    pBus,
                                    ParamName,
                                    1,
                                    &nval,
                                    &retVal);

    //Copy the value
    if( CCSP_SUCCESS == ret )
    {
        if( NULL != retVal[0]->parameterValue )
        {
            memcpy( pReturnVal, retVal[0]->parameterValue, strlen( retVal[0]->parameterValue ) + 1 );
        }

        if( retVal )
        {
            free_parameterValStruct_t (bus_handle, nval, retVal);
        }

        return ANSC_STATUS_SUCCESS;
    }

    if( retVal )
    {
       free_parameterValStruct_t (bus_handle, nval, retVal);
    }
  
    return ANSC_STATUS_FAILURE;
}

/* * CosaDmlWiFi_StartWiFiClientsMonitorAndSyncThread() */
ANSC_STATUS CosaDmlWiFi_StartWiFiClientsMonitorAndSyncThread( void )
{
    pthread_t    WiFiMonitorSyncThread;
    INT          iErrorCode = -1;

    iErrorCode = pthread_create(&WiFiMonitorSyncThread, NULL, &CosaDmlWiFi_WiFiClientsMonitorAndSyncThread, NULL );
    if( 0 != iErrorCode )
    {
        CcspTraceInfo(("%s - Fail to start WiFi clients monitor thread:%d\n",__FUNCTION__,iErrorCode));
        return ANSC_STATUS_FAILURE;
    }

    return ANSC_STATUS_SUCCESS;
}

/* * CosaDmlWiFi_WiFiClientsMonitorAndSyncThread() */
void* CosaDmlWiFi_WiFiClientsMonitorAndSyncThread( void *arg )
{
    UNREFERENCED_PARAMETER(arg);
    COSA_WIFI_CLIENT_CFG    astWiFiClientCfg[128]    = { 0 }; //per radio max is 64
    PCOSA_WIFI_LMHOST_CFG   pstWiFiLMHostCfg         = NULL;

    char                    acTmpReturnValue[256],
                            acTmpQueryParam[256],
                            acAssocListBuffer[512];
    int                     i,j,
                            iTotalActiveClients,
                            iTotalLMHostClients,
                            iTotalLMHostWiFiClients;
 
    //detach thread from caller stack
    pthread_detach(pthread_self());

    while( 1 )
    {
        //Init
        iTotalActiveClients      = 0;
        iTotalLMHostClients      = 0;
        iTotalLMHostWiFiClients  = 0;
        pstWiFiLMHostCfg         = NULL;
        memset( astWiFiClientCfg, 0, sizeof(astWiFiClientCfg) );

        //Sleep for 5mins
        sleep(300);

        //Collect all LMLite WiFi host information
        memset(acTmpReturnValue, 0, sizeof(acTmpReturnValue));
        if( ANSC_STATUS_FAILURE == CosaDmlWiFi_GetParamValues(LMLITE_COMPONENT_NAME, LMLITE_DBUS_PATH, LMLITE_HOSTS_NOE_PARAM_NAME, acTmpReturnValue))
        {
            continue;
        }

        //Total LMLite host count
        iTotalLMHostClients = atoi(acTmpReturnValue);

        //CcspTraceInfo(("%s - Total no of LM Host is:%d\n",__FUNCTION__,iTotalLMHostClients));

        if( 0 == iTotalLMHostClients )
        {
            continue;
        }

        pstWiFiLMHostCfg = (PCOSA_WIFI_LMHOST_CFG)malloc( sizeof(COSA_WIFI_LMHOST_CFG) * iTotalLMHostClients );
        if( NULL == pstWiFiLMHostCfg )
        {
            continue;
        }

        //Init memory
        memset(pstWiFiLMHostCfg, 0, sizeof(COSA_WIFI_LMHOST_CFG) * iTotalLMHostClients);

        for( i = 0 ; i < iTotalLMHostClients; i++ )
        {
            //Get Layer1Interface
            memset(acTmpQueryParam, 0, sizeof(acTmpQueryParam));
            memset(acTmpReturnValue, 0, sizeof(acTmpReturnValue));
            snprintf(acTmpQueryParam, sizeof(acTmpQueryParam), LMLITE_LAYER1_IF_PARAM_NAME, i + 1);
            CosaDmlWiFi_GetParamValues(LMLITE_COMPONENT_NAME, LMLITE_DBUS_PATH, acTmpQueryParam, acTmpReturnValue);

            //Collect only WiFi clients
            if( strstr( acTmpReturnValue, "WiFi" ) )
            {
                 char *pos2 = NULL,
                      *pos5 = NULL;

                 snprintf( pstWiFiLMHostCfg[iTotalLMHostWiFiClients].acLowerLayerInterface1, sizeof( pstWiFiLMHostCfg[iTotalLMHostWiFiClients].acLowerLayerInterface1 ) - 1 , "%s", acTmpReturnValue );

                 //Get Index
                 pos2    = strstr( acTmpReturnValue,".1" );
                 pos5    = strstr( acTmpReturnValue,".2" );

                 if( pos2 != NULL )
                 {
                     pstWiFiLMHostCfg[iTotalLMHostWiFiClients].iVAPIndex = 0;
                 }

                 if( pos5 != NULL )
                 {
                     pstWiFiLMHostCfg[iTotalLMHostWiFiClients].iVAPIndex = 1;
                 }

                 //Get MAC
                 memset(acTmpQueryParam, 0, sizeof(acTmpQueryParam));
                 memset(acTmpReturnValue, 0, sizeof(acTmpReturnValue));
                 snprintf(acTmpQueryParam, sizeof(acTmpQueryParam), LMLITE_PHY_ADDR_PARAM_NAME, i + 1);
                 CosaDmlWiFi_GetParamValues(LMLITE_COMPONENT_NAME, LMLITE_DBUS_PATH, acTmpQueryParam, acTmpReturnValue);

                 snprintf( pstWiFiLMHostCfg[iTotalLMHostWiFiClients].acMACAddress, sizeof(pstWiFiLMHostCfg[iTotalLMHostWiFiClients].acMACAddress) - 1 , "%s", acTmpReturnValue );

                 //Get Active Flag
                 memset(acTmpQueryParam, 0, sizeof(acTmpQueryParam));
                 memset(acTmpReturnValue, 0, sizeof(acTmpReturnValue));
                 snprintf(acTmpQueryParam, sizeof(acTmpQueryParam), LMLITE_ACTIVE_PARAM_NAME, i + 1);
                 CosaDmlWiFi_GetParamValues(LMLITE_COMPONENT_NAME, LMLITE_DBUS_PATH, acTmpQueryParam, acTmpReturnValue);

                 if( 0 == strncmp( acTmpReturnValue, "true", strlen("true") ) )
                 {
                    pstWiFiLMHostCfg[iTotalLMHostWiFiClients].bActive = TRUE;
                 }
                 else
                 {
                    pstWiFiLMHostCfg[iTotalLMHostWiFiClients].bActive = FALSE;
                 }

                 iTotalLMHostWiFiClients++;
            }
        }

        //No need to proceed when no WiFi clients connected
        if( 0 == iTotalLMHostWiFiClients )
        {
            if( NULL != pstWiFiLMHostCfg )
            {
                free(pstWiFiLMHostCfg);
                pstWiFiLMHostCfg = NULL;
            }

            continue;
        }
        //Needs to resync all private connected/disconnected WiFi clients
        //2.4 GHz clients
        memset(acAssocListBuffer, 0, sizeof(acAssocListBuffer));

        if ( ( 0 == wifi_getApAssociatedDevice( 0, acAssocListBuffer, sizeof(acAssocListBuffer) ) ) && 
             ( '\0' != acAssocListBuffer[0] ) 
           )
        { 
            char *token = NULL;

            //Initialize
            token = strtok(acAssocListBuffer, ",");
            while( token != NULL )
            {
              //Copy into string array  
              snprintf( astWiFiClientCfg[iTotalActiveClients].acMACAddress, sizeof( astWiFiClientCfg[iTotalActiveClients].acMACAddress ) - 1, "%s", token );
              astWiFiClientCfg[iTotalActiveClients].iVAPIndex = 0; //2.4GHz
              iTotalActiveClients++;
              
              token = strtok(NULL, ",");
            }
        }

        //5 GHz clients
        memset(acAssocListBuffer, 0, sizeof(acAssocListBuffer));

        if ( ( 0 == wifi_getApAssociatedDevice( 1, acAssocListBuffer, sizeof(acAssocListBuffer) ) ) &&
             ( '\0' != acAssocListBuffer[0] ) 
           )
        {   
            char *token = NULL;
            
            //Initialize
            token = strtok(acAssocListBuffer, ",");
            while( token != NULL )
            { 
              //Copy into string array  
              snprintf( astWiFiClientCfg[iTotalActiveClients].acMACAddress, sizeof(astWiFiClientCfg[iTotalActiveClients].acMACAddress) - 1 ,"%s", token );
              astWiFiClientCfg[iTotalActiveClients].iVAPIndex = 1; //5GHz
              iTotalActiveClients++;
              
              token = strtok(NULL, ",");
            }
        }

        /*
         *
         *  Check with existing host table for below cases,
         *  1. Host table mac is not matches with client list and check active flag is "true" then we should assign back as Offline.
         *  2. Host table mac is matches with client list and check active flag is "false" then we should assign back as Online.
         *
         *  Note:
         *  ----
         *  We should not delete removed client from host table by this case
         *
         */
        
         //Online
         for( i = 0; i < iTotalActiveClients; i++ )
         {
            for( j = 0; j < iTotalLMHostWiFiClients; j++ )
            {
                //Both are equal then result will be TRUE otherwise FALSE
                if( AnscEqualString( pstWiFiLMHostCfg[j].acMACAddress, astWiFiClientCfg[i].acMACAddress, FALSE ) )
                {
                    //Check whether host table status is offline but device attached with driver then we need to set it as online
                    if( FALSE == pstWiFiLMHostCfg[j].bActive )
                    {
                        wifi_associated_dev_t stAssociatedDev = { 0 };
                        char                  acTmpMAC[64]    = { 0 };

                        //Needs to send notification to lmlite
                        snprintf( acTmpMAC, sizeof(acTmpMAC) - 1, "%s", pstWiFiLMHostCfg[j].acMACAddress );
                        sMac_to_cMac( acTmpMAC, stAssociatedDev.cli_MACAddress );

                        stAssociatedDev.cli_Active = 1;
                        CosaDmlWiFi_AssociatedDevice_callback( astWiFiClientCfg[i].iVAPIndex, &stAssociatedDev );
                        CcspTraceInfo(("%s - Synchronize - MAC:%s is Online\n", __FUNCTION__,pstWiFiLMHostCfg[j].acMACAddress ));
                    
                        //For next iteration it should not come in this case
                        pstWiFiLMHostCfg[j].bActive = TRUE;
                    }
                }
             }
          }
           
          //Offline
          for( i = 0; i < iTotalLMHostWiFiClients; i++ )
          {
            int iIsDeletedHost = 1;

            for( j = 0; j < iTotalActiveClients; j++ )
            {
                //Both are equal then result will be TRUE otherwise FALSE
                if( AnscEqualString( pstWiFiLMHostCfg[i].acMACAddress, astWiFiClientCfg[j].acMACAddress, FALSE ) )
                {
                    //Break the loop since this is not deleted host
                    iIsDeletedHost = 0;
                    break;
                }
            }

            //Check whether host table status is online but device detached from driver then we need to set it as offline
            if( ( 1 == iIsDeletedHost ) && ( TRUE == pstWiFiLMHostCfg[i].bActive ) )
            {   
                //Needs to send notificaition to LMLite
                CosaDmlWiFi_DisAssociatedDevice_callback( pstWiFiLMHostCfg[i].iVAPIndex, pstWiFiLMHostCfg[i].acMACAddress, 0 );
                CcspTraceInfo(("%s - Synchronize - MAC:%s is Offline\n", __FUNCTION__,pstWiFiLMHostCfg[i].acMACAddress ));                
                //For next iteration it should not come in this case
                pstWiFiLMHostCfg[i].bActive = FALSE;
            }
         }
          
        //Free allocated resource
        if( NULL != pstWiFiLMHostCfg )
        {
            free(pstWiFiLMHostCfg);
            pstWiFiLMHostCfg = NULL;
        }
     }

     //Exit thread.
     pthread_exit(NULL);

     return NULL;
}
#endif /* * _HUB4_PRODUCT_REQ_ */

