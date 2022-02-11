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
#include "cosa_lgi_wifi_atm_dml.h"
#define  BAND_ATM_SETTING_BUFFER_LEN  2020
#define  GLOBALFAIRNESS               1
#define  WEIGHTEDFAIRNESS             2
#define  UPLINK                       1
#define  UPLINK_DOWNLINK              3

static COSA_DML_LG_WIFI_ATM_BAND_SETTING sWiFiDmlAtmBandCfg[2];

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

    X_LGI-COM_ATM.

    *  ATM_Common_GetParamStringValue
    *  ATM_Radio_GetEntryCount
    *  ATM_Radio_GetEntry
    *  ATM_Radio_GetParamBoolValue
    *  ATM_Radio_GetParamStringValue
    *  ATM_Radio_SetParamBoolValue
    *  ATM_Radio_SetParamStringValue
    *  ATM_Radio_Validate
    *  ATM_Radio_Commit
    *  ATM_Radio_Rollback

***********************************************************************/

ULONG
CosaDmlWiFiAtmBand_GetNumberOfBands(void)
{
	return WIFI_RADIO_COUNT;
}

ANSC_STATUS
CosaDmlWiFiAtmBand_GetAtmBand(int radioIndex, PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBandSetting)
{
	BOOL  enable = FALSE;
	char  staWeight[BAND_ATM_SETTING_BUFFER_LEN]; // PD32302
	char  ssidWeight[128]; 
	char  ssidDistType[128]; 
	int   ssidIndex = 0; 
	char  mwwWeight[128]; 
	char  tmp[10] = {0};
	BOOL  enableWmm = FALSE;

	if( NULL != pAtmBandSetting )
	{
		wifi_getAtmBandEnable( radioIndex, &enable );
		pAtmBandSetting->BandAtmEnable= enable;
		wifi_getAtmBandMode( radioIndex, tmp);
		pAtmBandSetting->BandAtmMode = _ansc_atoi(tmp);
		wifi_getAtmBandWaitThreshold( radioIndex, &pAtmBandSetting->BandAtmWaitThreshold);
		wifi_getAtmBandDirection( radioIndex, tmp);
		pAtmBandSetting->BandAtmDirection = _ansc_atoi(tmp);
		wifi_getAtmBandMWWEnable( radioIndex, &enableWmm );
		pAtmBandSetting->BandEnableWMMApplication = enableWmm;

		if(0 == wifi_getAtmBandMWWWeight(radioIndex,mwwWeight))
		{
			int mwwIndex = 0;
			char *token=NULL;

			token = strtok(mwwWeight, ",");
			while(token != NULL) {
				switch(mwwIndex)
				{
					case 0:
						pAtmBandSetting->RadioAtmWmmAppl.BeWeight = _ansc_atoi(token);
						break;
					case 1:
						pAtmBandSetting->RadioAtmWmmAppl.BkWeight = _ansc_atoi(token);
						break;
					case 2:
						pAtmBandSetting->RadioAtmWmmAppl.ViWeight = _ansc_atoi(token);
						break;
					case 3:
						pAtmBandSetting->RadioAtmWmmAppl.VoWeight = _ansc_atoi(token);
						break;
					default:
						break;
				}
				mwwIndex++;

				token = strtok(NULL, ",");
			}
		}

		if(0 == wifi_getAtmBandWeights( radioIndex, ssidWeight, sizeof(ssidWeight)))
		{
			int ssidIndex = 0;
			char *token=NULL;

			token = strtok(ssidWeight, ",");
			while(token != NULL) {
				pAtmBandSetting->RadioAtmSsid.Ssid[ssidIndex].SsidWeights = _ansc_atoi(token);

				ssidIndex++;
				if(ssidIndex >= ATM_MAX_SSID_NUMBER)
					break;

				token = strtok(NULL, ",");
			}
		}

		if(0 == wifi_getAtmBandDistributionType( radioIndex, ssidDistType))
		{
			int ssidIndex = 0;
			char *token=NULL;

			token = strtok(ssidDistType, ",");
			while(token != NULL) {
				if(0 == _ansc_atoi(token))
					strncpy(pAtmBandSetting->RadioAtmSsid.Ssid[ssidIndex].SsidDistributionType, "Dynamic",ATM_MAX_SSID_NUMBER );
				else
					strncpy(pAtmBandSetting->RadioAtmSsid.Ssid[ssidIndex].SsidDistributionType, "Static",ATM_MAX_SSID_NUMBER );

				ssidIndex++;
				if(ssidIndex >= ATM_MAX_SSID_NUMBER)
					break;

				token = strtok(NULL, ",");
			}
		}

		/*
		   Workaround for buggy HALs which return success but don't
		   initialise the staWeight buffer. The HAL should be fixed (it
		   should initialise the buffer in all cases) but for now init
		   the buffer here before passing it to the HAL.
		*/
		memset(staWeight, 0, sizeof(staWeight));

		if (0 == wifi_getAtmBandStaWeight(radioIndex, staWeight))
		{
			int staIndex = 0;
			char *token=NULL, *dev=NULL;
			int weight = 0;
			int period = 0;

			//"$MAC,$ATM_percent,$Period;$MAC,$ATM_percent,$Period;$MAC $ATM_percent;"
			token = strtok(staWeight, ";");
			while(token != NULL) {
				dev=strchr(token, ',');
				if(dev) {
					*dev=0;
					dev+=1;

					pAtmBandSetting->BandAtmSta.Sta[staIndex].Added = TRUE;
					strncpy(pAtmBandSetting->BandAtmSta.Sta[staIndex].MacAddress, token, ATM_MAC_ADDRESS_LENGTH-1);

					sscanf(dev,"%d,%d",&weight,&period); /*Currently Period is not used*/
					pAtmBandSetting->BandAtmSta.Sta[staIndex].Weight = weight;

					staIndex++;
				}
				token = strtok(NULL, ";");
			}
			pAtmBandSetting->BandAtmSta.StaCount = staIndex;
		}

		memcpy( &sWiFiDmlAtmBandCfg[ radioIndex ],
				pAtmBandSetting,
				sizeof( COSA_DML_LG_WIFI_ATM_BAND_SETTING ) );
	}

	return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS
CosaDmlWiFiAtmBand_SetAtmBand(int radioIndex, PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBandSetting)
{
	if( NULL != pAtmBandSetting )
	{
		BOOLEAN bChanged = FALSE;

		if( pAtmBandSetting->BandAtmEnable!= sWiFiDmlAtmBandCfg[ radioIndex ].BandAtmEnable )
		{
			wifi_setAtmBandEnable( radioIndex, pAtmBandSetting->BandAtmEnable );
			bChanged = TRUE;
		}

		if(pAtmBandSetting->BandAtmMode != sWiFiDmlAtmBandCfg[ radioIndex ].BandAtmMode)
		{
			wifi_setAtmBandMode( radioIndex, pAtmBandSetting->BandAtmMode);
			bChanged = TRUE;
		}

		if(pAtmBandSetting->BandAtmWaitThreshold != sWiFiDmlAtmBandCfg[ radioIndex ].BandAtmWaitThreshold)
		{
			wifi_setAtmBandWaitThreshold( radioIndex, pAtmBandSetting->BandAtmWaitThreshold);
			bChanged = TRUE;
		}

		if(pAtmBandSetting->BandAtmDirection != sWiFiDmlAtmBandCfg[ radioIndex ].BandAtmDirection)
		{
			wifi_setAtmBandDirection( radioIndex, pAtmBandSetting->BandAtmDirection);
			bChanged = TRUE;
		}

		if( pAtmBandSetting->BandEnableWMMApplication!= sWiFiDmlAtmBandCfg[ radioIndex ].BandEnableWMMApplication )
		{
			wifi_setAtmBandMWWEnable( radioIndex, pAtmBandSetting->BandEnableWMMApplication );
			bChanged = TRUE;
		}

		if( bChanged )
		{
			memcpy( &sWiFiDmlAtmBandCfg[ radioIndex ],
					pAtmBandSetting,
					sizeof( COSA_DML_LG_WIFI_ATM_BAND_SETTING ) );
			/*enable reset radio flag*/
			enable_reset_radio_flag(radioIndex);

		}
	}
	return ANSC_STATUS_SUCCESS;
}

ULONG
    ATM_Common_GetParamStringValue
    (
        ANSC_HANDLE                                hInsContext,
        char*                                      ParamName,
        char*                                      pValue,
        ULONG*                                     pUlSize
    )
{
	PCOSA_DATAMODEL_WIFI            pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM        pAATM     = pMyObject->pAATM;
	size_t                          len       = 0;

	CcspTraceInfo(("ATM_GetParamStringValue parameter '%s'\n", ParamName));

	if (strcmp(ParamName, "Capability") == 0)
	{
		//Just return "GlobalFairness,WeightedFairness" temporarily.
		len = AnscSizeOfString(pAATM->AtmInfo.AtmCapability) + 1;
		if (*pUlSize < len)
		{
			*pUlSize = len;
			return 1;
		}
		else
		{
			AnscCopyString(pValue, pAATM->AtmInfo.AtmCapability);
			return 0;
		}
	}

	return -1;
}

ULONG
ATM_Radio_GetEntryCount
    (
        ANSC_HANDLE                            hInsContext
    )
{
	PCOSA_DATAMODEL_WIFI            pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM        pAATM     = pMyObject->pAATM;

	if(NULL != pAATM)
		return pAATM->RadioCount;
	else
		return 0;

}

ANSC_HANDLE
ATM_Radio_GetEntry
    (
        ANSC_HANDLE                             hInsContext,
        ULONG                                   nIndex,
        ULONG*                                  pInsNumber
    )
{
	PCOSA_DATAMODEL_WIFI            pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM        pAATM     = pMyObject->pAATM;


	if( ( NULL != pAATM ) && ( nIndex < pAATM->RadioCount ) )
	{
		*pInsNumber = pAATM->pAtmBandSetting[ nIndex ].InstanceNumber;

		return ( &pAATM->pAtmBandSetting[ nIndex ] ); /* return the handle */
	}

	return NULL; /* return the NULL for invalid index */
}

BOOL
ATM_Radio_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBandSetting = ( PCOSA_DML_LG_WIFI_ATM_BAND_SETTING )hInsContext;

	if(pAtmBandSetting == NULL)
		return FALSE;

	if (strcmp(ParamName, "Enable") == 0)
	{
		*pBool = pAtmBandSetting->BandAtmEnable;
		return TRUE;
	}

	if (strcmp(ParamName, "EnableWMMApplication") == 0)
	{
		*pBool = pAtmBandSetting->BandEnableWMMApplication;
		return TRUE;
	}

	return FALSE;
}

