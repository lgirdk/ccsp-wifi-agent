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
#include "cosa_lgi_wifi_softblock_dml.h"
#include "cosa_wifi_apis.h"

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
/***********************************************************************

 APIs for Object:

    X_LGI-COM_SoftBlock.

    *  SoftBlock_GetParamBoolValue
    *  SoftBlock_GetParamUlongValue
    *  SoftBlock_GetParamStringValue
    *  SoftBlock_SetParamBoolValue
    *  SoftBlock_SetParamUlongValue
    *  SoftBlock_SetParamStringValue
    *  SoftBlock_Validate
    *  SoftBlock_Commit
    *  SoftBlock_Rollback

***********************************************************************/

BOOL
SoftBlock_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    BOOL retValue = FALSE;

    if (strcmp(ParamName, "Enable") == 0)
    {
        BOOL enable  = FALSE;
        wifi_getSoftBlockEnable(&enable);
        *pBool = enable;
        retValue = TRUE;
    }
    if (strcmp(ParamName, "ClearClientBlacklist") == 0)
    {
        *pBool = FALSE;   //always return false when reading
        retValue = TRUE;
    }

    return retValue;
}

BOOL
SoftBlock_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char                        *ParamName,
        ULONG                       *puLong
    )
{
    /* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
    return FALSE;
}

ULONG
SoftBlock_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pulSize
    )
{
    strcpy(pValue, "");
    return 0;
}

BOOL
SoftBlock_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
    BOOL retValue = FALSE;
    BOOL enable = 0;

    if (strcmp(ParamName, "Enable") == 0)
    {
        if (wifi_getSoftBlockEnable(&enable) == ANSC_STATUS_SUCCESS)
        {
            if (enable != bValue )
            {
                if (ANSC_STATUS_SUCCESS == wifi_setSoftBlockEnable(bValue))
                {
                    enable_reset_both_radio_flag();
                }
            }
            retValue = TRUE;
        }
    }
    if (strcmp(ParamName, "ClearClientBlacklist") == 0)
    {
        if (wifi_getSoftBlockEnable(&enable) == ANSC_STATUS_SUCCESS)
        {
            if (enable)
            {
                if (ANSC_STATUS_SUCCESS == wifi_clearSoftBlockBlacklist())
                {
                    retValue = TRUE;
                }
            }
            else
            {
                /* Nothing to be done if the value is false */
                retValue = TRUE;
            }
            
        } 
    }
    return retValue;
}

BOOL
SoftBlock_SetParamUlongValue(
    ANSC_HANDLE hInsContext,
    char        *ParamName,
    ULONG       ulValue
    )
{
    return FALSE;
}


BOOL
SoftBlock_SetParamStringValue
(
    ANSC_HANDLE                 hInsContext,
    char*                       ParamName,
    char*                       strValue
)
{
    return FALSE;
}


BOOL
SoftBlock_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    return TRUE;
}

ULONG
SoftBlock_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    /* Do nothing right now */
    return FALSE;

}

ULONG
SoftBlock_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    /* Do nothing right now */
    return FALSE;

}

ULONG
ClientBlacklist_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI           pMyObject       = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    
    return pMyObject->ulClientEntryCount;
}

ANSC_HANDLE
ClientBlacklist_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
   PCOSA_DATAMODEL_WIFI           pMyObject       = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
   COSA_DML_SOFTBLOCKING_CLIENT  *pBlackListTable = (COSA_DML_SOFTBLOCKING_CLIENT*)pMyObject->pBlackListTable;
   COSA_DML_SOFTBLOCKING_CLIENT  *pBlacklistEntry = (COSA_DML_SOFTBLOCKING_CLIENT*)NULL;

   pBlacklistEntry = (COSA_DML_SOFTBLOCKING_CLIENT*)((COSA_DML_SOFTBLOCKING_CLIENT*)pBlackListTable + nIndex);

   *pInsNumber  = nIndex + 1;

   return pBlacklistEntry;
}

ULONG
ClientBlacklist_Synchronize
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI           pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    // Release data allocated previous time
    if( pMyObject->pBlackListTable )
    {
        AnscFreeMemory(pMyObject->pBlackListTable);
        pMyObject->pBlackListTable = NULL;
        pMyObject->ulClientEntryCount = 0;
    }

    if ((wifi_getSoftBlockBlacklistEntries(RADIO_INDEX_2 -1,
            &pMyObject->ulClientEntryCount,
            (wifi_softblock_mac_table_t **)&pMyObject->pBlackListTable)) != ANSC_STATUS_SUCCESS)
    {
        CcspTraceError(("%s, Softblock blacklist failed to synchronize\n",__FUNCTION__));
    }

    return 0;
}

static ULONG last_tick;
#define REFRESH_INTERVAL 30
#define TIME_NO_NEGATIVE(x) ((long)(x) < 0 ? 0 : (x))

BOOL
ClientBlacklist_IsUpdated
    (
        ANSC_HANDLE                 hInsContext
    )
{
    if ( !last_tick )
    {
        last_tick = AnscGetTickInSeconds();

        return TRUE;
    }

    if ( last_tick >= TIME_NO_NEGATIVE(AnscGetTickInSeconds() - REFRESH_INTERVAL) )
    {
        return FALSE;
    }
    else
    {
        last_tick = AnscGetTickInSeconds();

        return TRUE;
    }
}


BOOL
ClientBlacklist_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
    return FALSE;
}

BOOL
ClientBlacklist_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    return FALSE;
}

ULONG
ClientBlacklist_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    COSA_DML_SOFTBLOCKING_CLIENT  *pSoftBlockingClient = (COSA_DML_SOFTBLOCKING_CLIENT*)hInsContext;

    if (pSoftBlockingClient)
    {
        if (strcmp(ParamName, "MacAddress") == 0)
        {
            AnscCopyString(pValue, pSoftBlockingClient->MacAddress);
            return 0;
        }
        if (strcmp(ParamName, "LastAssocTime") == 0)
        {
            AnscCopyString(pValue, pSoftBlockingClient->LastAssocTime);
            return 0;
        }
    }
    else
    {
        CcspTraceError(("%s, Softblocking client NULL\n",__FUNCTION__));
    }
    
    return -1;
}

BOOL
ClientBlacklist_GetParamUlongValue(
        ANSC_HANDLE hInsContext,
        char *ParamName,
        ULONG *pUlong)
{
    return FALSE;
}


