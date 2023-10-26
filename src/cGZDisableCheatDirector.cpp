////////////////////////////////////////////////////////////////////////
//
// This file is part of sc4-disable-cheats, a DLL Plugin for SimCity 4
// that allows players to control which cheats are enabled.
//
// Copyright (c) 2023 Nicholas Hayes
//
// This file is licensed under terms of the MIT License.
// See LICENSE.txt for more information.
//
////////////////////////////////////////////////////////////////////////

#include "version.h"
#include "cIGZFrameWork.h"
#include "cIGZApp.h"
#include "cISC4App.h"
#include "cIGZMessageServer2.h"
#include "cIGZMessageTarget.h"
#include "cIGZMessageTarget2.h"
#include "cIGZString.h"
#include "cIGZCheatCodeManager.h"
#include "cRZMessage2COMDirector.h"
#include "cRZMessage2Standard.h"
#include "cRZBaseString.h"
#include "GZServPtrs.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <Windows.h>
#include "wil/resource.h"
#include "wil/filesystem.h"

static constexpr uint32_t kGZMSG_CityInited = 0x26d31ec1;

static constexpr uint32_t kDisableCheatPluginDirectorID = 0x2f4f8834;

static constexpr std::string_view DisabledCheatsFileName = "DisabledCheats.txt";
static constexpr std::string_view PluginLogFileName = "SC4DisableCheats.log";

class cGZDisableCheatDirector : public cRZMessage2COMDirector
{
public:

	cGZDisableCheatDirector() : disabledCheatStrings(), disabledCheatFilePath(), logFile()
	{
		// DisabledCheats.txt and SC4DisableCheats.log are stored in the
		// same folder as the DLL (<SC4 Root>\Plugins).
		std::filesystem::path dllFolder = GetDllFolderPath();

		disabledCheatFilePath = dllFolder;
		disabledCheatFilePath /= DisabledCheatsFileName;

		std::filesystem::path logFilePath = dllFolder;
		logFilePath /= PluginLogFileName;

		logFile.open(logFilePath.c_str(), std::ofstream::out | std::ofstream::trunc);
		if (logFile)
		{
			logFile << "SC4DisableCheats v" PLUGIN_VERSION_STR << std::endl;
		}
	}

	uint32_t GetDirectorID() const
	{
		return kDisableCheatPluginDirectorID;
	}

	bool DoMessage(cIGZMessage2* pMessage)
	{
		cIGZMessage2Standard* pStandardMsg = static_cast<cIGZMessage2Standard*>(pMessage);
		uint32_t dwType = pMessage->GetType();

		if (dwType == kGZMSG_CityInited)
		{
			cISC4AppPtr pISC4App;
			if (pISC4App)
			{
				cIGZCheatCodeManager* pCheatMgr = pISC4App->GetCheatCodeManager();
				if (pCheatMgr)
				{
					// Attempt to disable each cheat code in the list.
					for (const std::string& cheatName : disabledCheatStrings)
					{
						uint32_t cheatID;

						// Translate the cheat code string into its integer ID.
						// The integer ID uniquely identifies the cheat code within the game's messaging system.
						// This will fail for any cheat code string that the game doesn't recognize.
						if (pCheatMgr->DoesCheatCodeMatch(cRZBaseString(cheatName), cheatID))
						{
							// The cheat code is registered in the game, try to unregister it.
							// When the cheat code is unregistered the game will no longer accept it.
							if (pCheatMgr->UnregisterCheatCode(cheatID))
							{
								if (logFile)
								{
									logFile << "Unregistered cheat code: " << cheatName.c_str() << std::endl;
								}
							}
							else
							{
								if (logFile)
								{
									logFile << "Failed to unregister cheat code: " << cheatName.c_str() << std::endl;
								}
							}
						}
						else
						{
							if (logFile)
							{
								logFile << "Unknown cheat code: " << cheatName.c_str() << std::endl;
							}
						}
					}
#ifdef _DEBUG
					DumpRegisteredCheats(pCheatMgr);
#endif // _DEBUG
				}
				else
				{
					if (logFile)
					{
						logFile << "pCheatMgr was null." << std::endl;
					}
				}
			}
			else
			{
				if (logFile)
				{
					logFile << "pISC4App was null." << std::endl;
				}
			}
		}

		return true;
	}