BOOL
ATM_Radio_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
	/* check the parameter name and return the corresponding value */
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBandSetting = ( PCOSA_DML_LG_WIFI_ATM_BAND_SETTING )hInsContext;

	/* check the parameter name and return the corresponding value */
	if (strcmp(ParamName, "Mode") == 0)
	{
		*puLong = pAtmBandSetting->BandAtmMode;
		return TRUE;
	}

	if (strcmp(ParamName, "WaitThreshold") == 0)
	{
		*puLong = pAtmBandSetting->BandAtmWaitThreshold;
		return TRUE;
	}

	if (strcmp(ParamName, "TrafficDirection") == 0)
	{
		*puLong = pAtmBandSetting->BandAtmDirection;
		return TRUE;
	}

	/* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
	return FALSE;
}

BOOL
ATM_Radio_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
	PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM              pWiFiAtm     = pMyObject->pAATM;
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBandCfg  = ( PCOSA_DML_LG_WIFI_ATM_BAND_SETTING )hInsContext;

	if (strcmp(ParamName, "Enable") == 0)
	{
		pAtmBandCfg->BandAtmEnable = bValue;
		pAtmBandCfg->bATMChanged = 1;
		return TRUE;
	}

	if (strcmp(ParamName, "EnableWMMApplication") == 0)
	{
		pAtmBandCfg->BandEnableWMMApplication = bValue;
		pAtmBandCfg->bATMChanged = 1;
		return TRUE;
	}

	return FALSE;
}

BOOL
ATM_Radio_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       ulValuep
    )
{
	PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM              pWiFiAtm     = pMyObject->pAATM;
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBandCfg  = ( PCOSA_DML_LG_WIFI_ATM_BAND_SETTING )hInsContext;

	if (strcmp(ParamName, "Mode") == 0)
	{
		if (ulValuep == GLOBALFAIRNESS || ulValuep == WEIGHTEDFAIRNESS)
		{
			pAtmBandCfg->BandAtmMode = ulValuep;
			pAtmBandCfg->bATMChanged = 1;
			return TRUE;
		}
	}

	if (strcmp(ParamName, "WaitThreshold") == 0)
        {
		pAtmBandCfg->BandAtmWaitThreshold = ulValuep;
		pAtmBandCfg->bATMChanged = 1;
		return TRUE;
	}


	if (strcmp(ParamName, "TrafficDirection") == 0)
	{
		if (ulValuep >= UPLINK && ulValuep<= UPLINK_DOWNLINK)
		{ 
			pAtmBandCfg->BandAtmDirection = ulValuep;
			pAtmBandCfg->bATMChanged = 1;
			return TRUE;
		}

	}

	return FALSE;
}

BOOL
ATM_Radio_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
    PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBandCfg  = ( PCOSA_DML_LG_WIFI_ATM_BAND_SETTING )hInsContext;
    bool  bPlumeNativeAtmBsControl = FALSE;
    if( pAtmBandCfg->BandAtmEnable )
    {
        /* To Make Native ATM feature unconfigurable when Plume native ATM/BS control is enabled. */
        Cdm_GetParamBool( "Device.X_LGI-COM_SON.NativeAtmBsControl", &bPlumeNativeAtmBsControl );
        if( bPlumeNativeAtmBsControl )
        {
            _ansc_strcpy(pReturnParamName, "Enable");
            return 0;
        }
    }
	return TRUE;
}

