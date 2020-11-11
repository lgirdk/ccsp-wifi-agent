 /*********************************************************************
 * Copyright 2017-2019 LGI Enterprises, LLC.
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
 **********************************************************************/

#ifndef  _COSA_WIFI_ATM_DML_H_
#define  _COSA_WIFI_ATM_DML_H_

#define WIFI_RADIO_COUNT 2
#define WIFI_ATM_CAPABILITY                  "GlobalFairness,WeightedFairness"
#define MAX_NUM_OF_BSS_ON_BAND               8   // PD42279
#define ATM_SSID_NO_WEIGHT                   0
#define ATM_SSID_MIN_WEIGHT                  5
#define ATM_SSID_MAX_WEIGHT                  100
#define WIFI_ATM_DISTRIBUTION_TYPE_DYNAMIC   0
#define WIFI_ATM_DISTRIBUTION_TYPE_STATIC    1
#define ATM_WAIT_THRESHOLD_MIN               0
#define ATM_WAIT_THRESHOLD_MAX               99
#define ATM_MAX_NUMBER_STA_WEIGHT            100
#define ATM_MAC_ADDRESS_LENGTH               18
#define ATM_STA_WEIGHT_MIN                   0
#define ATM_STA_WEIGHT_MAX                   100
#define ATM_STA_TOTAL_WEIGHT_MAX             100
#define ATM_MAX_SSID_LENGTH                  64
#define ATM_MAX_SSID_NUMBER                  8                
#define ATM_STATION_PERIOD                   0
#define ATM_WMM_MAX_WEIGHT                  100



typedef  struct
_COSA_DML_LG_WIFI_ATM_STAT
{
    ULONG                           Index;
    CHAR                            StationMAC[ATM_MAC_ADDRESS_LENGTH];
    ULONG                           SSID;
    ULONG                           StationWeight;
    ULONG                           SSIDWeight;
    ULONG                           MeasuredAirTime;
    ULONG                           TotalAirTime;
}
COSA_DML_LG_WIFI_ATM_STAT, *PCOSA_DML_LG_WIFI_ATM_STAT;

typedef struct
_COSA_DML_LG_WIFI_ATM_INFO
{
    char                            AtmCapability[64];
}
COSA_DML_LG_WIFI_ATM_INFO,  *PCOSA_DML_LG_WIFI_ATM_INFO;

typedef struct
_COSA_DML_LG_WIFI_ATM_STA
{
    BOOL                            Added;
    char                            MacAddress[ATM_MAC_ADDRESS_LENGTH];
    ULONG                           Weight;
}
COSA_DML_LG_WIFI_ATM_STA, *PCOSA_DML_LG_WIFI_ATM_STA;

typedef struct
_COSA_DML_LG_WIFI_ATM_BAND_STA
{
    ULONG                           StaCount;
    COSA_DML_LG_WIFI_ATM_STA     Sta[ATM_MAX_NUMBER_STA_WEIGHT];
}
COSA_DML_LG_WIFI_ATM_BAND_STA, *PCOSA_DML_LG_WIFI_ATM_BAND_STA;

typedef struct
_COSA_DML_LG_WIFI_ATM_RADIO_WMM
{
    ULONG                           BkWeight;
    ULONG                           BeWeight;
    ULONG                           ViWeight;
    ULONG                           VoWeight;
}
COSA_DML_LG_WIFI_ATM_RADIO_WMM, *PCOSA_DML_LG_WIFI_ATM_RADIO_WMM;

typedef struct
_COSA_DML_LG_WIFI_ATM_SSID
{
    ULONG                           SsidWeights;
    CHAR                            SsidDistributionType[24];
}
COSA_DML_LG_WIFI_ATM_SSID, *PCOSA_DML_LG_WIFI_ATM_SSID;



typedef struct
_COSA_DML_LG_WIFI_ATM_RADIO_SSID
{
    COSA_DML_LG_WIFI_ATM_SSID          Ssid[ATM_MAX_SSID_NUMBER];
}
COSA_DML_LG_WIFI_ATM_RADIO_SSID, *PCOSA_DML_LG_WIFI_ATM_RADIO_SSID;


