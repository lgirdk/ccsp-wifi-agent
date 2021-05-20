/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
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
    -------------------------------------------------------------------

    environment:

        platform independent

    -------------------------------------------------------------------

    author:

        Richard Yang

    -------------------------------------------------------------------

    revision:

        01/11/2011    initial revision.

**************************************************************************/

#include <telemetry_busmessage_sender.h>
#include "cosa_apis.h"
#include "cosa_wifi_apis.h"
#include "cosa_wifi_internal.h"
#include "plugin_main_apis.h"
#include "ccsp_WifiLog_wrapper.h"
#include "cosa_wifi_apis.h"
#include "cosa_wifi_dml.h"
#include "cosa_harvester_internal.h"
#include "wifi_hal.h"
#include "cosa_wifi_passpoint.h"
#include "wifi_data_plane.h"
#include "secure_wrapper.h"

extern void* g_pDslhDmlAgent;
#if defined(_ENABLE_BAND_STEERING_)
extern ANSC_STATUS CosaDmlWiFi_GetBandSteeringLog_2(void);
extern ANSC_STATUS CosaDmlWiFi_GetBandSteeringLog_3(void);
void* StartBandsteeringLogging( void *arg );
extern ULONG BandsteerLoggingInterval;
int print_Interval_BS_Status=0;
/**************************************************************************
*
*	Function Definitions
*
**************************************************************************/
void* StartBandsteeringLogging( void *arg )
{
        UNREFERENCED_PARAMETER(arg);
        BOOL enable  = FALSE;
        fprintf(stderr, "RDK_LOG_INFO, WIFI entering  %s\n", __FUNCTION__);
	//print_Interval_BS_Status=0;
        //ret=wifi_getBandSteeringEnable( &enable );   
	//CcspWifiTrace(("RDK_LOG_WARN, BANDSTEERING_ENABLE_STATUS:%s\n",(enable)?"true":"false"));

        while (1)
        {
        	wifi_getBandSteeringEnable( &enable );
		/*
	        if (print_Interval_BS_Status==6*BandsteerLoggingInterval) 
		{	
			CcspWifiTrace(("RDK_LOG_WARN, BANDSTEERING_ENABLE_STATUS:%s\n",(enable)?"true":"false"));
			print_Interval_BS_Status=0;
                        
                }
		*/
		CcspWifiTrace(("RDK_LOG_WARN, BANDSTEERING_ENABLE_STATUS:%s\n",(enable)?"true":"false"));
                t2_event_d("WIFI_INFO_BSEnabled", 1);
		if(enable) 
		{
            		CosaDmlWiFi_GetBandSteeringLog_2();
		}
		
            	//CosaDmlWiFi_GetBandSteeringLog_3();
            	sleep(BandsteerLoggingInterval);
                //print_Interval_BS_Status=print_Interval_BS_Status+BandsteerLoggingInterval;
	}
}
#endif



/**********************************************************************

    caller:     owner of the object

    prototype:

        ANSC_HANDLE
        CosaWifiCreate
            (
            );

    description:

        This function constructs cosa wifi object and return handle.

    argument:  

    return:     newly created wifi object.

**********************************************************************/

ANSC_HANDLE
CosaWifiCreate
    (
        VOID
    )
{
	PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI)NULL;

    /*
     * We create object by first allocating memory for holding the variables and member functions.
     */
    pMyObject = (PCOSA_DATAMODEL_WIFI)AnscAllocateMemory(sizeof(COSA_DATAMODEL_WIFI));

    if ( !pMyObject )
    {
        return  (ANSC_HANDLE)NULL;
    }

    /*
     * Initialize the common variables and functions for a container object.
     */
    pMyObject->Oid               = COSA_DATAMODEL_WIFI_OID;
    pMyObject->Create            = CosaWifiCreate;
    pMyObject->Remove            = CosaWifiRemove;
    pMyObject->Initialize        = CosaWifiInitialize;

    pMyObject->Initialize   ((ANSC_HANDLE)pMyObject);

    return  (ANSC_HANDLE)pMyObject;
}

void* updateCiruitIdThread(void *arg)
{
	UNREFERENCED_PARAMETER(arg);
	BOOL ret = FALSE;
	int count = 1;
    while ((!ret) && count <= 3) {
        sleep(15);
        ret = UpdateCircuitId();
		count++;
    }
    return NULL;
}


/**********************************************************************

    caller:     self

    prototype:

        ANSC_STATUS
        CosaWifiInitialize
            (
                ANSC_HANDLE                 hThisObject
            );

    description:

        This function initiate  cosa wifi object and return handle.

    argument:	ANSC_HANDLE                 hThisObject
            This handle is actually the pointer of this object
            itself.

    return:     operation status.

**********************************************************************/