ULONG
ATM_Radio_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
	PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM              pWiFiAtm     = pMyObject->pAATM;
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBandCfg  = (PCOSA_DML_LG_WIFI_ATM_BAND_SETTING)hInsContext;

	/* Set the ATM Current Options */
	if ( TRUE == pAtmBandCfg->bATMChanged)
	{
		CosaDmlWiFiAtmBand_SetAtmBand( pAtmBandCfg->InstanceNumber - 1, pAtmBandCfg );
		pAtmBandCfg->bATMChanged= 0;
	}

	return ANSC_STATUS_SUCCESS;
}

ULONG
ATM_Radio_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
	PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM        pWiFiAtm     = pMyObject->pAATM;
	int                             iLoopCount;

	/* Load Previous Values for ATM Settings */
	for ( iLoopCount = 0; iLoopCount < pWiFiAtm->RadioCount ; ++iLoopCount )
	{
		memset( &pWiFiAtm->pAtmBandSetting[ iLoopCount ], 0, sizeof( COSA_DML_LG_WIFI_ATM_BAND_SETTING ) );

		/* Instance Number Always from 1 */

		pWiFiAtm->pAtmBandSetting[ iLoopCount ].InstanceNumber = iLoopCount + 1;

		CosaDmlWiFiAtmBand_GetAtmBand( iLoopCount, &pWiFiAtm->pAtmBandSetting[ iLoopCount ] );
	}

	return 0;
}

/***********************************************************************

 APIs for Object:

    WiFi.X_LGI-COM_ATM.Radio.{i}.WMMApplication.

    *  ATM_Radio_WMMApplication_GetParamUlongValue
    *  ATM_Radio_WMMApplication_SetParamUlongValue
    *  ATM_Radio_WMMApplication_Validate
    *  ATM_Radio_WMMApplication_Commit
    *  ATM_Radio_WMMApplication_Rollback

***********************************************************************/
BOOL
ATM_Radio_WMMApplication_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
	/* check the parameter name and return the corresponding value */
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBandSetting = ( PCOSA_DML_LG_WIFI_ATM_BAND_SETTING )hInsContext;

	/* check the parameter name and return the corresponding value */
	if (strcmp(ParamName, "BEWeight") == 0)
	{
		*puLong = pAtmBandSetting->RadioAtmWmmAppl.BeWeight;
		return TRUE;
	}

	if (strcmp(ParamName, "BKWeight") == 0)
	{
		*puLong = pAtmBandSetting->RadioAtmWmmAppl.BkWeight;
		return TRUE;
	}

	if (strcmp(ParamName, "VIWeight") == 0)
	{
		*puLong = pAtmBandSetting->RadioAtmWmmAppl.ViWeight;
		return TRUE;
	}

	if (strcmp(ParamName, "VOWeight") == 0)
	{
		*puLong = pAtmBandSetting->RadioAtmWmmAppl.VoWeight;
		return 1;
	}

	/* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
	return FALSE;
}

BOOL
ATM_Radio_WMMApplication_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       ulValuep
    )
{
	PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM              pWiFiAtm     = pMyObject->pAATM;
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBandCfg  = ( PCOSA_DML_LG_WIFI_ATM_BAND_SETTING )hInsContext;

	char mwwWeights[128];
	ULONG totalWeight = 0;  

	if (strcmp(ParamName, "BKWeight") == 0)
	{
		if(pAtmBandCfg->RadioAtmWmmAppl.BkWeight == ulValuep)
			return TRUE;

		totalWeight = ulValuep + pAtmBandCfg->RadioAtmWmmAppl.BeWeight + pAtmBandCfg->RadioAtmWmmAppl.ViWeight + pAtmBandCfg->RadioAtmWmmAppl.VoWeight;
		if(totalWeight > ATM_WMM_MAX_WEIGHT)
			return FALSE;

		pAtmBandCfg->RadioAtmWmmAppl.BkWeight = ulValuep;
		pAtmBandCfg->bATMChanged = 1;
	}

	if (strcmp(ParamName, "BEWeight") == 0)
	{
		if(pAtmBandCfg->RadioAtmWmmAppl.BeWeight == ulValuep)
			return TRUE;

		totalWeight = pAtmBandCfg->RadioAtmWmmAppl.BkWeight + ulValuep + pAtmBandCfg->RadioAtmWmmAppl.ViWeight + pAtmBandCfg->RadioAtmWmmAppl.VoWeight;
		if(totalWeight > ATM_WMM_MAX_WEIGHT)
			return FALSE;

		pAtmBandCfg->RadioAtmWmmAppl.BeWeight = ulValuep;
		pAtmBandCfg->bATMChanged = 1;
	}

	if (strcmp(ParamName, "VIWeight") == 0)
	{
		if(pAtmBandCfg->RadioAtmWmmAppl.ViWeight == ulValuep)
			return TRUE;

		totalWeight = pAtmBandCfg->RadioAtmWmmAppl.BkWeight + pAtmBandCfg->RadioAtmWmmAppl.BeWeight + ulValuep + pAtmBandCfg->RadioAtmWmmAppl.VoWeight;
		if(totalWeight > ATM_WMM_MAX_WEIGHT)
			return FALSE;

		pAtmBandCfg->RadioAtmWmmAppl.ViWeight = ulValuep;
		pAtmBandCfg->bATMChanged = 1;
	}

	if (strcmp(ParamName, "VOWeight") == 0)
	{
		if(pAtmBandCfg->RadioAtmWmmAppl.VoWeight == ulValuep)
			return TRUE;

		totalWeight = pAtmBandCfg->RadioAtmWmmAppl.BkWeight + pAtmBandCfg->RadioAtmWmmAppl.BeWeight + pAtmBandCfg->RadioAtmWmmAppl.ViWeight + ulValuep;
		if(totalWeight > ATM_WMM_MAX_WEIGHT)
			return FALSE;

		pAtmBandCfg->RadioAtmWmmAppl.VoWeight = ulValuep;
		pAtmBandCfg->bATMChanged = 1;
	}


	if(pAtmBandCfg->bATMChanged)
	{
		/*Order is as per CL: BE,BK,VI,VO*/
		sprintf(mwwWeights,"%lu,%lu,%lu,%lu",pAtmBandCfg->RadioAtmWmmAppl.BeWeight,pAtmBandCfg->RadioAtmWmmAppl.BkWeight,pAtmBandCfg->RadioAtmWmmAppl.ViWeight,pAtmBandCfg->RadioAtmWmmAppl.VoWeight);
		if(0 != wifi_setAtmBandMWWWeight(pAtmBandCfg->InstanceNumber -1, mwwWeights))
		pAtmBandCfg->bATMChanged = 0;
		/*enable reset radio flag*/
		enable_reset_radio_flag(pAtmBandCfg->InstanceNumber -1);
		return TRUE;
	}

	return FALSE;
}
/*Radio_WMMApplication END*/

