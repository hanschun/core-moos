#include "Antler.h"

using namespace std;
#include <sstream>

#define DEBUG_LAUNCH 0
CAntler::CAntler()
{
	m_JobLock.UnLock();    
    m_bNewJob = false;
    m_sAntlerName = "Monach";
    m_sDBHost = "localhost";
    m_nDBPort = 9000;
    m_bQuitCurrentJob = false;
}


bool QuickXTest()
{
    //make a child process
    pid_t pid = fork();
    if(pid<0)
    {
        //hell!
        MOOSTrace("fork failed, not good\n");
        return false;
    }
    else if(pid ==0)
    {
        MOOSTrace("child");
        return true;
    }
    else
    {
        MOOSTrace("parent");
        return true;
    }
    
}

bool CAntler::Run(const std::string &  sMissionFile)
{
    m_bHeadless = false;
    m_sMissionFile = sMissionFile;
    //return QuickXTest();
    return Spawn(m_sMissionFile);
}

bool CAntler::Run(const std::string & sHost,  int nPort, const std::string & sAntlerName)
{
    //this is more interesting...
    m_sAntlerName = sAntlerName;
    m_sDBHost = sHost;
    m_nDBPort = nPort;
    m_bHeadless = true;
    
    MOOSTrace("This is headless Antler called \"%s\" waiting for direction from %s:%d\n",m_sAntlerName.c_str(),m_sDBHost.c_str(),m_nDBPort);
    
       //start a monitoring thread
    m_RemoteControlThread.Initialise(_RemoteControlCB, this);
    m_RemoteControlThread.Start();
    

    while(1)
    {
        //wait to be signalled that there is work to do...
        
        while(!m_bNewJob)
            MOOSPause(100);
    	
        //no more launching until this community is complete
        m_JobLock.Lock();
        Spawn(m_sReceivedMissionFile,true);
        m_bNewJob = false;
        m_JobLock.UnLock();
        
    }
}



bool CAntler::DoRemoteControl()
{
    
    
    
    if(!ConfigureMOOSComms())
        return true;
    
    
	
    
    while(1)
    {
        MOOSPause(100);
        if(!m_pMOOSComms->IsConnected())
            continue;
        
        //better check mail
        MOOSMSG_LIST NewMail;
        if(m_pMOOSComms->Fetch(NewMail))
        {
            CMOOSMsg Msg;
            if(m_pMOOSComms->PeekMail(NewMail,"MISSION_FILE",Msg))
            {
                //tell the current job to quit
                m_bQuitCurrentJob = true;
                
                
                             
                //wait for that to happen
                m_JobLock.Lock();        
                
            	//make a new file name
                m_sReceivedMissionFile = MOOSFormat("%s.moos",MOOSGetDate().c_str());   
                            
                MOOSTrace("received mission file at run time\n");
                
                //here we copy the mission file contained in the message to 
                std::stringstream ss(Msg.GetString());
                std::ofstream Out(m_sReceivedMissionFile.c_str());
                if(!Out.is_open())
                {
                    m_JobLock.UnLock();
                   return MOOSFail("failed to open mission file for writing");
                }
                
                //you've gotta lurve C++ ...
                Out<<ss.rdbuf();
                
                Out.close();

                //we no longer want the current job to quit (it already has)
                m_bQuitCurrentJob = false;
                //signal that we have more work to do
                m_bNewJob =true;
                            
                //let thrad 0 continue
                m_JobLock.UnLock();
     			
                
            }
        }
    }        
}

bool CAntler::ConfigureMOOSComms()
{
    m_pMOOSComms = new CMOOSCommClient;
    m_pMOOSComms->SetOnConnectCallBack(_MOOSConnectCB,this);
	m_pMOOSComms->SetOnDisconnectCallBack(_MOOSDisconnectCB,this);
    m_pMOOSComms->SetQuiet(true);
    
    std::string sMe =MOOSFormat("Antler{%s}",m_sAntlerName.c_str());
    
    //try and connect to a DB
    if(!m_pMOOSComms->Run(m_sDBHost.c_str(), (long int)m_nDBPort, sMe.c_str(), 1))
        return MOOSFail("could not set up MOOSComms\n");
    
    if(m_bHeadless==false)
    {
        MOOSTrace("This is a master MOOS called %s\n",m_sAntlerName.c_str());
    }
    
    return true;
}



