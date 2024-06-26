 /****************************************************************************
  If not stated otherwise in this file or this component's LICENSE     
  file the following copyright and licenses apply:                          
                                                                            
  Copyright 2020 RDK Management                                             
                                                                            
  Licensed under the Apache License, Version 2.0 (the "License");           
  you may not use this file except in compliance with the License.          
  You may obtain a copy of the License at                                   
                                                                            
      http://www.apache.org/licenses/LICENSE-2.0                            
                                                                            
  Unless required by applicable law or agreed to in writing, software       
  distributed under the License is distributed on an "AS IS" BASIS,         
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  
  See the License for the specific language governing permissions and       
  limitations under the License.                                            
                                                                            
 ****************************************************************************/

#include <avro.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include "cosa_apis.h"
#include "wifi_hal.h"
#include "collection.h"
#include "wifi_monitor.h"
#include "wifi_blaster.h"
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <sys/un.h>
#include <assert.h>
#include <uuid/uuid.h>
#include "ansc_status.h"
#include <sysevent/sysevent.h>
#include "ccsp_base_api.h"
#include "harvester.h"
#include "safec_lib_common.h"
#include "ccsp_WifiLog_wrapper.h"

// UUID - 8b27dafc-0c4d-40a1-b62c-f24a34074914

// HASH - 4ce3404669247e94eecb36cf3af21750

static void to_plan_char(unsigned char *plan, unsigned char *key)
{
    int i = 0;
    for(i=0; i<16; i++)
    {
        sscanf((char*)plan,"%c",(char*)&key[i]);
        plan++;
    }
}

uint8_t HASHVAL[16] = {0x4c, 0xe3, 0x40, 0x46, 0x69, 0x24, 0x7e, 0x94,
                    0xee, 0xcb, 0x36, 0xcf, 0x3a, 0xf2, 0x17, 0x50
                   };

uint8_t UUIDVAL[16] = {0x8b, 0x27, 0xda, 0xfc, 0x0c, 0x4d, 0x40, 0xa1,
                    0xb6, 0x2c, 0xf2, 0x4a, 0x34, 0x07, 0x49, 0x14
                   };

/* Active Measurement Data values */

// UUID - 96673104-5a8b-4976-82dd-b204f13dfeee

// HASH - 9ab7c7e0a9b043f0bd52a1cf024374ec

uint8_t ACTHASHVAL[16] = {0x9a, 0xb7, 0xc7, 0xe0, 0xa9, 0xb0, 0x43, 0xf0,
                       0xbd, 0x52, 0xa1, 0xcf, 0x02, 0x43, 0x74, 0xec};

uint8_t ACTUUIDVAL[16] = {0x96, 0x67, 0x31, 0x04, 0x5a, 0x8b, 0x49, 0x76,
                       0x82, 0xdd, 0xb2, 0x04, 0xf1, 0x3d, 0xfe, 0xee
                      };

// local data, load it with real data if necessary
char Report_Source[] = "wifi";
char CPE_TYPE_STR[] = "Gateway";

#define MAX_BUFF_SIZE	20480
#define MAX_STR_LEN	32

#define MAGIC_NUMBER      0x85
#define MAGIC_NUMBER_SIZE 1
#define SCHEMA_ID_LENGTH  32
#define MAC_KEY_LEN 13
#define PLAN_ID_LEN 33

typedef enum {
	single_client_msmt_type_all,
	single_client_msmt_type_all_per_bssid,
	single_client_msmt_type_one,
} single_client_msmt_type_t;