BOOL
GetSsidRadioIndex
    (
        PCOSA_DML_LG_WIFI_ATM_SSID pSsid, int *iRadoIndex
    )
{
	PCOSA_DATAMODEL_WIFI                  pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM              pWiFiAtm  = pMyObject->pAATM;
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pBand;
	int   radioIndex = 0;
	int   ssidIndex = 0;

	for (radioIndex = 0; radioIndex < WIFI_RADIO_COUNT; radioIndex ++)
	{
		pBand = pWiFiAtm->pAtmBandSetting + radioIndex;
		for (ssidIndex = 0; ssidIndex < 8; ssidIndex++)
		{
			if ((&pBand->RadioAtmSsid.Ssid[ssidIndex]) == pSsid)
			{
				*iRadoIndex = radioIndex;
				return TRUE;
			}
		}
	}

	return FALSE;
}
/***********************************************************************

 APIs for Object:

    WiFi.X_LGI-COM_ATM.Radio.{i}.SSID.

    *  ATM_Radio_SSID_GetEntryCount
    *  ATM_Radio_SSID_GetEntry
    *  ATM_Radio_SSID_GetParamUlongValue
    *  ATM_Radio_SSID_GetParamStringValue
    *  ATM_Radio_SSID_SetParamUlongValue
    *  ATM_Radio_SSID_SetParamStringValue

***********************************************************************/
ULONG
ATM_Radio_SSID_GetEntryCount
    (
        ANSC_HANDLE                            hInsContext
    )
{
	PCOSA_DATAMODEL_WIFI            pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM        pAATM     = pMyObject->pAATM;

	return ATM_MAX_SSID_NUMBER;
}

ANSC_HANDLE
ATM_Radio_SSID_GetEntry
    (
        ANSC_HANDLE                             hInsContext,
        ULONG                                   nIndex,
        ULONG*                                  pInsNumber
    )
{
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBandSetting = ( PCOSA_DML_LG_WIFI_ATM_BAND_SETTING )hInsContext;

	if( ( NULL != pAtmBandSetting ) && ( nIndex < ATM_MAX_SSID_NUMBER ) )
	{
		*pInsNumber = nIndex + 1;
		return ( &pAtmBandSetting->RadioAtmSsid.Ssid[nIndex] ); /* return the handle */
	}

	return NULL; /* return the NULL for invalid index */
}