ANSC_STATUS
CosaWifiInitialize
    (
        ANSC_HANDLE                 hThisObject
    )
{
    ANSC_STATUS                     returnStatus        = ANSC_STATUS_SUCCESS;
    PCOSA_DATAMODEL_WIFI            pMyObject           = (PCOSA_DATAMODEL_WIFI)hThisObject;
    ULONG                           uIndex              = 0; 
    ULONG                           uMacFiltIdx         = 0; 
    ULONG                           uSsidCount          = 0;
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
    ULONG                           ssidIndex           = 0;
#endif
    ULONG                           uMacFiltCount       = 0;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoCOSA     = (PPOAM_IREP_FOLDER_OBJECT )NULL;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoWifi     = (PPOAM_IREP_FOLDER_OBJECT )NULL;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoWifiSsid = (PPOAM_IREP_FOLDER_OBJECT )NULL;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoWifiAP   = (PPOAM_IREP_FOLDER_OBJECT )NULL;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoMacFilt  = (PPOAM_IREP_FOLDER_OBJECT )NULL;
	PCOSA_DATAMODEL_RDKB_WIFIREGION			pWiFiRegion = (PCOSA_DATAMODEL_RDKB_WIFIREGION)NULL;
    PCOSA_DML_WIFI_RADIO            pWifiRadio          = NULL;
	PCOSA_DML_WIFI_BANDSTEERING		pWifiBandSteering   = (PCOSA_DML_WIFI_BANDSTEERING )NULL;
    PCOSA_DML_WIFI_HARVESTER        pHarvester          = (PCOSA_DML_WIFI_HARVESTER)NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid           = (PCOSA_DML_WIFI_SSID      )NULL;
    PCOSA_DML_WIFI_AP               pWifiAp             = (PCOSA_DML_WIFI_AP        )NULL;        
    PCOSA_DML_WIFI_AP_MAC_FILTER    pMacFilt            = (PCOSA_DML_WIFI_AP_MAC_FILTER        )NULL;        
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj            = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pMacFiltLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PSLAP_VARIABLE                  pSlapVariable       = (PSLAP_VARIABLE           )NULL;
    PSINGLE_LINK_ENTRY              pSLinkEntry         = (PSINGLE_LINK_ENTRY       )NULL;
   
    /*ULONG                           ulRole              = LPC_ROLE_NONE;*/
    /*PPOAM_COSAWIFIDM_OBJECT*/ANSC_HANDLE         pPoamWiFiDm         = (/*PPOAM_COSAWIFIDM_OBJECT*/ANSC_HANDLE  )NULL;
    /*PSLAP_COSAWIFIDM_OBJECT*/ANSC_HANDLE         pSlapWifiDm         = (/*PSLAP_COSAWIFIDM_OBJECT*/ANSC_HANDLE  )NULL;
	PCOSA_DML_WIFI_ATM				pATM=NULL;

    CcspWifiTrace(("RDK_LOG_WARN, RDKB_SYSTEM_BOOT_UP_LOG : CosaWifiInitialize - WiFi initialize. \n"));
#if 0
    PCOSA_PLUGIN_INFO               pPluginInfo         = (PCOSA_PLUGIN_INFO        )g_pCosaBEManager->hCosaPluginInfo;

    PSLAP_OBJECT_DESCRIPTOR         pObjDescriptor      = (PSLAP_OBJECT_DESCRIPTOR  )NULL;

    COSAGetHandleProc               pProc               = (COSAGetHandleProc        )NULL;

    pProc = (COSAGetHandleProc)pPluginInfo->AcquireFunction("COSAGetLPCRole");
    
    if (pProc)
    {
        ulRole = (ULONG)(*pProc)();
        CcspTraceWarning(("CosaWifiInitialize - LPC role is %d...\n", ulRole));
    }
    
#ifdef _COSA_SIM_

    if ( ulRole == LPC_ROLE_PARTY ) 
    {
        CcspTraceWarning(("CosaWifiInitialize - AcquireFunction COSACreateSlapObject..."));
        pProc = (COSAGetHandleProc)pPluginInfo->AcquireFunction("COSACreateSlapObject");
    }
    else if ( ulRole == LPC_ROLE_MANAGER ) 
    {
        CcspTraceWarning(("CosaWifiInitialize - AcquireFunction COSAAcquirePoamObject..."));
        pProc = (COSAGetHandleProc)pPluginInfo->AcquireFunction("COSAAcquirePoamObject");
    }
    
    if ((ulRole == LPC_ROLE_MANAGER)||(ulRole == LPC_ROLE_PARTY))
    {
        if (NULL != pProc)
        {
            CcspTraceWarning(("succeeded!\n"));
        }
        else
        {
            CcspTraceWarning(("failed!\n"));
        }
    }

    if ( ulRole == LPC_ROLE_PARTY ) 
    {
        CcspTraceWarning(("CosaWifiInitialize - create slap object..."));
        pObjDescriptor = (PSLAP_OBJECT_DESCRIPTOR)SlapCosaWifiDmGetSlapObjDescriptor((ANSC_HANDLE)NULL);
        pSlapWifiDm    = (*pProc)(pObjDescriptor);
        if (NULL != pSlapWifiDm)
        {
#ifdef _COSA_SIM_        
            g_pCosaBEManager->has_wifi_slap = 1;
#endif            
            CcspTraceWarning(("succeeded!\n"));
        }
        else
        {
            CcspTraceWarning(("failed!\n"));
        }
    }
    else if ( ulRole == LPC_ROLE_MANAGER )
    {
        CcspTraceWarning(("CosaWifiInitialize - create poam object..."));
        pObjDescriptor = (PSLAP_OBJECT_DESCRIPTOR)PoamCosaWifiDmGetPoamObjDescriptor((ANSC_HANDLE)NULL);
        pPoamWiFiDm    = (*pProc)(pObjDescriptor);
        if (NULL != pPoamWiFiDm)
        {
            CcspTraceWarning(("succeeded!\n"));
        }
        else
        {
            CcspTraceWarning(("failed!\n"));
        }
    }
    else if ( ulRole == LPC_ROLE_NONE )
    {
#ifdef _COSA_SIM_        
            g_pCosaBEManager->has_wifi_slap = 1;
#endif      
    }
#elif _COSA_DRG_CNS_
    if ( (ulRole != LPC_ROLE_NONE) && (ulRole != LPC_ROLE_INVALID))
    {
        CcspTraceWarning(("CosaWifiInitialize - AcquireFunction COSACreateSlapObject..."));
    
        pProc = (COSAGetHandleProc)pPluginInfo->AcquireFunction("COSACreateSlapObject");
        
        if (NULL != pProc)
        {
            CcspTraceWarning(("succeeded!\n"));
        }
        else
        {
            CcspTraceWarning(("failed!\n"));
        }
        
        CcspTraceWarning(("CosaWifiInitialize - create slap object..."));
            
        pObjDescriptor = (PSLAP_OBJECT_DESCRIPTOR)SlapCosaWifiDmGetSlapObjDescriptor((ANSC_HANDLE)NULL);
        
        pSlapWifiDm    = (*pProc)(pObjDescriptor);
        
        if (NULL != pSlapWifiDm)
        {
            CcspTraceWarning(("succeeded!\n"));
        }
        else
        {
            CcspTraceWarning(("failed!\n"));
        }
    }
#elif _COSA_DRG_TPG_

    if ( (ulRole != LPC_ROLE_NONE) && (ulRole != LPC_ROLE_INVALID))
    {
        CcspTraceWarning(("CosaWifiInitialize - AcquireFunction COSAAcquirePoamObject..."));
        
        pProc = (COSAGetHandleProc)pPluginInfo->AcquireFunction("COSAAcquirePoamObject");
        
        if (NULL != pProc)
        {
            CcspTraceWarning(("succeeded!\n"));
        }
        else
        {
            CcspTraceWarning(("failed!\n"));
        }
        
        CcspTraceWarning(("CosaWifiInitialize - create poam object..."));
        
        pObjDescriptor = (PSLAP_OBJECT_DESCRIPTOR)PoamCosaWifiDmGetPoamObjDescriptor((ANSC_HANDLE)NULL);
        
        pPoamWiFiDm    = (*pProc)(pObjDescriptor);
        
        if (NULL != pPoamWiFiDm)
        {
            CcspTraceWarning(("succeeded!\n"));
        }
        else
        {
            CcspTraceWarning(("failed!\n"));
        }
    }
#endif
#endif

    pMyObject->hPoamWiFiDm = (ANSC_HANDLE)pPoamWiFiDm;
    pMyObject->hSlapWiFiDm = (ANSC_HANDLE)pSlapWifiDm;

    returnStatus = CosaDmlWiFiInit((ANSC_HANDLE)pMyObject->hPoamWiFiDm, (ANSC_HANDLE)pMyObject);
    
    if ( returnStatus != ANSC_STATUS_SUCCESS )
    {
        CcspWifiTrace(("RDK_LOG_WARN, RDKB_SYSTEM_BOOT_UP_LOG : CosaWifiInitialize - WiFi failed to initialize. Is WiFi supposed to start?\n"));
        
        return  returnStatus;
    }

    // RDKB3939-878: This is safer recovery mechanism to avoid any illegal MacFilter entries.
    // i.e  Remove invalid entry of Macfilter from PSM if present.
    RemoveInvalidMacFilterListFromPsm();
    
    /* Initiation all functions */
    pMyObject->RadioCount  = 0;
    pMyObject->pRadio      = NULL;
    
    /* next instance starts from 1 */
    pMyObject->ulSsidNextInstance = 1;
    pMyObject->ulAPNextInstance   = 1;
    pMyObject->ulResultNextInstance   = 1;

    AnscInitializeQueue(&pMyObject->SsidQueue);
    AnscInitializeQueue(&pMyObject->AccessPointQueue);
    AnscSListInitializeHeader(&pMyObject->ResultList);

    /* Initialize WiFiRegion */
	pWiFiRegion = (PCOSA_DATAMODEL_RDKB_WIFIREGION)AnscAllocateMemory(sizeof(COSA_DATAMODEL_RDKB_WIFIREGION));

	if ( NULL != pWiFiRegion )
	{
		/* Memset Allocated Address */
		memset( pWiFiRegion, 0, sizeof( COSA_DATAMODEL_RDKB_WIFIREGION ) );

		CosaDmlWiFiRegionInit(pWiFiRegion);

		pMyObject->pWiFiRegion 	   = pWiFiRegion;
	}

    CcspTraceWarning(("CosaWifiInitialize - Get Radio Info ...\n"));

    /*Get Radio Info*/
    pMyObject->RadioCount = CosaDmlWiFiRadioGetNumberOfEntries((ANSC_HANDLE)pMyObject->hPoamWiFiDm);

    if ( pMyObject->RadioCount != 0 )
    {
        pMyObject->pRadio = (PCOSA_DML_WIFI_RADIO)AnscAllocateMemory( sizeof(COSA_DML_WIFI_RADIO) * pMyObject->RadioCount);
        
        for( uIndex = 0; uIndex < pMyObject->RadioCount; uIndex++)
        {
            pWifiRadio = pMyObject->pRadio+uIndex;
            
            if ( CosaDmlWiFiRadioGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, uIndex, &pWifiRadio->Radio) == ANSC_STATUS_SUCCESS )
            {
                if ( pWifiRadio->Radio.Cfg.InstanceNumber == 0 )
                {
                    /* Generate Default InstanceNumber and Alias */
                    _ansc_sprintf(pWifiRadio->Radio.Cfg.Alias, "wl%lu", uIndex );
                    
                    CosaDmlWiFiRadioSetValues
                        (
                            (ANSC_HANDLE)pMyObject->hPoamWiFiDm,
                            uIndex, 
                            uIndex + 1, 
                            pWifiRadio->Radio.Cfg.Alias
                        );            
                    
                    pWifiRadio->Radio.Cfg.InstanceNumber = uIndex + 1;
                }				
            }
         }
    }
    
    /*Read configuration*/
    pMyObject->hIrepFolderCOSA = g_GetRegistryRootFolder(g_pDslhDmlAgent);
    pPoamIrepFoCOSA = (PPOAM_IREP_FOLDER_OBJECT)pMyObject->hIrepFolderCOSA;

    if ( !pPoamIrepFoCOSA )
    {
        returnStatus = ANSC_STATUS_FAILURE;
        CcspTraceWarning(("CosaWifiInitialize - hIrepFolderCOSA failed\n"));

        goto  EXIT;
    }
    
    /*Get Wifi entry*/
    pPoamIrepFoWifi = 
        (PPOAM_IREP_FOLDER_OBJECT)pPoamIrepFoCOSA->GetFolder
            (
                (ANSC_HANDLE)pPoamIrepFoCOSA,
                COSA_IREP_FOLDER_NAME_WIFI
            );

    if ( !pPoamIrepFoWifi )
    {
        pPoamIrepFoWifi =
            pPoamIrepFoCOSA->AddFolder
                (
                    (ANSC_HANDLE)pPoamIrepFoCOSA,
                    COSA_IREP_FOLDER_NAME_WIFI,
                    0
                );
    }

    if ( !pPoamIrepFoWifi )
    {
        returnStatus = ANSC_STATUS_FAILURE;
        CcspTraceWarning(("CosaWifiInitialize - pPoamIrepFoWifi failed\n"));

        goto  EXIT;
    }
    else
    {
        pMyObject->hIrepFolderWifi = (ANSC_HANDLE)pPoamIrepFoWifi;
    }

    /*Get Wifi.SSID entry*/
    pPoamIrepFoWifiSsid = 
        (PPOAM_IREP_FOLDER_OBJECT)pPoamIrepFoWifi->GetFolder
            (
                (ANSC_HANDLE)pPoamIrepFoWifi,
                COSA_IREP_FOLDER_NAME_WIFI_SSID
            );

    if ( !pPoamIrepFoWifiSsid )
    {
        pPoamIrepFoWifiSsid =
            pPoamIrepFoWifi->AddFolder
                (
                    (ANSC_HANDLE)pPoamIrepFoWifi,
                    COSA_IREP_FOLDER_NAME_WIFI_SSID,
                    0
                );
    }

    if ( !pPoamIrepFoWifiSsid )
    {
        returnStatus = ANSC_STATUS_FAILURE;
        CcspTraceWarning(("CosaWifiInitialize - pPoamIrepFoWifiSsid failed\n"));

        goto  EXIT;
    }
    else
    {
        pMyObject->hIrepFolderWifiSsid = (ANSC_HANDLE)pPoamIrepFoWifiSsid;
    }
    
    /*Ssid Next Instance Number*/
    if ( TRUE )
    {
        pSlapVariable =
            (PSLAP_VARIABLE)pPoamIrepFoWifiSsid->GetRecord
                (
                    (ANSC_HANDLE)pPoamIrepFoWifiSsid,
                    COSA_DML_RR_NAME_WifiSsidNextInsNunmber,
                    NULL
                );

        if ( pSlapVariable )
        {
            pMyObject->ulSsidNextInstance = pSlapVariable->Variant.varUint32;

            SlapFreeVariable(pSlapVariable);
        }
    }
    
    /*Get Wifi.AccessPoint entry*/
    pPoamIrepFoWifiAP = 
        (PPOAM_IREP_FOLDER_OBJECT)pPoamIrepFoWifi->GetFolder
            (
                (ANSC_HANDLE)pPoamIrepFoWifi,
                COSA_IREP_FOLDER_NAME_WIFI_AP
            );

    if ( !pPoamIrepFoWifiAP )
    {
        pPoamIrepFoWifiAP =
            pPoamIrepFoWifi->AddFolder
                (
                    (ANSC_HANDLE)pPoamIrepFoWifi,
                    COSA_IREP_FOLDER_NAME_WIFI_AP,
                    0
                );
    }

    if ( !pPoamIrepFoWifiAP )
    {
        returnStatus = ANSC_STATUS_FAILURE;
        CcspTraceWarning(("CosaWifiInitialize - pPoamIrepFoWifiAP failed\n"));

        goto  EXIT;
    }
    else
    {
        pMyObject->hIrepFolderWifiAP = (ANSC_HANDLE)pPoamIrepFoWifiAP;
    }
    
    /*AP Next Instance Number*/
    if ( TRUE )
    {
        pSlapVariable =
            (PSLAP_VARIABLE)pPoamIrepFoWifiAP->GetRecord
                (
                    (ANSC_HANDLE)pPoamIrepFoWifiAP,
                    COSA_DML_RR_NAME_WifiAPNextInsNunmber,
                    NULL
                );

        if ( pSlapVariable )
        {
            pMyObject->ulAPNextInstance = pSlapVariable->Variant.varUint32;

            SlapFreeVariable(pSlapVariable);
        }
    }

    CcspTraceWarning(("CosaWifiInitialize - Initialize middle layer ...\n"));

    /*Initialize middle layer*/
    /* First the SSID part */
    uSsidCount = CosaDmlWiFiSsidGetNumberOfEntries((ANSC_HANDLE)pMyObject->hPoamWiFiDm);
    
    for( uIndex = 0; uIndex < uSsidCount; uIndex++)
    {
        pWifiSsid = (PCOSA_DML_WIFI_SSID)AnscAllocateMemory(sizeof(COSA_DML_WIFI_SSID));
        
        if (!pWifiSsid)
        {
            CcspTraceWarning(("CosaWifiInitialize - pWifiSsid failed\n"));
            return ANSC_STATUS_RESOURCES;
        }
        
        /*retrieve data from backend*/
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
        CosaDmlWiFiSsidGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, uIndex, &pWifiSsid->SSID);
#else
        CosaDmlWiFiSsidGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, ssidIndex, &pWifiSsid->SSID);