typedef  struct
_COSA_DML_LG_WIFI_ATM_BAND_SETTING
{
    ULONG                           InstanceNumber;
    BOOLEAN                         BandAtmEnable;
    ULONG                           BandAtmWaitThreshold;
    ULONG                           BandAtmMode;
    ULONG                           BandAtmDirection;
    BOOLEAN                         BandEnableWMMApplication;
    COSA_DML_LG_WIFI_ATM_RADIO_WMM  RadioAtmWmmAppl;
    COSA_DML_LG_WIFI_ATM_RADIO_SSID RadioAtmSsid;
    COSA_DML_LG_WIFI_ATM_BAND_STA   BandAtmSta;
    BOOLEAN                         bATMChanged;
}
COSA_DML_LG_WIFI_ATM_BAND_SETTING,  *PCOSA_DML_LG_WIFI_ATM_BAND_SETTING;

typedef  struct
_COSA_DML_LG_WIFI_ATM_FULL
{
    PCOSA_DML_LG_WIFI_ATM_BAND_SETTING  pAtmBandSetting;
    COSA_DML_LG_WIFI_ATM_INFO           AtmInfo;
    ULONG                               RadioCount;
    ULONG                               BandAtmStatCount;
    PCOSA_DML_LG_WIFI_ATM_STAT          pBandAtmStat;
}
COSA_DML_LG_WIFI_ATM,  *PCOSA_DML_LG_WIFI_ATM;

ULONG CosaDmlWiFiAtmBand_GetNumberOfBands(void);
ANSC_STATUS CosaDmlWiFiAtmBand_GetAtmBand(int radioIndex, PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBand);
ANSC_STATUS CosaDmlWiFiAtmBand_SetAtmBand(int radioIndex, PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBand);

ULONG
ATM_Common_GetParamStringValue
    (
        ANSC_HANDLE                  hInsContext,
        char*                        ParamName,
        char*                        pValue,
        ULONG*                       pUlSize
    );

ULONG
ATM_Radio_GetEntryCount
    (
        ANSC_HANDLE                  hInsContext
    );

ANSC_HANDLE
ATM_Radio_GetEntry
    (
        ANSC_HANDLE                  hInsContext,
        ULONG                        nIndex,
        ULONG*                       pInsNumber
    );

BOOL
ATM_Radio_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    );

BOOL
ATM_Radio_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      pUlong
    );

BOOL
ATM_Radio_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    );

BOOL
ATM_Radio_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValuepUlong
    );

BOOL
ATM_Radio_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    );

ULONG
ATM_Radio_Commit
    (
        ANSC_HANDLE                 hInsContext
    );

ULONG
ATM_Radio_Rollback
    (
        ANSC_HANDLE                 hInsContext
    );

BOOL
ATM_Radio_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      pUlong
    );

BOOL
ATM_Radio_WMMApplication_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      pUlong
    );

BOOL
ATM_Radio_WMMApplication_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValuepUlong
    );


ULONG
ATM_Radio_SSID_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    );


ANSC_HANDLE
ATM_Radio_SSID_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    );

BOOL
ATM_Radio_SSID_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      pUlong
    );

ULONG
ATM_Radio_SSID_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    );

BOOL
ATM_Radio_SSID_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValuepUlong
    );

BOOL
ATM_Radio_SSID_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       strValue
    );

ULONG
ATM_Radio_Sta_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    );


ANSC_HANDLE
ATM_Radio_Sta_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    );

ANSC_HANDLE
ATM_Radio_Sta_AddEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG*                      pInsNumber
    );

ULONG
ATM_Radio_Sta_DelEntry
    (
        ANSC_HANDLE                 hInsContext,
        ANSC_HANDLE                 hInstance
    );

BOOL
ATM_Radio_Sta_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      pUlong
    );

ULONG
ATM_Radio_Sta_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    );

BOOL
ATM_Radio_Sta_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValuepUlong
    );

BOOL
ATM_Radio_Sta_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       strValue
    );

BOOL
ATM_Radio_Sta_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    );

ULONG
ATM_Radio_Sta_Commit
    (
        ANSC_HANDLE                 hInsContext
    );

ULONG
ATM_Radio_Sta_Rollback
    (
        ANSC_HANDLE                 hInsContext
    );


ULONG
ATM_Stats_Client_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    );
ANSC_HANDLE
ATM_Stats_Client_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    );
BOOL
ATM_Stats_Client_IsUpdated
    (
        ANSC_HANDLE                 hInsContext
    );
ULONG
ATM_Stats_Client_Synchronize
    (
        ANSC_HANDLE                 hInsContext
    );
BOOL
ATM_Stats_Client_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    );
ULONG
ATM_Stats_Client_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    );
#endif