ULONG
ATM_Radio_SSID_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pulSize
    )
{
	PCOSA_DML_LG_WIFI_ATM_SSID pSsid = (PCOSA_DML_LG_WIFI_ATM_SSID)hInsContext;

	/* check the parameter name and return the corresponding value */
	if (strcmp(ParamName, "DistributionType") == 0)
	{

		/* collect value */
		if ( AnscSizeOfString(pSsid->SsidDistributionType) < *pulSize)
		{
			AnscCopyString(pValue, pSsid->SsidDistributionType);
			return 0;
		}
		else
		{
			*pulSize = AnscSizeOfString(pSsid->SsidDistributionType)+1;
			return 1;
		}
	}

	/* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
	return -1;
}

BOOL
ATM_Radio_SSID_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
	PCOSA_DML_LG_WIFI_ATM_SSID pSsid = (PCOSA_DML_LG_WIFI_ATM_SSID)hInsContext;

	/* check the parameter name and return the corresponding value */
	if (strcmp(ParamName, "Weight") == 0)
	{
		*puLong = pSsid->SsidWeights;       
		return TRUE;
	}

	/* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
	return FALSE;
}

BOOL
ATM_Radio_SSID_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
	PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM              pWiFiAtm     = pMyObject->pAATM;
	PCOSA_DML_LG_WIFI_ATM_SSID               pSsid = (PCOSA_DML_LG_WIFI_ATM_SSID)hInsContext;
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pBand;
	char  ssidSetting[128] = {0};
	int   radioIndex = 0;
	BOOL  bResult;
	int distArray[8] = {0};
	int index = 0;

	if (strcmp(ParamName, "DistributionType") == 0)
	{
		if(strcmp(pSsid->SsidDistributionType, pString) == 0)	
			return TRUE;

		if(strcmp(pString,"Dynamic") == 0 || strcmp(pString,"Static") == 0)
			strcpy(pSsid->SsidDistributionType,pString);
		else
			return FALSE;

		bResult = GetSsidRadioIndex(pSsid, &radioIndex);

		if (bResult == TRUE)
		{
			pBand = pWiFiAtm->pAtmBandSetting + radioIndex;
			for(index = 0; index < ATM_MAX_SSID_NUMBER; index++)
			{
				if(strcmp(pBand->RadioAtmSsid.Ssid[index].SsidDistributionType,"Static") == 0 )
					distArray[index] = 1;	

			}

			sprintf(ssidSetting,"%d,%d,%d,%d,%d,%d,%d,%d",distArray[0] ,distArray[1] ,distArray[2] ,distArray[3] ,distArray[4] ,distArray[5] ,distArray[6] ,distArray[7] );

			wifi_setAtmBandDistributionType(pBand->InstanceNumber - 1, ssidSetting);

			/*enable reset radio flag*/
			enable_reset_radio_flag(pBand->InstanceNumber - 1);
			return TRUE;
		}

	}
	return FALSE;
}

BOOL
ATM_Radio_SSID_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       ulValuep
    )
{
	PCOSA_DATAMODEL_WIFI            pMyObject    = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM              pWiFiAtm     = pMyObject->pAATM;
	PCOSA_DML_LG_WIFI_ATM_SSID               pSsid = (PCOSA_DML_LG_WIFI_ATM_SSID)hInsContext;
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pBand;
	int currentWeight = 0;
	char  ssidSetting[128] = {0};
	int   radioIndex = 0;
	BOOL  bResult;
	int index  = 0;
	ULONG ssidCumWeight = 0;

	if (strcmp(ParamName, "Weight") == 0)
	{
		if(ulValuep < ATM_SSID_MIN_WEIGHT && ulValuep > ATM_SSID_MAX_WEIGHT)
			return FALSE;

		if(pSsid->SsidWeights == ulValuep)
			return TRUE;

		currentWeight = pSsid->SsidWeights;	
		pSsid->SsidWeights = ulValuep; 

		bResult = GetSsidRadioIndex(pSsid, &radioIndex);

		if (bResult == TRUE)
		{
			pBand = pWiFiAtm->pAtmBandSetting + radioIndex;

			for(index = 0; index < ATM_MAX_SSID_NUMBER; index ++)
				ssidCumWeight = ssidCumWeight + pBand->RadioAtmSsid.Ssid[index].SsidWeights; 

			if(ssidCumWeight > ATM_SSID_MAX_WEIGHT) 
			{
				pSsid->SsidWeights = currentWeight;
				return FALSE;
			}

			sprintf(ssidSetting,"%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
					pBand->RadioAtmSsid.Ssid[0].SsidWeights,
					pBand->RadioAtmSsid.Ssid[1].SsidWeights,
					pBand->RadioAtmSsid.Ssid[2].SsidWeights,
					pBand->RadioAtmSsid.Ssid[3].SsidWeights,
					pBand->RadioAtmSsid.Ssid[4].SsidWeights,
					pBand->RadioAtmSsid.Ssid[5].SsidWeights,
					pBand->RadioAtmSsid.Ssid[6].SsidWeights,
					pBand->RadioAtmSsid.Ssid[7].SsidWeights);

			wifi_setAtmBandWeights( pBand->InstanceNumber - 1, ssidSetting);
			/*enable reset radio flag*/
			enable_reset_radio_flag(pBand->InstanceNumber - 1);

			return TRUE;
		}
	}
	return TRUE;
}
/***********************************************************************

 APIs for Object:

    WiFi.SSID.{i}.

    *  ATM_Radio_Sta_GetEntryCount
    *  ATM_Radio_Sta_GetEntry
    *  ATM_Radio_Sta_AddEntry
    *  ATM_Radio_Sta_DelEntry
    *  ATM_Radio_Sta_GetParamUlongValue
    *  ATM_Radio_Sta_GetParamStringValue
    *  ATM_Radio_Sta_SetParamUlongValue
    *  ATM_Radio_Sta_SetParamStringValue
    *  ATM_Radio_Sta_Validate
    *  ATM_Radio_Sta_Commit
    *  ATM_Radio_Sta_Rollback

***********************************************************************/
ULONG
ATM_Radio_Sta_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBandSetting = (PCOSA_DML_LG_WIFI_ATM_BAND_SETTING)hInsContext;
	return pAtmBandSetting->BandAtmSta.StaCount;
}

ANSC_HANDLE
ATM_Radio_Sta_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBandSetting = ( PCOSA_DML_LG_WIFI_ATM_BAND_SETTING )hInsContext;
	int iStaIndex = 0;

	for (iStaIndex = nIndex; iStaIndex < ATM_MAX_NUMBER_STA_WEIGHT; iStaIndex++)
	{
		if (pAtmBandSetting->BandAtmSta.Sta[iStaIndex].Added == TRUE)
		{
			*pInsNumber = iStaIndex + 1;
			return ( &pAtmBandSetting->BandAtmSta.Sta[iStaIndex] ); /* return the handle */
		}
	}
	return NULL; /* return the NULL for invalid index */
}

