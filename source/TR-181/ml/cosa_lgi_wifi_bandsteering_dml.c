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

#include "ctype.h"
#include "ansc_platform.h"
#include "cosa_wifi_dml.h"
#include "cosa_wifi_internal.h"
#include "plugin_main_apis.h"
#include "wifi_hal.h"
#include "cosa_lgi_wifi_bandsteering_dml.h"
#include "ccsp_dm_api.h"


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

#if 0
/***********************************************************************

 APIs for Object:

    X_LGI_COM_Bandsteering.

    *  LGI_BandSteering_GetParamStringValue
    *  LGI_BandSteering_SetParamStringValue
    *  LGI_BandSteering_Validate
    *  LGI_BandSteering_Commit
    *  LGI_BandSteering_Rollback

***********************************************************************/
ULONG
LGI_BandSteering_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pulSize
    )
{
    if (strcmp(ParamName, "Capability") == 0)
    {
        char capability[64] = "RSSIThresholdAdvanced";

        size_t len = AnscSizeOfString(capability) + 1;
        if (*pulSize < len)
        {
            *pulSize = len;
            return 1;
        }

        AnscCopyString(pValue, capability);
        return 0;
    }
    if (strcmp(ParamName, "Mode") == 0)
    {
        char mode[64] = "RSSIThresholdAdvanced";

        size_t len = AnscSizeOfString(mode) + 1;
        if (*pulSize < len)
        {
            *pulSize = len;
            return 1;
        }

        AnscCopyString(pValue, mode);
        return 0;
    }

    return -1;
}

BOOL
LGI_BandSteering_SetParamStringValue
(
    ANSC_HANDLE                 hInsContext,
    char*                       ParamName,
    char*                       strValue
)
{
    BOOL retValue = FALSE;

    if (strValue == NULL)
    {
        return FALSE;
    }

    if (strcmp(ParamName, "Mode") == 0)
    {
        if(!strcmp(strValue,"RSSIThresholdAdvanced"))
        {
            //Phase 1, only RSSIThresholdAdvanced is supported
            retValue = TRUE;
        }
        else
        {
            return FALSE;
        }

    }

    return retValue;
}
#endif

BOOL
LGI_BandSteering_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    return TRUE;
}

ULONG
LGI_BandSteering_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    /* Do nothing right now */
    return FALSE;

}

ULONG
LGI_BandSteering_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    /* Do nothing right now */
    return FALSE;

}
/***********************************************************************

 APIs for Object:

    X_LGI_COM_Bandsteering.SSID.

    *  LGI_BandSteeringSSID_GetEntryCount
    *  LGI_BandSteeringSSID_GetEntry
    *  LGI_BandSteeringSSID_GetParamBoolValue
    *  LGI_BandSteeringSSID_SetParamBoolValue

***********************************************************************/
ULONG
LGI_BandSteeringSSID_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI           pMyObject       = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;

     return pMyObject->ulBandSteeringSSIDEntryCount;
}


ANSC_HANDLE
LGI_BandSteeringSSID_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
    PCOSA_DATAMODEL_WIFI           pMyObject       = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_DML_BANDSTEERING_SSID    pBandSteeringSSIDEntry = NULL;

    if ( pMyObject->pBandSteeringSSIDTable && nIndex < pMyObject->ulBandSteeringSSIDEntryCount)
    {
        pBandSteeringSSIDEntry = pMyObject->pBandSteeringSSIDTable+nIndex;
        *pInsNumber  = nIndex + 1;

        return pBandSteeringSSIDEntry;
    }
    return NULL;
}