#endif

        if (TRUE)
        {
            pLinkObj = (PCOSA_CONTEXT_LINK_OBJECT)AnscAllocateMemory(sizeof(COSA_CONTEXT_LINK_OBJECT));
            
            if (!pLinkObj)
            {
                AnscFreeMemory(pWifiSsid);
                CcspTraceWarning(("CosaWifiInitialize - pLinkObj failed\n"));
                
                return ANSC_STATUS_RESOURCES;
            }
            
            if ( pWifiSsid->SSID.Cfg.InstanceNumber != 0 )
            {
                pLinkObj->InstanceNumber = pWifiSsid->SSID.Cfg.InstanceNumber;
                
                if (pMyObject->ulSsidNextInstance <= pWifiSsid->SSID.Cfg.InstanceNumber)
                {
                    pMyObject->ulSsidNextInstance = pWifiSsid->SSID.Cfg.InstanceNumber + 1;
                    
                    if (pMyObject->ulSsidNextInstance == 0)
                    {
                        pMyObject->ulSsidNextInstance = 1;
                    }
                }
            }
            else
            {
                pLinkObj->InstanceNumber = pMyObject->ulSsidNextInstance;
                
                pMyObject->ulSsidNextInstance++;
                
                if (pMyObject->ulSsidNextInstance == 0)
                {
                    pMyObject->ulSsidNextInstance = 1;
                }
                
                /* Generate Alias */
#if !defined(_INTEL_BUG_FIXES_)
                _ansc_sprintf(pWifiSsid->SSID.Cfg.Alias, "SSID%lu", pLinkObj->InstanceNumber);
#else
                _ansc_sprintf(pWifiSsid->SSID.Cfg.Alias, "cpe-SSID%lu", pLinkObj->InstanceNumber);
#endif
                
                CosaDmlWiFiSsidSetValues
                    (
                        (ANSC_HANDLE)pMyObject->hPoamWiFiDm,
                        uIndex, 
                        pLinkObj->InstanceNumber, 
                        pWifiSsid->SSID.Cfg.Alias
                    );
                /* Set the instance number to the object also */
                pWifiSsid->SSID.Cfg.InstanceNumber = pLinkObj->InstanceNumber;
            }
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
            ssidIndex = pWifiSsid->SSID.Cfg.InstanceNumber;
#endif
            
            pLinkObj->hContext     = (ANSC_HANDLE)pWifiSsid;
            pLinkObj->hParentTable = NULL;
            pLinkObj->bNew         = FALSE;
            pLinkObj->hPoamIrepUpperFo = (ANSC_HANDLE)NULL;
            pLinkObj->hPoamIrepFo      = (ANSC_HANDLE)NULL;
            
            CosaSListPushEntryByInsNum((PSLIST_HEADER)&pMyObject->SsidQueue, pLinkObj);
        }
    }
    
    /*Then the AP part*/
    for (uIndex = 0; uIndex < uSsidCount; uIndex++)
    {
        pWifiAp = (PCOSA_DML_WIFI_AP)AnscAllocateMemory(sizeof(COSA_DML_WIFI_AP));
        
        if (!pWifiAp)
        {
            CcspTraceWarning(("CosaWifiInitialize - pWifiAp failed\n"));
            return ANSC_STATUS_RESOURCES;
        }
        
        pSLinkEntry = AnscQueueGetEntryByIndex(&pMyObject->SsidQueue, uIndex);
        pLinkObj    = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        
        pWifiSsid   = pLinkObj->hContext;
#if defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
        pWifiAp->AP.Cfg.InstanceNumber = pWifiSsid->SSID.Cfg.InstanceNumber;
#endif

#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
        CosaDmlWiFiApGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->AP);   
        CosaDmlWiFiApSecGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->SEC);
        CosaDmlWiFiApAcctGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->ACCT);
        CosaDmlWiFiApWpsGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->WPS);
        CosaDmlWiFiApMfGetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->MF);
#else
        CosaDmlWiFiApGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->AP);   
        CosaDmlWiFiApSecGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->SEC);
        CosaDmlWiFiApAcctGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->ACCT);
        CosaDmlWiFiApWpsGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->WPS);
        CosaDmlWiFiApMfGetCfg((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->MF);
#endif
		/* Rapid Reconnection */
		CosaDmlWiFi_GetRapidReconnectCountEnable( uIndex, &(pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_rapidReconnectCountEnable ), TRUE );
		CosaDmlWiFi_GetRapidReconnectThresholdValue( uIndex, &(pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_rapidReconnectMaxTime ) );

        //Load the vAP stats enable value
        CosaDmlWiFiApGetStatsEnable(uIndex + 1, &pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_StatsEnable);
        //NeighborReport value
        /*ARRISXB3-10566 - Duplicate psm call*/
	//CosaDmlWiFiApGetNeighborReportActivated(uIndex , &(pWifiAp->AP.Cfg.X_RDKCENTRAL_COM_NeighborReportActivated), TRUE);

        if (TRUE)
        {
            pLinkObj = (PCOSA_CONTEXT_LINK_OBJECT)AnscAllocateMemory(sizeof(COSA_CONTEXT_LINK_OBJECT));
            
            if (!pLinkObj)
            {
                AnscFreeMemory(pWifiAp);
                CcspTraceWarning(("CosaWifiInitialize -  CosaDmlWiFiApGetNeighborReportActivated pLinkObj failed\n"));
                
                return ANSC_STATUS_RESOURCES;
            }
            
            if ( pWifiAp->AP.Cfg.InstanceNumber != 0 )
            {
                pLinkObj->InstanceNumber = pWifiAp->AP.Cfg.InstanceNumber;
                
                if (pMyObject->ulAPNextInstance <= pWifiAp->AP.Cfg.InstanceNumber)
                {
                    pMyObject->ulAPNextInstance = pWifiAp->AP.Cfg.InstanceNumber + 1;
                    
                    if (pMyObject->ulAPNextInstance == 0)
                    {
                        pMyObject->ulAPNextInstance = 1;
                    }
                }
            }
            else
            {
                pLinkObj->InstanceNumber = pMyObject->ulAPNextInstance;
                
                pMyObject->ulAPNextInstance++;
                
                if ( pMyObject->ulAPNextInstance == 0 )
                {
                    pMyObject->ulAPNextInstance = 1;
                }
                
                /*Generate Alias*/
#if !defined(_INTEL_BUG_FIXES_)
                _ansc_sprintf(pWifiAp->AP.Cfg.Alias, "AccessPoint%lu", pLinkObj->InstanceNumber);
#else
                _ansc_sprintf(pWifiAp->AP.Cfg.Alias, "cpe-AccessPoint%lu", pLinkObj->InstanceNumber);                
#endif
                CosaDmlWiFiApSetValues
                (
                    (ANSC_HANDLE)pMyObject->hPoamWiFiDm,
                    uIndex, 
                    pLinkObj->InstanceNumber, 
                    pWifiAp->AP.Cfg.Alias
                );
                /* Set the instance number to the object also */
                pWifiAp->AP.Cfg.InstanceNumber = pLinkObj->InstanceNumber;
            }
            
            pLinkObj->hContext      = (ANSC_HANDLE)pWifiAp;
            pLinkObj->hParentTable  = NULL;
            pLinkObj->bNew          = FALSE;
            pLinkObj->hPoamIrepUpperFo = (ANSC_HANDLE)NULL;
            pLinkObj->hPoamIrepFo      = (ANSC_HANDLE)NULL;
            
            CosaSListPushEntryByInsNum((PSLIST_HEADER)&pMyObject->AccessPointQueue, pLinkObj);
        }
        
        /* Device.WiFi.AccessPoint.{i}.X_CISCO_COM_MacFilterTable.{i}. */
        pPoamIrepFoMacFilt = 
            (PPOAM_IREP_FOLDER_OBJECT)pPoamIrepFoWifiAP->GetFolder
                (
                    (ANSC_HANDLE)pPoamIrepFoWifiAP,
                    COSA_IREP_FOLDER_NAME_MAC_FILT_TAB
                );

        if ( !pPoamIrepFoMacFilt )
        {
            pPoamIrepFoMacFilt = 
                pPoamIrepFoWifiAP->AddFolder
                    (
                        (ANSC_HANDLE)pPoamIrepFoWifiAP,
                        COSA_IREP_FOLDER_NAME_MAC_FILT_TAB,
                        0
                    );
        }

        if ( !pPoamIrepFoMacFilt )
        {
            returnStatus = ANSC_STATUS_FAILURE;
            CcspTraceWarning(("CosaWifiInitialize - pPoamIrepFoMacFilt failed\n"));
            goto EXIT;
        }
        else
        {
            pWifiAp->AP.hIrepFolderMacFilt = (ANSC_HANDLE)pPoamIrepFoMacFilt;
        }

        if ( TRUE )
        {
            pSlapVariable = 
                (PSLAP_VARIABLE)pPoamIrepFoMacFilt->GetRecord
                    (
                        (ANSC_HANDLE)pPoamIrepFoMacFilt,
                        COSA_DML_RR_NAME_MacFiltTabNextInsNunmber,
                        NULL
                    );

            if ( pSlapVariable )
            {
                pWifiAp->AP.ulMacFilterNextInsNum = pSlapVariable->Variant.varUint32;
                SlapFreeVariable(pSlapVariable);
            } else {
                pWifiAp->AP.ulMacFilterNextInsNum = 1;
            }
        }

        AnscInitializeQueue(&pWifiAp->AP.MacFilterList);
        CosaDmlWiFiApSetMFQueue(&pWifiAp->AP.MacFilterList, pWifiAp->AP.Cfg.InstanceNumber);

        uMacFiltCount = CosaDmlMacFilt_GetNumberOfEntries( pWifiAp->AP.Cfg.InstanceNumber);
        for (uMacFiltIdx = 0; uMacFiltIdx < uMacFiltCount; uMacFiltIdx++)
        {
            pMacFilt = (PCOSA_DML_WIFI_AP_MAC_FILTER)AnscAllocateMemory(sizeof(COSA_DML_WIFI_AP_MAC_FILTER));
            if (!pMacFilt)
                return ANSC_STATUS_RESOURCES;

            CosaDmlMacFilt_GetEntryByIndex(pWifiAp->AP.Cfg.InstanceNumber, uMacFiltIdx, pMacFilt);

            pMacFiltLinkObj = (PCOSA_CONTEXT_LINK_OBJECT)AnscAllocateMemory(sizeof(COSA_CONTEXT_LINK_OBJECT));
            if ( !pMacFiltLinkObj )
            {
                AnscFreeMemory(pMacFilt);
                return ANSC_STATUS_RESOURCES;
            }

            if (pMacFilt->InstanceNumber != 0)
            {
                pMacFiltLinkObj->InstanceNumber = pMacFilt->InstanceNumber;
                if (pWifiAp->AP.ulMacFilterNextInsNum <= pMacFilt->InstanceNumber)
                {
                    pWifiAp->AP.ulMacFilterNextInsNum = pMacFilt->InstanceNumber + 1;
                    if (pWifiAp->AP.ulMacFilterNextInsNum == 0)
                        pWifiAp->AP.ulMacFilterNextInsNum = 1;
                }
            }
            else
            {
                pMacFiltLinkObj->InstanceNumber = pWifiAp->AP.ulMacFilterNextInsNum;
                pWifiAp->AP.ulMacFilterNextInsNum++;
                if (pWifiAp->AP.ulMacFilterNextInsNum == 0)
                    pWifiAp->AP.ulMacFilterNextInsNum = 1;

                _ansc_sprintf(pMacFilt->Alias, "MacFilterTable%lu", pMacFiltLinkObj->InstanceNumber);

                CosaDmlMacFilt_SetValues(pWifiAp->AP.Cfg.InstanceNumber, 
                        uMacFiltIdx,
                        pMacFiltLinkObj->InstanceNumber, 
                        pMacFilt->Alias);
            }

            pMacFiltLinkObj->hContext       = (ANSC_HANDLE)pMacFilt;
            pMacFiltLinkObj->hParentTable   = (ANSC_HANDLE)pWifiAp;
            pMacFiltLinkObj->bNew           = FALSE;

            CosaSListPushEntryByInsNum((PSLIST_HEADER)&pWifiAp->AP.MacFilterList, pMacFiltLinkObj);
        }
    }
    
	/* Part of WiFi.X_RDKCENTRAL-COM_BandSteering. */
	pWifiBandSteering = (PCOSA_DML_WIFI_BANDSTEERING)AnscAllocateMemory(sizeof(COSA_DML_WIFI_BANDSTEERING));

    if ( NULL != pWifiBandSteering )
	{
		PCOSA_DML_WIFI_BANDSTEERING_SETTINGS pBSSettings = NULL;
		int	iLoopCount = 0;
		
		/* Memset Allocated Address */
		memset( pWifiBandSteering, 0, sizeof( COSA_DML_WIFI_BANDSTEERING ) );

		pWifiBandSteering->RadioCount = pMyObject->RadioCount;

		/* Load Previous Values for Band Steering Options */
		CosaDmlWiFi_GetBandSteeringOptions( &pWifiBandSteering->BSOption );

		pBSSettings =(PCOSA_DML_WIFI_BANDSTEERING_SETTINGS)\
			          AnscAllocateMemory(sizeof(COSA_DML_WIFI_BANDSTEERING_SETTINGS) \
			          					  * pWifiBandSteering->RadioCount);

		/* Free previous allocated memory when fail to allocate memory  */
		if( NULL == pBSSettings )
		{
			AnscFreeMemory(pWifiBandSteering);
                        CcspTraceWarning(("CosaWifiInitialize - pBSSettings failed\n"));
			return ANSC_STATUS_RESOURCES;
		}

		/* Load Previous Values for Band Steering Settings */
		for ( iLoopCount = 0; iLoopCount < pWifiBandSteering->RadioCount; ++iLoopCount )
		{
			/* Memset Allocated Address */
			memset( &pBSSettings[ iLoopCount ], 0, sizeof( COSA_DML_WIFI_BANDSTEERING_SETTINGS ) );

			/* Instance Number Always from 1 */
			pBSSettings[ iLoopCount ].InstanceNumber = iLoopCount + 1;

			CosaDmlWiFi_GetBandSteeringSettings( iLoopCount, 
												  &pBSSettings[ iLoopCount ] );
		}

		pWifiBandSteering->pBSSettings = pBSSettings;
		pMyObject->pBandSteering 	   = pWifiBandSteering;
	}

   /* Initialize Wifi Harvester */
    pHarvester = (PCOSA_DML_WIFI_HARVESTER)AnscAllocateMemory(sizeof(COSA_DML_WIFI_HARVESTER));
    if ( NULL != pHarvester )
    {
          memset(pHarvester, 0, sizeof(COSA_DML_WIFI_HARVESTER));
          CosaDmlHarvesterInit(pHarvester);
          pMyObject->pHarvester = pHarvester;
    }

    CcspTraceWarning(("CosaWifiInitialize - CosaWifiRegGetSsidInfo ...\n"));

    /*Load the newly added but not yet commited entries, if exist*/
    CosaWifiRegGetSsidInfo((ANSC_HANDLE)pMyObject);
    
    /*Load orphan AP entries*/
    CosaWifiRegGetAPInfo((ANSC_HANDLE)pMyObject);
    if (pWifiAp != NULL) {
        CosaWifiRegGetMacFiltInfo(pWifiAp);
    }

    CosaLgiWifiInitialize((ANSC_HANDLE)pMyObject);

	//CosaWifiRegGetATMInfo((ANSC_HANDLE)pMyObject);
	pATM = (PCOSA_DML_WIFI_ATM)AnscAllocateMemory(sizeof(COSA_DML_WIFI_ATM));
    if ( NULL != pATM )	{		
		memset( pATM, 0, sizeof( COSA_DML_WIFI_ATM ) );
		CosaDmlWiFi_GetATMOptions( pATM );
		CosaWifiRegGetATMInfo( pATM );
		pMyObject->pATM	   = pATM;
	}

	
	
	pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_t *attrp = NULL;

        attrp = &attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
        pthread_create(&tid, &attr,updateCiruitIdThread, NULL);

	pthread_t tid2;

   	pthread_create(&tid2, attrp, &RegisterWiFiConfigureCallBack, NULL);
        if(attrp != NULL)
                pthread_attr_destroy( attrp );
	
        CcspTraceWarning(("CosaWifiInitialize - CosaDmlWiFiNeighbouringGetEntry ...\n"));