ANSC_HANDLE
ATM_Radio_Sta_AddEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG*                      pInsNumber
    )
{
	int iStaIndex = 0;
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBandSetting = ( PCOSA_DML_LG_WIFI_ATM_BAND_SETTING )hInsContext;

	for (iStaIndex = 0; iStaIndex < ATM_MAX_NUMBER_STA_WEIGHT; iStaIndex++)
	{
		if (pAtmBandSetting->BandAtmSta.Sta[iStaIndex].Added == FALSE)
		{
			pAtmBandSetting->BandAtmSta.Sta[iStaIndex].Added = TRUE;
			pAtmBandSetting->BandAtmSta.StaCount = pAtmBandSetting->BandAtmSta.StaCount + 1;
			*pInsNumber = iStaIndex + 1;
			return ( &pAtmBandSetting->BandAtmSta.Sta[iStaIndex] ); /* return the handle */
		}
	}

	return NULL; /* return the NULL for invalid index */
}

ULONG
ATM_Radio_Sta_DelEntry
    (
        ANSC_HANDLE                 hInsContext,
        ANSC_HANDLE                 hInstance
    )
{
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pAtmBandSetting = (PCOSA_DML_LG_WIFI_ATM_BAND_SETTING)hInsContext;
	PCOSA_DML_LG_WIFI_ATM_STA pAtmSta = (PCOSA_DML_LG_WIFI_ATM_STA)hInstance;

	if (pAtmSta != NULL)
	{
		pAtmSta->Added = FALSE;
		pAtmSta->MacAddress[0] = '\0';
		pAtmSta->Weight = 0;
		pAtmBandSetting->BandAtmSta.StaCount = pAtmBandSetting->BandAtmSta.StaCount - 1;
		ATM_Radio_Sta_Commit(hInstance);
		return 0;
	}

	return 1; /* return the NULL for invalid index */
}

BOOL
ATM_Radio_Sta_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
	PCOSA_DML_LG_WIFI_ATM_STA pSta = (PCOSA_DML_LG_WIFI_ATM_STA)hInsContext;

	/* check the parameter name and return the corresponding value */
	if (strcmp(ParamName, "Weight") == 0)
	{
		/* collect value */
		*puLong  = pSta->Weight;
		return TRUE;
	}

	/* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
	return FALSE;
}

ULONG
ATM_Radio_Sta_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
	PCOSA_DML_LG_WIFI_ATM_STA pSta = (PCOSA_DML_LG_WIFI_ATM_STA)hInsContext;

	/* check the parameter name and return the corresponding value */
	if (strcmp(ParamName, "MACAddress") == 0)
	{
		/* collect value */
		if ( AnscSizeOfString(pSta->MacAddress) < *pUlSize)
		{
			AnscCopyString(pValue, pSta->MacAddress);
			return 0;
		}
		else
		{
			*pUlSize = AnscSizeOfString(pSta->MacAddress)+1;
			return 1;
		}
	}

	/* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
	return -1;
}

BOOL
GetStaRadioIndex
    (
        PCOSA_DML_LG_WIFI_ATM_STA pSta, int *iRadoIndex
    )
{
	PCOSA_DATAMODEL_WIFI                  pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM              pWiFiAtm  = pMyObject->pAATM;
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pBand;
	int   radioIndex = 0;
	int   staIndex = 0;

	for (radioIndex = 0; radioIndex < WIFI_RADIO_COUNT; radioIndex ++)
	{
		pBand = pWiFiAtm->pAtmBandSetting + radioIndex;
		for (staIndex = 0; staIndex < ATM_MAX_NUMBER_STA_WEIGHT; staIndex++)
		{
			if ((&pBand->BandAtmSta.Sta[staIndex]) == pSta)
			{
				*iRadoIndex = radioIndex;
				return TRUE;
			}
		}
	}

	return FALSE;
}

void GetBandStaSettingStr(PCOSA_DML_LG_WIFI_ATM_BAND_STA pSta, char* str)
{
	int iStaIndex = 0;
	int len=0;
	int iStaCount = pSta->StaCount;

	for (iStaIndex = 0; iStaIndex < ATM_MAX_NUMBER_STA_WEIGHT && iStaCount > 0; iStaIndex++)
	{
		if (pSta->Sta[iStaIndex].MacAddress[0] != '\0' && pSta->Sta[iStaIndex].Weight > 0)
		{
			len = strlen(str);
			snprintf(str + len, BAND_ATM_SETTING_BUFFER_LEN - len, "%s,%lu,%d;",
					pSta->Sta[iStaIndex].MacAddress, pSta->Sta[iStaIndex].Weight,ATM_STATION_PERIOD);
			iStaCount = iStaCount - 1;
		}
	}
}

BOOL
ATM_Radio_Sta_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
	PCOSA_DATAMODEL_WIFI                  pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM              pWiFiAtm  = pMyObject->pAATM;
	PCOSA_DML_LG_WIFI_ATM_STA          pSta = (PCOSA_DML_LG_WIFI_ATM_STA)hInsContext;
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pBand;
	int   radioIndex = 0;
	int   iWeightTotal = 0;
	int   iStaIndex = 0;
	BOOL  bResult;

	/* check the parameter name and return the corresponding value */
	if (strcmp(ParamName, "Weight") == 0)
	{
		if (uValue >= ATM_STA_WEIGHT_MIN && uValue <= ATM_STA_WEIGHT_MAX)
		{
			bResult = GetStaRadioIndex(pSta, &radioIndex);
			if (bResult == TRUE)
			{
				pBand = pWiFiAtm->pAtmBandSetting + radioIndex;
				for (iStaIndex = 0; iStaIndex < ATM_MAX_NUMBER_STA_WEIGHT; iStaIndex++)
				{
					iWeightTotal = iWeightTotal + pBand->BandAtmSta.Sta[iStaIndex].Weight;
				}

				iWeightTotal = iWeightTotal - pSta->Weight + uValue;

				if (iWeightTotal <= ATM_STA_TOTAL_WEIGHT_MAX )
				{
					pSta->Weight = uValue;
					return TRUE;
				}
			}
		}
	}

	/* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
	return FALSE;
}

BOOL
ATM_Radio_Sta_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
	PCOSA_DATAMODEL_WIFI                  pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM              pWiFiAtm  = pMyObject->pAATM;
	PCOSA_DML_LG_WIFI_ATM_STA          pSta = (PCOSA_DML_LG_WIFI_ATM_STA)hInsContext;
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pBand;
	int   radioIndex = 0;
	int   iStaIndex = 0;
	BOOL  bResult;

	/* check the parameter name and return the corresponding value */
	if (strcmp(ParamName, "MACAddress") == 0)
	{
		if (pString != NULL && ((strlen((char *)pString) == 17) || strlen((char *)pString) == 0) )
		{
			bResult = GetStaRadioIndex(pSta, &radioIndex);
			if (bResult == TRUE)
			{
				pBand = pWiFiAtm->pAtmBandSetting + radioIndex;
				if (strlen((char *)pString) == 17)
				{  
					// check for MAC Address duplication
					for (iStaIndex = 0; iStaIndex < ATM_MAX_NUMBER_STA_WEIGHT; iStaIndex++)
					{
						if (strcasecmp(pBand->BandAtmSta.Sta[iStaIndex].MacAddress, pString) == 0)
						{
							return FALSE;
						}
					}
				}
				strncpy(pSta->MacAddress, pString, sizeof(pSta->MacAddress) - 1 );
				return TRUE;
			}
		}
	}

	/* CcspTraceWarning(("Unsupported parameter '%s'\n", ParamName)); */
	return FALSE;
}