BOOL
LGI_BandSteeringSSID_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    PCOSA_DML_BANDSTEERING_SSID pBandSteeringSSID = hInsContext;
    BOOL retValue = FALSE;

    if (strcmp(ParamName, "Enable") == 0)
    {
        BOOL enable = FALSE;
        wifi_getBandSteeringEnable_perSSID(pBandSteeringSSID->ifIndex,&enable);
        *pBool = enable;
        retValue = TRUE;
    }
    if (strcmp(ParamName, "Active") == 0)
    {
        BOOL active = FALSE;
        wifi_getBandSteeringActive_perSSID(pBandSteeringSSID->ifIndex,&active);
        *pBool = active;
        retValue = TRUE;
    }
    if (strcmp(ParamName, "ClearCapable5G") == 0)
    {
        *pBool = FALSE; //always return false when reading
        retValue = TRUE;
    }
    if (strcmp(ParamName, "ClearBlacklist24G") == 0)
    {
        *pBool = FALSE; //always return false when reading
        retValue = TRUE;
    }

    return retValue;
}

BOOL
LGI_BandSteeringSSID_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
    PCOSA_DML_BANDSTEERING_SSID pBandSteeringSSID  = hInsContext;
    BOOL retValue = FALSE;
    bool bCCEnable = FALSE;

    if (strcmp(ParamName, "Enable") == 0)
    {
        pBandSteeringSSID->Enable = bValue;
        retValue = TRUE;

        enable_reset_both_radio_flag();
    }
    if (strcmp(ParamName, "ClearCapable5G") == 0)
    {
        wifi_setBandSteeringClear5GCapableTable(bValue);
        retValue = TRUE;
    }
    if (strcmp(ParamName, "ClearBlacklist24G") == 0)
    {
        wifi_setBandSteeringClear24GTempBlacklistTable(bValue);
        retValue = TRUE;
    }

    return retValue;
}

BOOL
LGI_BandSteeringSSID_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
    PCOSA_DML_BANDSTEERING_SSID pBandSteeringSSID = hInsContext;

     /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "RSSIThreshold") == 0)
    {
        INT RSSIThr = -70;
        wifi_getBandSteeringRSSIThreshold_perSSID(pBandSteeringSSID->ifIndex, &RSSIThr);
        *pInt = RSSIThr;
        return TRUE;
    }

    return FALSE;
}

BOOL
LGI_BandSteeringSSID_SetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int                         value
    )
{
    PCOSA_DML_BANDSTEERING_SSID pBandSteeringSSID = hInsContext;

    if (strcmp(ParamName, "RSSIThreshold") == 0)
    {
        wifi_setBandSteeringRSSIThreshold_perSSID(pBandSteeringSSID->ifIndex, value);
        enable_reset_both_radio_flag();
        return TRUE;
    }

    return FALSE;
}


BOOL
LGI_BandSteeringSSID_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                        pULong
    )
{
    PCOSA_DML_BANDSTEERING_SSID pBandSteeringSSID = hInsContext;

     /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "DeltaThreshold") == 0)
    {
        ULONG DeltaThreshold= 5;
        wifi_getBandSteeringDeltaThreshold_perSSID(pBandSteeringSSID->ifIndex, (uint*)&DeltaThreshold);
        *pULong = DeltaThreshold;
        return TRUE;
    }
    if (strcmp(ParamName, "BlacklistTimeout") == 0)
    {
        ULONG BlacklistTimeout= 15000;
        wifi_getBandSteeringBlacklistTimeout_perSSID(pBandSteeringSSID->ifIndex, (uint*)&BlacklistTimeout);
        *pULong = BlacklistTimeout;
        return TRUE;
    }

    return FALSE;
}

BOOL
LGI_BandSteeringSSID_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                         value
    )
{
    PCOSA_DML_BANDSTEERING_SSID pBandSteeringSSID = hInsContext;

    if (strcmp(ParamName, "DeltaThreshold") == 0)
    {
        wifi_setBandSteeringDeltaThreshold_perSSID(pBandSteeringSSID->ifIndex, value);
        enable_reset_both_radio_flag();
        return TRUE;
    }
    if (strcmp(ParamName, "BlacklistTimeout") == 0)
    {
        wifi_setBandSteeringBlacklistTimeout_perSSID(pBandSteeringSSID->ifIndex, value);
        enable_reset_both_radio_flag();
        return TRUE;
    }

    return FALSE;
}

