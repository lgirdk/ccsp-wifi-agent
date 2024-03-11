/*
 * If not stated otherwise in this file or this component's LICENSE file the
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

#ifdef __GNUC__
#if (!defined _BUILD_ANDROID) && (!defined _NO_EXECINFO_H_)
#include <execinfo.h>
#endif
#endif

#include "ssp_global.h"
#include "stdlib.h"
#include "ccsp_dm_api.h"
#include "ccsp_WifiLog_wrapper.h"
#include "syscfg/syscfg.h"
#include "telemetry_busmessage_sender.h"
#include "secure_wrapper.h"
#include "safec_lib_common.h"

#if defined (FEATURE_SUPPORT_WEBCONFIG)
#include "webconfig_framework.h"
#endif

#include <semaphore.h>
#include <fcntl.h>

#ifdef INCLUDE_BREAKPAD
#include "breakpad_wrapper.h"
#endif
#include "print_uptime.h"
#include <sys/sysinfo.h>

#define DEBUG_INI_NAME  "/etc/debug.ini"
#if defined (_CBR_PRODUCT_REQ_) || ( defined (INTEL_PUMA7) && !defined (_XB7_PRODUCT_REQ_) )|| (defined (_XB6_PRODUCT_REQ_) && defined (_COSA_BCM_ARM_))
#include "cap.h"
static cap_user appcaps;
#endif

#if defined(_PUMA6_ATOM_)
bool migration_to_mng = FALSE;
#endif

PDSLH_CPE_CONTROLLER_OBJECT     pDslhCpeController      = NULL;
PCOMPONENT_COMMON_DM            g_pComponent_Common_Dm  = NULL;
char                            g_Subsystem[32]         = {0};
PCCSP_COMPONENT_CFG             gpWifiStartCfg           = NULL;
PCCSP_FC_CONTEXT                pWifiFcContext           = (PCCSP_FC_CONTEXT           )NULL;
PCCSP_CCD_INTERFACE             pWifiCcdIf               = (PCCSP_CCD_INTERFACE        )NULL;
PCCC_MBI_INTERFACE              pWifiMbiIf               = (PCCC_MBI_INTERFACE         )NULL;
BOOL                            g_bActive               = FALSE;
int gChannelSwitchingCount = 0;

    sem_t *sem;

#if defined(_COSA_INTEL_USG_ATOM_)
void _get_shell_output(char * cmd, char * out, int len)
{
    FILE * fp;
    char   buf[256] = {0};
    fp = popen(cmd, "r");
    if (fp)
    {
        while( fgets(buf, sizeof(buf), fp) )
        {
            buf[strcspn(buf, "\r\n")] = 0;
            if( buf[0] != '\0' )
            {
                strncpy(out, buf, len-1);
                break;
            }
        }
        pclose(fp);
    }
}
#endif

#if 0
void* getSyscfgLogLevel( void *arg )
{
    pthread_detach(pthread_self());
	UNREFERENCED_PARAMETER(arg);
#if defined(_COSA_INTEL_USG_ATOM_) && !defined (_XB6_PRODUCT_REQ_)
    {
        char out[8];

        out[0] = '\0';
        _get_shell_output ("/usr/bin/rpcclient2 \"syscfg get X_RDKCENTRAL-COM_LoggerEnable\" | grep -v RPC", out, sizeof(out));
        RDKLogEnable = (BOOL)atoi(out);

        out[0] = '\0';
        _get_shell_output ("/usr/bin/rpcclient2 \"syscfg get X_RDKCENTRAL-COM_LogLevel\" | grep -v RPC", out, sizeof(out));
        RDKLogLevel = (ULONG)atoi(out);

        out[0] = '\0';
        _get_shell_output ("/usr/bin/rpcclient2 \"syscfg get X_RDKCENTRAL-COM_WiFi_LogLevel\" | grep -v RPC", out, sizeof(out));
        WiFi_RDKLogLevel = (ULONG)atoi(out);

        out[0] = '\0';
        _get_shell_output ("/usr/bin/rpcclient2 \"syscfg get X_RDKCENTRAL-COM_WiFi_LoggerEnable\" | grep -v RPC", out, sizeof(out));
        WiFi_RDKLogEnable = (BOOL)atoi(out);
        
        CcspTraceInfo(("WIFI_DBG:-------Log Info values from arm RDKLogEnable:%d,RDKLogLevel:%u,WiFi_RDKLogLevel:%u,WiFi_RDKLogEnable:%d\n",RDKLogEnable,RDKLogLevel,WiFi_RDKLogLevel, WiFi_RDKLogEnable ));
    }
#else
    CcspTraceInfo(("WIFI_DBG:-------Read Log Info\n"));
    char buffer[5];
    if( 0 == syscfg_get( NULL, "X_RDKCENTRAL-COM_LoggerEnable" , buffer, sizeof( buffer ) ) &&  ( buffer[0] != '\0' ) )
    {
        RDKLogEnable = (BOOL)atoi(buffer);
    }
    if( 0 == syscfg_get( NULL, "X_RDKCENTRAL-COM_LogLevel" , buffer, sizeof( buffer ) ) &&  ( buffer[0] != '\0' ) )
    {
        RDKLogLevel = (ULONG )atoi(buffer);
    }
    if( 0 == syscfg_get( NULL, "X_RDKCENTRAL-COM_WiFi_LogLevel" , buffer, sizeof( buffer ) ) &&  ( buffer[0] != '\0' ) )
    {
        WiFi_RDKLogLevel = (ULONG)atoi(buffer);
    }
    if( 0 == syscfg_get( NULL, "X_RDKCENTRAL-COM_WiFi_LoggerEnable" , buffer, sizeof( buffer ) ) &&  ( buffer[0] != '\0' ) )
    {
        WiFi_RDKLogEnable = (BOOL)atoi(buffer);
    }
    CcspTraceInfo(("WIFI_DBG:-------Log Info values RDKLogEnable:%d,RDKLogLevel:%u,WiFi_RDKLogLevel:%u,WiFi_RDKLogEnable:%d\n",RDKLogEnable,RDKLogLevel,WiFi_RDKLogLevel, WiFi_RDKLogEnable ));
#endif
    return NULL;
}
#endif
int  cmd_dispatch(int  command)
{
    char*                           pParamNames[]      = {"Device.X_CISCO_COM_DDNS."};
    parameterValStruct_t**          ppReturnVal        = NULL;
    int                             ulReturnValCount   = 0;
    int                             i                  = 0;
    errno_t                         rc                 = -1;

    switch ( command )
    {
            case    'e' :

                CcspTraceInfo(("Connect to bus daemon...\n"));

            {
                char                            CName[256];

                rc = sprintf_s(CName , sizeof(CName) ,"%s%s", g_Subsystem, gpWifiStartCfg->ComponentId);
                if(rc < EOK)
                {
                    ERR_CHK(rc);
                }

                ssp_WifiMbi_MessageBusEngage
                    (
                        CName,
                        CCSP_MSG_BUS_CFG,
                        gpWifiStartCfg->DbusPath
                    );
            }


                ssp_create_wifi(gpWifiStartCfg);
                ssp_engage_wifi(gpWifiStartCfg);

                g_bActive = TRUE;

                CcspTraceInfo(("Wifi Agent loaded successfully...\n"));

            break;

            case    'r' :

            CcspCcMbi_GetParameterValues
                (
                    DSLH_MPA_ACCESS_CONTROL_ACS,
                    pParamNames,
                    1,
                    &ulReturnValCount,
                    &ppReturnVal,
                    NULL
                );



            for ( i = 0; i < ulReturnValCount; i++ )
            {
                CcspTraceWarning(("Parameter %d name: %s value: %s \n", i+1, ppReturnVal[i]->parameterName, ppReturnVal[i]->parameterValue));
            }

			break;

        case    'm':
                AnscPrintComponentMemoryTable(pComponentName);

                break;

        case    't':
                AnscTraceMemoryTable();

                break;

        case    'c':
                ssp_cancel_wifi(gpWifiStartCfg);

                break;

        default:
            break;
    }

    return 0;
}

static void _print_stack_backtrace(void)
{
#ifdef __GNUC__
#if (!defined _BUILD_ANDROID) && (!defined _NO_EXECINFO_H_)
        void* tracePtrs[100];
        char** funcNames = NULL;
        int i, count = 0;

        count = backtrace( tracePtrs, 100 );
        backtrace_symbols_fd( tracePtrs, count, 2 );

        funcNames = backtrace_symbols( tracePtrs, count );

        if ( funcNames ) {
            // Print the stack trace
            for( i = 0; i < count; i++ )
                printf("%s\n", funcNames[i] );

            // Free the string pointers
            free( funcNames );
        }
#endif
#endif
}

static void daemonize(void) {

	/* initialize semaphores for shared processes */
	sem = sem_open ("pSemCcspWifi", O_CREAT | O_EXCL, 0644, 0);
	if(SEM_FAILED == sem)
	{
	       AnscTrace("Failed to create semaphore %d - %s\n", errno, strerror(errno));
	       _exit(1);
	}
	/* name of semaphore is "pSemCcspWifi", semaphore is reached using this name */
	sem_unlink ("pSemCcspWifi");
	/* unlink prevents the semaphore existing forever */
	/* if a crash occurs during the execution         */
	AnscTrace("Semaphore initialization Done!!\n");

	switch (fork()) {
	case 0:
		break;
	case -1:
		// Error
		CcspTraceInfo(("Error daemonizing (fork)! %d - %s\n", errno, strerror(
				errno)));
		exit(0);
		break;
	default:
		sem_wait (sem);
		sem_close (sem);
		_exit(0);
	}

	if (setsid() < 	0) {
		CcspTraceInfo(("Error demonizing (setsid)! %d - %s\n", errno, strerror(errno)));
		exit(0);
	}

    /*
     *  What is the point to change current directory?
     *
    chdir("/");
     */