bool CAntler::SendMissionFile( )
{
    std::ifstream In(m_sMissionFile.c_str());
    
    if(!In.is_open())
        return MOOSFail("failed to open file for reading");
        
    
    std::stringstream ss;
    
    //you've gotta lurve C++ ...
    ss<<In.rdbuf();
    
    m_pMOOSComms->Notify("MISSION_FILE",ss.str());
    
    In.close();
    
    MOOSTrace("Master MOOS Published mission file\n");
    return true;
    
}
bool CAntler::OnMOOSConnect()
{
    if(m_bHeadless)
    {
        MOOSTrace("Headless Antler found a DB\n");    
        m_pMOOSComms->Register("MISSION_FILE",0);
    }
    else
    {
        SendMissionFile();
    }
    return true;
}
bool CAntler::OnMOOSDisconnect()
{
	MOOSTrace("Headless Antler lost DB Connection\n");    
    return true;
}



bool CAntler::Spawn(const std::string &  sMissionFile, bool bHeadless)
{
    m_nCurrentLaunch = 0;
    
    
    //set up the mission file reader
    if(!m_MissionReader.SetFile(sMissionFile))
        return MOOSFail("error reading mission file\n");
    
    
    m_MissionReader.SetAppName("ANTLER"); //NB no point in running under another name...(I guess Anter1 could luanch Antler2 though...)
    
    STRING_LIST      sParams;
    
    if(!m_MissionReader.GetConfiguration(  m_MissionReader.GetAppName(),sParams))
        return MOOSFail("error reading antler config block from mission file\n");
    
    
    //fetch all the lines in teg Antler configuration block
    STRING_LIST::iterator p;
    sParams.reverse();
    
    int nTimeMSBetweenSpawn=DEFAULTTIMEBETWEENSPAWN;
    m_MissionReader.GetConfigurationParam("MSBetweenLaunches",nTimeMSBetweenSpawn);
    
    //here we'll figure out a what paths to use when looking for  executables
    m_sDefaultExecutablePath = "SYSTEMPATH";
    m_MissionReader.GetConfigurationParam("ExecutablePath",m_sDefaultExecutablePath);
    
    if(!MOOSStrCmp("SYSTEMPATH",m_sDefaultExecutablePath))
    {
        MOOSTrace("\"ExecutablePath\" is %s\n",m_sDefaultExecutablePath.c_str());
        if(*m_sDefaultExecutablePath.rbegin()!='/')
        {
            //look to add extra / if needed
            m_sDefaultExecutablePath+='/';
        }
        
    }
    else
    {
        MOOSTrace("Unless directed otherwise using system path to locate binaries \n");
        m_sDefaultExecutablePath="";
    }
    
	//no cycle through each line in teh configuration block. If it begins with run then it means launch
    for(p = sParams.begin();p!=sParams.end();p++)
    {
        std::string sLine = *p;
        
        std::string sWhat = MOOSChomp(sLine,"=");
        
        if(MOOSStrCmp(sWhat,"RUN"))
        {
            //OK we are being asked to run a process
            
            //try to create a process
            MOOSProc* pNew  = CreateMOOSProcess(sLine);
            
            if(pNew!=NULL)
            {
                MOOSTrace("MOOSProcess: \"%s\" launched successfully\n",pNew->m_sApp.c_str());
                m_ProcList.push_front(pNew);
				m_nCurrentLaunch++;
            }
            
	        //wait a while
            MOOSPause(nTimeMSBetweenSpawn);
            
        }
    }
    
    
    
    if(bHeadless==false)
    {
        bool bMaster=false;
        m_MissionReader.GetConfigurationParam("EnableDistributed",bMaster);
        if(bMaster)
        {
            
            m_MissionReader.GetValue("ServerHost",m_sDBHost);
            m_MissionReader.GetValue("ServerPort",m_nDBPort);
            
            
            if(!ConfigureMOOSComms())
                return MOOSFail("failed to start MOOS comms");
            
        }
    }
    

    
    //now wait on all our processes to close....
    while(m_ProcList.size()!=0)
    {
        MOOSPROC_LIST::iterator q;
        
        for(q = m_ProcList.begin();q!=m_ProcList.end();q++)
        {
            MOOSProc * pMOOSProc = *q;
            
#ifdef _WIN32
            if(m_bQuitCurrentJob)
            {
				pMOOSProc->pWin32Proc->vTerminate();                
            }
            
            
            pMOOSProc->pWin32Proc->vWaitForTerminate(100);
            if(    pMOOSProc->pWin32Proc->dwGetExitCode()!=STILL_ACTIVE)
            {
                MOOSTrace("%s has quit\n",pMOOSProc->m_sApp.c_str());
                
                delete pMOOSProc->pWin32Attrib;
                delete pMOOSProc->pWin32Proc;
                delete pMOOSProc;
                
                m_ProcList.erase(q);
                break;
            }
#else
            if(m_bQuitCurrentJob)
            {
                MOOSTrace("actively killing running child %s\n",pMOOSProc->m_sApp.c_str());
				kill(pMOOSProc->m_ChildPID,SIGKILL);
                //just give it a little time - for pities sake - no need for this pause
                MOOSPause(300);
            }
            
            
            int nStatus = 0;
            if(waitpid(pMOOSProc->m_ChildPID,&nStatus,WNOHANG)>0)
            {
                MOOSTrace("%s has quit\n",pMOOSProc->m_sApp.c_str());
                
                m_ProcList.erase(q);
                break;
            }
            MOOSPause(100);
#endif
            
        }
              
    }
    
    MOOSTrace("All MOOS Processes complete...\n");

    
    return 0;
    
}




