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
#include "ansc_platform.h"
#include "cosa_wifi_dml.h"
#include "cosa_wifi_internal.h"
#include "plugin_main_apis.h"
#include "cosa_wifi_apis.h"
#include "cosa_lgi_wifi_radius_dml.h"

/***********************************************************************

 APIs for Object:

    X_LGI_COM_Radius.

    *  LGI_Radius_GetParamUlongValue
    *  LGI_Radius_SetParamUlongValue
    *  LGI_Radius_Validate
    *  LGI_Radius_Commit
    *  LGI_Radius_Rollback

***********************************************************************/

BOOL
LGI_Radius_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
PCOSA_DATAMODEL_WIFI            pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
PCOSA_DML_LG_WIFI_RADIUS        pRADIUS   = pMyObject->pRADIUS;
    /* check the parameter name and return the corresponding value */

    if (strcmp(ParamName, "TransportInterface") == 0)
    {
		*puLong = pRADIUS->TransportInterface;
		return TRUE;
    }

    return FALSE;
}

BOOL
LGI_Radius_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValuepUlong
    )
{
PCOSA_DATAMODEL_WIFI            pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
PCOSA_DML_LG_WIFI_RADIUS        pRADIUS   = pMyObject->pRADIUS;
    if (strcmp(ParamName, "TransportInterface") == 0)
    {
		if ( pRADIUS->TransportInterface != uValuepUlong )
                {
			pRADIUS->TransportInterface = uValuepUlong;
			setRadiusTransportInterface(uValuepUlong);
		}
		return TRUE;
    }

    return FALSE;
}



BOOL
LGI_Radius_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    return TRUE;
}

ULONG
LGI_Radius_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
    /* Do nothing right now */
    return FALSE;

}

ULONG
LGI_Radius_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
    /* Do nothing right now */
    return FALSE;

}