	bool PostAppInit()
	{
		std::ifstream disabledCheatsFile(disabledCheatFilePath.c_str());

		if (disabledCheatsFile)
		{
			std::string value;
			while (std::getline(disabledCheatsFile, value))
			{
				// Lines starting with a semicolon are comments.
				if (value[0] != ';')
				{
					disabledCheatStrings.push_back(value);
				}
			}

			if (logFile)
			{
				logFile << "Loaded " << DisabledCheatsFileName.data() << " with " << disabledCheatStrings.size() << " values." << std::endl;
			}
		}
		else
		{
			if (logFile)
			{
				logFile << "Failed to open " << DisabledCheatsFileName.data() << std::endl;
			}
		}

		cIGZMessageServer2Ptr pMsgServ;
		if (pMsgServ)
		{
			// Subscribe for a notification when a city is being loaded.
			// Most of the game's cheat codes are only registered at that stage.
			pMsgServ->AddNotification(this, kGZMSG_CityInited);
		}

		return true;
	}

	bool OnStart(cIGZCOM* pCOM)
	{
		cIGZFrameWork* const pFramework = RZGetFrameWork();
		if (pFramework)
		{
			if (pFramework->GetState() < cIGZFrameWork::kStatePreAppInit)
			{
				pFramework->AddHook(this);
			}
			else
			{
				PreAppInit();
			}
		}

		return true;
	}

private:

#ifdef _DEBUG
	// Prints a list of all registered cheat code strings to the debugger console.
	void DumpRegisteredCheats(cIGZCheatCodeManager* pCheatMgr)
	{
		uint32_t cheatStringCount = 0;

		// Call GetListOfAllCheatStrings with a NULL string pointer and a string count of 0 to get
		// the total number of cheats registered in the game.
		int registeredCheatCount = pCheatMgr->GetListOfAllCheatStrings(nullptr, cheatStringCount);
		if (registeredCheatCount > 0)
		{
			std::vector<cIGZString*> cheatStrings(static_cast<size_t>(registeredCheatCount));
			cheatStringCount = static_cast<uint32_t>(cheatStrings.size());

			// Call GetListOfAllCheatStrings a second time to fill the cheat string array.
			int cheatsReturned = pCheatMgr->GetListOfAllCheatStrings(cheatStrings.data(), cheatStringCount);

			if (cheatsReturned > 0)
			{
				for (uint32_t i = 0; i < cheatStringCount; i++)
				{
					cIGZString* str = cheatStrings[i];

					PrintLineToDebugOutput(str->ToChar());

					// SC4 encounters a heap corruption crash when releasing some of the strings.
					// Not freeing the string may cause a memory leak, but it doesn't matter as
					// this code only runs in debug builds.
					//str->Release();
				}
			}
		}
	}

	void PrintLineToDebugOutput(const char* str)
	{
		OutputDebugStringA(str);
		OutputDebugStringA("\r\n");
	}
#endif // _DEBUG

	std::filesystem::path GetDllFolderPath()
	{
		wil::unique_cotaskmem_string dllPath = wil::GetModuleFileNameW(wil::GetModuleInstanceHandle());

		std::filesystem::path temp(dllPath.get());

		return temp.parent_path();
	}

	std::vector<std::string> disabledCheatStrings;
	std::filesystem::path disabledCheatFilePath;
	std::wofstream logFile;
};

cRZCOMDllDirector* RZGetCOMDllDirector() {
	static cGZDisableCheatDirector sDirector;
	return &sDirector;
}