bool CAntler::MakeExtraExecutableParameters(std::string sParam,STRING_LIST & ExtraCommandLineParameters,std::string sProcName,std::string sMOOSName)
{
    
    ExtraCommandLineParameters.clear();
    
    std::string sExtraParamsName;
    if(!MOOSValFromString(sExtraParamsName, sParam, "ExtraProcessParams", true))
        return true;//nothing to do
    
    std::string sExtraParams;
    
    //OK look for this configuration string
    if(!m_MissionReader.GetConfigurationParam(sExtraParamsName,sExtraParams))
        return MOOSFail("cannot find extra parameters named \"%s\"\n",sExtraParamsName.c_str());
    
    while(!sExtraParams.empty())
        ExtraCommandLineParameters.push_back(MOOSChomp(sExtraParams,","));
    
    return true;
    
}

bool CAntler::MakeConsoleLaunchParams(std::string sParam,STRING_LIST & LaunchList,std::string sProcName,std::string sMOOSName)
{
    //sParam is a string in the Run=ProcName @ sParam ~ MOOSName
    std::string sLaunchConfigurationName;
    std::string sLaunchConfiguration;
#ifdef _WIN32
	bool bNIX = false;
	
#else
	bool bNIX= true; 
#endif
    
    
	std::string sLaunchKey = bNIX?"XConfig":"Win32Config"; 
    if(!MOOSValFromString(sLaunchConfigurationName, sParam, sLaunchKey))
    {
        //some applications are v.important in MOOS if not told otherwise they get special colours
		string sBgColor = bNIX? "#DDDDFF" : "";
		string sFgColor = bNIX? "#000000" : "";
        
        if(MOOSStrCmp(sProcName,"iRemote"))
        {
			sBgColor = bNIX ? "#CC0000" : "RED";
			sFgColor = bNIX ? "#FFFFFF" : "";
        }
        if(MOOSStrCmp(sProcName,"MOOSDB"))
        {
			sBgColor = bNIX? "#003300" : "BLUE";
			sFgColor = bNIX? "#FFFFFF" : "";
        }
        if(bNIX)
		{
			sLaunchConfiguration = "-geometry,"+MOOSFormat("80x12+2+%d",(m_nCurrentLaunch++)*50)+
			",+sb,"+
			",-fg,"+sFgColor+
			",-bg,"+sBgColor+
			",-T,"+MOOSFormat("%s %s",sProcName.c_str(),sMOOSName.empty()?"":MOOSFormat("as MOOSName \"%s\"",sMOOSName.c_str()).c_str());
		}
		else
		{
			sLaunchConfiguration = sBgColor;
		}
    }
    else
    {
        
        //OK look for this configuration string
        if(!m_MissionReader.GetConfigurationParam(sLaunchConfigurationName,sLaunchConfiguration))
			return MOOSFail("could not find resource string called \"%s\"",sLaunchConfigurationName.c_str()) ;
    }
    
    //OK now simply chomp our way through a space delimited list...
    while(!sLaunchConfiguration.empty())
    {
        MOOSTrimWhiteSpace(sLaunchConfiguration);
        std::string sP = MOOSChomp(sLaunchConfiguration,",");
        MOOSTrimWhiteSpace(sP);
        if(!sP.empty())
        	LaunchList.push_back(sP);
    }
    
    return !LaunchList.empty();
    
    
}



