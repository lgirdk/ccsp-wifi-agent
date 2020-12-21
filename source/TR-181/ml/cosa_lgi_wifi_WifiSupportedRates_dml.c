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
#include "wifi_hal.h"
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

/***********************************************************************

 APIs for Object:

    X_LGI-COM_WifiSupportedRates.

    *  WifiSupportedRatesControl_GetParamBoolValue
    *  WifiSupportedRatesControl_SetParamBoolValue

***********************************************************************/

BOOL
WifiSupportedRatesControl_GetParamBoolValue
(
	ANSC_HANDLE                 hInsContext,
	char*                       ParamName,
	BOOL*                       pBool
)
{
	if (strcmp(ParamName, "WiFiBitmapControlFeature") == 0)
	{
		BOOL enable = FALSE;
		wifi_getSupportRatesBitmapControlFeature(&enable);
		*pBool = enable;
		return TRUE;
	}

	return FALSE;
}

BOOL
WifiSupportedRatesControl_SetParamBoolValue
(
	ANSC_HANDLE                 hInsContext,
	char*                       ParamName,
	BOOL                        bValue
)
{
	if (strcmp(ParamName, "WiFiBitmapControlFeature") == 0)
	{
		BOOL enable = FALSE;
		if(!wifi_getSupportRatesBitmapControlFeature(&enable))
		{
			if(enable == bValue)
				return TRUE;
		}

		if(!wifi_setSupportRatesBitmapControlFeature(bValue))
		{
			enable_reset_both_radio_flag();
			return TRUE;
		}
		else
		{
			return FALSE;
		}
	}

	return FALSE;
}
