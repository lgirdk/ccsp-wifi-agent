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

#include "cosa_wifi_internal.h"
#include "cosa_lgi_wifi_bandsteering_dml.h"
#include "wifi_hal.h"
#include "messagebus_interface_helper.h"

// Called once when agent initializes
ANSC_STATUS CosaLgiWifiInitialize( ANSC_HANDLE hThisObject )
{
    ANSC_STATUS           returnStatus = ANSC_STATUS_SUCCESS;
    PCOSA_DATAMODEL_WIFI  pMyObject    = (PCOSA_DATAMODEL_WIFI)hThisObject;

    //Device.WiFi.X_LGI-COM_BandSteering.
    PCOSA_DML_BANDSTEERING_SSID pBandSteeringSSIDEntry = NULL;

    //Device.WiFi.X_LGI-COM_BandSteering.
    //init BandSteering SSID Table with 8 entries, all values false
    pMyObject->pBandSteeringSSIDTable = NULL;
    pMyObject->ulBandSteeringSSIDEntryCount = 1;
    int i;

    pMyObject->pBandSteeringSSIDTable = (PCOSA_DML_BANDSTEERING_SSID)AnscAllocateMemory(
      sizeof(COSA_DML_BANDSTEERING_SSID) * pMyObject->ulBandSteeringSSIDEntryCount);

    if (pMyObject->pBandSteeringSSIDTable != NULL)
    {
        for( i = 0; i < pMyObject->ulBandSteeringSSIDEntryCount; i++)
        {
            pBandSteeringSSIDEntry = pMyObject->pBandSteeringSSIDTable+i;

            pBandSteeringSSIDEntry->ifIndex = i;
            pBandSteeringSSIDEntry->Enable = FALSE;
            pBandSteeringSSIDEntry->Active = FALSE;
            pBandSteeringSSIDEntry->ClearCapable5G = FALSE;
            pBandSteeringSSIDEntry->ClearBlacklist24G = FALSE;
            pBandSteeringSSIDEntry->RSSIThreshold = -70;
            pBandSteeringSSIDEntry->DeltaThreshold = 5;
            pBandSteeringSSIDEntry->BlacklistTimeout = 15000;
            pBandSteeringSSIDEntry->pCapable5GEntryCount = 0;
            pBandSteeringSSIDEntry->pCapable5G = NULL;
            pBandSteeringSSIDEntry->pBlacklist24GEntryCount = 0;
            pBandSteeringSSIDEntry->pBlacklist24G = NULL;
        }
    }

    return  returnStatus;
}

// Called after wifi driver restarts
ANSC_STATUS CosaLgiWifiReInitialize( ANSC_HANDLE hThisObject )
{
    ANSC_STATUS           returnStatus = ANSC_STATUS_SUCCESS;

    // Nothing to do yet

    return  returnStatus;
}

// Called after wifi restores to default
ANSC_STATUS CosaLgiWifiReInitializeRadioAndAp( ANSC_HANDLE hThisObject )
{
    ANSC_STATUS                     returnStatus        = ANSC_STATUS_SUCCESS;

    return returnStatus;
}

// Called once before exiting
ANSC_STATUS CosaLgiWifiRemove( ANSC_HANDLE hThisObject )
{
    ANSC_STATUS                        returnStatus = ANSC_STATUS_SUCCESS;
    PCOSA_DATAMODEL_WIFI               pMyObject    = (PCOSA_DATAMODEL_WIFI)hThisObject;

    /*Remove BandSteering Object*/
    if (pMyObject->pBandSteeringSSIDTable != NULL)
    {
        AnscFreeMemory((ANSC_HANDLE)pMyObject->pBandSteeringSSIDTable);
    }

    return  returnStatus;
}