CAntler::MOOSProc* CAntler::CreateMOOSProcess(string sConfiguration)
{
    
    
    //what tis its name? (if no @ symbol we just get the name and no cmdline)
    string sProcName = MOOSChomp(sConfiguration,"@");
    
    //further parameters are to left left of @
    string sParam = sConfiguration;
    
    if(sProcName.empty())
    {
        MOOSTrace("no process specified - RUN=???\n");
        return NULL;
    }
    
    //std::string sFullProcName = m_sDefaultExecutablePath+sProcName;
    
    //if things go well this will eventually point to a new Process
    MOOSProc * pNewProc    = NULL;
    
	//look for tilde demarking end of param=val block
    string sOption = MOOSChomp(sParam,"~");
    
    
    
    bool bDistributed=false;
    m_MissionReader.GetConfigurationParam("EnableDistributed",bDistributed);

    if(bDistributed)
    {
               
        
        if(m_bHeadless)
        {
            //we are a drone
            std::string sAntlerRequired;
            if(!MOOSValFromString(sAntlerRequired, sOption, "AntlerID", true))
                return NULL; //this is for primary Antler
            
                        
            if(!MOOSStrCmp(sAntlerRequired, m_sAntlerName))
                return NULL; //for some other Antler
            
            //OK it is for us...
            MOOSTrace("Headless Antler found a RUN directive...\n");
        }
        else
        {
            //we are a TopMOOS
            std::string sAntlerRequired;
            if(MOOSValFromString(sAntlerRequired, sOption, "AntlerID", true))
                return NULL; //this is for a drone
            
        }
    }
    else
    {
        //we run everything
    }
    
    //do we want a new console?
    bool bNewConsole = false;
	MOOSValFromString(bNewConsole, sOption, "NEWCONSOLE",true);
    
    //do we want to inhibit the passing of MOOS parameters (mission file and MOOSName)
    bool bInhibitMOOSParams = false;
    MOOSValFromString(bInhibitMOOSParams, sOption, "InhibitMOOSParams", true);
    
    //by default process are assumed to be on the system path
    //users can specify an alternative path for all process by setting "ExectutablePath=<Path>" in the mission file
    //configuration block.
	//user has the option of specifying paths individually process by process.
    //alternativelt they can specify that a particular process shouod be located by  system wide and not
    //in the default executable path
    std::string sSpecifiedPath;
    std::string sFullProcName=sProcName;
    if(MOOSValFromString(sSpecifiedPath, sOption, "PATH",true))
    {
        if(MOOSStrCmp(sSpecifiedPath,"SYSTEM"))
        {
           	//do nothing - the system should know where to look 
        }
        else
        {
            //ok we are being told to look in a special place
            sFullProcName=sSpecifiedPath+"/"+sProcName;
        }
    }
    else
    {
        //we just use the Anter-wide Exepath
        sFullProcName = m_sDefaultExecutablePath+sProcName;
    }
    
    //name of process as registered...is the first param after "~"
    string sMOOSName = MOOSChomp(sParam);
    
    //here we figure out what MOOS name is implied if none is given (thanks to N.P and F.A)
    if(sMOOSName.empty())
	{
        std::string sTmp = sProcName;
        if(sTmp.rfind('/') != string::npos) 
        {
            sTmp = sTmp.substr(sTmp.rfind('/')+1);
        }
        if(sTmp.empty()) 
        { 
            // ended with a / ?
            MOOSTrace("Error in configuration  -MOOS Name cannot end in \" / \" : %s\n",sProcName.c_str());
            return NULL;
        }
        sMOOSName = sTmp;
	}
    
    
    //it is pssible to specifiy complicated parameters to the process being launched. (For example
    //xterm being passed a whole load of configurations and then the name of the MOOS process it should
    //itself launch. This next call fills in a string list of such parameters
    STRING_LIST sLaunchParams;
    if(bNewConsole)
    	MakeConsoleLaunchParams(sOption, sLaunchParams,sProcName,sMOOSName);
    
    
    //here we figure what extra command line parameters should be given to the MOOS Process
    //being launched.
    STRING_LIST ExtraCommandLineParameters;
    MakeExtraExecutableParameters(sOption,ExtraCommandLineParameters,sProcName,sMOOSName);
    
    
    
    //All good up to here, now make a new process info holder stucture...
    pNewProc = new MOOSProc;
    pNewProc->m_sApp = sFullProcName;
    pNewProc->m_ExtraCommandLineParameters =ExtraCommandLineParameters;
    pNewProc->m_ConsoleLaunchParameters = sLaunchParams;
    pNewProc->m_bInhibitMOOSParams = bInhibitMOOSParams;
	pNewProc->m_sMOOSName = sMOOSName;
    pNewProc->m_bNewConsole = bNewConsole;
    pNewProc->m_sMissionFile = m_MissionReader.GetFileName();
    
    //finally spawn each according to his own
#ifndef _WIN32
    if(DoNixOSLaunch(pNewProc))
#else
        if(DoWin32Launch(pNewProc))
#endif
        {
            return pNewProc;
        }
        else
        {        
            delete pNewProc;
            return NULL;
        }
}