#ifndef  _DEBUG
	int fd;
	fd = open("/dev/null", O_RDONLY);
	if (fd != 0) {
		dup2(fd, 0);
		close(fd);
	}
	fd = open("/dev/null", O_WRONLY);
	if (fd != 1) {
		dup2(fd, 1);
		close(fd);
	}
	fd = open("/dev/null", O_WRONLY);
	if (fd != 2) {
		dup2(fd, 2);
		close(fd);
	}
#endif
}

void sig_handler(int sig)
{

    if ( sig == SIGINT ) {
    	signal(SIGINT, sig_handler); /* reset it to this function */
    	CcspTraceInfo(("SIGINT received!\n"));
        exit(0);
    }
    else if ( sig == SIGUSR1 ) {
    	signal(SIGUSR1, sig_handler); /* reset it to this function */
    	CcspTraceInfo(("SIGUSR1 received!\n"));
    }
    else if ( sig == SIGUSR2 ) {
    	CcspTraceInfo(("SIGUSR2 received!\n"));
    }
    else if ( sig == SIGCHLD ) {
    	signal(SIGCHLD, sig_handler); /* reset it to this function */
    	CcspTraceInfo(("SIGCHLD received!\n"));
    }
    else if ( sig == SIGPIPE ) {
    	signal(SIGPIPE, sig_handler); /* reset it to this function */
    	CcspTraceInfo(("SIGPIPE received!\n"));
    }
	else if ( sig == SIGALRM ) {

    	signal(SIGALRM, sig_handler); /* reset it to this function */
    	CcspTraceInfo(("SIGALRM received!\n"));
		gChannelSwitchingCount = 0;
    }
    else if ( sig == SIGTERM )
    {
        CcspTraceInfo(("SIGTERM received!\n"));
        exit(0);
    }
    else if ( sig == SIGKILL )
    {
        CcspTraceInfo(("SIGKILL received!\n"));
        exit(0);
    }
    else {
    	/* get stack trace first */
    	_print_stack_backtrace();
    	CcspTraceInfo(("Signal %d received, exiting!\n", sig));
    	exit(0);
    }
}