BOOL
ATM_Radio_Sta_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
    )
{
	return TRUE;
}

ULONG
ATM_Radio_Sta_Commit
    (
        ANSC_HANDLE                 hInsContext
    )
{
	PCOSA_DATAMODEL_WIFI                  pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM              pWiFiAtm  = pMyObject->pAATM;
	PCOSA_DML_LG_WIFI_ATM_STA          pSta = (PCOSA_DML_LG_WIFI_ATM_STA)hInsContext;
	PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pBand;
	char  staSetting[BAND_ATM_SETTING_BUFFER_LEN] = {0};
	int   radioIndex = 0;
	BOOL  bResult;

	bResult = GetStaRadioIndex(pSta, &radioIndex);

	if (bResult == TRUE)
	{
		pBand = pWiFiAtm->pAtmBandSetting + radioIndex;
		GetBandStaSettingStr(&(pBand->BandAtmSta), staSetting);
		wifi_setAtmBandStaWeight(pBand->InstanceNumber - 1, staSetting);
		/*enable reset radio flag*/
		enable_reset_radio_flag(pBand->InstanceNumber - 1);
		return 0;
	}

	return 1;
}

ULONG
ATM_Radio_Sta_Rollback
    (
        ANSC_HANDLE                 hInsContext
    )
{
	return 0;
}

/**********************************************************************
    description:
        This function is called to retrieve a station weight on a specified band

    argument:   PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pBand
                The band handle;

                char                                 *mac
                The station mac;

    return:     the station weight retrived

**********************************************************************/
ULONG
ATM_Band_Get_StaWeight
    (
        PCOSA_DML_LG_WIFI_ATM_BAND_SETTING pBand,
        char                                 *mac
    )
{
	int iStaIndex;
	ULONG staWeight = 0;

	for (iStaIndex = 0; iStaIndex < pBand->BandAtmSta.StaCount; iStaIndex++)
	{
		if (!strcasecmp(pBand->BandAtmSta.Sta[iStaIndex].MacAddress, mac) && pBand->BandAtmSta.Sta[iStaIndex].Added)
		{
			staWeight = pBand->BandAtmSta.Sta[iStaIndex].Weight;
			break;
		}
	}
	return staWeight;
}

ANSC_STATUS
ATM_Band_Get_Statistics()
{
	PCOSA_DATAMODEL_WIFI      pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM     pAATM     = pMyObject->pAATM;
	PCOSA_DML_LG_WIFI_ATM_STAT pStat = NULL;
	int bandWeight[MAX_NUM_OF_BSS_ON_BAND] = {0};
	char bandWeights[24];
	ULONG count24g = 0;
	ULONG count5g = 0;
	int staTotal = 0;
	int ssidIndex = 0;
	int i = 0;
	int index5g = 0;
	int SSID_INDEX_Prefix_5g = 32;/*As per wifi hal*/

	if(NULL == pAATM)
		return 0;

	wlan_ATM_report_t *pStationAirtime = NULL;

	wifi_getAtmStationAirtime(&pStationAirtime, &staTotal);

	if(pStationAirtime == NULL)
		return 0;

	if(staTotal == 0)
		return ANSC_STATUS_SUCCESS;

	pStat= (PCOSA_DML_LG_WIFI_ATM_STAT)AnscAllocateMemory(staTotal * sizeof(COSA_DML_LG_WIFI_ATM_STAT));
	if( NULL == pStat )
		return ANSC_STATUS_RESOURCES;

	for(i=0; i < staTotal; i++)
	{

		strcpy(pStat[i].StationMAC, pStationAirtime[i].nMac);

		ssidIndex = pStationAirtime[i].nConnectBSS;

		if(ssidIndex<8) /*24g index 0~7*/
		{
			pStat[i].StationWeight = ATM_Band_Get_StaWeight(&pAATM->pAtmBandSetting[0], pStat[i].StationMAC);
			wifi_getAtmBandWeights(0, bandWeights, sizeof(bandWeights));
			pStat[i].SSID = (ssidIndex*2)+1; /*0~7 index mapped to 1,3,5,7,....15 for Radio 0 SSIDs*/

			sscanf(bandWeights,"%d,%d,%d,%d,%d,%d,%d,%d", &bandWeight[0], &bandWeight[1], &bandWeight[2], &bandWeight[3],
					&bandWeight[4], &bandWeight[5], &bandWeight[6], &bandWeight[7]);
			pStat[i].SSIDWeight = bandWeight[ssidIndex];
		}
		else /*5G index 32+0~7*/
		{
			pStat[i].StationWeight = ATM_Band_Get_StaWeight(&pAATM->pAtmBandSetting[1], pStat[i].StationMAC);

			wifi_getAtmBandWeights(1, bandWeights, sizeof(bandWeights));
			pStat[i].SSID = (ssidIndex-SSID_INDEX_Prefix_5g)*2;
			/*why 32? This is as per wifi hal Setting to differentiate between 2.4 and 5 Ghz index,
			  in hal parserWeightData() +32 added for 5G radio */
			sscanf(bandWeights,"%d,%d,%d,%d,%d,%d,%d,%d", &bandWeight[0], &bandWeight[1], &bandWeight[2], &bandWeight[3],
					&bandWeight[4], &bandWeight[5], &bandWeight[6], &bandWeight[7]);
			index5g = ssidIndex-SSID_INDEX_Prefix_5g;

			if(index5g < 0 || index5g > 7)
			{
				printf("5G index out of Range\n");
				pStat[i].SSIDWeight = 0;
			}
			else
			{
				pStat[i].SSIDWeight = bandWeight[(ssidIndex-SSID_INDEX_Prefix_5g)];
			}
		}

		pStat[i].MeasuredAirTime = atol(pStationAirtime[i].nSTAConfigAirTimeDL);
		pStat[i].TotalAirTime = atol(pStationAirtime[i].nSTATotalAirTimeDL);

	}
	pAATM->BandAtmStatCount = staTotal;
	pAATM->pBandAtmStat = pStat;
	pMyObject->pAATM = pAATM;

	if(pStationAirtime != NULL)
	{
		AnscFreeMemory(pStationAirtime);
	}

	return ANSC_STATUS_SUCCESS;
}