// For WiFi Neighbouring Diagnostics
	CosaDmlWiFiNeighbouringGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, &pMyObject->Diagnostics);
        #if defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_)
        v_secure_system("touch /tmp/wifi_dml_complete");
        v_secure_system("uptime > /tmp/wifi_dml_complete");
        #endif


#if defined (FEATURE_SUPPORT_WEBCONFIG)
    CosaDmlWiFiWebConfigFrameworkInit();
#endif

    	CcspWifiTrace(("RDK_LOG_WARN, RDKB_SYSTEM_BOOT_UP_LOG : CosaWifiInitialize - WiFi initialization complete. \n"));
        t2_event_d("WIFI_INFO_CosaWifiinit",1);

#if 0	

#if defined(_ENABLE_BAND_STEERING_)
    	pthread_t tid3;
    	if (pthread_create(&tid3, NULL, StartBandsteeringLogging, NULL))
    	{
		CcspWifiTrace(("RDK_LOG_ERROR, WIFI %s : Failed steer thread\n", __FUNCTION__ ));
    	}
        else
	{
		CcspWifiTrace(("RDK_LOG_WARN, WIFI %s : success steer thread\n", __FUNCTION__ ));
	}
# endif

#endif

//    CosaDmlWifi_getDppConfigFromPSM((ANSC_HANDLE)pMyObject);
#if defined (FEATURE_SUPPORT_PASSPOINT)
    if(ANSC_STATUS_SUCCESS != CosaDmlWiFi_InitGasConfig((ANSC_HANDLE)pMyObject)){
        CcspWifiTrace(("RDK_LOG_WARN, RDKB_SYSTEM_BOOT_UP_LOG : CosaWifiInitialize - WiFi failed to Initialize GAS Configuration.\n"));
    }
    if(0 != init_wifi_data_plane()){
        CcspWifiTrace(("RDK_LOG_WARN, RDKB_SYSTEM_BOOT_UP_LOG : CosaWifiInitialize - WiFi failed to Initialize Wifi Data/Mgmt Handler.\n"));
    }
#endif
	        
EXIT:
        CcspTraceWarning(("CosaWifiInitialize - returnStatus %d\n", returnStatus));
	return returnStatus;
}

/**********************************************************************

    caller:     self

    prototype:

        ANSC_STATUS
        CosaWifiReInitialize
            (
                ANSC_HANDLE                 hThisObject
            );

    description:

        This function initiate  cosa wifi object and return handle.

    argument:	ANSC_HANDLE                 hThisObject
            This handle is actually the pointer of this object
            itself.

    return:     operation status.

**********************************************************************/

ANSC_STATUS
CosaWifiReInitialize
    (
        ANSC_HANDLE                 hThisObject,
        ULONG                       uRadioIndex  //0, 1
    )
{
    ANSC_STATUS                     returnStatus        = ANSC_STATUS_SUCCESS;
    PCOSA_DATAMODEL_WIFI            pMyObject           = (PCOSA_DATAMODEL_WIFI)hThisObject;
    ULONG                           uIndex              = 0;
    ULONG                           uApIndex            = 0; 
    ULONG                           uSsidCount          = 0;
    PCOSA_DML_WIFI_RADIO            pWifiRadio          = NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid           = (PCOSA_DML_WIFI_SSID      )NULL;
    PCOSA_DML_WIFI_AP               pWifiAp             = (PCOSA_DML_WIFI_AP        )NULL;        
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj            = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PSINGLE_LINK_ENTRY              pSLinkEntry         = (PSINGLE_LINK_ENTRY       )NULL;

    returnStatus = CosaDmlWiFiInit((ANSC_HANDLE)pMyObject->hPoamWiFiDm, (ANSC_HANDLE)pMyObject);

    if ( returnStatus != ANSC_STATUS_SUCCESS )
    {
        CcspTraceWarning(("CosaWifiInitialize - WiFi failed to initialize. Is WiFi supposed to start?\n"));

        return  returnStatus;
    }


    if ( uRadioIndex <= pMyObject->RadioCount)
    {
        pWifiRadio = pMyObject->pRadio+uRadioIndex;

        CosaDmlWiFiRadioGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, uRadioIndex, &pWifiRadio->Radio);
    }

    /* First the SSID part */
    uSsidCount = CosaDmlWiFiSsidGetNumberOfEntries((ANSC_HANDLE)pMyObject->hPoamWiFiDm);
    for( uIndex = 0; uIndex < uSsidCount; uIndex++)
    {
		//for each Device.WiFi.SSID.1
        pSLinkEntry = AnscQueueGetEntryByIndex(&pMyObject->SsidQueue, uIndex);
        pLinkObj    = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        pWifiSsid   = pLinkObj->hContext;
        CHAR    PathName[64] = {0};

        if (!pWifiSsid)
        {
            return ANSC_STATUS_RESOURCES;
        }
	#if defined(_CBR_PRODUCT_REQ_)
	wifi_getRadioIfName(uRadioIndex, PathName);
	#else
	snprintf(PathName, sizeof(PathName), "wifi%lu", uRadioIndex);
	#endif
        pWifiSsid->SSID.Cfg.WiFiRadioName[sizeof(pWifiSsid->SSID.Cfg.WiFiRadioName) - 1] = '\0';
        pWifiRadio->Radio.StaticInfo.Name[sizeof(pWifiRadio->Radio.StaticInfo.Name) - 1] = '\0';
        //if Device.WiFi.SSID.1.LowerLayers(Device.WiFi.Radio.1. (Device.WiFi.Radio.1.Name (wifi0)))  == wifi0
#if !defined(DMCLI_SUPPORT_TO_ADD_DELETE_VAP)
        if (AnscEqualString(pWifiSsid->SSID.Cfg.WiFiRadioName, PathName, TRUE)) {
#else
        if (AnscEqualString(pWifiSsid->SSID.Cfg.WiFiRadioName, pWifiRadio->Radio.StaticInfo.Name, TRUE)) {
#endif
            /*retrieve data from backend*/
			//reload ssid parameters  
            CosaDmlWiFiSsidGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, uIndex, &pWifiSsid->SSID);

			//give PathName=Device.WiFi.SSID.1.
            snprintf(PathName, sizeof(PathName), "Device.WiFi.SSID.%lu.", pLinkObj->InstanceNumber);
            for (uApIndex = 0; uApIndex < uSsidCount; uApIndex++)
            {
                pSLinkEntry = AnscQueueGetEntryByIndex(&pMyObject->AccessPointQueue, uApIndex);
                pLinkObj    = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
                pWifiAp   = pLinkObj->hContext;
                pWifiAp->AP.Cfg.SSID[sizeof(pWifiAp->AP.Cfg.SSID) - 1] = '\0';
				//if Device.WiFi.AccessPoint.x.SSIDReference == Device.WiFi.SSID.1.
                if (AnscEqualString(pWifiAp->AP.Cfg.SSID, PathName, TRUE))
                {
					//reload AP, SEC WPS
#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
                    CosaDmlWiFiApGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->AP);   
                    CosaDmlWiFiApSecGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->SEC);
                    CosaDmlWiFiApWpsGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->WPS);
#else
                    CosaDmlWiFiApGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->AP);   
                    CosaDmlWiFiApSecGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->SEC);
                    CosaDmlWiFiApAcctGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->ACCT);
                    CosaDmlWiFiApWpsGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->WPS);
#endif

                    break;
                }
            }
        }
    }

	//zqiu: reload the BS settings
	if ( NULL != pMyObject->pBandSteering ) {
		CosaDmlWiFi_GetBandSteeringOptions( &(pMyObject->pBandSteering->BSOption) );
		
		if(pMyObject->pBandSteering->pBSSettings) {		
			pMyObject->pBandSteering->pBSSettings[0].InstanceNumber = 1;
			CosaDmlWiFi_GetBandSteeringSettings( 0, pMyObject->pBandSteering->pBSSettings+0 );
			pMyObject->pBandSteering->pBSSettings[1].InstanceNumber = 2;
			CosaDmlWiFi_GetBandSteeringSettings( 1, pMyObject->pBandSteering->pBSSettings+1 );
		}		
	}

	//zqiu: reload the ATM settings //???	
		CosaDmlWiFi_GetATMOptions( pMyObject->pATM );
		
		//if(pMyObject->pBandSteering->pBSSettings) {		
		//	pMyObject->pBandSteering->pBSSettings[0].InstanceNumber = 1;
		//	CosaDmlWiFi_GetBandSteeringSettings( 0, pMyObject->pBandSteering->pBSSettings+0 );
		//	pMyObject->pBandSteering->pBSSettings[1].InstanceNumber = 2;
		//	CosaDmlWiFi_GetBandSteeringSettings( 1, pMyObject->pBandSteering->pBSSettings+1 );
		//}		
	
    CosaLgiWifiReInitialize((ANSC_HANDLE)pMyObject);

    return returnStatus;
}