static char *to_sta_key    (mac_addr_t mac, sta_key_t key) {
    snprintf(key, STA_KEY_LEN, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return (char *)key;
}

static void print_plan_id(unsigned char *plan, unsigned char *key)
{
    int i = 0, len = 0;
    for(i = 0; i < PLAN_ID_LENGTH; i++)
    {
        len += snprintf((char *)(key + len), PLAN_ID_LEN, "%2hhx", plan[i]);
    }
}

static void printBlastMetricData(single_client_msmt_type_t msmtType, wifi_monitor_t *monRadio,
				sta_data_t *staData, wifi_actvie_msmt_t *monitor,
				bool activeClient, const char *callerFunc)
{
	int RadioCount = 0, radioIdx = 0, sampleCount = 0;
	int Count = GetActiveMsmtNumberOfSamples();
	unsigned char PlanId[PLAN_ID_LEN] = {0};
	static char *radio_arr[3] = {"radio_2_4G", "radio_5G", "radio_6G"};
	char freqBand[MAX_STR_LEN] = {0}, chanBw[MAX_STR_LEN] = {0};

	CcspWifiTrace(("RDK_LOG_DEBUG, debug called from %s\n", callerFunc));

	if (monRadio == NULL || monRadio->radio_data == NULL || staData == NULL || monitor == NULL)
	{
		CcspTraceError(("%s:%d: Debug function failed as NULL data received\n", __FUNCTION__, __LINE__));
		return;
	}

	/* ID */
	print_plan_id(monitor->active_msmt.PlanId, PlanId);
	CcspWifiTrace(("RDK_LOG_INFO, \n\tplanID:\t[%s]\n \tstepID:\t[%d]\n",
		PlanId, monitor->curStepData.StepId));

	if (activeClient)
	{
		/* Radio Data */
#ifdef WIFI_HAL_VERSION_3
		for(RadioCount = 0; RadioCount < (int)getNumberRadios(); RadioCount++)
#else
		for(RadioCount = 0; RadioCount < MAX_RADIO_INDEX; RadioCount++)
#endif
		{
			CcspWifiTrace(("RDK_LOG_INFO, ===== RADIO METRICS ====\n"));
			CcspWifiTrace(("RDK_LOG_INFO, {\n\t radioId: %s,\n\t NoiseFloor: %d,\n\t ChanUtil: %d,\n\t "
				"activityFactor: %d,\n\t careerSenseThresholdExceed: %d,\n\t channelsInUse: %s\n}\n",
				radio_arr[RadioCount], monRadio->radio_data[RadioCount].NoiseFloor,
				monRadio->radio_data[RadioCount].channelUtil,
				monRadio->radio_data[RadioCount].RadioActivityFactor,
				monRadio->radio_data[RadioCount].CarrierSenseThreshold_Exceeded,
				monRadio->radio_data[RadioCount].ChannelsInUse));
		}

		/* Operating Channel metrics */
		if (monitor->curStepData.ApIndex >= 0)
		{
#ifdef WIFI_HAL_VERSION_3
			radioIdx = getRadioIndexFromAp(monitor->curStepData.ApIndex);
#else
			radioIdx = (monitor->curStepData.ApIndex >= 0) ? (monitor->curStepData.ApIndex % 2) : 0;
#endif
			if ( strstr("20MHz", staData->sta_active_msmt_data[0].Operating_channelwidth))
			{
				snprintf(chanBw, MAX_STR_LEN, "\"%s\"", "set to _20MHz");
			}
			else if ( strstr("40MHz", staData->sta_active_msmt_data[0].Operating_channelwidth) )
			{
				snprintf(chanBw, MAX_STR_LEN, "\"%s\"", "set to _40MHz");
			}
			else if ( strstr("80MHz", staData->sta_active_msmt_data[0].Operating_channelwidth) )
			{
				snprintf(chanBw, MAX_STR_LEN, "\"%s\"", "set to _80MHz");
			}
			else if ( strstr("160MHz", staData->sta_active_msmt_data[0].Operating_channelwidth) )
			{
				snprintf(chanBw, MAX_STR_LEN, "\"%s\"", "set to _160MHz");
			}

			if (strstr("2.4GHz", monRadio->radio_data[radioIdx].frequency_band))
			{
				snprintf(freqBand, MAX_STR_LEN, "\"%s\"", "2.4GHz, set to _2_4GHz");
			}
			else if (strstr("5GHz", monRadio->radio_data[radioIdx].frequency_band))
			{
				snprintf(freqBand, MAX_STR_LEN, "\"%s\"", "5GHz, set to _5GHz");
			}
			else if (strstr("6GHz", monRadio->radio_data[radioIdx].frequency_band))
			{
				snprintf(freqBand, MAX_STR_LEN, "\"%s\"", "6GHz, set to _6GHz");
			}
			CcspWifiTrace(("RDK_LOG_INFO, {\n\tOperatingStandard: %s,\n\tOperatingChannel: %d,\n\t"
				"OperatingChannelBandwidth: %s,\n\tFreqBand: %s\n}\n",
				staData->sta_active_msmt_data[0].Operating_standard,
				monRadio->radio_data[radioIdx].primary_radio_channel,
				chanBw, freqBand));

		} else {
			CcspWifiTrace(("RDK_LOG_INFO, {\n\tOperatingStandard: %s,\n\tOperatingChannel: 0,\n\t"
				"OperatingChannelBandwidth: %s,\n\tFreqBand: %s\n}\n",
				"Not defined, set to NULL", "set to NULL", "frequency_band set to NULL"));
		}

		/* Client Blast metrics */
		CcspWifiTrace(("RDK_LOG_INFO, ===== Client [%02x:%02x:%02x:%02x:%02x:%02x] Blast Metrics =====\n",
			staData->dev_stats.cli_MACAddress[0], staData->dev_stats.cli_MACAddress[1],
			staData->dev_stats.cli_MACAddress[2], staData->dev_stats.cli_MACAddress[3],
			staData->dev_stats.cli_MACAddress[4], staData->dev_stats.cli_MACAddress[5]));

		/* TX metrics */
		CcspWifiTrace(("RDK_LOG_INFO, {\n\ttx_retransmissions: %d,\n \tmax_tx_rate: %d\n}\n",
			staData->sta_active_msmt_data[Count-1].ReTransmission - staData->sta_active_msmt_data[0].ReTransmission,
			staData->sta_active_msmt_data[0].MaxTxRate));

		for (sampleCount = 0; sampleCount < Count; sampleCount++)
		{
			CcspWifiTrace(("RDK_LOG_INFO, {\n \t SampleCount: %d\n \t\t{signalStrength: %d,\n"
				"\t\tSNR: %d,\n \t\ttx_phy_rate: %ld,\n \t\trx_phy_rate: %ld,\n"
				"\t\tthroughput: %lf }\n}\n", sampleCount, staData->sta_active_msmt_data[sampleCount].rssi,
				staData->sta_active_msmt_data[sampleCount].SNR,
				staData->sta_active_msmt_data[sampleCount].TxPhyRate,
				staData->sta_active_msmt_data[sampleCount].RxPhyRate,
				staData->sta_active_msmt_data[sampleCount].throughput));
		}
	}
}

void upload_single_client_msmt_data(bssid_data_t *bssid_info, sta_data_t *sta_info)
{
	const char * serviceName = "wifi";
  	const char * dest = "event:raw.kestrel.reports.WifiSingleClient";
  	const char * contentType = "avro/binary"; // contentType "application/json", "avro/binary"
  	uuid_t transaction_id;
  	char trans_id[37];
	FILE *fp;
	char *buff;
	int size;
#ifdef WIFI_HAL_VERSION_3
    unsigned int radioCount = 0;
#endif //WIFI_HAL_VERSION_3
	bssid_data_t *bssid_data;
	hash_map_t *sta_map;
	sta_data_t	*sta_data;
	wifi_monitor_t *monitor;
	single_client_msmt_type_t msmt_type;
  	
	avro_writer_t writer;
	avro_schema_t inst_msmt_schema = NULL;
	avro_schema_error_t	error = NULL;
	avro_value_iface_t  *iface = NULL;
  	avro_value_t  adr = {0}; /*RDKB-7463, CID-33353, init before use */
  	avro_value_t  adrField = {0}; /*RDKB-7463, CID-33485, init before use */
  	avro_value_t optional  = {0}; 

#ifdef WIFI_HAL_VERSION_3
    radioCount = getNumberRadios();
#endif //WIFI_HAL_VERSION_3

	if (bssid_info == NULL) { 
		if (sta_info != NULL) {
       		wifi_dbg_print(1, "%s:%d: Invalid arguments\n", __func__, __LINE__);
			return;
		} else {
			msmt_type = single_client_msmt_type_all;
		}

	} else {
		if (sta_info == NULL) {
			msmt_type = single_client_msmt_type_all_per_bssid;
		} else {
			msmt_type = single_client_msmt_type_one;
		}
	}
       	
	wifi_dbg_print(1, "%s:%d: Measurement Type: %d\n", __func__, __LINE__, msmt_type);
	monitor = get_wifi_monitor();

	/* open schema file */
	fp = fopen (WIFI_SINGLE_CLIENT_AVRO_FILENAME , "rb");
	if (fp == NULL)
	{
		wifi_dbg_print(1, "%s:%d: Unable to open schema file: %s\n", __func__, __LINE__, WIFI_SINGLE_CLIENT_AVRO_FILENAME);
		return;
	}

	/* seek through file and get file size*/
	fseek(fp , 0L , SEEK_END);
	size = ftell(fp);
	if(size < 0)
	{
		wifi_dbg_print(1, "%s:%d: ftell error\n", __func__, __LINE__);
		fclose(fp);
		return;
	}
	/*back to the start of the file*/
	rewind(fp);

	/* allocate memory for entire content */
	buff = malloc(size + 1);
	memset(buff, 0, size + 1);

	/* copy the file into the buffer */
	if (1 != fread(buff , size, 1 , fp))
	{
		fclose(fp);
		free(buff);
		wifi_dbg_print(1, "%s:%d: Unable to read schema file: %s\n", __func__, __LINE__, WIFI_SINGLE_CLIENT_AVRO_FILENAME);
		return ;
	}
	buff[size]='\0';
	fclose(fp);

	if (avro_schema_from_json(buff, strlen(buff), &inst_msmt_schema, &error))
	{
		free(buff);
		wifi_dbg_print(1, "%s:%d: Unable to parse steering schema, len: %d, error:%s\n", __func__, __LINE__, size, avro_strerror());
		return;
	}
	free(buff);

	//generate an avro class from our schema and get a pointer to the value interface
	iface = avro_generic_class_from_schema(inst_msmt_schema);

	avro_schema_decref(inst_msmt_schema);

	buff = malloc(MAX_BUFF_SIZE);
	memset(buff, 0, MAX_BUFF_SIZE);
  	buff[0] = MAGIC_NUMBER; /* fill MAGIC number = Empty, i.e. no Schema ID */

  	memcpy( &buff[MAGIC_NUMBER_SIZE], UUIDVAL, sizeof(UUIDVAL));
  	memcpy( &buff[MAGIC_NUMBER_SIZE + sizeof(UUIDVAL)], HASHVAL, sizeof(HASHVAL));

  	writer = avro_writer_memory((char*)&buff[MAGIC_NUMBER_SIZE + SCHEMA_ID_LENGTH], MAX_BUFF_SIZE - MAGIC_NUMBER_SIZE - SCHEMA_ID_LENGTH);
	avro_writer_reset(writer);
  	avro_generic_value_new(iface, &adr);

  	// timestamp - long
  	avro_value_get_by_name(&adr, "header", &adrField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
  	avro_value_get_by_name(&adrField, "timestamp", &adrField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
  	avro_value_set_branch(&adrField, 1, &optional);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());

  	struct timeval ts;
  	gettimeofday(&ts, NULL);

  	int64_t tstamp_av_main = ((int64_t) (ts.tv_sec) * 1000000) + (int64_t) ts.tv_usec;
  
  	tstamp_av_main = tstamp_av_main/1000;
  	avro_value_set_long(&optional, tstamp_av_main );

  	// uuid - fixed 16 bytes
  	uuid_generate_random(transaction_id);
  	uuid_unparse(transaction_id, trans_id);

  	avro_value_get_by_name(&adr, "header", &adrField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
  	avro_value_get_by_name(&adrField, "uuid", &adrField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
  	avro_value_set_branch(&adrField, 1, &optional);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
  	avro_value_set_fixed(&optional, transaction_id, 16);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
	wifi_dbg_print(1, "Report transaction uuid generated is %s\n", trans_id);
  	CcspTraceInfo(("Single client report transaction uuid generated is %s\n", trans_id ));

  	//source - string
  	avro_value_get_by_name(&adr, "header", &adrField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
  	avro_value_get_by_name(&adrField, "source", &adrField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
  	avro_value_set_branch(&adrField, 1, &optional);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
  	avro_value_set_string(&optional, Report_Source);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());

	const char *macStr = NULL;
	char CpemacStr[32];
	int safe_rc = -1;

  	//cpe_id block
  	/* MAC - Get CPE mac address, do it only pointer is NULL */
  	if ( macStr == NULL )
  	{
		macStr = getDeviceMac();
		safe_rc = strcpy_s( CpemacStr, sizeof(CpemacStr), macStr);
		if (safe_rc != 0) ERR_CHK(safe_rc);
		wifi_dbg_print(1, "RDK_LOG_DEBUG, Received DeviceMac from Atom side: %s\n",macStr);
  	}

  	char CpeMacHoldingBuf[ 20 ] = {0};
  	unsigned char CpeMacid[ 7 ] = {0};
	unsigned int k;

  	for (k = 0; k < 6; k++ )
  	{
		/* copy 2 bytes */
		CpeMacHoldingBuf[ k * 2 ] = CpemacStr[ k * 2 ];
		CpeMacHoldingBuf[ k * 2 + 1 ] = CpemacStr[ k * 2 + 1 ];
		CpeMacid[ k ] = (unsigned char)strtol(&CpeMacHoldingBuf[ k * 2 ], NULL, 16);
  	}
	
  	avro_value_get_by_name(&adr, "cpe_id", &adrField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
  	avro_value_get_by_name(&adrField, "mac_address", &adrField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
  	avro_value_set_branch(&adrField, 1, &optional);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
  	avro_value_set_fixed(&optional, CpeMacid, 6);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
  	unsigned char *pMac = (unsigned char*)CpeMacid;
  	wifi_dbg_print(1, "RDK_LOG_DEBUG, mac_address = 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", pMac[0], pMac[1], pMac[2], pMac[3], pMac[4], pMac[5] );
	
  	// cpe_type - string
  	avro_value_get_by_name(&adr, "cpe_id", &adrField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
  	avro_value_get_by_name(&adrField, "cpe_type", &adrField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
  	avro_value_set_branch(&adrField, 1, &optional);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
  	avro_value_set_string(&optional, CPE_TYPE_STR);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());

  	//Data Field block
	wifi_dbg_print(1, "data field\n");
  	avro_value_get_by_name(&adr, "data", &adrField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());

	//adrField now contains a reference to the Single Client WiFi ReportsArray
  	//Device Report

  	//Current Device Report Field
  	avro_value_t drField = {0}; /*RDKB-7463, CID-33269, init before use */

	//data block

        /*unsigned int i;
	for (i = 0; i < MAX_VAP; i++) 
	{
		if (msmt_type == single_client_msmt_type_all) {
		bssid_data = &monitor->bssid_data[i];
		} else {
		bssid_data = bssid_info;
		if (msmt_type == single_client_msmt_type_one) {
			sta_data = sta_info;
		} else {

		}
		}
	}*/
	wifi_dbg_print(1, "updating bssid_data and sta_data\n");
	bssid_data = bssid_info;
        sta_data = sta_info;
	
	if(sta_data == NULL)
	{
		wifi_dbg_print(1, "sta_data is empty\n");
	}
	else
	{
		//device_mac - fixed 6 bytes
		wifi_dbg_print(1, "adding cli_MACAddress field\n");
		avro_value_get_by_name(&adrField, "device_id", &drField, NULL);
		if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
		avro_value_get_by_name(&drField, "mac_address", &drField, NULL);
		if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
		avro_value_set_branch(&drField, 1, &optional);
		if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
		avro_value_set_fixed(&optional, sta_data->dev_stats.cli_MACAddress, 6);
		if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
	}

	//device_status - enum
	avro_value_get_by_name(&adrField, "device_id", &drField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "Avro error: %s\n",  avro_strerror());
	avro_value_get_by_name(&drField, "device_status", &drField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, " Avro error: %s\n",  avro_strerror());

	if((sta_data != NULL) && sta_data->dev_stats.cli_Active)
	{
		wifi_dbg_print(1,"active\n");
		avro_value_set_enum(&drField, avro_schema_enum_get_by_name(avro_value_get_schema(&drField), "Online"));
	}
	else
	{
		avro_value_set_enum(&drField, avro_schema_enum_get_by_name(avro_value_get_schema(&drField), "Offline"));
	}
	if (CHK_AVRO_ERR) wifi_dbg_print(1, " Avro error: %s\n",  avro_strerror());

	//timestamp - long
	avro_value_get_by_name(&adrField, "timestamp", &drField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
	avro_value_set_branch(&drField, 1, &optional);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
	avro_value_set_long(&optional, tstamp_av_main);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());

	memset(CpeMacHoldingBuf, 0, sizeof CpeMacHoldingBuf);
	memset(CpeMacid, 0, sizeof CpeMacid);
	char bssid[MAC_KEY_LEN];
	snprintf(bssid, MAC_KEY_LEN, "%02x%02x%02x%02x%02x%02x",
	    bssid_data->bssid[0], bssid_data->bssid[1], bssid_data->bssid[2],
	    bssid_data->bssid[3], bssid_data->bssid[4], bssid_data->bssid[5]);

	wifi_dbg_print(1, "BSSID for vap : %s\n",bssid);

    	for (k = 0; k < 6; k++ ) {
		/* copy 2 bytes */
		CpeMacHoldingBuf[ k * 2 ] = bssid[ k * 2 ];
		CpeMacHoldingBuf[ k * 2 + 1 ] = bssid[ k * 2 + 1 ];
		CpeMacid[ k ] = (unsigned char)strtol(&CpeMacHoldingBuf[ k * 2 ], NULL, 16);
    	}

    	// interface_mac
	avro_value_get_by_name(&adrField, "interface_mac", &drField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
	avro_value_set_fixed(&drField, CpeMacid, 6);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
	pMac = (unsigned char*)CpeMacid;
	wifi_dbg_print(1, "RDK_LOG_DEBUG, interface mac_address = 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", pMac[0], pMac[1], pMac[2], pMac[3], pMac[4], pMac[5] );

	// vAP_index
	avro_value_get_by_name(&adrField, "vAP_index", &drField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "Avro error: %s\n",  avro_strerror());
	avro_value_set_branch(&drField, 1, &optional);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "Avro error: %s\n",  avro_strerror());
	if(monitor !=NULL)
	{
		avro_value_set_int(&optional, (monitor->inst_msmt.ap_index)+1);
	}
	else
	{
		avro_value_set_int(&optional, 0);
	}
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "Avro error: %s\n",  avro_strerror());

    	//interface metrics block
		if (msmt_type == single_client_msmt_type_one) {
			sta_data = sta_info;
		} else {
			sta_map = bssid_data->sta_map;
			sta_data = hash_map_get_first(sta_map); 
		}
	while (sta_data != NULL) {
    	//rx_rate
	avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
    	avro_value_set_branch(&drField, 1, &optional);
    	avro_value_get_by_name(&optional, "rx_rate", &drField, NULL);
    	avro_value_set_branch(&drField, 1, &optional);
	avro_value_set_int(&optional, (int)sta_data->dev_stats.cli_LastDataDownlinkRate);

    	//tx_rate
	avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
    	avro_value_set_branch(&drField, 1, &optional);
    	avro_value_get_by_name(&optional, "tx_rate", &drField, NULL);
    	avro_value_set_branch(&drField, 1, &optional);
	avro_value_set_int(&optional, (int)sta_data->dev_stats.cli_LastDataUplinkRate);

	//tx_packets
	avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	avro_value_get_by_name(&optional, "tx_packets", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	avro_value_set_int(&optional, sta_data->dev_stats.cli_PacketsReceived);

	//rx_packets
	avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	avro_value_get_by_name(&optional, "rx_packets", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	avro_value_set_int(&optional, sta_data->dev_stats.cli_PacketsSent);

	//tx_error_packets
	avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	avro_value_get_by_name(&optional, "tx_error_packets", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	avro_value_set_int(&optional, sta_data->dev_stats.cli_ErrorsSent);

    	//retransmissions
	avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	avro_value_get_by_name(&optional, "retransmissions", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	avro_value_set_int(&optional, sta_data->dev_stats.cli_Retransmissions);

    //channel_utilization_percent_radio3
    wifi_dbg_print(1,"channel_utilization_percent_radio3 field\n");
    avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);
    avro_value_get_by_name(&optional, "channel_utilization_percent_radio3", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);

#ifdef WIFI_HAL_VERSION_3
    if (monitor->radio_data !=NULL)
    {
        wifi_dbg_print(1,"avro set monitor->radio_data[2].channelUtil to int\n");
        if (radioCount > 2)
        {
            avro_value_set_int(&optional, (int)monitor->radio_data[radioCount-1].channelUtil);
        }
        else
        {
            avro_value_set_int(&optional, 0);
        }
    }
    else
    {
        avro_value_set_int(&optional, 0);
    }
#else
    avro_value_set_int(&optional, 0);
#endif //WIFI_HAL_VERSION_3

    //channel_interference_percent_radio3
    wifi_dbg_print(1,"channel_interference_percent_radio3 field\n");
    avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);
    avro_value_get_by_name(&optional, "channel_interference_percent_radio3", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);

#ifdef WIFI_HAL_VERSION_3
    if (monitor->radio_data !=NULL)
    {
        wifi_dbg_print(1,"avro set monitor->radio_data[2].channelInterference to int\n");
        if (radioCount > 2)
        {
            avro_value_set_int(&optional, (int)monitor->radio_data[radioCount-1].channelInterference);
        }
        else
        {
            avro_value_set_int(&optional, 0);
        }
    }
    else
    {
        avro_value_set_int(&optional, 0);
    }
#else
    avro_value_set_int(&optional, 0);
#endif //WIFI_HAL_VERSION_3

    //channel_noise_floor_radio3
    wifi_dbg_print(1,"channel_noise_floor_radio3 field\n");
    avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);
    avro_value_get_by_name(&optional, "channel_noise_floor_radio3", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);

#ifdef WIFI_HAL_VERSION_3
    if((monitor !=NULL) && ((monitor->inst_msmt.ap_index+1) == (unsigned int)(getPrivateApFromRadioIndex(2)+1)))
    {
        wifi_dbg_print(1,"avro set monitor->radio_data[2].NoiseFloor to int\n");
        if (radioCount > 2)
        {
            avro_value_set_int(&optional, (int)monitor->radio_data[radioCount-1].NoiseFloor);
        }
        else
        {
            avro_value_set_int(&optional, 0);
        }
    }
    else
    {
        avro_value_set_int(&optional, 0);
    }
#else
    avro_value_set_int(&optional, 0);
#endif ////WIFI_HAL_VERSION_3

	//channel_utilization_percent_5ghz
	avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	avro_value_get_by_name(&optional, "channel_utilization_percent_5ghz", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);

	if (monitor->radio_data !=NULL)
	{
		wifi_dbg_print(1,"avro set monitor->radio_data[1].channelUtil to int\n");
		avro_value_set_int(&optional, (int)monitor->radio_data[1].channelUtil);
	}
	else
	{
		avro_value_set_int(&optional, 0);
	}

	//channel_interference_percent_5ghz
	wifi_dbg_print(1,"channel_interference_percent_5ghz field\n");
	avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	avro_value_get_by_name(&optional, "channel_interference_percent_5ghz", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	if (monitor->radio_data !=NULL)
	{
		avro_value_set_int(&optional, (int)monitor->radio_data[1].channelInterference);
	}
	else
	{
		avro_value_set_int(&optional, 0);
	}

	//channel_noise_floor_5ghz
	avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	avro_value_get_by_name(&optional, "channel_noise_floor_5ghz", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);

	if((monitor !=NULL) && ((monitor->inst_msmt.ap_index+1) == 2)) //Noise floor for vAP index 2 (5GHz)
	{
		//avro_value_set_int(&optional, (int)(sta_data->dev_stats.cli_SignalStrength - sta_data->dev_stats.cli_SNR));
		avro_value_set_int(&optional, (int)monitor->radio_data[1].NoiseFloor);
	}
	else
	{
		avro_value_set_int(&optional, 0);
	}

	//channel_utilization_percent_2_4ghz
	avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	avro_value_get_by_name(&optional, "channel_utilization_percent_2_4ghz", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	if(monitor->radio_data !=NULL)
	{
		avro_value_set_int(&optional, (int)monitor->radio_data[0].channelUtil);
	}
	else
	{
		avro_value_set_int(&optional, 0);
	}

	//channel_interference_percent_2_4ghz
	avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	avro_value_get_by_name(&optional, "channel_interference_percent_2_4ghz", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	if(monitor->radio_data !=NULL)
	{
		avro_value_set_int(&optional, (int)monitor->radio_data[0].channelInterference);
	}
	else
	{
		avro_value_set_int(&optional, 0);
	}

	//channel_noise_floor_2_4ghz
	avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);
	avro_value_get_by_name(&optional, "channel_noise_floor_2_4ghz", &drField, NULL);
	avro_value_set_branch(&drField, 1, &optional);

	if((monitor!=NULL) && ((monitor->inst_msmt.ap_index+1) == 1)) //Noise floor for vAP index 1 (2.4GHz)
	{
		//avro_value_set_int(&optional, (int)(sta_data->dev_stats.cli_SignalStrength - sta_data->dev_stats.cli_SNR));
		avro_value_set_int(&optional, (int)monitor->radio_data[0].NoiseFloor);
	}
	else
	{
		avro_value_set_int(&optional, 0);
	}

    	//signal_strength
	avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    	avro_value_set_branch(&drField, 1, &optional);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    	avro_value_get_by_name(&optional, "signal_strength", &drField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    	avro_value_set_branch(&drField, 1, &optional);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
	avro_value_set_int(&optional, (int)sta_data->dev_stats.cli_SignalStrength);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());

	//snr
	avro_value_get_by_name(&adrField, "interface_metrics", &drField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
	avro_value_set_branch(&drField, 1, &optional);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
	avro_value_get_by_name(&optional, "snr", &drField, NULL);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
	avro_value_set_branch(&drField, 1, &optional);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
	avro_value_set_int(&optional, (int)sta_data->dev_stats.cli_SNR);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());

		if (msmt_type != single_client_msmt_type_all) break;	
	}

        /* check for writer size, if buffer is almost full, skip trailing linklist */
        avro_value_sizeof(&adr, (size_t*)&size);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
  
	//Thats the end of that
  	avro_value_write(writer, &adr);
	if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());

	wifi_dbg_print(1, "Avro packing done\n");
	CcspWifiTrace(("RDK_LOG_INFO, %s-%d AVRO packing done\n", __FUNCTION__, __LINE__));

	char *json;
        if (!avro_value_to_json(&adr, 1, &json))
	{
		wifi_dbg_print(1,"json is %s\n", json);
		free(json);
        }
  	//Free up memory
  	avro_value_decref(&adr);
  	avro_writer_free(writer);

	size += MAGIC_NUMBER_SIZE + SCHEMA_ID_LENGTH;
	sendWebpaMsg((char *)(serviceName), (char *)(dest), trans_id, (char *)(contentType), buff, size);
	wifi_dbg_print(1, "Creating telemetry record successful\n");
	CcspWifiTrace(("RDK_LOG_INFO, %s-%d Creation of Telemetry record is successful\n", __FUNCTION__, __LINE__));
}

void upload_single_client_active_msmt_data(bssid_data_t *bssid_info, sta_data_t *sta_info)
{
    const char * serviceName = "wifi";
    const char * dest = "event:raw.kestrel.reports.WifiSingleClientActiveMeasurement";
    const char * contentType = "avro/binary"; // contentType "application/json", "avro/binary"
    char *schema_version = "1.1";
    unsigned char PlanId[PLAN_ID_LENGTH];
    unsigned char printPlanId[PLAN_ID_LEN] = {0};
    char *radio_arr[3] = {"radio_2_4G", "radio_5G", "radio_6G"};
    uuid_t transaction_id;
    char trans_id[37];
    FILE *fp;
    char *buff;
    int size;
    int sampleCount = 0;
    int RadioCount = 0;
    int Count = GetActiveMsmtNumberOfSamples();
    int radio_idx = 0;
    bssid_data_t *bssid_data;
    wifi_monitor_t *g_monitor;
    hash_map_t *sta_map;
    sta_data_t	*sta_data;
    sta_data_t  *sta_del = NULL;
    wifi_actvie_msmt_t *monitor;
    single_client_msmt_type_t msmt_type;
    sta_key_t       sta_key;
    avro_writer_t writer;
    avro_schema_t inst_msmt_schema = NULL;
    avro_schema_error_t	error = NULL;
    avro_value_iface_t  *iface = NULL;
    avro_value_t  adr = {0};
    avro_value_t  adrField = {0};
    avro_value_t optional  = {0};

    if (bssid_info == NULL) {
        if (sta_info != NULL) {
            wifi_dbg_print(1, "%s:%d: Invalid arguments\n", __func__, __LINE__);
            return;
        } else {
            msmt_type = single_client_msmt_type_all;
        }
    } else {

        if (sta_info == NULL) {
            msmt_type = single_client_msmt_type_all_per_bssid;
        } else {
            msmt_type = single_client_msmt_type_one;
        }
    }

    wifi_dbg_print(1, "%s:%d: Measurement Type: %d\n", __func__, __LINE__, msmt_type);
    CcspWifiTrace(("RDK_LOG_DEBUG, %s:%d: Measurement Type: %d\n", __func__, __LINE__, msmt_type));

    g_monitor = get_wifi_monitor();
    if(g_monitor == NULL)
    {
        wifi_dbg_print(1, "%s:%d: global wifi monitor data is null \n", __func__, __LINE__);
    }
    monitor = (wifi_actvie_msmt_t *)get_active_msmt_data();

    if(monitor == NULL)
    {
        wifi_dbg_print(1, "%s:%d: wifi monitor active msmt data is null \n", __func__, __LINE__);
    }

    wifi_dbg_print(1, "%s:%d: opening the schema file %s\n", __func__, __LINE__, WIFI_SINGLE_CLIENT_BLASTER_AVRO_FILENAME);
    /* open schema file */
    fp = fopen (WIFI_SINGLE_CLIENT_BLASTER_AVRO_FILENAME , "rb");

    if (fp == NULL)
    {
        wifi_dbg_print(1, "%s:%d: Unable to open schema file: %s\n", __func__, __LINE__, WIFI_SINGLE_CLIENT_BLASTER_AVRO_FILENAME);
        return;
    }
    else
    {
        wifi_dbg_print(1, "%s:%d: successfully opened schema file: %s\n", __func__, __LINE__, WIFI_SINGLE_CLIENT_BLASTER_AVRO_FILENAME);
    }

    /* seek through file and get file size*/
    fseek(fp , 0L , SEEK_END);
    size = ftell(fp);
    wifi_dbg_print(1, "%s:%d: size of %s is %d \n", __func__, __LINE__,WIFI_SINGLE_CLIENT_BLASTER_AVRO_FILENAME,size);

    if (size < 0)
    {
        wifi_dbg_print(1, "%s:%d: ftell error\n", __func__, __LINE__);
        fclose(fp);
        return;
    }
    /*back to the start of the file*/
    rewind(fp);

    /* allocate memory for entire content */
    wifi_dbg_print(1, "%s:%d: allocating memory for entire content\n", __func__, __LINE__);
    buff = (char *) malloc(size + 1);

    if (buff == NULL)
    {
        if (fp)
        {
            fclose(fp);
            fp = NULL;
        }
        wifi_dbg_print(1, "%s:%d: allocating memory for entire content failed\n", __func__, __LINE__);
        /*CID: 146754 Dereference after null check*/
        return;
    }

    memset(buff, 0, size + 1);

    wifi_dbg_print(1, "%s:%d: copying the content of the file \n", __func__, __LINE__);

    /* copy the file into the buffer */
    if (1 != fread(buff , size, 1 , fp))
    {
        if (fp)
        {
            fclose(fp);
            fp = NULL;
        }
        free(buff);
        wifi_dbg_print(1, "%s:%d: Unable to read schema file: %s\n", __func__, __LINE__, WIFI_SINGLE_CLIENT_BLASTER_AVRO_FILENAME);
        CcspWifiTrace(("RDK_LOG_ERROR, %s:%d: !ERROR! Unable to read schema file: %s\n", __func__, __LINE__, WIFI_SINGLE_CLIENT_BLASTER_AVRO_FILENAME));
        return ;
    }

    buff[size]='\0';
    fclose(fp);

    wifi_dbg_print(1, "%s:%d: calling avro_schema_from_json \n", __func__, __LINE__);
    if (avro_schema_from_json(buff, strlen(buff), &inst_msmt_schema, &error))
    {
        free(buff);
        buff = NULL;
        wifi_dbg_print(1, "%s:%d: Unable to parse active measurement schema, len: %d, error:%s\n", __func__, __LINE__, size, avro_strerror());
        return;
    }

    if(buff)
    {
        free(buff);
        buff = NULL;
    }

    wifi_dbg_print(1, "%s:%d: generate an avro class from our schema and get a pointer to the value interface \n", __func__, __LINE__);
    //generate an avro class from our schema and get a pointer to the value interface
    iface = avro_generic_class_from_schema(inst_msmt_schema);

    avro_schema_decref(inst_msmt_schema);

    buff = malloc(MAX_BUFF_SIZE);
    memset(buff, 0, MAX_BUFF_SIZE);
    wifi_dbg_print(1, "%s:%d: filling MAGIC NUMBER in buff[0] \n", __func__, __LINE__);
    //generate an avro class from our schema and get a pointer to the value interface
    buff[0] = MAGIC_NUMBER; /* fill MAGIC number = Empty, i.e. no Schema ID */

    memcpy( &buff[MAGIC_NUMBER_SIZE], ACTUUIDVAL, sizeof(ACTUUIDVAL));
    memcpy( &buff[MAGIC_NUMBER_SIZE + sizeof(ACTUUIDVAL)], ACTHASHVAL, sizeof(ACTHASHVAL));

    writer = avro_writer_memory((char*)&buff[MAGIC_NUMBER_SIZE + SCHEMA_ID_LENGTH], MAX_BUFF_SIZE - MAGIC_NUMBER_SIZE - SCHEMA_ID_LENGTH);
    avro_writer_reset(writer);
    avro_generic_value_new(iface, &adr);

    // timestamp - long
    wifi_dbg_print(1, "%s:%d: timestamp \n", __func__, __LINE__);
    avro_value_get_by_name(&adr, "header", &adrField, NULL);

    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    avro_value_get_by_name(&adrField, "timestamp", &adrField, NULL);

    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    avro_value_set_branch(&adrField, 1, &optional);

    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    struct timeval ts;
    gettimeofday(&ts, NULL);

    int64_t tstamp_av_main = ((int64_t) (ts.tv_sec) * 1000000) + (int64_t) ts.tv_usec;

    //Set timestamp value in the report
    tstamp_av_main = tstamp_av_main/1000;
    avro_value_set_long(&optional, tstamp_av_main );

    // uuid - fixed 16 bytes
    uuid_generate_random(transaction_id);
    uuid_unparse(transaction_id, trans_id);

    wifi_dbg_print(1, "%s:%d: schema version is %s\n", __func__, __LINE__, schema_version);
    avro_value_get_by_name(&adr, "header", &adrField, NULL);

    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    avro_value_get_by_name(&adrField, "schema_version", &adrField, NULL);

    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    avro_value_set_branch(&adrField, 1, &optional);

    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    avro_value_set_fixed(&optional, schema_version, 16);

    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    to_plan_char(monitor->active_msmt.PlanId, PlanId);
    print_plan_id(monitor->active_msmt.PlanId, printPlanId);
    wifi_dbg_print(1, "%s:%d: Plan Id is %s\n", __func__, __LINE__,printPlanId);

    avro_value_get_by_name(&adr, "header", &adrField, NULL);

    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    avro_value_get_by_name(&adrField, "plan_id", &adrField, NULL);

    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    avro_value_set_branch(&adrField, 1, &optional);

    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    //Set uuid value in the report
    avro_value_set_fixed(&optional, PlanId, 16);

    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    wifi_dbg_print(1, "Report transaction uuid generated is %s\n", trans_id);
    CcspTraceInfo(("Single client report transaction uuid generated is %s\n", trans_id ));

    avro_value_get_by_name(&adr, "header", &adrField, NULL);

    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    avro_value_get_by_name(&adrField, "step_id", &adrField, NULL);

    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    avro_value_set_branch(&adrField, 1, &optional);

    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    wifi_dbg_print(1, "%s : %d setting the step Id : %d\n",__func__,__LINE__,monitor->curStepData.StepId);

    avro_value_set_int(&optional, monitor->curStepData.StepId);

    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    const char *macStr = NULL;
    char CpemacStr[32];

    //cpe_id block
    /* MAC - Get CPE mac address, do it only pointer is NULL */
    if ( macStr == NULL )
    {
            macStr = getDeviceMac();
	    int rc = -1;
            rc = strcpy_s( CpemacStr, sizeof(CpemacStr), macStr);
	    if (rc != 0) {
                ERR_CHK(rc);
                return;
            }
            wifi_dbg_print(1, "RDK_LOG_DEBUG, Received DeviceMac from Atom side: %s\n",macStr);
            CcspWifiTrace(("RDK_LOG_INFO, %s-%d Received DeviceMac from Atom side: %s\n", __FUNCTION__, __LINE__, macStr));
    }

    char CpeMacHoldingBuf[ 20 ] = {0};
    unsigned char CpeMacid[ 7 ] = {0};
    unsigned int k;

    for (k = 0; k < 6; k++ )
    {
            /* copy 2 bytes */
            CpeMacHoldingBuf[ k * 2 ] = CpemacStr[ k * 2 ];
            CpeMacHoldingBuf[ k * 2 + 1 ] = CpemacStr[ k * 2 + 1 ];
            CpeMacid[ k ] = (unsigned char)strtol(&CpeMacHoldingBuf[ k * 2 ], NULL, 16);
    }

    avro_value_get_by_name(&adr, "cpe_id", &adrField, NULL);

    if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    avro_value_get_by_name(&adrField, "mac_address", &adrField, NULL);
    if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    avro_value_set_branch(&adrField, 1, &optional);
    if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    avro_value_set_fixed(&optional, CpeMacid, 6);
    if (CHK_AVRO_ERR) wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    unsigned char *pMac = (unsigned char*)CpeMacid;
    wifi_dbg_print(1, "RDK_LOG_DEBUG, mac_address = 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", pMac[0], pMac[1], pMac[2], pMac[3], pMac[4], pMac[5] );
    CcspWifiTrace(("RDK_LOG_INFO, CPE MAC address = 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X\n",
        pMac[0], pMac[1], pMac[2], pMac[3], pMac[4], pMac[5]));

    //Data Field block
    wifi_dbg_print(1, "data field\n");
    avro_value_get_by_name(&adr, "data", &adrField, NULL);
    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    //Current Device Report Field
    avro_value_t drField = {0};

    wifi_dbg_print(1, "updating bssid_data and sta_data\n");
    bssid_data = bssid_info;
    sta_data = sta_info;

    if(sta_data == NULL)
    {
        wifi_dbg_print(1, "sta_data is empty\n");
    }
    else
    {
        //device_mac - fixed 6 bytes
        wifi_dbg_print(1, "adding cli_MACAddress field %02x%02x%02x%02x%02x%02x\n",sta_data->dev_stats.cli_MACAddress[0],sta_data->dev_stats.cli_MACAddress[1],sta_data->dev_stats.cli_MACAddress[2],sta_data->dev_stats.cli_MACAddress[3],sta_data->dev_stats.cli_MACAddress[4],sta_data->dev_stats.cli_MACAddress[5]);
        avro_value_get_by_name(&adrField, "client_mac", &drField, NULL);
        if (CHK_AVRO_ERR) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
        avro_value_set_branch(&drField, 1, &optional);
        if (CHK_AVRO_ERR) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
        avro_value_set_fixed(&optional, sta_data->dev_stats.cli_MACAddress, 6);
        if (CHK_AVRO_ERR) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
    }

    //Polling Period
    wifi_dbg_print(1, "adding sampling_interval field\n");
    avro_value_get_by_name(&adr, "data", &adrField, NULL);
    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }
    avro_value_get_by_name(&adrField, "sampling_interval", &drField, NULL);
    avro_value_set_int(&drField, GetActiveMsmtSampleDuration());
    wifi_dbg_print(1, "%s:%d sampling interval : %d\n", __func__, __LINE__, GetActiveMsmtSampleDuration());
    if ( CHK_AVRO_ERR ) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    //packet_size
    wifi_dbg_print(1, "adding packet_size field\n");
    avro_value_get_by_name(&adr, "data", &adrField, NULL);
    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }
    avro_value_get_by_name(&adrField, "packet_size", &drField, NULL);
    avro_value_set_int(&drField, GetActiveMsmtPktSize());
    wifi_dbg_print(1, "%s:%d: packet size : %d type : %d\n", __func__, __LINE__,GetActiveMsmtPktSize(), avro_value_get_type(&adrField));
    if ( CHK_AVRO_ERR ) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }


    //Blast metrics block
    if (msmt_type == single_client_msmt_type_one) {
        sta_data = sta_info;
    } else {
        sta_map = bssid_data->sta_map;
        sta_data = hash_map_get_first(sta_map);
    }

    avro_value_get_by_name(&adrField, "blast_metrics", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);
    avro_value_get_by_name(&optional, "BlastRadioMetrics", &drField, NULL);
    wifi_dbg_print(1, "RDK_LOG_DEBUG, BlastRadioMetrics\tType: %d\n", avro_value_get_type(&adrField));
    if ( CHK_AVRO_ERR ) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    //args: (/* msmt_type */, /* radioStruct */, /* sta struct */, /* msmt struct */ /* 1 or 0*/, /* __func__ */);
    printBlastMetricData(msmt_type, g_monitor, sta_data, monitor, true, __FUNCTION__);

    avro_value_t rdr = {0};
    avro_value_t brdrField = {0};
#ifdef WIFI_HAL_VERSION_3
    for(RadioCount = 0; RadioCount < (int)getNumberRadios(); RadioCount++)
#else
    for(RadioCount = 0; RadioCount < MAX_RADIO_INDEX; RadioCount++)
#endif
    {
        avro_value_append(&drField, &rdr, NULL);

        //radio
        avro_value_get_by_name(&rdr, "radio", &brdrField, NULL);
        if ( CHK_AVRO_ERR ) {
             wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
        avro_value_set_branch(&brdrField, 1, &optional);
        if ( CHK_AVRO_ERR ) {
             wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
        wifi_dbg_print(1, "RDK_LOG_DEBUG, radio number set to : \"%s\"\n", radio_arr[RadioCount]);
        avro_value_set_enum(&optional, avro_schema_enum_get_by_name(avro_value_get_schema(&optional), radio_arr[RadioCount]));

        //noise_floor
        avro_value_get_by_name(&rdr, "noise_floor", &brdrField, NULL);
        if ( CHK_AVRO_ERR ) {
             wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
        avro_value_set_branch(&brdrField, 1, &optional);
        if ( CHK_AVRO_ERR ) {
             wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }

        if ((g_monitor != NULL) && (g_monitor->radio_data != NULL)) //Noise floor
        {
           avro_value_set_int(&optional, (int)g_monitor->radio_data[RadioCount].NoiseFloor);
        }
        else
        {
           avro_value_set_int(&optional, 0);
        }
        wifi_dbg_print(1,"RDK_LOG_DEBUG, noise_floor : %d \tType: %d\n", g_monitor->radio_data[RadioCount].NoiseFloor,avro_value_get_type(&optional));

        //channel_utilization
        avro_value_get_by_name(&rdr, "channel_utilization", &brdrField, NULL);
        avro_value_set_branch(&brdrField, 1, &optional);
        if (g_monitor->radio_data != NULL)
        {
            avro_value_set_float(&optional, (float)g_monitor->radio_data[RadioCount].channelUtil);
        }
        else
        {
            avro_value_set_float(&optional, 0);
        }
        wifi_dbg_print(1,"RDK_LOG_DEBUG, channel_utilization : %d \tType: %d\n", g_monitor->radio_data[RadioCount].channelUtil,avro_value_get_type(&optional));
        //activity_factor
        avro_value_get_by_name(&rdr, "activity_factor", &brdrField, NULL);
        avro_value_set_branch(&brdrField, 1, &optional);

        if ((g_monitor != NULL) && (g_monitor->radio_data != NULL)) //Noise floor
        {
            avro_value_set_int(&optional, (int)g_monitor->radio_data[RadioCount].RadioActivityFactor);
        }
        else
        {
            avro_value_set_int(&optional, 0);
        }
        wifi_dbg_print(1,"RDK_LOG_DEBUG, activity_factor : %d \tType: %d\n", g_monitor->radio_data[RadioCount].RadioActivityFactor,avro_value_get_type(&optional));

        //carrier_sense_threshold_exceeded
        avro_value_get_by_name(&rdr, "carrier_sense_threshold_exceeded", &brdrField, NULL);
        avro_value_set_branch(&brdrField, 1, &optional);

        if ((g_monitor != NULL) && (g_monitor->radio_data != NULL))
        {
            avro_value_set_int(&optional, (int)g_monitor->radio_data[RadioCount].CarrierSenseThreshold_Exceeded);
        }
        else
        {
            avro_value_set_int(&optional, 0);
        }
        wifi_dbg_print(1,"RDK_LOG_DEBUG, carrier_sense_threshold_exceeded : %d \tType: %d\n", g_monitor->radio_data[RadioCount].CarrierSenseThreshold_Exceeded,avro_value_get_type(&optional));
        //channels_in_use
        avro_value_get_by_name(&rdr, "channels_in_use", &brdrField, NULL);
        avro_value_set_branch(&brdrField, 1, &optional);

        if ((g_monitor != NULL) && (g_monitor->radio_data != NULL))
        {
            if (strlen(g_monitor->radio_data[RadioCount].ChannelsInUse) == 0)
            {
                avro_value_set_null(&optional);
            } else {
                avro_value_set_string(&optional, g_monitor->radio_data[RadioCount].ChannelsInUse);
            }
        }
        wifi_dbg_print(1,"RDK_LOG_DEBUG, channels_in_use : %s \tType: %d\n", g_monitor->radio_data[RadioCount].ChannelsInUse,avro_value_get_type(&optional));
    }
    // operating_standards
    avro_value_get_by_name(&adrField, "blast_metrics", &drField, NULL);
    if ( CHK_AVRO_ERR ) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }
    avro_value_set_branch(&drField, 1, &optional);
    avro_value_get_by_name(&optional, "operating_standards", &drField, NULL);
    if ( CHK_AVRO_ERR ) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }
    avro_value_set_branch(&drField, 1, &optional);
    wifi_dbg_print(1, "RDK_LOG_DEBUG, operating_standard\tType: %d\n", avro_value_get_type(&optional));
    //Patch HAL values if necessary
    if ( monitor->curStepData.ApIndex < 0)
    {
        wifi_dbg_print(1, "RDK_LOG_DEBUG, operating_standard = \"%s\"\n", "Not defined, set to NULL" );
        avro_value_set_null(&optional);
    }
    else
    {
        wifi_dbg_print(1, "RDK_LOG_DEBUG, operating_standard = \"%s\"\n", sta_data->sta_active_msmt_data[0].Operating_standard);
        avro_value_set_enum(&optional, avro_schema_enum_get_by_name(avro_value_get_schema(&optional),
        sta_data->sta_active_msmt_data[0].Operating_standard));
    }
    if ( CHK_AVRO_ERR ) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    // operating channel bandwidth
    avro_value_get_by_name(&adrField, "blast_metrics", &drField, NULL);
    if ( CHK_AVRO_ERR ) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }
    avro_value_set_branch(&drField, 1, &optional);
    avro_value_get_by_name(&optional, "operating_channel_bandwidth", &drField, NULL);
    if ( CHK_AVRO_ERR ) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }
    avro_value_set_branch(&drField, 1, &optional);
    wifi_dbg_print(1, "RDK_LOG_DEBUG, operating_channel_bandwidth\tType: %d\n", avro_value_get_type(&optional));
    //Patch HAL values if necessary
    if ( monitor->curStepData.ApIndex < 0)
    {
        wifi_dbg_print(1, "RDK_LOG_DEBUG, operating_channel_bandwidth = \"%s\"\n", "set to NULL" );
        avro_value_set_null(&optional);
    }
    else
    {
        if ( strstr("20MHz", sta_data->sta_active_msmt_data[0].Operating_channelwidth))
        {
            wifi_dbg_print(1, "RDK_LOG_DEBUG, operating_channel_bandwidth = \"%s\"\n", "set to _20MHz" );
            avro_value_set_enum(&optional, avro_schema_enum_get_by_name(avro_value_get_schema(&optional), "_20MHz"));
        }
        else if ( strstr("40MHz", sta_data->sta_active_msmt_data[0].Operating_channelwidth) )
        {
            wifi_dbg_print(1, "RDK_LOG_DEBUG, operating_channel_bandwidth = \"%s\"\n", "set to _40MHz" );
            avro_value_set_enum(&optional, avro_schema_enum_get_by_name(avro_value_get_schema(&optional), "_40MHz"));
        }
        else if ( strstr("80MHz", sta_data->sta_active_msmt_data[0].Operating_channelwidth) )
        {
            wifi_dbg_print(1, "RDK_LOG_DEBUG, operating_channel_bandwidth = \"%s\"\n", "set to _80MHz" );
            avro_value_set_enum(&optional, avro_schema_enum_get_by_name(avro_value_get_schema(&optional), "_80MHz"));
        }
        else if ( strstr("160MHz", sta_data->sta_active_msmt_data[0].Operating_channelwidth) )
        {
            wifi_dbg_print(1, "RDK_LOG_DEBUG, operating_channel_bandwidth = \"%s\"\n", "set to _160MHz" );
            avro_value_set_enum(&optional, avro_schema_enum_get_by_name(avro_value_get_schema(&optional), "_160MHz"));
        }
    }
    if ( CHK_AVRO_ERR ) {
         wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }
#ifdef WIFI_HAL_VERSION_3
    radio_idx = getRadioIndexFromAp(monitor->curStepData.ApIndex);
#else
    radio_idx = (monitor->curStepData.ApIndex >= 0) ? (monitor->curStepData.ApIndex % 2) : 0;
#endif
    // channel #
    avro_value_get_by_name(&adrField, "blast_metrics", &drField, NULL);
    if ( CHK_AVRO_ERR ) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }
    avro_value_set_branch(&drField, 1, &optional);
    avro_value_get_by_name(&optional, "channel", &drField, NULL);
    if ( CHK_AVRO_ERR ) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }
    avro_value_set_branch(&drField, 1, &optional);
    if (monitor->curStepData.ApIndex >= 0)
    {
        wifi_dbg_print(1, "RDK_LOG_DEBUG, channel = %d\n", g_monitor->radio_data[radio_idx].primary_radio_channel);
        wifi_dbg_print(1, "RDK_LOG_DEBUG, channel\tType: %d\n", avro_value_get_type(&optional));
        avro_value_set_int(&optional, g_monitor->radio_data[radio_idx].primary_radio_channel);
    }
    else
    {
        wifi_dbg_print(1, "RDK_LOG_DEBUG, channel = 0\n");
        wifi_dbg_print(1, "RDK_LOG_DEBUG, channel\tType: %d\n", avro_value_get_type(&optional));
        avro_value_set_int(&optional, 0);
    }
    if ( CHK_AVRO_ERR ) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    // frequency band
    avro_value_get_by_name(&adrField, "blast_metrics", &drField, NULL);
    if ( CHK_AVRO_ERR ) {
       wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }
    avro_value_set_branch(&drField, 1, &optional);
    avro_value_get_by_name(&optional, "frequency_band", &drField, NULL);
    if ( CHK_AVRO_ERR ) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }
    avro_value_set_branch(&drField, 1, &optional);
    wifi_dbg_print(1, "RDK_LOG_DEBUG, frequency_band\tType: %d\n", avro_value_get_type(&optional));
    //Patch HAL values if necessary
    if (monitor->curStepData.ApIndex < 0)
    {
        wifi_dbg_print(1, "RDK_LOG_DEBUG, frequency_band set to NULL\n");
        avro_value_set_null(&optional);
    }
    else
    {
        if (strstr("2.4GHz", g_monitor->radio_data[radio_idx].frequency_band))
        {
            wifi_dbg_print(1, "RDK_LOG_DEBUG, frequency_band = \"%s\"\n", "2.4GHz, set to _2_4GHz" );
            avro_value_set_enum(&optional, avro_schema_enum_get_by_name(avro_value_get_schema(&optional), "_2_4GHz" ));
        }
        else if (strstr("5GHz", g_monitor->radio_data[radio_idx].frequency_band))
        {
            wifi_dbg_print(1, "RDK_LOG_DEBUG, frequency_band = \"%s\"\n", "5GHz, set to _5GHz" );
            avro_value_set_enum(&optional, avro_schema_enum_get_by_name(avro_value_get_schema(&optional), "_5GHz" ));
        }
        else if (strstr("6GHz", g_monitor->radio_data[radio_idx].frequency_band))
        {
            wifi_dbg_print(1, "RDK_LOG_DEBUG, frequency_band = \"%s\"\n", "6GHz, set to _6GHz" );
            avro_value_set_enum(&optional, avro_schema_enum_get_by_name(avro_value_get_schema(&optional), "_6GHz" ));
        }
    }
    if ( CHK_AVRO_ERR ) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    //tx_retransmissions
    avro_value_get_by_name(&adrField, "blast_metrics", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);
    avro_value_get_by_name(&optional, "tx_retransmissions", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);
    if ((monitor != NULL) && (sta_data != NULL))
    {
        avro_value_set_int(&optional, (sta_data->sta_active_msmt_data[Count-1].ReTransmission - sta_data->sta_active_msmt_data[0].ReTransmission));
    }
    else
    {
        avro_value_set_int(&optional, 0);
    }
    wifi_dbg_print(1, "RDK_LOG_DEBUG, tx_retransmissions = %d\n",(sta_data->sta_active_msmt_data[Count-1].ReTransmission - sta_data->sta_active_msmt_data[0].ReTransmission));

    //max_tx_rate
    avro_value_get_by_name(&adrField, "blast_metrics", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);
    avro_value_get_by_name(&optional, "max_tx_rate", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);
    if ((monitor != NULL) && (sta_data != NULL))
    {
        avro_value_set_int(&optional, sta_data->sta_active_msmt_data[0].MaxTxRate);
    }
    else
    {
        avro_value_set_int(&optional, 0);
    }
    wifi_dbg_print(1, "RDK_LOG_DEBUG, maximum TX rate = %d\n",sta_data->sta_active_msmt_data[0].MaxTxRate);

    //max_rx_rate
    avro_value_get_by_name(&adrField, "blast_metrics", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);
    avro_value_get_by_name(&optional, "max_rx_rate", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);
    if ((monitor != NULL) && (sta_data != NULL))
    {
        avro_value_set_int(&optional, sta_data->sta_active_msmt_data[0].MaxRxRate);
    }
    else
    {
        avro_value_set_int(&optional, 0);
    }
    wifi_dbg_print(1, "RDK_LOG_DEBUG, maximum RX rate = %d\n",sta_data->sta_active_msmt_data[0].MaxRxRate);

    //Array of device reports
    avro_value_get_by_name(&adrField, "blast_metrics", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);
    avro_value_get_by_name(&optional, "BlastMetricsArrayOfReadings", &drField, NULL);
    wifi_dbg_print(1, "RDK_LOG_DEBUG, BlastMetricsArrayOfReading\tType: %d\n", avro_value_get_type(&adrField));
    if ( CHK_AVRO_ERR ) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    //Device Report for wifi-blaster array of readings
    avro_value_t dr = {0};
    avro_value_t bdrField = {0}; //Used for array readings per blast

    for (sampleCount = 0; sampleCount < Count; sampleCount++)
    {
        avro_value_append(&drField, &dr, NULL);

        wifi_dbg_print(1, "%s : %d count = %d signal_strength= %d\n",__func__,__LINE__,sampleCount,sta_data->sta_active_msmt_data[sampleCount].rssi);
        avro_value_get_by_name(&dr, "signal_strength", &bdrField, NULL);
        if ( CHK_AVRO_ERR ) {
             wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
        avro_value_set_branch(&bdrField, 1, &optional);
        if ( CHK_AVRO_ERR ) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
        avro_value_set_int(&optional, (int)sta_data->sta_active_msmt_data[sampleCount].rssi);
        if ( CHK_AVRO_ERR ) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }

        //snr
        wifi_dbg_print(1, "%s : %d count = %d snr= %d\n",__func__,__LINE__,sampleCount,sta_data->sta_active_msmt_data[sampleCount].SNR);
        avro_value_get_by_name(&dr, "snr", &bdrField, NULL);
        if ( CHK_AVRO_ERR ) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
        avro_value_set_branch(&bdrField, 1, &optional);
        if ( CHK_AVRO_ERR ) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
        avro_value_set_int(&optional, (int)sta_data->sta_active_msmt_data[sampleCount].SNR);
        if ( CHK_AVRO_ERR ) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }

        //tx_phy_rate
        wifi_dbg_print(1, "%s : %d count = %d tx_phy_rate = %d\n",__func__,__LINE__,sampleCount,sta_data->sta_active_msmt_data[sampleCount].TxPhyRate);
        avro_value_get_by_name(&dr, "tx_phy_rate", &bdrField, NULL);
        if ( CHK_AVRO_ERR ) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
        avro_value_set_branch(&bdrField, 1, &optional);
        if ( CHK_AVRO_ERR ) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
        avro_value_set_int(&optional, (int)sta_data->sta_active_msmt_data[sampleCount].TxPhyRate );
        if ( CHK_AVRO_ERR ) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }

        //rx_phy_rate
        wifi_dbg_print(1, "%s : %d count = %d rx_phy_rate = %d\n",__func__,__LINE__,sampleCount,sta_data->sta_active_msmt_data[sampleCount].RxPhyRate);
        avro_value_get_by_name(&dr, "rx_phy_rate", &bdrField, NULL);
        if ( CHK_AVRO_ERR ) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
        avro_value_set_branch(&bdrField, 1, &optional);
        if ( CHK_AVRO_ERR ) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
        avro_value_set_int(&optional, (int)sta_data->sta_active_msmt_data[sampleCount].RxPhyRate);
        if ( CHK_AVRO_ERR ) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }

        //throughput
        wifi_dbg_print(1, "%s : %d count = %d throughput = %lf\n",__func__,__LINE__,sampleCount,sta_data->sta_active_msmt_data[sampleCount].throughput);
        avro_value_get_by_name(&dr, "throughput", &bdrField, NULL);
        if ( CHK_AVRO_ERR ) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
        avro_value_set_branch(&bdrField, 1, &optional);
        if ( CHK_AVRO_ERR ) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
        avro_value_set_float(&optional, (float)sta_data->sta_active_msmt_data[sampleCount].throughput);
        if ( CHK_AVRO_ERR ) {
            wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
        }
    }

    avro_value_t cpu_dr = {0};
    avro_value_get_by_name(&adrField, "blast_metrics", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);

    // CPU health metrics
    wifi_dbg_print(1, "%s:%d: WiFiBlasterCPUMetrics\n", __func__, __LINE__, avro_strerror());
    avro_value_get_by_name(&optional, "WiFiBlasterCPUMetrics", &drField, NULL);
    avro_value_set_branch(&drField, 1, &cpu_dr);
    if ( CHK_AVRO_ERR  ) {
         wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }
    wifi_dbg_print(1, "%s:%d: CPU Usage : 0\n", __func__, __LINE__, avro_strerror());
    avro_value_get_by_name(&cpu_dr, "CPU_Usage", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);
    avro_value_set_int(&optional, 0);
    if ( CHK_AVRO_ERR  ) {
         wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    wifi_dbg_print(1, "%s:%d: Memory Usage : 0\n", __func__, __LINE__, avro_strerror());
    avro_value_get_by_name(&cpu_dr, "Memory_Usage", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);
    avro_value_set_long(&optional, 0);
    if ( CHK_AVRO_ERR  ) {
         wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    wifi_dbg_print(1, "%s:%d: Load Average : 0.0\n", __func__, __LINE__, avro_strerror());
    avro_value_get_by_name(&cpu_dr, "Load_Average", &drField, NULL);
    avro_value_set_branch(&drField, 1, &optional);
    avro_value_set_long(&optional, 0);
    if ( CHK_AVRO_ERR  ) {
         wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    /* free the sta_data->sta_active_msmt_data allocated memory */
    if (sta_data->sta_active_msmt_data != NULL)
    {
        wifi_dbg_print(1, "%s : %d memory freed for sta_active_msmt_data\n",__func__,__LINE__);
        free(sta_data->sta_active_msmt_data);
        sta_data->sta_active_msmt_data = NULL;
    }

    /* free the sta_data allocated memory for offline clients and remove from hash map*/
    if ( monitor->curStepData.ApIndex < 0)
    {
        if (sta_data != NULL)
        {
            pthread_mutex_lock(&g_monitor->lock);
            sta_del = (sta_data_t *) hash_map_remove(bssid_info->sta_map, to_sta_key(sta_data->sta_mac, sta_key));
            pthread_mutex_unlock(&g_monitor->lock);
            if (sta_del != NULL)
            {
                wifi_dbg_print(1, "%s : %d removed offline client %s from sta_map\n",__func__,__LINE__, sta_del->sta_mac);
            }
            free(sta_data);
            sta_data = NULL;
        }
    }

    /* check for writer size, if buffer is almost full, skip trailing linklist */
    avro_value_sizeof(&adr, (size_t*)&size);
    if (CHK_AVRO_ERR) {
        wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    //Thats the end of that
    wifi_dbg_print(1, "%s:%d: Writing the avro values \n", __func__, __LINE__);
    avro_value_write(writer, &adr);
    if (CHK_AVRO_ERR) {
         wifi_dbg_print(1, "%s:%d: Avro error: %s\n", __func__, __LINE__, avro_strerror());
    }

    wifi_dbg_print(1, "AVRO packing done\n");
    CcspWifiTrace(("RDK_LOG_INFO, %s-%d AVRO packing done\n", __FUNCTION__, __LINE__));
#if 0
    char *json;
    if (!avro_value_to_json(&adr, 1, &json))
    {
        wifi_dbg_print(1,"json is %s\n", json);
        free(json);
    }
#endif
    //Free up memory
    avro_value_decref(&adr);
    avro_writer_free(writer);

    size += MAGIC_NUMBER_SIZE + SCHEMA_ID_LENGTH;
    sendWebpaMsg((char *)(serviceName),  (char *)(dest), trans_id, (char *)(contentType), buff, size);
    wifi_dbg_print(1, "Creation of Telemetry record is successful\n");
    CcspWifiTrace(("RDK_LOG_INFO, %s-%d Blaster report successfully sent to Parodus WebPA component\n", __FUNCTION__, __LINE__));

}

/* This function calls the ODP streamer function with station and radio data.
   If ActiveMsmtFlag is true then the streamer for active measurement is called or
   streamer for instant measurement has been triggered.
*/

void stream_client_msmt_data(bool ActiveMsmtFlag)
{
	wifi_monitor_t *monitor;
        wifi_actvie_msmt_t *act_monitor;
	hash_map_t  *sta_map;
	sta_data_t *data;
	mac_addr_str_t key;
        int ap_index = 0;

	monitor = get_wifi_monitor();
        act_monitor = (wifi_actvie_msmt_t *)get_active_msmt_data();

        if (!ActiveMsmtFlag)
        {
            sta_map = monitor->bssid_data[monitor->inst_msmt.ap_index].sta_map;
            to_sta_key(monitor->inst_msmt.sta_mac, key);

            data = (sta_data_t *)hash_map_get(sta_map, key);
            if (data != NULL) {
                upload_single_client_msmt_data(&monitor->bssid_data[monitor->inst_msmt.ap_index], data);
             }
        }
        else
        {
            ap_index = (act_monitor->curStepData.ApIndex < 0) ? 0 : act_monitor->curStepData.ApIndex;
            sta_map = monitor->bssid_data[ap_index].sta_map;
            to_sta_key(act_monitor->curStepData.DestMac, key);

            data = (sta_data_t *)hash_map_get(sta_map, key);
            if (data != NULL) {
                upload_single_client_active_msmt_data(&monitor->bssid_data[ap_index], data);
            }
        }
}