#ifndef INCLUDE_BREAKPAD
static int is_core_dump_opened(void)
{
    FILE *fp;
    char path[256];
    char line[1024];
    char *start, *tok, *sp;
#define TITLE   "Max core file size"

    snprintf(path, sizeof(path), "/proc/%d/limits", getpid());
    if ((fp = fopen(path, "rb")) == NULL)
        return 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        if ((start = strstr(line, TITLE)) == NULL)
            continue;

        start += strlen(TITLE);
        if ((tok = strtok_r(start, " \t\r\n", &sp)) == NULL)
            break;

        fclose(fp);

        if (strcmp(tok, "0") == 0)
            return 0;
        else
            return 1;
    }

    fclose(fp);
    return 0;
}
#endif


#if defined (_CBR_PRODUCT_REQ_) || ( defined (INTEL_PUMA7) && !defined (_XB7_PRODUCT_REQ_) ) || (defined (_XB6_PRODUCT_REQ_) && defined (_COSA_BCM_ARM_))
static bool drop_root()
{
  appcaps.caps = NULL;
  appcaps.user_name = NULL;
  bool retval = false;
  bool ret = false;
  ret = isBlocklisted();
  if(ret)
  {
    CcspTraceInfo(("NonRoot feature is disabled\n"));
  }
  else
  {
    CcspTraceInfo(("NonRoot feature is enabled, dropping root privileges for CcspWiFiAgent process\n"));
    if(init_capability() != NULL) {
      if(drop_root_caps(&appcaps) != -1) {
        if(update_process_caps(&appcaps) != -1) {
          read_capability(&appcaps);
          retval = true;
        }
      }
    }
  }
  return retval;
}
#endif