ANSC_STATUS
CosaWifiReInitializeRadioAndAp
    (
        ANSC_HANDLE                 hThisObject,
        ULONG 						indexes
    )
{
    ANSC_STATUS                     returnStatus        = ANSC_STATUS_SUCCESS;
    PCOSA_DATAMODEL_WIFI            pMyObject           = (PCOSA_DATAMODEL_WIFI)hThisObject;
    ULONG                           uApIndex            = 0; 
    PCOSA_DML_WIFI_RADIO            pWifiRadio          = NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid           = (PCOSA_DML_WIFI_SSID      )NULL;
    PCOSA_DML_WIFI_AP               pWifiAp             = (PCOSA_DML_WIFI_AP        )NULL;        
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj            = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PSINGLE_LINK_ENTRY              pSLinkEntry         = (PSINGLE_LINK_ENTRY       )NULL;
	ULONG                       	uRadioIndex			= 0; 
	ULONG 							radioIndex=0, apIndex=0, radioIndex_2=0, apIndex_2=0;
	
	radioIndex   =(indexes>>24) & 0xff;
	radioIndex_2 =(indexes>>16) & 0xff;
	apIndex      =(indexes>>8) & 0xff;
	apIndex_2    =indexes & 0xff;
	
	//printf("-- %s indexes=%lu radioIndex=%lu apIndex=%lu radioIndex_2=%lu, apIndex_2=%lu\n", __func__, indexes, radioIndex, apIndex, radioIndex_2, apIndex_2);
	
    if(radioIndex_2==0 && apIndex_2==0) {
		returnStatus = CosaDmlWiFiFactoryResetRadioAndAp(radioIndex, apIndex, TRUE);
	} else {
		returnStatus = CosaDmlWiFiFactoryResetRadioAndAp(radioIndex, apIndex, FALSE);
		returnStatus = CosaDmlWiFiFactoryResetRadioAndAp(radioIndex_2, apIndex_2, TRUE);
	}
	
    if ( returnStatus != ANSC_STATUS_SUCCESS )  {
        CcspTraceWarning(("CosaWifiInitialize - WiFi failed to initialize. Is WiFi supposed to start?\n"));
        return  returnStatus;
    }

	if(radioIndex>0) {
		uRadioIndex	= radioIndex-1; 
		//reload pWifiRadio
		pWifiRadio = pMyObject->pRadio+uRadioIndex;
        CosaDmlWiFiRadioGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, uRadioIndex, &pWifiRadio->Radio);
	}
	if(radioIndex_2>0) {
		uRadioIndex	= radioIndex_2-1; 
		//reload pWifiRadio
		pWifiRadio = pMyObject->pRadio+uRadioIndex;
        CosaDmlWiFiRadioGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, uRadioIndex, &pWifiRadio->Radio);
	}
	
	if(apIndex>0) {
		uApIndex = apIndex-1;
		
		pSLinkEntry = AnscQueueGetEntryByIndex(&pMyObject->SsidQueue, uApIndex);
        pLinkObj    = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        pWifiSsid   = pLinkObj->hContext;
        if (!pWifiSsid)
            return ANSC_STATUS_RESOURCES;

		pSLinkEntry = AnscQueueGetEntryByIndex(&pMyObject->AccessPointQueue, uApIndex);
		pLinkObj    = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
		pWifiAp   = pLinkObj->hContext;
				
		//reload ssid parameters  
        CosaDmlWiFiSsidGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, uApIndex, &pWifiSsid->SSID);
		
		// reload AP, SEC WPS
#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
		CosaDmlWiFiApGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->AP);   
		CosaDmlWiFiApSecGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->SEC);
		CosaDmlWiFiApWpsGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->WPS);
#else
		CosaDmlWiFiApGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->AP);   
		CosaDmlWiFiApSecGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->SEC);
		CosaDmlWiFiApAcctGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->ACCT);
		CosaDmlWiFiApWpsGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->WPS);
#endif	
	}
	
	if(apIndex_2>0) {
		uApIndex = apIndex_2-1;
		
		pSLinkEntry = AnscQueueGetEntryByIndex(&pMyObject->SsidQueue, uApIndex);
        pLinkObj    = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        pWifiSsid   = pLinkObj->hContext;
        if (!pWifiSsid)
            return ANSC_STATUS_RESOURCES;

		pSLinkEntry = AnscQueueGetEntryByIndex(&pMyObject->AccessPointQueue, uApIndex);
		pLinkObj    = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
		pWifiAp   = pLinkObj->hContext;
				
		//reload ssid parameters  
        CosaDmlWiFiSsidGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, uApIndex, &pWifiSsid->SSID);
		
		// reload AP, SEC WPS
#if !defined(_COSA_INTEL_USG_ATOM_) && !defined(_COSA_BCM_MIPS_) && !defined(_COSA_BCM_ARM_) && !defined(_PLATFORM_TURRIS_)
		CosaDmlWiFiApGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->AP);   
		CosaDmlWiFiApSecGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->SEC);
		CosaDmlWiFiApWpsGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.Cfg.SSID, &pWifiAp->WPS);
#else
		CosaDmlWiFiApGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->AP);   
		CosaDmlWiFiApSecGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->SEC);
		CosaDmlWiFiApWpsGetEntry((ANSC_HANDLE)pMyObject->hPoamWiFiDm, pWifiSsid->SSID.StaticInfo.Name, &pWifiAp->WPS);
#endif	
	}
 
	//zqiu: reload the BS settings
	if ( NULL != pMyObject->pBandSteering ) {
		CosaDmlWiFi_GetBandSteeringOptions( &(pMyObject->pBandSteering->BSOption) );
		
		if(pMyObject->pBandSteering->pBSSettings) {		
			pMyObject->pBandSteering->pBSSettings[0].InstanceNumber = 1;
			CosaDmlWiFi_GetBandSteeringSettings( 0, pMyObject->pBandSteering->pBSSettings+0 );
			pMyObject->pBandSteering->pBSSettings[1].InstanceNumber = 2;
			CosaDmlWiFi_GetBandSteeringSettings( 1, pMyObject->pBandSteering->pBSSettings+1 );
		}		
	}
	
	//zqiu: reload the ATM settings //???
	if ( NULL != pMyObject->pATM ) {
		CosaDmlWiFi_GetATMOptions( (pMyObject->pATM) );
		
		//if(pMyObject->pBandSteering->pBSSettings) {		
		//	pMyObject->pBandSteering->pBSSettings[0].InstanceNumber = 1;
		//	CosaDmlWiFi_GetBandSteeringSettings( 0, pMyObject->pBandSteering->pBSSettings+0 );
		//	pMyObject->pBandSteering->pBSSettings[1].InstanceNumber = 2;
		//	CosaDmlWiFi_GetBandSteeringSettings( 1, pMyObject->pBandSteering->pBSSettings+1 );
		//}		
	}

    CosaLgiWifiReInitializeRadioAndAp((ANSC_HANDLE)pMyObject);

    return returnStatus;
}
/**********************************************************************

    caller:     self

    prototype:

        ANSC_STATUS
        CosaWifiRemove
            (
                ANSC_HANDLE                 hThisObject
            );

    description:

        This function initiate  cosa wifi object and return handle.

    argument:	ANSC_HANDLE                 hThisObject
            This handle is actually the pointer of this object
            itself.

    return:     operation status.

**********************************************************************/
ANSC_STATUS
CosaWifiRemove
    (
        ANSC_HANDLE                 hThisObject
    )
{
    ANSC_STATUS                     returnStatus = ANSC_STATUS_SUCCESS;
    PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI)hThisObject;
    PSINGLE_LINK_ENTRY              pSLinkEntry  = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pLinkObj     = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_AP               pWifiAp      = (PCOSA_DML_WIFI_AP        )NULL;
    PCOSA_DML_WIFI_BANDSTEERING		pWifiBandSteering = (PCOSA_DML_WIFI_BANDSTEERING  )NULL;

    /* Remove Poam or Slap resounce */
    if(!pMyObject)
        return returnStatus;

    /* Remove necessary resounce */
    if ( pMyObject->pRadio )
        AnscFreeMemory( pMyObject->pRadio );
    
    /* Remove all SSID entry */
    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
    while (pSLinkEntry)
    {
        pLinkObj      = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        pSLinkEntry   = AnscQueueGetNextEntry(pSLinkEntry);

        /*RDKB-6906, CID-33501, null check before use*/
        if(pLinkObj)
        {
            AnscQueuePopEntryByLink(&pMyObject->SsidQueue, &pLinkObj->Linkage);
            
            if (pLinkObj->hContext)
            {
                AnscFreeMemory(pLinkObj->hContext);
                pLinkObj->hContext = NULL;
            }
            AnscFreeMemory(pLinkObj);
            pLinkObj = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
        }
    }

    /* Remove all AccessPoint entry */
    pSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->AccessPointQueue);
    while (pSLinkEntry)
    {
        pLinkObj      = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        pSLinkEntry   = AnscQueueGetNextEntry(pSLinkEntry);

        /*RDKB-6906, CID-33552, null check before use*/
        if(pLinkObj)
        {
            pWifiAp       = pLinkObj->hContext;

            AnscQueuePopEntryByLink(&pMyObject->AccessPointQueue, &pLinkObj->Linkage);

            if (pWifiAp && (pWifiAp->AssocDevices))
            {
                AnscFreeMemory(pWifiAp->AssocDevices);
                pWifiAp->AssocDevices = (PCOSA_DML_WIFI_AP_ASSOC_DEVICE)NULL;
            }
            if (pLinkObj->hContext)
            {
                AnscFreeMemory(pLinkObj->hContext);
                pLinkObj->hContext = NULL;
            }

            AnscFreeMemory(pLinkObj);
            pLinkObj = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
        }
    }

	/* Remove Band Steering Object */
	pWifiBandSteering = pMyObject->pBandSteering;

	if( NULL != pWifiBandSteering )
	{
		AnscFreeMemory((ANSC_HANDLE)pWifiBandSteering->pBSSettings);
		AnscFreeMemory((ANSC_HANDLE)pWifiBandSteering);
	}
     
	if( NULL != pMyObject->pATM )
		AnscFreeMemory((ANSC_HANDLE)pMyObject->pATM); 
	 
    /* Remove self */
    AnscFreeMemory((ANSC_HANDLE)pMyObject);

    CosaLgiWifiRemove((ANSC_HANDLE)pMyObject);
	return returnStatus;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        ANSC_STATUS
        CosaWifiRegGetSsidInfo
            (
                ANSC_HANDLE                 hThisObject
            );

    description:

        This function is called to retrieve Dslm policy parameters.

    argument:   ANSC_HANDLE                 hThisObject
                This handle is actually the pointer of this object
                itself.

                ANSC_HANDLE                 hDdnsInfo
                Specifies the Dslm policy parameters to be filled.

    return:     status of operation.

**********************************************************************/