BOOL
LGI_BandSteeringSSID_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    PCOSA_DML_BANDSTEERING_SSID pBandSteeringSSID = hInsContext;
    bool bCCEnable = FALSE;
    bool bPlumeNativeAtmBsControl = FALSE;

    if(pBandSteeringSSID->Enable)
    {
        Cdm_GetParamBool("Device.X_LGI-COM_SON.NativeAtmBsControl", &bPlumeNativeAtmBsControl);
        if (bPlumeNativeAtmBsControl)
        {
            _ansc_strcpy(pReturnParamName, "Enable");
            return FALSE;
        }
    }
    return TRUE;
}

ULONG
LGI_BandSteeringSSID_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DML_BANDSTEERING_SSID pBandSteeringSSID = hInsContext;

    wifi_setBandSteeringEnable_perSSID(pBandSteeringSSID->ifIndex, pBandSteeringSSID->Enable);
    return 0;
}

ULONG
LGI_BandSteeringSSID_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DML_BANDSTEERING_SSID pBandSteeringSSID = hInsContext;

    wifi_getBandSteeringEnable_perSSID(pBandSteeringSSID->ifIndex, &(pBandSteeringSSID->Enable));
    return 0;
}

ULONG
LGI_Capable5G_GetEntryCount
    (
        ANSC_HANDLE             hInsContext
    )
{
    PCOSA_DML_BANDSTEERING_SSID pBandSteeringSSID = hInsContext;
    return pBandSteeringSSID->pCapable5GEntryCount;
}

ANSC_HANDLE
LGI_Capable5G_GetEntry
    (
        ANSC_HANDLE             hInsContext,
        ULONG                   nIndex,
        ULONG*                  pInsNumber
    )
{
    PCOSA_DML_BANDSTEERING_SSID pBandSteeringSSID = hInsContext;
    PCOSA_DML_BANDSTEERING_SSID_CAPABLE5G_ENTRY  pCapable5GTable = pBandSteeringSSID->pCapable5G;
    PCOSA_DML_BANDSTEERING_SSID_CAPABLE5G_ENTRY  pCapable5GEntry = NULL;

    pCapable5GEntry = pCapable5GTable + nIndex;
    *pInsNumber = nIndex + 1;
    return pCapable5GEntry;
}

ULONG
LGI_Capable5G_Synchronize
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DML_BANDSTEERING_SSID pBandSteeringSSID = hInsContext;

    if(pBandSteeringSSID->pCapable5G)
    {
        AnscFreeMemory(pBandSteeringSSID->pCapable5G);
        pBandSteeringSSID->pCapable5G = NULL;
        pBandSteeringSSID->pCapable5GEntryCount = 0;
    }
    wifi_getBandSteering5GCapableEntries_perSSID((pBandSteeringSSID->ifIndex+1),
    &pBandSteeringSSID->pCapable5GEntryCount,
    (wifi_5gcapable_table_t **)&pBandSteeringSSID->pCapable5G);

    return 0;
}

static ULONG last_tick_5g;
#define REFRESH_INTERVAL 1
#define TIME_NO_NEGATIVE(x) ((long)(x) < 0 ? 0 : (x))


BOOL
LGI_Capable5G_IsUpdated
    (
        ANSC_HANDLE                 hInsContext
    )
{
    if ( !last_tick_5g )
    {
        last_tick_5g = AnscGetTickInSeconds();
        return TRUE;
    }

    if ( last_tick_5g >= TIME_NO_NEGATIVE(AnscGetTickInSeconds() - REFRESH_INTERVAL) )
    {
        return FALSE;
    }
    else
    {
        last_tick_5g = AnscGetTickInSeconds();
        return TRUE;
    }
}