#ifdef _WIN32
bool CAntler::DoWin32Launch(CAntler::MOOSProc * pNewProc)
{
    
    
    try
    {
        
        // make the command line ProcessBinaryName MissionFile ProcessMOOSName
        string sCmd = pNewProc->m_sApp+" ";
        if(pNewProc->m_bInhibitMOOSParams==false)
            sCmd+=pNewProc->m_sMissionFile+" "+pNewProc->m_sMOOSName+" ";
        
        //continuing this task, here we pass extra parameters to the MOOS process if required
		for(STRING_LIST::iterator p = pNewProc->m_ExtraCommandLineParameters.begin();p!=pNewProc->m_ExtraCommandLineParameters.end();p++)
        {
            sCmd+=*p+" ";
        }
        
        
        //make a new process attributes class
        pNewProc->pWin32Attrib = NULL;
        pNewProc->pWin32Attrib = new XPCProcessAttrib(NULL,(char *)sCmd.c_str());
        
        if(pNewProc->m_bNewConsole)
        {
            pNewProc->pWin32Attrib->vSetCreationFlag(CREATE_NEW_CONSOLE | CREATE_SUSPENDED);        
        }
        else
        {
            pNewProc->pWin32Attrib->vSetCreationFlag(CREATE_SUSPENDED);
        }
        
        
        //we shall use our own start up image as a starting point
        STARTUPINFO StartUp;
        GetStartupInfo (&StartUp);
        
        //set up the title
        StartUp.lpTitle = (char*)pNewProc->m_sApp.c_str();
        
        //set up white text
        StartUp.dwFillAttribute = 
        FOREGROUND_INTENSITY| 
        FOREGROUND_BLUE |
        FOREGROUND_RED |
        FOREGROUND_GREEN|
        BACKGROUND_INTENSITY ;
        
        
		//give users basic control over backgroun as an RGB combination
        for(STRING_LIST::iterator q=pNewProc->m_ConsoleLaunchParameters.begin();q!=pNewProc->m_ConsoleLaunchParameters.end();q++)
        {
            if(MOOSStrCmp(*q, "BACKGROUND_BLUE"))
            {
                StartUp.dwFillAttribute|=BACKGROUND_BLUE;
                StartUp.dwFlags|=STARTF_USEFILLATTRIBUTE;
            }
            if(MOOSStrCmp(*q, "BACKGROUND_GREEN"))
            {
                StartUp.dwFillAttribute|=BACKGROUND_GREEN;
                StartUp.dwFlags|=STARTF_USEFILLATTRIBUTE;
            }
            if(MOOSStrCmp(*q, "BACKGROUND_RED"))
            {
                StartUp.dwFillAttribute|=BACKGROUND_RED;
                StartUp.dwFlags|=STARTF_USEFILLATTRIBUTE;
            }
            
        }
        
        pNewProc->pWin32Attrib->vSetStartupInfo(&StartUp);
        
        //no create an object capable of laucnhing win32 processes
        pNewProc->pWin32Proc = NULL;
        pNewProc->pWin32Proc =new XPCProcess(* pNewProc->pWin32Attrib);
        
        //go!
        pNewProc->pWin32Proc->vResume();
        
    }
    catch (XPCException & e)
    {
        if(pNewProc->pWin32Attrib!=NULL)
        {
            delete pNewProc->pWin32Attrib;
        }
        if(pNewProc->pWin32Proc!=NULL)
        {
            delete pNewProc->pWin32Proc;
        }
       	MOOSTrace("*** %s Launch Failed:***\n\a\a",pNewProc->m_sApp.c_str());
       	MOOSTrace("%s\n",e.sGetException());
        return false;
    }
    
    return true;
    
}
#else