ANSC_STATUS
CosaWifiRegGetSsidInfo
    (
        ANSC_HANDLE                 hThisObject
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject            = (PCOSA_DATAMODEL_WIFI     )hThisObject;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoWifiSsid  = (PPOAM_IREP_FOLDER_OBJECT )pMyObject->hIrepFolderWifiSsid;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoWifiSsidE = (PPOAM_IREP_FOLDER_OBJECT )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pCosaContext         = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid            = (PCOSA_DML_WIFI_SSID      )NULL;
    PSLAP_VARIABLE                  pSlapVariable        = (PSLAP_VARIABLE           )NULL;
    ULONG                           ulEntryCount         = 0;
    ULONG                           ulIndex              = 0;
    ULONG                           uInstanceNumber      = 0;
    char*                           pFolderName          = NULL;
    char*                           pName                = NULL;

    if ( !pPoamIrepFoWifiSsid )
    {
        return ANSC_STATUS_FAILURE;
    }
    
    /* Load the newly added but not yet commited entries */
    ulEntryCount = pPoamIrepFoWifiSsid->GetFolderCount((ANSC_HANDLE)pPoamIrepFoWifiSsid);

    for ( ulIndex = 0; ulIndex < ulEntryCount; ulIndex++ )
    {
        /*which instance*/
        pFolderName =
            pPoamIrepFoWifiSsid->EnumFolder
                (
                    (ANSC_HANDLE)pPoamIrepFoWifiSsid,
                    ulIndex
                );

        if ( !pFolderName )
        {
            continue;
        }

        uInstanceNumber = _ansc_atol(pFolderName);

        if ( uInstanceNumber == 0 )
        {
            continue;
        }

        pPoamIrepFoWifiSsidE = pPoamIrepFoWifiSsid->GetFolder((ANSC_HANDLE)pPoamIrepFoWifiSsid, pFolderName);

        AnscFreeMemory(pFolderName);

        if ( !pPoamIrepFoWifiSsidE )
        {
            continue;
        }

        if ( TRUE )
        {
            pSlapVariable =
                (PSLAP_VARIABLE)pPoamIrepFoWifiSsidE->GetRecord
                    (
                        (ANSC_HANDLE)pPoamIrepFoWifiSsidE,
                        COSA_DML_RR_NAME_WifiSsidName,
                        NULL
                    );

            if ( pSlapVariable )
            {
                pName = AnscCloneString(pSlapVariable->Variant.varString);

                SlapFreeVariable(pSlapVariable);
            }
        }

        pCosaContext = (PCOSA_CONTEXT_LINK_OBJECT)AnscAllocateMemory(sizeof(COSA_CONTEXT_LINK_OBJECT));

        if ( !pCosaContext )
        {
            AnscFreeMemory(pName);

            return ANSC_STATUS_FAILURE;
        }

        pWifiSsid = (PCOSA_DML_WIFI_SSID)AnscAllocateMemory(sizeof(COSA_DML_WIFI_SSID));

        if ( !pWifiSsid )
        {
            AnscFreeMemory(pCosaContext);

            AnscFreeMemory(pName);
            
            return ANSC_STATUS_FAILURE;
        }

        //CosaDmlWiFiSsidGetEntryByName(NULL, pName, pWifiSsid);

        AnscCopyString(pWifiSsid->SSID.StaticInfo.Name, pName);
        AnscCopyString(pWifiSsid->SSID.Cfg.Alias, pName);
#if defined (MULTILAN_FEATURE)
        pWifiSsid->SSID.Cfg.InstanceNumber   = uInstanceNumber;
#endif
        pCosaContext->hContext         = (ANSC_HANDLE)pWifiSsid;
        pCosaContext->hParentTable     = NULL;
        pCosaContext->InstanceNumber   = uInstanceNumber;
        pCosaContext->bNew             = TRUE;
        pCosaContext->hPoamIrepUpperFo = (ANSC_HANDLE)pPoamIrepFoWifiSsid;
        pCosaContext->hPoamIrepFo      = (ANSC_HANDLE)pPoamIrepFoWifiSsidE;

        CosaSListPushEntryByInsNum((PSLIST_HEADER)&pMyObject->SsidQueue, pCosaContext);
        
        if ( pName )
        {
            AnscFreeMemory(pName);
            pName = NULL;
        }
    }


    return ANSC_STATUS_SUCCESS;
}