ULONG
LGI_Capable5G_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_DML_BANDSTEERING_SSID_CAPABLE5G_ENTRY  pCapable5GEntry = hInsContext;

    if (strcmp(ParamName, "MACAddress") == 0)
    {
        AnscCopyString(pValue, pCapable5GEntry->MacAddress);
        return 0;
    }
    if (strcmp(ParamName, "EntryTime") == 0)
    {
        AnscCopyString(pValue, pCapable5GEntry->EntryTime);
        return 0;
    }

    return -1;
}


ULONG
LGI_Blacklist24G_GetEntryCount
    (
        ANSC_HANDLE             hInsContext
    )
{
    PCOSA_DML_BANDSTEERING_SSID pBandSteeringSSID = hInsContext;

    return pBandSteeringSSID->pBlacklist24GEntryCount;
}

ANSC_HANDLE
LGI_Blacklist24G_GetEntry
    (
        ANSC_HANDLE             hInsContext,
        ULONG                   nIndex,
        ULONG*                  pInsNumber
    )
{
    PCOSA_DML_BANDSTEERING_SSID pBandSteeringSSID = hInsContext;
    PCOSA_DML_BANDSTEERING_SSID_BLACKLIST24G_ENTRY  pBlacklist24GTable = pBandSteeringSSID->pBlacklist24G;
    PCOSA_DML_BANDSTEERING_SSID_BLACKLIST24G_ENTRY  pBlacklist24GEntry = NULL;

    pBlacklist24GEntry = pBlacklist24GTable + nIndex;

    *pInsNumber = nIndex + 1;
    return pBlacklist24GEntry;
}

ULONG
LGI_Blacklist24G_Synchronize
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DML_BANDSTEERING_SSID pBandSteeringSSID = hInsContext;

    if(pBandSteeringSSID->pBlacklist24G)
    {
        AnscFreeMemory(pBandSteeringSSID->pBlacklist24G);
        pBandSteeringSSID->pBlacklist24G = NULL;
        pBandSteeringSSID->pBlacklist24GEntryCount = 0;
    }

    wifi_getBandSteering24GBlacklistEntries_perSSID((pBandSteeringSSID->ifIndex+1),
        &pBandSteeringSSID->pBlacklist24GEntryCount,
        (wifi_24gblacklist_table_t **)&pBandSteeringSSID->pBlacklist24G);

    return 0;
}

static ULONG last_tick_24g;

BOOL
LGI_Blacklist24G_IsUpdated
    (
        ANSC_HANDLE                 hInsContext
    )
{
    if ( !last_tick_24g )
    {
        last_tick_24g = AnscGetTickInSeconds();
        return TRUE;
    }

    if ( last_tick_24g >= TIME_NO_NEGATIVE(AnscGetTickInSeconds() - REFRESH_INTERVAL) )
    {
        return FALSE;
    }
    else
    {
        last_tick_24g = AnscGetTickInSeconds();
        return TRUE;
    }
}

ULONG
LGI_Blacklist24G_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_DML_BANDSTEERING_SSID_BLACKLIST24G_ENTRY  pBlacklist24GEntry = hInsContext;

    if (strcmp(ParamName, "MACAddress") == 0)
    {
        AnscCopyString(pValue, pBlacklist24GEntry->MacAddress);
        return 0;
    }
    if (strcmp(ParamName, "EntryTime") == 0)
    {
        AnscCopyString(pValue, pBlacklist24GEntry->EntryTime);
        return 0;
    }

    return -1;
}

BOOL
LGI_Blacklist24G_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                        pULong
    )
{
    PCOSA_DML_BANDSTEERING_SSID_BLACKLIST24G_ENTRY  pBlacklist24GEntry = hInsContext;

    if (strcmp(ParamName, "TimeRemaining") == 0)
    {
        *pULong = pBlacklist24GEntry->TimeRemaining;
        return TRUE;
    }

    return FALSE;
}