int main(int argc, char* argv[])
{
    int                             cmdChar            = 0;
    BOOL                            bRunAsDaemon       = TRUE;
    int                             idx                = 0;
    FILE                           *fd                 = NULL;
    DmErr_t                         err;
    char                            *subSys            = NULL;

    extern ANSC_HANDLE bus_handle;

    errno_t                         rc                 = -1;
    
    
    // Buffer characters till newline for stdout and stderr
    setlinebuf(stdout);
    setlinebuf(stderr);

    /*
     *  Load the start configuration
     */
#if defined(FEATURE_SUPPORT_RDKLOG)
        RDK_LOGGER_INIT();
#endif

    gpWifiStartCfg = (PCCSP_COMPONENT_CFG)AnscAllocateMemory(sizeof(CCSP_COMPONENT_CFG));
    if ( gpWifiStartCfg )
    {
        CcspComponentLoadCfg("/usr/ccsp/wifi/CcspWifi.cfg", gpWifiStartCfg);
    }
    else
    {
        printf("Insufficient resources for start configuration, quit!\n");
        exit(1);
    }

    /* Set the global pComponentName */
    pComponentName = gpWifiStartCfg->ComponentName;

#if defined(_DEBUG) && defined(_COSA_SIM_)
    AnscSetTraceLevel(CCSP_TRACE_LEVEL_INFO);
#endif

    for (idx = 1; idx < argc; idx++)
    {
        if ( (strcmp(argv[idx], "-subsys") == 0) )
        {
            if ((idx+1) < argc)
            {
                rc = strcpy_s(g_Subsystem, sizeof(g_Subsystem), argv[idx+1]);
                ERR_CHK(rc);
                CcspTraceWarning(("\nSubsystem is %s\n", g_Subsystem));
            }
            else
            {
                CcspTraceError(("Argument missing after -subsys\n"));
            }
        }
        else if ( strcmp(argv[idx], "-c" ) == 0 )
        {
            bRunAsDaemon = FALSE;
        }
    }

  #if defined (_CBR_PRODUCT_REQ_) || ( defined (INTEL_PUMA7) && !defined (_XB7_PRODUCT_REQ_) ) || (defined (_XB6_PRODUCT_REQ_) && defined (_COSA_BCM_ARM_)) //Applicable only for TCHCBR, TCHXB6 & TCHXB7
    if(!drop_root())
    {
        CcspTraceError(("drop_root function failed!\n"));
        gain_root_privilege();
    }
  #endif
    if ( bRunAsDaemon )
        daemonize();

/* Legacy Devices Like XB3 have systemd on the side with WiFi Agent, but don't use Service Files */
#if defined(ENABLE_SD_NOTIFY) && (defined(_XB6_PRODUCT_REQ_) || defined(_COSA_BCM_MIPS_)|| defined(_COSA_BCM_ARM_) || defined(_PLATFORM_TURRIS_))
    char cmd[1024]          = {0};
    /*This is used for systemd */
    fd = fopen("/var/tmp/CcspWifiAgent.pid", "w+");
    if ( !fd )
    {
        CcspTraceWarning(("Create /var/tmp/CcspWifiAgent.pid error. \n"));
        return 1;
    }
    else
    {
        rc = sprintf_s(cmd, sizeof(cmd), "%d", getpid());
        if(rc < EOK)
        {
            ERR_CHK(rc);
        }
        fputs(cmd, fd);
        fclose(fd);
    }
#endif

#ifdef INCLUDE_BREAKPAD
    breakpad_ExceptionHandler();
    signal(SIGUSR1, sig_handler);
#else

    if (is_core_dump_opened())
    {
        signal(SIGUSR1, sig_handler);
        CcspTraceWarning(("Core dump is opened, do not catch signal\n"));
    }
    else
    {
        CcspTraceWarning(("Core dump is NOT opened, backtrace if possible\n"));
    	signal(SIGTERM, sig_handler);
    	signal(SIGINT, sig_handler);
    	signal(SIGUSR1, sig_handler);
    	signal(SIGUSR2, sig_handler);

    	signal(SIGSEGV, sig_handler);
    	signal(SIGBUS, sig_handler);
    	signal(SIGKILL, sig_handler);
    	signal(SIGFPE, sig_handler);
    	signal(SIGILL, sig_handler);
    	signal(SIGQUIT, sig_handler);
    	signal(SIGHUP, sig_handler);
	signal(SIGALRM, sig_handler);
    }

#endif

    t2_init("ccsp-wifi-agent");

    /* Default handling of SIGCHLD signals */
    if (signal(SIGCHLD, SIG_DFL) == SIG_ERR)
    {
        CcspTraceError(("ERROR: Couldn't set SIGCHLD handler!\n"));
    }
    cmd_dispatch('e');
    // printf("Calling Docsis\n");

    // ICC_init();
    // DocsisIf_StartDocsisManager();
    /* For XB3, there are  4 rpc calls to arm to get loglevel values from syscfg
     * this takes around 7 to 10 seconds, so we are moving this to a thread */

#ifdef _COSA_SIM_
    subSys = "";        /* PC simu use empty string as subsystem */
#else
    subSys = NULL;      /* use default sub-system */
#endif
    err = Cdm_Init(bus_handle, subSys, NULL, NULL, pComponentName);
    if (err != CCSP_SUCCESS)
    {
        fprintf(stderr, "Cdm_Init: %s\n", Cdm_StrError(err));
        exit(1);
    }

#if defined (FEATURE_SUPPORT_WEBCONFIG)
    /* Inform Webconfig framework if component is coming after crash */
    check_component_crash("/tmp/wifi_initialized");
#endif

    /* For some reason, touching the file via system command was not working consistently.
     * We'll fopen the file and dump in a value */
    if ((fd = fopen ("/tmp/wifi_initialized", "w+")) != NULL) {
        fprintf(fd,"1");
        fclose(fd);
    }

#if defined(_PUMA6_ATOM_)
    if (access("/tmp/migration_to_mng", F_OK) == 0) {
        unlink("/tmp/migration_to_mng");
        migration_to_mng=TRUE;
    }
    else if (access("/tmp/cbn_mv1_to_mng", F_OK) == 0) {
        unlink("/tmp/cbn_mv1_to_mng");
        migration_to_mng=TRUE;
    }

    //Passing wifi initialized status
    system("rpcclient2 'touch /tmp/wifi_initialized'");

    //Trigerring radius relay start after wifi_initialized
    system("rpcclient2 'sysevent set radiusrelay-start'");
#endif

    char*                           pParamNames[]      = {"Device.WiFi.AccessPoint.7.X_LGI-COM_ActiveTimeout"};
    parameterValStruct_t**          ppReturnVal        = NULL;
    parameterInfoStruct_t**         ppReturnValNames   = NULL;
    parameterAttributeStruct_t**    ppReturnvalAttr    = NULL;
    ULONG                           ulReturnValCount   = 0;

    //get the timeout parameter
    CcspCcMbi_GetParameterValues(
        DSLH_MPA_ACCESS_CONTROL_ACS,
        pParamNames,
        1,
        &ulReturnValCount,
        &ppReturnVal,
        NULL);

    if (ulReturnValCount > 0 &&
        strlen(ppReturnVal[0]->parameterValue) > 0)
    {
        int iMin, iHour, iDay, iMonth, iYear, iGnIndex24=7, iGnIndex50=8;
        char strCronCmd[256];

        //remove the current entry from crontab, if any
#if defined(_LG_MV1_CELENO_) || defined(_LG_MV1_QCA_)
        system("rpcclient2 'sed -i \"/Device.WiFi.SSID./d\" /var/spool/cron/crontabs/root'");
        system("rpcclient2 'sed -i \'/Device.WiFi.Radio./d\' /var/spool/cron/crontabs/root'");
#else
        system("sed -i '/Device.WiFi.SSID./d' /var/spool/cron/crontabs/root");
        system("sed -i '/Device.WiFi.Radio./d' /var/spool/cron/crontabs/root");
#endif

        //setup the crontab entry
        sscanf(ppReturnVal[0]->parameterValue,"%d/%d/%d-%d:%d", &iDay, &iMonth, &iYear, &iHour, &iMin);
#if defined(_LG_MV1_CELENO_) || defined(_LG_MV1_QCA_)
        snprintf (strCronCmd, sizeof(strCronCmd), "rpcclient2 'echo \"%d %d %d %d * dmcli eRT setvalue Device.WiFi.SSID.%d.Enable bool false; dmcli eRT setvalue Device.WiFi.Radio.1.X_CISCO_COM_ApplySetting bool true\" >> /var/spool/cron/crontabs/root'",
            iMin, iHour, iDay, iMonth, iGnIndex24);
#else
        sprintf (strCronCmd, "echo '%d %d %d %d * dmcli eRT setvalue Device.WiFi.SSID.%d.Enable bool false; dmcli eRT setvalue Device.WiFi.Radio.1.X_CISCO_COM_ApplySetting bool true' >> /var/spool/cron/crontabs/root",
            iMin, iHour, iDay, iMonth, iGnIndex24);
#endif

        //add the crontab entry
        system(strCronCmd);

        //setup the next crontab entry
#if defined(_LG_MV1_CELENO_) || defined(_LG_MV1_QCA_)
        snprintf (strCronCmd, sizeof(strCronCmd), "rpcclient2 'echo \"%d %d %d %d * dmcli eRT setvalue Device.WiFi.SSID.%d.Enable bool false; dmcli eRT setvalue Device.WiFi.Radio.2.X_CISCO_COM_ApplySetting bool true\" >> /var/spool/cron/crontabs/root'",
            iMin, iHour, iDay, iMonth, iGnIndex50);
#else
        sprintf (strCronCmd, "echo '%d %d %d %d * dmcli eRT setvalue Device.WiFi.SSID.%d.Enable bool false; dmcli eRT setvalue Device.WiFi.Radio.2.X_CISCO_COM_ApplySetting bool true' >> /var/spool/cron/crontabs/root",
            iMin, iHour, iDay, iMonth, iGnIndex50);
#endif

        //add the crontab entry
        system(strCronCmd);
    }
    free_parameterValStruct_t(bus_handle, ulReturnValCount, ppReturnVal);

    printf("Entering Wifi loop\n");
    struct sysinfo l_sSysInfo;
    sysinfo(&l_sSysInfo);
    char uptime[16] = {0};
    snprintf(uptime, sizeof(uptime), "%ld", l_sSysInfo.uptime);
    print_uptime("boot_to_WIFI_uptime",NULL, uptime);
    CcspTraceWarning(("RDKB_SYSTEM_BOOT_UP_LOG : Entering Wifi loop \n"));
    if ( bRunAsDaemon )
    {
        sem_post (sem);
        sem_close(sem);
        while(1)
        {
            sleep(30);
        }
    }
    else
    {
        while ( cmdChar != 'q' )
        {
            cmdChar = getchar();

            cmd_dispatch(cmdChar);
        }
    }

    err = Cdm_Term();
    if (err != CCSP_SUCCESS)
    {
        fprintf(stderr, "Cdm_Term: %s\n", Cdm_StrError(err));
        exit(1);
    }

    if ( g_bActive )
    {
        if(ANSC_STATUS_SUCCESS != ssp_cancel_wifi(gpWifiStartCfg))
            return -1;

        g_bActive = FALSE;
    }

    return 0;
}


