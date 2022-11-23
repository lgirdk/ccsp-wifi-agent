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

/***********************************************************************

 APIs for Object:

    Device.WiFi.AccessPoint.{}.Accounting

    *  Accounting_GetParamBoolValue
    *  Accounting_GetParamIntValue
    *  Accounting_GetParamUlongValue
    *  Accounting_GetParamStringValue
    *  Accounting_SetParamIntValue
    *  Accounting_SetParamUlongValue
    *  Accounting_SetParamStringValue
    *  Accounting_Validate
    *  Accounting_Commit
    *  Accounting_Rollback

***********************************************************************/

/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        Accounting_GetParamBoolValue
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
Accounting_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APACCT_FULL       pWifiApAcct   = (PCOSA_DML_WIFI_APACCT_FULL)&pWifiAp->ACCT;

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "Enable") == 0)
    {
        /*
        * The value of this parameter is not part of the device configuration and is
        * always false when read.
        */
        *pBool = pWifiApAcct->Cfg.bEnabled;

        return TRUE;
    }

    return FALSE;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        Accounting_GetParamIntValue
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
Accounting_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int*                        pInt
    )
{
    return FALSE;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        Accounting_GetParamUlongValue
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
Accounting_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APACCT_FULL       pWifiApAcct   = (PCOSA_DML_WIFI_APACCT_FULL)&pWifiAp->ACCT;
    /* check the parameter name and return the corresponding value */

    if (strcmp(ParamName, "InterimInterval") == 0)
    {
        /* collect value */
        *puLong = pWifiApAcct->Cfg.InterimInterval;
        return TRUE;
    }

    if (strcmp(ParamName, "ServerPort") == 0)
    {
        /* collect value */
        *puLong = pWifiApAcct->Cfg.AcctServerPort;
        return TRUE;
    }

    if (strcmp(ParamName, "SecondaryServerPort") == 0)
    {
        /* collect value */
        *puLong = pWifiApAcct->Cfg.SecondaryAcctServerPort;
        return TRUE;
    }

    return FALSE;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        ULONG
        Accounting_GetParamStringValue
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
Accounting_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APACCT_FULL       pWifiApAcct   = (PCOSA_DML_WIFI_APACCT_FULL)&pWifiAp->ACCT;

    if (strcmp(ParamName, "Secret") == 0)
    {
        /* Acct Secret should always return empty string when read */
        AnscCopyString(pValue, "");
        return 0;
    }

    if (strcmp(ParamName, "SecondarySecret") == 0)
    {
        /* Acct Secret should always return empty string when read */
        AnscCopyString(pValue, "");
        return 0;
    }

    if (strcmp(ParamName, "ServerIPAddr") == 0)
    {
        /* Acct Secret should always return empty string when read */
        AnscCopyString(pValue, pWifiApAcct->Cfg.AcctServerIPAddr);
        return 0;
    }

    if (strcmp(ParamName, "SecondaryServerIPAddr") == 0)
    {
        /* Acct Secret should always return empty string when read */
        AnscCopyString(pValue, pWifiApAcct->Cfg.SecondaryAcctServerIPAddr);
        return 0;
    }

    return -1;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        Accounting_SetParamBoolValue
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
Accounting_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APACCT_FULL       pWifiApAcct   = (PCOSA_DML_WIFI_APACCT_FULL)&pWifiAp->ACCT;

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "Enable") == 0)
    {
        if ( bValue != pWifiApAcct->Cfg.bEnabled )
        {
            pWifiApAcct->Cfg.bEnabled = bValue;
            /* To set changes made flag */
            pWifiAp->bSecChanged     = TRUE;
        }
        return TRUE;
    }

    return FALSE;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        Accounting_SetParamIntValue
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
Accounting_SetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        int                         iValue
    )
{
    return FALSE;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        Accounting_SetParamUlongValue
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
Accounting_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APACCT_FULL       pWifiApAcct   = (PCOSA_DML_WIFI_APACCT_FULL)&pWifiAp->ACCT;

    /* check the parameter name and set the corresponding value */
    if (strcmp(ParamName, "InterimInterval") == 0)
    {
        if ( (pWifiApAcct->Cfg.InterimInterval != uValue) && ((uValue == 0) || (uValue >= 60)) )
        {
            /* save update to backup */
            pWifiApAcct->Cfg.InterimInterval = uValue;
            pWifiAp->bSecChanged             = TRUE;
            return TRUE;
        }
    }

    if (strcmp(ParamName, "ServerPort") == 0)
    {
        if ( pWifiApAcct->Cfg.AcctServerPort != uValue )
        {
            /* save update to backup */
            pWifiApAcct->Cfg.AcctServerPort = uValue;
            pWifiAp->bSecChanged             = TRUE;
        }
        return TRUE;
    }

    if (strcmp(ParamName, "SecondaryServerPort") == 0)
    {
        if ( pWifiApAcct->Cfg.SecondaryAcctServerPort != uValue )
        {
            /* save update to backup */
            pWifiApAcct->Cfg.SecondaryAcctServerPort = uValue;
            pWifiAp->bSecChanged             = TRUE;
        }
        return TRUE;
    }

    return FALSE;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        Accounting_SetParamStringValue
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
Accounting_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APACCT_FULL       pWifiApAcct   = (PCOSA_DML_WIFI_APACCT_FULL)&pWifiAp->ACCT;

    if (strcmp(ParamName, "Secret") == 0)
    {
        if ( AnscEqualString(pString, pWifiApAcct->Cfg.AcctSecret, TRUE) )
            return TRUE;

        /* save update to backup */
        AnscCopyString( pWifiApAcct->Cfg.AcctSecret, pString );
        pWifiAp->bSecChanged = TRUE;
        return TRUE;
    }

    if (strcmp(ParamName, "SecondarySecret") == 0)
    {
        if ( AnscEqualString(pString, pWifiApAcct->Cfg.SecondaryAcctSecret, TRUE) )
            return TRUE;

        /* save update to backup */
        AnscCopyString( pWifiApAcct->Cfg.SecondaryAcctSecret, pString );
        pWifiAp->bSecChanged = TRUE;
        return TRUE;
    }

    if (strcmp(ParamName, "ServerIPAddr") == 0)
    {
        if ( AnscEqualString(pString, pWifiApAcct->Cfg.AcctServerIPAddr, TRUE) )
            return TRUE;

        /* save update to backup */
        AnscCopyString( pWifiApAcct->Cfg.AcctServerIPAddr, pString );
        pWifiAp->bSecChanged = TRUE;
        return TRUE;
    }

    if (strcmp(ParamName, "SecondaryServerIPAddr") == 0)
    {
        if ( AnscEqualString(pString, pWifiApAcct->Cfg.SecondaryAcctServerIPAddr, TRUE) )
            return TRUE;

        /* save update to backup */
        AnscCopyString( pWifiApAcct->Cfg.SecondaryAcctServerIPAddr, pString );
        pWifiAp->bSecChanged = TRUE;
        return TRUE;
    }

    return FALSE;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        Accounting_Validate
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
Accounting_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    return TRUE;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        ULONG
        Accounting_Commit
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
Accounting_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI     )g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj      = (PCOSA_CONTEXT_LINK_OBJECT)hInsContext;
    PCOSA_DML_WIFI_AP               pWifiAp       = (PCOSA_DML_WIFI_AP        )pLinkObj->hContext;
    PCOSA_DML_WIFI_APACCT_FULL       pWifiApAcct    = (PCOSA_DML_WIFI_APACCT_FULL)&pWifiAp->ACCT;
    PCOSA_DML_WIFI_APACCT_CFG        pWifiApAcctCfg = (PCOSA_DML_WIFI_APACCT_CFG )&pWifiApAcct->Cfg;
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
        return CosaDmlWiFiApAcctSetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, pWifiApAcctCfg);
    }

    return ANSC_STATUS_FAILURE;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        ULONG
        Accounting_Rollback
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
Accounting_Rollback
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

        CosaDmlWiFiApAcctGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->ACCT);
    }

    return ANSC_STATUS_SUCCESS;
}