bool CAntler::DoNixOSLaunch(CAntler::MOOSProc * pNewProc)
{
    
    //make a child process
    if((pNewProc->m_ChildPID = fork())<0)
    {
        //hell!
        MOOSTrace("fork failed, not good\n");
        return false;
    }
    else if(pNewProc->m_ChildPID ==0)
    {
        //I'm the child now..
        
        STRING_LIST::iterator p = pNewProc->m_ConsoleLaunchParameters.begin();
        char ** pExecVParams = new char  *  [pNewProc->m_ConsoleLaunchParameters.size()+pNewProc->m_ExtraCommandLineParameters.size()+ 6];
        int i = 0;
        
        //do we need to configure an xterm?
        if(pNewProc->m_bNewConsole)
        {
            pExecVParams[i++] =DEFAULT_NIX_TERMINAL; 
            if(!pNewProc->m_ConsoleLaunchParameters.empty())
            {
                while(p!=pNewProc->m_ConsoleLaunchParameters.end())
                {
                    pExecVParams[i++] = (char*)(p++->c_str());
                }
            }
            
            pExecVParams[i++] = "-e";
            
        }
        
        //here we fill in the process name we really care about
        pExecVParams[i++] = (char *)pNewProc->m_sApp.c_str() ;
        if(!pNewProc->m_bInhibitMOOSParams)
        {
            //we do teh usual thing f supplying Mission file and MOOSName
            pExecVParams[i++] = (char *)pNewProc->m_sMissionFile.c_str(); 
            pExecVParams[i++] = (char *)pNewProc->m_sMOOSName.c_str(); 
        }
        
        
        //here we pass extra parameters to the MOOS process if required
        for(p = pNewProc->m_ExtraCommandLineParameters.begin();p!=pNewProc->m_ExtraCommandLineParameters.end();p++)
        {
            pExecVParams[i++] = (char *)(p->c_str());
        }
        
        //terminate list
        pExecVParams[i++] = NULL;
#if(DEBUG_LAUNCH)
        for(int j = 0;j<i;j++)
            MOOSTrace("argv[%d]:\"%s\"\n",j,pExecVParams[j]);
#endif
        
        //and finally replace ourselves with a new xterm process image
        if(execvp(pExecVParams[0],(char * const *)pExecVParams)==-1)
        {
            MOOSTrace("Failed exec - not good. Called exec as follows:\n");
            exit(EXIT_FAILURE);
            
        }
        
    }
    
    //Parent execution stream continues here...
    //with nothing to do
    
    return true;
}

#endif