/**********************************************************************

    caller:     owner of this object

    prototype:

        ANSC_STATUS
        CosaWifiRegAddSsidInfo
            (
                ANSC_HANDLE                 hThisObject,
                ANSC_HANDLE                 hCosaContext
            );

    description:

        This function is called to configure Dslm policy parameters.

    argument:   ANSC_HANDLE                 hThisObject
                This handle is actually the pointer of this object
                itself.

                ANSC_HANDLE                 hDdnsInfo
                Specifies the Dslm policy parameters to be filled.

    return:     status of operation.

**********************************************************************/
ANSC_STATUS
CosaWifiRegAddSsidInfo
    (
        ANSC_HANDLE                 hThisObject,
        ANSC_HANDLE                 hCosaContext
    )
{
    ANSC_STATUS                     returnStatus         = ANSC_STATUS_SUCCESS;
    PCOSA_DATAMODEL_WIFI            pMyObject            = (PCOSA_DATAMODEL_WIFI     )hThisObject;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoWifiSsid  = (PPOAM_IREP_FOLDER_OBJECT )pMyObject->hIrepFolderWifiSsid;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoWifiSsidE = (PPOAM_IREP_FOLDER_OBJECT )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pCosaContext         = (PCOSA_CONTEXT_LINK_OBJECT)hCosaContext;
    PCOSA_DML_WIFI_SSID             pWifiSsid            = (PCOSA_DML_WIFI_SSID      )NULL;
    PSLAP_VARIABLE                  pSlapVariable        = (PSLAP_VARIABLE           )NULL;
    char                            FolderName[16]       = {0};

    if ( !pPoamIrepFoWifiSsid || !hCosaContext)
    {
        return ANSC_STATUS_FAILURE;
    }
    else
    {
        pPoamIrepFoWifiSsid->EnableFileSync((ANSC_HANDLE)pPoamIrepFoWifiSsid, FALSE);
    }

    if ( TRUE )
    {
        SlapAllocVariable(pSlapVariable);

        if ( !pSlapVariable )
        {
            returnStatus = ANSC_STATUS_RESOURCES;

            goto  EXIT1;
        }
    }
    
    /*Next Instance*/
    if ( TRUE )
    {
        returnStatus = 
            pPoamIrepFoWifiSsid->DelRecord
                (
                    (ANSC_HANDLE)pPoamIrepFoWifiSsid,
                    COSA_DML_RR_NAME_WifiSsidNextInsNunmber
                );        
        
        pSlapVariable->Syntax            = SLAP_VAR_SYNTAX_uint32;
        pSlapVariable->Variant.varUint32 = pMyObject->ulSsidNextInstance;

        returnStatus =
            pPoamIrepFoWifiSsid->AddRecord
                (
                    (ANSC_HANDLE)pPoamIrepFoWifiSsid,
                    COSA_DML_RR_NAME_WifiSsidNextInsNunmber,
                    SYS_REP_RECORD_TYPE_UINT,
                    SYS_RECORD_CONTENT_DEFAULT,
                    pSlapVariable,
                    0
                );

        SlapCleanVariable(pSlapVariable);
        SlapInitVariable (pSlapVariable);
    }

        pWifiSsid    = (PCOSA_DML_WIFI_SSID)pCosaContext->hContext;

        _ansc_sprintf(FolderName, "%lu", pCosaContext->InstanceNumber);

        pPoamIrepFoWifiSsidE =
            pPoamIrepFoWifiSsid->AddFolder
                (
                    (ANSC_HANDLE)pPoamIrepFoWifiSsid,
                    FolderName,
                    0
                );

        if ( !pPoamIrepFoWifiSsidE )
        {
            returnStatus = ANSC_STATUS_FAILURE;
            
            goto EXIT1;
        }

        if ( TRUE )
        {
            pSlapVariable->Syntax            = SLAP_VAR_SYNTAX_string;
            pSlapVariable->Variant.varString = AnscCloneString(pWifiSsid->SSID.StaticInfo.Name);

            returnStatus =
                pPoamIrepFoWifiSsidE->AddRecord
                    (
                        (ANSC_HANDLE)pPoamIrepFoWifiSsidE,
                        COSA_DML_RR_NAME_WifiSsidName,
                        SYS_REP_RECORD_TYPE_ASTR,
                        SYS_RECORD_CONTENT_DEFAULT,
                        pSlapVariable,
                        0
                    );

            SlapCleanVariable(pSlapVariable);
            SlapInitVariable (pSlapVariable);
        }

        pCosaContext->hPoamIrepUpperFo = (ANSC_HANDLE)pPoamIrepFoWifiSsid;
        pCosaContext->hPoamIrepFo      = (ANSC_HANDLE)pPoamIrepFoWifiSsidE;
        
EXIT1:

    if ( pSlapVariable )
    {
        SlapFreeVariable(pSlapVariable);
    }

    pPoamIrepFoWifiSsid->EnableFileSync((ANSC_HANDLE)pPoamIrepFoWifiSsid, TRUE);

    return returnStatus;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        ANSC_STATUS
        CosaWifiRegDelSsidInfo
            (
                ANSC_HANDLE                 hThisObject,
                ANSC_HANDLE                 hCosaContext
            );

    description:

        This function is called to configure Dslm policy parameters.

    argument:   ANSC_HANDLE                 hThisObject
                This handle is actually the pointer of this object
                itself.

                ANSC_HANDLE                 hDdnsInfo
                Specifies the Dslm policy parameters to be filled.

    return:     status of operation.

**********************************************************************/
ANSC_STATUS
CosaWifiRegDelSsidInfo
    (
        ANSC_HANDLE                 hThisObject,
        ANSC_HANDLE                 hCosaContext
    )
{
    UNREFERENCED_PARAMETER(hThisObject);
    ANSC_STATUS                     returnStatus         = ANSC_STATUS_SUCCESS;
    PCOSA_CONTEXT_LINK_OBJECT       pCosaContext         = (PCOSA_CONTEXT_LINK_OBJECT)hCosaContext;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoWifiSsid  = (PPOAM_IREP_FOLDER_OBJECT )pCosaContext->hPoamIrepUpperFo;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoWifiSsidE = (PPOAM_IREP_FOLDER_OBJECT )pCosaContext->hPoamIrepFo;

    if ( !pPoamIrepFoWifiSsid || !pPoamIrepFoWifiSsidE)
    {
        return ANSC_STATUS_FAILURE;
    }
    else
    {
        pPoamIrepFoWifiSsid->EnableFileSync((ANSC_HANDLE)pPoamIrepFoWifiSsid, FALSE);
    }

    if ( TRUE )
    {
        pPoamIrepFoWifiSsidE->Close((ANSC_HANDLE)pPoamIrepFoWifiSsidE);
        
        pPoamIrepFoWifiSsid->DelFolder
            (
                (ANSC_HANDLE)pPoamIrepFoWifiSsid, 
                pPoamIrepFoWifiSsidE->GetFolderName((ANSC_HANDLE)pPoamIrepFoWifiSsidE)
            );

        AnscFreeMemory(pPoamIrepFoWifiSsidE);
    }

    pPoamIrepFoWifiSsid->EnableFileSync((ANSC_HANDLE)pPoamIrepFoWifiSsid, TRUE);
    
    return returnStatus;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        ANSC_STATUS
        CosaWifiRegGetAPInfo
            (
                ANSC_HANDLE                 hThisObject
            );

    description:

        This function is called to retrieve Dslm policy parameters.

    argument:   ANSC_HANDLE                 hThisObject
                This handle is actually the pointer of this object
                itself.

                ANSC_HANDLE                 hDdnsInfo
                Specifies the Dslm policy parameters to be filled.

    return:     status of operation.

**********************************************************************/

ANSC_STATUS
CosaWifiRegGetAPInfo
    (
        ANSC_HANDLE                 hThisObject
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject            = (PCOSA_DATAMODEL_WIFI     )hThisObject;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoWifiAP    = (PPOAM_IREP_FOLDER_OBJECT )pMyObject->hIrepFolderWifiAP;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoWifiAPE   = (PPOAM_IREP_FOLDER_OBJECT )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pCosaContext         = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_AP               pWifiAP              = (PCOSA_DML_WIFI_AP        )NULL;
    PSLAP_VARIABLE                  pSlapVariable        = (PSLAP_VARIABLE           )NULL;
    ULONG                           ulEntryCount         = 0;
    ULONG                           ulIndex              = 0;
    ULONG                           uInstanceNumber      = 0;
    char*                           pFolderName          = NULL;
    char*                           pName                = NULL;
    char*                           pSsidReference       = NULL;

    if ( !pPoamIrepFoWifiAP )
    {
        return ANSC_STATUS_FAILURE;
    }

    ulEntryCount = pPoamIrepFoWifiAP->GetFolderCount((ANSC_HANDLE)pPoamIrepFoWifiAP);

    for ( ulIndex = 0; ulIndex < ulEntryCount; ulIndex++ )
    {
        /*which instance*/
        pFolderName =
            pPoamIrepFoWifiAP->EnumFolder
                (
                    (ANSC_HANDLE)pPoamIrepFoWifiAP,
                    ulIndex
                );

        if ( !pFolderName )
        {
            continue;
        }

        uInstanceNumber = _ansc_atol(pFolderName);

        if ( uInstanceNumber == 0 )
        {
            continue;
        }

        pPoamIrepFoWifiAPE = pPoamIrepFoWifiAP->GetFolder((ANSC_HANDLE)pPoamIrepFoWifiAP, pFolderName);

        AnscFreeMemory(pFolderName);
        pFolderName = NULL;

        if ( !pPoamIrepFoWifiAPE )
        {
            continue;
        }

        if ( TRUE )
        {
            pSlapVariable =
                (PSLAP_VARIABLE)pPoamIrepFoWifiAPE->GetRecord
                    (
                        (ANSC_HANDLE)pPoamIrepFoWifiAPE,
                        COSA_DML_RR_NAME_WifiAPSSID,
                        NULL
                    );

            if ( pSlapVariable )
            {
                pName = AnscCloneString(pSlapVariable->Variant.varString);

                SlapFreeVariable(pSlapVariable);
            }
        }

        if ( TRUE )
        {
            pSlapVariable =
                (PSLAP_VARIABLE)pPoamIrepFoWifiAPE->GetRecord
                    (
                        (ANSC_HANDLE)pPoamIrepFoWifiAPE,
                        COSA_DML_RR_NAME_WifiAPSSIDReference,
                        NULL
                    );

            if ( pSlapVariable )
            {
                pSsidReference = AnscCloneString(pSlapVariable->Variant.varString);

                SlapFreeVariable(pSlapVariable);
            }
        }

        pCosaContext = (PCOSA_CONTEXT_LINK_OBJECT)AnscAllocateMemory(sizeof(COSA_CONTEXT_LINK_OBJECT));

        if ( !pCosaContext )
        {
            /*RDKB-6906,CID-33542, CID-33244, free unused resource before exit*/
            if(pName)
                AnscFreeMemory(pName);
            if(pSsidReference)
                AnscFreeMemory(pSsidReference);

            return ANSC_STATUS_FAILURE;
        }

        pWifiAP = (PCOSA_DML_WIFI_AP)AnscAllocateMemory(sizeof(COSA_DML_WIFI_AP));

        if ( !pWifiAP )
        {
            AnscFreeMemory(pCosaContext);

            /*RDKB-6906,CID-33542, CID-33244, free unused resource before exit*/
            if(pName)
                AnscFreeMemory(pName);
            if(pSsidReference)
                AnscFreeMemory(pSsidReference);

            return ANSC_STATUS_FAILURE;
        }

        AnscCopyString(pWifiAP->AP.Cfg.SSID, pSsidReference);
#if defined (MULTILAN_FEATURE)
        pWifiAP->AP.Cfg.InstanceNumber   = uInstanceNumber;
#endif
        pCosaContext->InstanceNumber   = uInstanceNumber;
        pCosaContext->bNew             = TRUE;
        pCosaContext->hContext         = (ANSC_HANDLE)pWifiAP;
        pCosaContext->hParentTable     = NULL;
        pCosaContext->hPoamIrepUpperFo = (ANSC_HANDLE)pPoamIrepFoWifiAP;
        pCosaContext->hPoamIrepFo      = (ANSC_HANDLE)pPoamIrepFoWifiAPE;

        CosaSListPushEntryByInsNum((PSLIST_HEADER)&pMyObject->AccessPointQueue, pCosaContext);

        if ( pName )
        {
            AnscFreeMemory(pName);
            pName = NULL;
        }

        if (pSsidReference)
        {
            AnscFreeMemory(pSsidReference);
            pSsidReference = NULL;
        }
    }

    return ANSC_STATUS_SUCCESS;
}


/**********************************************************************

    caller:     owner of this object

    prototype:

        ANSC_STATUS
        CosaWifiRegAddAPInfo
            (
                ANSC_HANDLE                 hThisObject,
                ANSC_HANDLE                 hCosaContext
            );

    description:

        This function is called to configure Dslm policy parameters.

    argument:   ANSC_HANDLE                 hThisObject
                This handle is actually the pointer of this object
                itself.

                ANSC_HANDLE                 hDdnsInfo
                Specifies the Dslm policy parameters to be filled.

    return:     status of operation.

**********************************************************************/

ANSC_STATUS
CosaWifiRegAddAPInfo
    (
        ANSC_HANDLE                 hThisObject,
        ANSC_HANDLE                 hCosaContext
    )
{
    ANSC_STATUS                     returnStatus         = ANSC_STATUS_SUCCESS;
    PCOSA_DATAMODEL_WIFI            pMyObject            = (PCOSA_DATAMODEL_WIFI     )hThisObject;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoWifiAP    = (PPOAM_IREP_FOLDER_OBJECT )pMyObject->hIrepFolderWifiAP;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoWifiAPE   = (PPOAM_IREP_FOLDER_OBJECT )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pCosaContext         = (PCOSA_CONTEXT_LINK_OBJECT)hCosaContext;
    PCOSA_DML_WIFI_AP               pWifiAP              = (PCOSA_DML_WIFI_AP        )NULL;
    PSINGLE_LINK_ENTRY              pSsidSLinkEntry      = (PSINGLE_LINK_ENTRY       )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pSSIDLinkObj         = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_SSID             pWifiSsid            = (PCOSA_DML_WIFI_SSID      )NULL;
    PSLAP_VARIABLE                  pSlapVariable        = (PSLAP_VARIABLE           )NULL;
    char                            FolderName[16]       = {0};
    CHAR                            PathName[64]         = {0};

    if ( !pPoamIrepFoWifiAP || !pCosaContext)
    {
        return ANSC_STATUS_FAILURE;
    }
    else
    {
        pPoamIrepFoWifiAP->EnableFileSync((ANSC_HANDLE)pPoamIrepFoWifiAP, FALSE);
    }

    if ( TRUE )
    {
        SlapAllocVariable(pSlapVariable);

        if ( !pSlapVariable )
        {
            returnStatus = ANSC_STATUS_RESOURCES;

            goto  EXIT1;
        }
    }

    /*Next Instance*/
    if ( TRUE )
    {
        returnStatus = 
        pPoamIrepFoWifiAP->DelRecord
            (
                (ANSC_HANDLE)pPoamIrepFoWifiAP,
                COSA_DML_RR_NAME_WifiAPNextInsNunmber
            );
                
        pSlapVariable->Syntax            = SLAP_VAR_SYNTAX_uint32;
        pSlapVariable->Variant.varUint32 = pMyObject->ulAPNextInstance;

        returnStatus =
            pPoamIrepFoWifiAP->AddRecord
                (
                    (ANSC_HANDLE)pPoamIrepFoWifiAP,
                    COSA_DML_RR_NAME_WifiAPNextInsNunmber,
                    SYS_REP_RECORD_TYPE_UINT,
                    SYS_RECORD_CONTENT_DEFAULT,
                    pSlapVariable,
                    0
                );

        SlapCleanVariable(pSlapVariable);
        SlapInitVariable (pSlapVariable);
    }

    pWifiAP      = (PCOSA_DML_WIFI_AP)pCosaContext->hContext;

    _ansc_sprintf(FolderName, "%lu", pCosaContext->InstanceNumber);

    pPoamIrepFoWifiAPE =
        pPoamIrepFoWifiAP->AddFolder
            (
                (ANSC_HANDLE)pPoamIrepFoWifiAP,
                FolderName,
                0
            );

    if ( !pPoamIrepFoWifiAPE )
    {
        returnStatus = ANSC_STATUS_FAILURE;
        
        goto  EXIT1;
    }


    /*Store the corresponding SSID and SSIDReference*/
    if ( TRUE )
    {
        pSlapVariable->Syntax            = SLAP_VAR_SYNTAX_string;

        pSsidSLinkEntry = AnscQueueGetFirstEntry(&pMyObject->SsidQueue);
    
        while ( pSsidSLinkEntry )
        {
            pSSIDLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSsidSLinkEntry);
            pWifiSsid    = (PCOSA_DML_WIFI_SSID)pSSIDLinkObj->hContext;
    
            sprintf(PathName, "Device.WiFi.SSID.%lu.", pSSIDLinkObj->InstanceNumber);
    
            /*see whether the corresponding SSID entry exists*/
            if ( AnscEqualString(pWifiAP->AP.Cfg.SSID, PathName, TRUE) )
            {
                break;
            }
    
            pSsidSLinkEntry             = AnscQueueGetNextEntry(pSsidSLinkEntry);
        }

        if (pSsidSLinkEntry)
        {
            pSlapVariable->Variant.varString = AnscCloneString(pWifiSsid->SSID.Cfg.SSID);

            returnStatus =
                pPoamIrepFoWifiAPE->AddRecord
                    (
                        (ANSC_HANDLE)pPoamIrepFoWifiAPE,
                        COSA_DML_RR_NAME_WifiAPSSID,
                        SYS_REP_RECORD_TYPE_ASTR,
                        SYS_RECORD_CONTENT_DEFAULT,
                        pSlapVariable,
                        0
                    );

            SlapCleanVariable(pSlapVariable);
            SlapInitVariable (pSlapVariable);
        
            pSlapVariable->Syntax            = SLAP_VAR_SYNTAX_string;
            pSlapVariable->Variant.varString = AnscCloneString(PathName);

            returnStatus =
                pPoamIrepFoWifiAPE->AddRecord
                    (
                        (ANSC_HANDLE)pPoamIrepFoWifiAPE,
                        COSA_DML_RR_NAME_WifiAPSSIDReference,
                        SYS_REP_RECORD_TYPE_ASTR,
                        SYS_RECORD_CONTENT_DEFAULT,
                        pSlapVariable,
                        0
                    );

            SlapCleanVariable(pSlapVariable);
            SlapInitVariable (pSlapVariable);
        }
        else
        {
            returnStatus = ANSC_STATUS_FAILURE;
        
            goto  EXIT1;                
        }
    }


    pCosaContext->hPoamIrepUpperFo = (ANSC_HANDLE)pPoamIrepFoWifiAP;
    pCosaContext->hPoamIrepFo      = (ANSC_HANDLE)pPoamIrepFoWifiAPE;

EXIT1:

    if ( pSlapVariable )
    {
        SlapFreeVariable(pSlapVariable);
    }

    pPoamIrepFoWifiAP->EnableFileSync((ANSC_HANDLE)pPoamIrepFoWifiAP, TRUE);

    return returnStatus;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        ANSC_STATUS
        CosaWifiRegDelAPInfo
            (
                ANSC_HANDLE                 hThisObject,
                ANSC_HANDLE                 hCosaContext
            );

    description:

        This function is called to configure Dslm policy parameters.

    argument:   ANSC_HANDLE                 hThisObject
                This handle is actually the pointer of this object
                itself.

                ANSC_HANDLE                 hDdnsInfo
                Specifies the Dslm policy parameters to be filled.

    return:     status of operation.

**********************************************************************/

ANSC_STATUS
CosaWifiRegDelAPInfo
    (
        ANSC_HANDLE                 hThisObject,
        ANSC_HANDLE                 hCosaContext
    )
{
    UNREFERENCED_PARAMETER(hThisObject);
    ANSC_STATUS                     returnStatus         = ANSC_STATUS_SUCCESS;
    PCOSA_CONTEXT_LINK_OBJECT       pCosaContext         = (PCOSA_CONTEXT_LINK_OBJECT)hCosaContext;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoWifiAP    = (PPOAM_IREP_FOLDER_OBJECT )pCosaContext->hPoamIrepUpperFo;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoWifiAPE   = (PPOAM_IREP_FOLDER_OBJECT )pCosaContext->hPoamIrepFo;

    if ( !pPoamIrepFoWifiAP || !pPoamIrepFoWifiAPE )
    {
        return ANSC_STATUS_FAILURE;
    }
    else
    {
        pPoamIrepFoWifiAP->EnableFileSync((ANSC_HANDLE)pPoamIrepFoWifiAP, FALSE);
    }

    if ( TRUE )
    {
        pPoamIrepFoWifiAPE->Close((ANSC_HANDLE)pPoamIrepFoWifiAPE);
        
        pPoamIrepFoWifiAP->DelFolder
            (
                (ANSC_HANDLE)pPoamIrepFoWifiAP, 
                pPoamIrepFoWifiAPE->GetFolderName((ANSC_HANDLE)pPoamIrepFoWifiAPE)
            );

        AnscFreeMemory(pPoamIrepFoWifiAPE);
    }

    pPoamIrepFoWifiAP->EnableFileSync((ANSC_HANDLE)pPoamIrepFoWifiAP, TRUE);
    
    return returnStatus;
}

ANSC_STATUS
CosaDmlWiFiApMfSetMacList
    (
        CHAR        *maclist,
        UCHAR       *mac,
        ULONG       *numList
    )
{
    int     i = 0;
    char *buf = NULL;
    unsigned char macAddr[COSA_DML_WIFI_MAX_MAC_FILTER_NUM][6];

    buf = strtok(maclist, ",");
    while(buf != NULL)
    {
        if(CosaUtilStringToHex(buf, macAddr[i], 6) != ANSC_STATUS_SUCCESS)
        {
            *numList = 0;
            return ANSC_STATUS_FAILURE;
        }
        i++;
        buf = strtok(NULL, ",");
    }
    *numList = i;
    memcpy(mac, macAddr, 6*i);
    
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiApMfGetMacList
    (
        UCHAR       *mac,
        CHAR        *maclist,
        ULONG       numList
    )
{
    unsigned int i = 0;
    int     j = 0;
    char macAddr[COSA_DML_WIFI_MAX_MAC_FILTER_NUM][18];

    for(i = 0; i<numList; i++) {
        if(i > 0)
            strcat(maclist, ",");
        sprintf(macAddr[i], "%02x:%02x:%02x:%02x:%02x:%02x", mac[j], mac[j+1], mac[j+2], mac[j+3], mac[j+4], mac[j+5]);
        strcat(maclist, macAddr[i]);
        j +=6;
    }
    return ANSC_STATUS_SUCCESS;
}


ANSC_STATUS
CosaWifiRegGetMacFiltInfo
    (
        ANSC_HANDLE                 hThisObject
    )
{
    PCOSA_DML_WIFI_AP_FULL          pMyObject            = (PCOSA_DML_WIFI_AP_FULL     )hThisObject;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoMacFilt   = (PPOAM_IREP_FOLDER_OBJECT )pMyObject->hIrepFolderMacFilt;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoMacFiltE  = (PPOAM_IREP_FOLDER_OBJECT )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pCosaContext         = (PCOSA_CONTEXT_LINK_OBJECT)NULL;
    PCOSA_DML_WIFI_AP_MAC_FILTER    pMacFilt             = (PCOSA_DML_WIFI_AP_MAC_FILTER      )NULL;
    PSLAP_VARIABLE                  pSlapVariable        = (PSLAP_VARIABLE           )NULL;
    ULONG                           ulEntryCount         = 0;
    ULONG                           ulIndex              = 0;
    ULONG                           uInstanceNumber      = 0;
    char*                           pFolderName          = NULL;
    char*                           pName                = NULL;

    if ( !pPoamIrepFoMacFilt )
    {
        return ANSC_STATUS_FAILURE;
    }
    
    /* Load the newly added but not yet commited entries */
    ulEntryCount = pPoamIrepFoMacFilt->GetFolderCount((ANSC_HANDLE)pPoamIrepFoMacFilt);

    for ( ulIndex = 0; ulIndex < ulEntryCount; ulIndex++ )
    {
        /*which instance*/
        pFolderName =
            pPoamIrepFoMacFilt->EnumFolder
                (
                    (ANSC_HANDLE)pPoamIrepFoMacFilt,
                    ulIndex
                );

        if ( !pFolderName )
        {
            continue;
        }

        uInstanceNumber = _ansc_atol(pFolderName);

        if ( uInstanceNumber == 0 )
        {
            continue;
        }

        pPoamIrepFoMacFiltE = pPoamIrepFoMacFilt->GetFolder((ANSC_HANDLE)pPoamIrepFoMacFilt, pFolderName);

        AnscFreeMemory(pFolderName);

        if ( !pPoamIrepFoMacFiltE )
        {
            continue;
        }

        if ( TRUE )
        {
            pSlapVariable =
                (PSLAP_VARIABLE)pPoamIrepFoMacFiltE->GetRecord
                    (
                        (ANSC_HANDLE)pPoamIrepFoMacFiltE,
                        COSA_DML_RR_NAME_MacFiltTab,
                        NULL
                    );

            if ( pSlapVariable )
            {
                pName = AnscCloneString(pSlapVariable->Variant.varString);

                SlapFreeVariable(pSlapVariable);
            }
        }

        pCosaContext = (PCOSA_CONTEXT_LINK_OBJECT)AnscAllocateMemory(sizeof(COSA_CONTEXT_LINK_OBJECT));

        if ( !pCosaContext )
        {
            AnscFreeMemory(pName);

            return ANSC_STATUS_FAILURE;
        }

        pMacFilt = (PCOSA_DML_WIFI_AP_MAC_FILTER)AnscAllocateMemory(sizeof(COSA_DML_WIFI_AP_MAC_FILTER));

        if ( !pMacFilt )
        {
            AnscFreeMemory(pCosaContext);

            AnscFreeMemory(pName);
            
            return ANSC_STATUS_FAILURE;
        }

        AnscCopyString(pMacFilt->Alias, pName);
    
        pCosaContext->hContext         = (ANSC_HANDLE)pMacFilt;
        pCosaContext->hParentTable     = NULL;
        pCosaContext->InstanceNumber   = uInstanceNumber;
        pCosaContext->bNew             = TRUE;
        pCosaContext->hPoamIrepUpperFo = (ANSC_HANDLE)pPoamIrepFoMacFilt;
        pCosaContext->hPoamIrepFo      = (ANSC_HANDLE)pPoamIrepFoMacFiltE;

        CosaSListPushEntryByInsNum((PSLIST_HEADER)&pMyObject->MacFilterList, pCosaContext);
        
        if ( pName )
        {
            AnscFreeMemory(pName);
            pName = NULL;
        }
    }


    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaWifiRegAddMacFiltInfo
    (
        ANSC_HANDLE                 hThisObject,
        ANSC_HANDLE                 hCosaContext
    )
{
    ANSC_STATUS                     returnStatus         = ANSC_STATUS_SUCCESS;
    PCOSA_DML_WIFI_AP_FULL          pMyObject            = (PCOSA_DML_WIFI_AP_FULL     )hThisObject;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoMacFilt  = (PPOAM_IREP_FOLDER_OBJECT )pMyObject->hIrepFolderMacFilt;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoMacFiltE = (PPOAM_IREP_FOLDER_OBJECT )NULL;
    PCOSA_CONTEXT_LINK_OBJECT       pCosaContext         = (PCOSA_CONTEXT_LINK_OBJECT)hCosaContext;
    PCOSA_DML_WIFI_AP_MAC_FILTER    pMacFilt            = (PCOSA_DML_WIFI_AP_MAC_FILTER      )NULL;
    PSLAP_VARIABLE                  pSlapVariable        = (PSLAP_VARIABLE           )NULL;
    char                            FolderName[16]       = {0};

    if ( !pPoamIrepFoMacFilt || !hCosaContext)
    {
        return ANSC_STATUS_FAILURE;
    }
    else
    {
        pPoamIrepFoMacFilt->EnableFileSync((ANSC_HANDLE)pPoamIrepFoMacFilt, FALSE);
    }

    if ( TRUE )
    {
        SlapAllocVariable(pSlapVariable);

        if ( !pSlapVariable )
        {
            returnStatus = ANSC_STATUS_RESOURCES;

            goto  EXIT1;
        }
    }
    
    /*Next Instance*/
    if ( TRUE )
    {
        returnStatus = 
            pPoamIrepFoMacFilt->DelRecord
                (
                    (ANSC_HANDLE)pPoamIrepFoMacFilt,
                    COSA_DML_RR_NAME_MacFiltTabNextInsNunmber
                );        
        
        pSlapVariable->Syntax            = SLAP_VAR_SYNTAX_uint32;
        pSlapVariable->Variant.varUint32 = pMyObject->ulMacFilterNextInsNum;

        returnStatus =
            pPoamIrepFoMacFilt->AddRecord
                (
                    (ANSC_HANDLE)pPoamIrepFoMacFilt,
                    COSA_DML_RR_NAME_MacFiltTabNextInsNunmber,
                    SYS_REP_RECORD_TYPE_UINT,
                    SYS_RECORD_CONTENT_DEFAULT,
                    pSlapVariable,
                    0
                );

        SlapCleanVariable(pSlapVariable);
        SlapInitVariable (pSlapVariable);
    }

        pMacFilt    = (PCOSA_DML_WIFI_AP_MAC_FILTER)pCosaContext->hContext;

        _ansc_sprintf(FolderName, "%lu", pCosaContext->InstanceNumber);

        pPoamIrepFoMacFiltE =
            pPoamIrepFoMacFilt->AddFolder
                (
                    (ANSC_HANDLE)pPoamIrepFoMacFilt,
                    FolderName,
                    0
                );

        if ( !pPoamIrepFoMacFiltE )
        {
            returnStatus = ANSC_STATUS_FAILURE;
            
            goto EXIT1;
        }

        if ( TRUE )
        {
            pSlapVariable->Syntax            = SLAP_VAR_SYNTAX_string;
            pSlapVariable->Variant.varString = AnscCloneString(pMacFilt->Alias);

            returnStatus =
                pPoamIrepFoMacFiltE->AddRecord
                    (
                        (ANSC_HANDLE)pPoamIrepFoMacFiltE,
                        COSA_DML_RR_NAME_MacFiltTab,
                        SYS_REP_RECORD_TYPE_ASTR,
                        SYS_RECORD_CONTENT_DEFAULT,
                        pSlapVariable,
                        0
                    );

            SlapCleanVariable(pSlapVariable);
            SlapInitVariable (pSlapVariable);
        }

        pCosaContext->hPoamIrepUpperFo = (ANSC_HANDLE)pPoamIrepFoMacFilt;
        pCosaContext->hPoamIrepFo      = (ANSC_HANDLE)pPoamIrepFoMacFiltE;
        
EXIT1:

    if ( pSlapVariable )
    {
        SlapFreeVariable(pSlapVariable);
    }

    pPoamIrepFoMacFilt->EnableFileSync((ANSC_HANDLE)pPoamIrepFoMacFilt, TRUE);

    return returnStatus;
}

ANSC_STATUS
CosaWifiRegDelMacFiltInfo
    (
        ANSC_HANDLE                 hThisObject,
        ANSC_HANDLE                 hCosaContext
    )
{
    UNREFERENCED_PARAMETER(hThisObject);
    ANSC_STATUS                     returnStatus         = ANSC_STATUS_SUCCESS;
    PCOSA_CONTEXT_LINK_OBJECT       pCosaContext         = (PCOSA_CONTEXT_LINK_OBJECT)hCosaContext;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoMacFilt  = (PPOAM_IREP_FOLDER_OBJECT )pCosaContext->hPoamIrepUpperFo;
    PPOAM_IREP_FOLDER_OBJECT        pPoamIrepFoMacFiltE = (PPOAM_IREP_FOLDER_OBJECT )pCosaContext->hPoamIrepFo;

    if ( !pPoamIrepFoMacFilt || !pPoamIrepFoMacFiltE)
    {
        return ANSC_STATUS_FAILURE;
    }
    else
    {
        pPoamIrepFoMacFilt->EnableFileSync((ANSC_HANDLE)pPoamIrepFoMacFilt, FALSE);
    }

    if ( TRUE )
    {
        pPoamIrepFoMacFiltE->Close((ANSC_HANDLE)pPoamIrepFoMacFiltE);
        
        pPoamIrepFoMacFilt->DelFolder
            (
                (ANSC_HANDLE)pPoamIrepFoMacFilt, 
                pPoamIrepFoMacFiltE->GetFolderName((ANSC_HANDLE)pPoamIrepFoMacFiltE)
            );

        AnscFreeMemory(pPoamIrepFoMacFiltE);
    }

    pPoamIrepFoMacFilt->EnableFileSync((ANSC_HANDLE)pPoamIrepFoMacFilt, TRUE);
    
    return returnStatus;
}
