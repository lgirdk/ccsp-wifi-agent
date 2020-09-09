/*********************************************************************
 * Copyright 2017-2019 ARRIS Enterprises, LLC.
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

#ifndef  _COSA_LGI_WIFI_INTERNAL_H
#define  _COSA_LGI_WIFI_INTERNAL_H

#include "cosa_wifi_apis.h"
#include "poam_irepfo_interface.h"
#include "sys_definitions.h"
#include "cosa_lgi_wifi_bandsteering_dml.h"
#include "ccsp_psm_helper.h"

extern ANSC_HANDLE bus_handle;
extern char g_Subsystem[32];

#define  COSA_DATAMODEL_LGI_WIFI_ADD_CLASS_CONTENT           \
    /*BandSteering*/                                           \
    PCOSA_DML_BANDSTEERING_SSID  pBandSteeringSSIDTable;       \
    ULONG               ulBandSteeringSSIDEntryCount;          \


ANSC_STATUS CosaLgiWifiInitialize  ( ANSC_HANDLE hThisObject );
ANSC_STATUS CosaLgiWifiReInitialize( ANSC_HANDLE hThisObject );
ANSC_STATUS CosaLgiWifiRemove      ( ANSC_HANDLE hThisObject );
ANSC_STATUS CosaLgiWifiReInitializeRadioAndAp( ANSC_HANDLE hThisObject );

#endif