/***********************************************************************

 APIs for Object:

    WiFi.X_COM_ATM.Stats.Client.{i}.
    *  ATM_Stats_Client_GetEntryCount
    *  ATM_Stats_Client_GetEntry
    *  ATM_Stats_Client_IsUpdated
    *  ATM_Stats_Client_Synchronize
    *  ATM_Stats_Client_GetParamUlongValue
    *  ATM_Stats_Client_GetParamStringValue

***********************************************************************/
/**********************************************************************

    caller:     owner of this object

    prototype:

        ULONG
        ATM_Stats_Client_GetEntryCount
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to retrieve the count of the table.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The count of the table

**********************************************************************/
ULONG
ATM_Stats_Client_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
        PCOSA_DATAMODEL_WIFI            pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
        PCOSA_DML_LG_WIFI_ATM        pAATM     = pMyObject->pAATM;

        if(NULL != pAATM)
                return pAATM->BandAtmStatCount;
        else
                return 0;
}
/**********************************************************************

    caller:     owner of this object

    prototype:

        ANSC_HANDLE
        ATM_Stats_Client_GetEntry
            (
                ANSC_HANDLE                 hInsContext,
                ULONG                       nIndex,
                ULONG*                      pInsNumber
            );

    description:

        This function is called to retrieve the entry specified by the index.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                ULONG                       nIndex,
                The index of this entry;

                ULONG*                      pInsNumber
                The output instance number;

    return:     The handle to identify the entry

**********************************************************************/
ANSC_HANDLE
ATM_Stats_Client_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
        PCOSA_DATAMODEL_WIFI            pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
        PCOSA_DML_LG_WIFI_ATM        pAATM     = pMyObject->pAATM;


        if( ( NULL != pAATM ) && ( nIndex < pAATM->BandAtmStatCount ) )
        {
                *pInsNumber = nIndex + 1;

                return ( &pAATM->pBandAtmStat[ nIndex ] ); /* return the handle */
        }

        return NULL; /* return the NULL for invalid index */
}
/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        ATM_Stats_Client_IsUpdated
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is checking whether the table is updated or not.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     TRUE or FALSE.

**********************************************************************/
BOOL
ATM_Stats_Client_IsUpdated
    (
        ANSC_HANDLE                 hInsContext
    )
{
	return TRUE;
}
/**********************************************************************

    caller:     owner of this object

    prototype:

        ULONG
        ATM_Stats_Client_Synchronize
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to synchronize the table.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
ATM_Stats_Client_Synchronize
    (
        ANSC_HANDLE                 hInsContext
    )
{
	PCOSA_DATAMODEL_WIFI            pMyObject = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
	PCOSA_DML_LG_WIFI_ATM        pAATM     = pMyObject->pAATM;

	ANSC_STATUS                           ret = ANSC_STATUS_SUCCESS;

	if(NULL == pAATM)
		return 0;

	if(pAATM->pBandAtmStat)
	{
		AnscFreeMemory(pAATM->pBandAtmStat);
		pAATM->pBandAtmStat = NULL;
		pAATM->BandAtmStatCount = 0;
	}
	ret = ATM_Band_Get_Statistics();

	return ret;
}
/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        ATM_Stats_Client_GetParamUlongValue
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
ATM_Stats_Client_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
	PCOSA_DML_LG_WIFI_ATM_STAT pStatInfo = (PCOSA_DML_LG_WIFI_ATM_STAT)hInsContext;

	if (strcmp(ParamName, "STAWeight") == 0)
	{
		*puLong = pStatInfo->StationWeight;
		return TRUE;
	}
	if (strcmp(ParamName, "SSID") == 0)
	{
		*puLong = pStatInfo->SSID;
		return TRUE;
	}
	if (strcmp(ParamName, "SSIDWeight") == 0)
	{
		*puLong = pStatInfo->SSIDWeight;
		return TRUE;
	}
	if (strcmp(ParamName, "CurrentPercentAirTime") == 0)
	{
		*puLong = pStatInfo->MeasuredAirTime;
		return TRUE;
	}
	if (strcmp(ParamName, "CumulativeAirTime") == 0)
	{
		*puLong = pStatInfo->TotalAirTime;
		return TRUE;
	}
	return FALSE;
}
/**********************************************************************

    caller:     owner of this object

    prototype:

        ULONG
        ATM_Stats_Client_GetParamStringValue
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
ATM_Stats_Client_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
	PCOSA_DML_LG_WIFI_ATM_STAT pStatInfo = (PCOSA_DML_LG_WIFI_ATM_STAT)hInsContext;

	if (strcmp(ParamName, "MACAddress") == 0)
	{
		if ( _ansc_strlen(pStatInfo->StationMAC) >= *pUlSize )
		{
			*pUlSize = _ansc_strlen(pStatInfo->StationMAC);
			return 1;
		}
		AnscCopyString(pValue, pStatInfo->StationMAC);
		return 0;
	}
	return -1;
}
/*ATM_Stats_Client - END*/
