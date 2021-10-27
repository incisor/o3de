/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <source/applicationManager.h>
#include <AzCore/Jobs/Algorithms.h>

#include <AzFramework/Process/ProcessCommunicator.h>
#include <AzFramework/Process/ProcessWatcher.h>
#include <AzCore/UserSettings/UserSettingsComponent.h>
#include <AzCore/Utils/Utils.h>

#include <AzFramework/Asset/AssetCatalogComponent.h>
#include <AzFramework/Entity/GameEntityContextComponent.h>
#include <AzFramework/Components/AzFrameworkConfigurationSystemComponent.h>
#include <AzFramework/Input/System/InputSystemComponent.h>
#include <AzFramework/StringFunc/StringFunc.h>

#include <AzCore/Slice/SliceSystemComponent.h>
#include <AzToolsFramework/Archive/ArchiveComponent.h>
#include <AzToolsFramework/AssetBundle/AssetBundleComponent.h>
#include <AzToolsFramework/AssetCatalog/PlatformAddressedAssetCatalogBus.h>
#include <AzToolsFramework/Prefab/PrefabSystemComponent.h>
#include <AzToolsFramework/Asset/AssetDebugInfo.h>
#include <AzToolsFramework/Archive/ArchiveAPI.h>
#include <source/AssetGraphWalker.h>
#include <AzFramework/Archive/INestedArchive.h>
#include <AzFramework/Archive/ZipDirStructures.h>

namespace BRAssetBundler
{
    const unsigned int g_sleepDuration = 1;

    ApplicationManager::ApplicationManager(int* argc, char*** argv)
        : AzToolsFramework::ToolsApplication(argc, argv)
    {
    }

    ApplicationManager::~ApplicationManager()
    {
        DestroyApplication();
    }

    bool ApplicationManager::Init()
    {
        AZ::Debug::TraceMessageBus::Handler::BusConnect();
        Start(AzFramework::Application::Descriptor());
        AZ::SerializeContext* context;
        EBUS_EVENT_RESULT(context, AZ::ComponentApplicationBus, GetSerializeContext);
        AZ_Assert(context, "No serialize context");
        AzToolsFramework::AssetSeedManager::Reflect(context);

        m_assetSeedManager = AZStd::make_unique<AzToolsFramework::AssetSeedManager>();

        // There is no need to update the UserSettings file, so we can avoid a race condition by disabling save on shutdown
        AZ::UserSettingsComponentRequestBus::Broadcast(&AZ::UserSettingsComponentRequests::DisableSaveOnFinalize);

        m_archive = AZ::Interface<AZ::IO::IArchive>::Get();
        if (m_archive == nullptr)
        {
            AZ_Error(AppWindowName, false, "Failed to get IArchive interface!");
        }
        return true;
    }

    void ApplicationManager::DestroyApplication()
    {
        m_showVerboseOutput = false;
        m_assetSeedManager.reset();
        Stop();
        AZ::Debug::TraceMessageBus::Handler::BusDisconnect();
    }

    bool ApplicationManager::Run()
    {
        const AZ::CommandLine* parser = GetCommandLine();

        if (parser->HasSwitch(ProjectArg))
        {
            if (parser->GetNumSwitchValues(ProjectArg) != 1)
            {
                AZ_Error(AppWindowName, false, "Invalid command : \"--%s\" must have exactly one value.", ProjectArg);
                return false;
            }
            m_currentProjectName = parser->GetSwitchValue(ProjectArg, 0);
            AZ_TracePrintf(AppWindowName, "Setting project to ( %s ).\n", m_currentProjectName.c_str());
        }

        m_showVerboseOutput = ShouldPrintVerbose(parser);

        bool shouldPrintHelp = ShouldPrintHelp(parser);

        m_currentProjectName = AZStd::string_view{ AZ::Utils::GetProjectName() };

        if (m_currentProjectName.empty())
        {
            AZ_Error(AppWindowName, false, "Unable to retrieve project name from the Settings Registry");
            return false;
        }

        // Check for what command we are running, and if the user wants to see the Help text
        m_commandType = GetCommandType(parser, shouldPrintHelp);

        AZStd::string commandLineStr = GetCleanCommandLine(parser, m_commandType);

        m_platformCatalogManager = AZStd::make_unique<AzToolsFramework::PlatformAddressedAssetCatalogManager>();

        AZ::IO::FixedMaxPathString executableDirectory;
        if (AZ::Utils::GetExecutableDirectory(executableDirectory.data(), executableDirectory.max_size()) ==
            AZ::Utils::ExecutablePathResult::Success)
        {
            // Update the size member of the FixedString stored in the path class
            executableDirectory.resize_no_construct(AZStd::char_traits<char>::length(executableDirectory.data()));
        }

        AZ::IO::FixedMaxPath assetBundlerPath{ executableDirectory };
        assetBundlerPath /= AssetBundlerBatchName;
        if (LaunchProcess(assetBundlerPath.c_str(), commandLineStr) == 0)
        {
            switch (m_commandType)
            {
            case CommandType::Seeds:
                return RunSeedsCommands(ParseSeedsCommandData(parser));
            case CommandType::AssetLists:
                return RunAssetListsCommands(ParseAssetListsCommandData(parser));
            case CommandType::Bundles:
                return RunBundlesCommands(ParseBundlesCommandData(parser));
            }
        }

        return false;
    }

    AZ::ComponentTypeList ApplicationManager::GetRequiredSystemComponents() const
    {
        AZ::ComponentTypeList components = AzFramework::Application::GetRequiredSystemComponents();

        /*components.emplace_back(azrtti_typeid<AzToolsFramework::AssetBundleComponent>());*/
        components.emplace_back(azrtti_typeid<AzToolsFramework::ArchiveComponent>());
        components.emplace_back(azrtti_typeid<AzToolsFramework::Prefab::PrefabSystemComponent>());

        for (auto iter = components.begin(); iter != components.end();)
        {
            if (*iter == azrtti_typeid<AzFramework::GameEntityContextComponent>() ||
                *iter == azrtti_typeid<AzFramework::AzFrameworkConfigurationSystemComponent>() ||
                *iter == azrtti_typeid<AzFramework::InputSystemComponent>() || *iter == azrtti_typeid<AZ::SliceSystemComponent>())
            {
                // Asset Bundler does not require the above components to be active
                iter = components.erase(iter);
            }
            else
            {
                ++iter;
            }
        }

        return components;
    }

    bool ApplicationManager::ShouldPrintHelp(const AZ::CommandLine* parser)
    {
        return parser->HasSwitch(BRAssetBundler::HelpFlag) || parser->HasSwitch(BRAssetBundler::HelpFlagAlias);
    }

    CommandType ApplicationManager::GetCommandType(const AZ::CommandLine* parser, [[maybe_unused]] bool suppressErrors)
    {
        // Verify that the user has only typed in one sub-command
        size_t numMiscValues = parser->GetNumMiscValues();
        if (numMiscValues == 0)
        {
            AZ_Error(AppWindowName, suppressErrors, "Invalid command: Must provide a sub-command (ex: \"%s\").", BRAssetBundler::SeedsCommand);
            return CommandType::Invalid;
        }
        else if (numMiscValues > 1)
        {
            AZ_Error(AppWindowName, suppressErrors, "Invalid command: Cannot perform more than one sub-command operation at once");
            return CommandType::Invalid;
        }

        AZStd::string subCommand = parser->GetMiscValue(0);
        if (!azstricmp(subCommand.c_str(), BRAssetBundler::SeedsCommand))
        {
            return CommandType::Seeds;
        }
        else if (!azstricmp(subCommand.c_str(), BRAssetBundler::AssetListsCommand))
        {
            return CommandType::AssetLists;
        }
        else if (!azstricmp(subCommand.c_str(), BRAssetBundler::ComparisonRulesCommand))
        {
            return CommandType::ComparisonRules;
        }
        else if (!azstricmp(subCommand.c_str(), BRAssetBundler::CompareCommand))
        {
            return CommandType::Compare;
        }
        else if (!azstricmp(subCommand.c_str(), BRAssetBundler::BundleSettingsCommand))
        {
            return CommandType::BundleSettings;
        }
        else if (!azstricmp(subCommand.c_str(), BRAssetBundler::BundlesCommand))
        {
            return CommandType::Bundles;
        }
        else if (!azstricmp(subCommand.c_str(), BRAssetBundler::BundleSeedCommand))
        {
            return CommandType::BundleSeed;
        }
        else
        {
            AZ_Error(AppWindowName, false, "( %s ) is not a valid sub-command", subCommand.c_str());
            return CommandType::Invalid;
        }
    }

    bool ApplicationManager::ShouldPrintVerbose(const AZ::CommandLine* parser)
    {
        return parser->HasSwitch(VerboseFlag);
    }

    AZStd::string ApplicationManager::GetCleanCommandLine(const AZ::CommandLine* parser, CommandType commandType)
    {
        AZ::CommandLine::ParamContainer commandLineArgs;
        parser->Dump(commandLineArgs);

        if(parser->HasSwitch(RegsetFlag))
        {
            while (true)
            {
                auto iter = AZStd::find(commandLineArgs.begin(), commandLineArgs.end(), AZStd::string("-") + RegsetFlag);
                if (iter == commandLineArgs.end())
                    break;
                AZStd::string regSetVal = parser->GetSwitchValue(RegsetFlag, 0);

                commandLineArgs.erase(iter); // erase the regset flag
                commandLineArgs.erase(iter); // erase the value
            }
        }

        if ((commandType == AssetLists || commandType == Seeds) && parser->HasSwitch(AddSeedArg))
        {
            size_t numAddSeedArgs = parser->GetNumSwitchValues(AddSeedArg);
            size_t marker;
            AZStd::string newStr;
            for (size_t addSeedIndex = 0; addSeedIndex < numAddSeedArgs; ++addSeedIndex)
            {
                AZStd::string addSeedVal = parser->GetSwitchValue(AddSeedArg, addSeedIndex);
                auto iter = AZStd::find_if(
                    commandLineArgs.begin(), commandLineArgs.end(),
                    [addSeedVal](const AZStd::string& param)
                    {
                        return param.find(addSeedVal) != AZStd::string::npos;
                    });

                if (iter != commandLineArgs.end())
                {
                    marker = addSeedVal.find(PackIdFirstMarker);
                    if (marker != AZStd::string::npos)
                        *iter = addSeedVal.substr(0, marker);
                }
            }
        }
        

        AZStd::string commandLineStr;
        for (auto& arg : commandLineArgs) commandLineStr = commandLineStr + " " + arg;
        return commandLineStr;
    }

    AZ::Outcome<SeedsParams, AZStd::string> ApplicationManager::ParseSeedsCommandData(const AZ::CommandLine* parser)
    {
        using namespace AzToolsFramework;

        SeedsParams params;

        params.m_ignoreFileCase = parser->HasSwitch(IgnoreFileCaseFlag);

        // Read in Seed List Files arg
        auto requiredArgOutcome = GetFilePathArg(parser, SeedListFileArg, SeedsCommand, true);
        if (!requiredArgOutcome.IsSuccess())
        {
            return AZ::Failure(requiredArgOutcome.GetError());
        }
        bool checkFileCase = true;
        // Seed List files do not have platform-specific file names
        params.m_seedListFile = FilePath(requiredArgOutcome.GetValue(), checkFileCase, params.m_ignoreFileCase);

        if (!params.m_seedListFile.IsValid())
        {
            return AZ::Failure(params.m_seedListFile.ErrorString());
        }

        //// Read in Add/Remove Platform to All Seeds flag
        //params.m_addPlatformToAllSeeds = parser->HasSwitch(AddPlatformToAllSeedsFlag);
        //params.m_removePlatformFromAllSeeds = parser->HasSwitch(RemovePlatformFromAllSeedsFlag);

        //if (params.m_addPlatformToAllSeeds && params.m_removePlatformFromAllSeeds)
        //{
        //    return AZ::Failure(AZStd::string::format(
        //        "Invalid command: Unable to run \"--%s\" and \"--%s\" at the same time.", AssetBundler::AddPlatformToAllSeedsFlag,
        //        AssetBundler::RemovePlatformFromAllSeedsFlag));
        //}

        //if ((params.m_addPlatformToAllSeeds || params.m_removePlatformFromAllSeeds) && !parser->HasSwitch(PlatformArg))
        //{
        //    return AZ::Failure(AZStd::string::format(
        //        "Invalid command: When running \"--%s\" or \"--%s\", the \"--%s\" arg is required.", AddPlatformToAllSeedsFlag,
        //        RemovePlatformFromAllSeedsFlag, PlatformArg));
        //}

        // Read in Platform arg
        auto platformOutcome = GetPlatformArg(parser);
        if (!platformOutcome.IsSuccess())
        {
            return AZ::Failure(platformOutcome.GetError());
        }
        params.m_platformFlags = GetInputPlatformFlagsOrEnabledPlatformFlags(platformOutcome.GetValue());

        // Read in Asset Catalog File arg
        auto argOutcome = GetFilePathArg(parser, AssetCatalogFileArg, SeedsCommand);
        if (!argOutcome.IsSuccess())
        {
            return AZ::Failure(argOutcome.GetError());
        }
        if (!argOutcome.IsSuccess())
        {
            params.m_assetCatalogFile = FilePath(argOutcome.GetValue(), checkFileCase, params.m_ignoreFileCase);
            if (!params.m_assetCatalogFile.IsValid())
            {
                return AZ::Failure(params.m_assetCatalogFile.ErrorString());
            }
        }

        // Read in Add Seed arg
        params.m_addSeedList = GetAddSeedArgList(parser);

        // Read in Remove Seed arg
        size_t numRemoveSeedArgs = 0;
        if (parser->HasSwitch(RemoveSeedArg))
        {
            numRemoveSeedArgs = parser->GetNumSwitchValues(RemoveSeedArg);
            for (size_t removeSeedIndex = 0; removeSeedIndex < numRemoveSeedArgs; ++removeSeedIndex)
            {
                params.m_removeSeedList.push_back(parser->GetSwitchValue(RemoveSeedArg, removeSeedIndex));
            }
        }

        return AZ::Success(params);
    }

    AZ::Outcome<AssetListsParams, AZStd::string> ApplicationManager::ParseAssetListsCommandData(const AZ::CommandLine* parser)
    {
        AssetListsParams params;

        // Read in Platform arg
        auto platformOutcome = GetPlatformArg(parser);
        if (!platformOutcome.IsSuccess())
        {
            return AZ::Failure(platformOutcome.GetError());
        }
        params.m_platformFlags = GetInputPlatformFlagsOrEnabledPlatformFlags(platformOutcome.GetValue());

        // Read in Print flag
        params.m_print = parser->HasSwitch(PrintFlag);

        // Read in Asset List File arg
        auto requiredArgOutcome = GetFilePathArg(parser, AssetListFileArg, AssetListsCommand, false);

        //replace extension with assethints file.
        AZStd::string outputFile = requiredArgOutcome.GetValue();
        AzFramework::StringFunc::Path::ReplaceExtension(outputFile, AssetHintsExtension);
        params.m_assetHintsFile = FilePath(outputFile);
        
        if (!params.m_print && !params.m_assetHintsFile.IsValid())
        {
            return AZ::Failure(GetBinaryArgOptionFailure(PrintFlag, AssetListFileArg));
        }

        // Read in Seed List File arg
        size_t numSeedListFiles = parser->GetNumSwitchValues(SeedListFileArg);
        for (size_t seedListFileIndex = 0; seedListFileIndex < numSeedListFiles; ++seedListFileIndex)
        {
            params.m_seedListFiles.emplace_back(FilePath(parser->GetSwitchValue(SeedListFileArg, seedListFileIndex)));
        }

        // Read in Add Seed arg
        params.m_seedList = GetAddSeedArgList(parser, &params.m_levelAssetHints);

        // Read in Skip arg
        params.m_skipList = GetSkipArgList(parser);

        // Read in Asset Catalog File arg
        auto argOutcome = GetFilePathArg(parser, AssetCatalogFileArg, AssetListsCommand);
        if (!argOutcome.IsSuccess())
        {
            return AZ::Failure(argOutcome.GetError());
        }
        if (!argOutcome.IsSuccess())
        {
            params.m_assetCatalogFile = FilePath(argOutcome.GetValue());
        }

        // Read in Allow Overwrites flag
        params.m_allowOverwrites = parser->HasSwitch(AllowOverwritesFlag);

        return AZ::Success(params);
    }

    AZ::Outcome<BundlesParamsList, AZStd::string> ApplicationManager::ParseBundlesCommandData(const AZ::CommandLine* parser)
    {
        /*auto validateArgsOutcome = ValidateInputArgs(parser, m_allBundlesArgs);
        if (!validateArgsOutcome.IsSuccess())
        {
            OutputHelpBundles();
            return AZ::Failure(validateArgsOutcome.TakeError());
        }*/

        auto parseSettingsOutcome = ParseBundleSettingsAndOverrides(parser, BundlesCommand);
        if (!parseSettingsOutcome.IsSuccess())
        {
            return AZ::Failure(parseSettingsOutcome.GetError());
        }

        return AZ::Success(parseSettingsOutcome.TakeValue());
    }

    AZ::Outcome<AZStd::string, AZStd::string> ApplicationManager::GetFilePathArg(
        const AZ::CommandLine* parser, const char* argName, const char* subCommandName, bool isRequired)
    {
        if (!parser->HasSwitch(argName))
        {
            if (isRequired)
            {
                return AZ::Failure(
                    AZStd::string::format("Invalid command: \"--%s\" is required when running \"%s\".", argName, subCommandName));
            }
            return AZ::Success(AZStd::string());
        }

        if (parser->GetNumSwitchValues(argName) != 1)
        {
            return AZ::Failure(AZStd::string::format("Invalid command: \"--%s\" must have exactly one value.", argName));
        }

        return AZ::Success(parser->GetSwitchValue(argName, 0));
    }

    AZ::Outcome<void, AZStd::string> ApplicationManager::InitAssetCatalog(AzFramework::PlatformFlags platforms, const AZStd::string& assetCatalogFile)
    {
        using namespace AzToolsFramework;
        if (platforms == AzFramework::PlatformFlags::Platform_NONE)
        {
            return AZ::Failure(AZStd::string("Invalid platform.\n"));
        }

        for (const AzFramework::PlatformId& platformId : AzFramework::PlatformHelper::GetPlatformIndicesInterpreted(platforms))
        {
            AZStd::string platformSpecificAssetCatalogPath;
            if (assetCatalogFile.empty())
            {
                AZ::StringFunc::Path::ConstructFull(
                    PlatformAddressedAssetCatalog::GetAssetRootForPlatform(platformId).c_str(), BRAssetBundler::AssetCatalogFilename,
                    platformSpecificAssetCatalogPath);
            }
            else
            {
                platformSpecificAssetCatalogPath = assetCatalogFile;
            }

            AZ_TracePrintf(
                BRAssetBundler::AppWindowNameVerbose, "Loading asset catalog from ( %s ).\n", platformSpecificAssetCatalogPath.c_str());

            bool success = false;
            {
                AzToolsFramework::AssetCatalog::PlatformAddressedAssetCatalogRequestBus::EventResult(
                    success, platformId, &AzToolsFramework::AssetCatalog::PlatformAddressedAssetCatalogRequestBus::Events::LoadCatalog,
                    platformSpecificAssetCatalogPath.c_str());
            }
            if (!success && !AzFramework::PlatformHelper::IsSpecialPlatform(platforms))
            {
                return AZ::Failure(
                    AZStd::string::format("Failed to open asset catalog file ( %s ).", platformSpecificAssetCatalogPath.c_str()));
            }
        }

        return AZ::Success();
    }


    void ApplicationManager::AddOrRemoveSeeds(AzFramework::PlatformId platformId, AssetPackInfoList seedList, bool bAddSeed)
    {
        AzFramework::PlatformFlags platformFlag = AzFramework::PlatformHelper::GetPlatformFlagFromPlatformIndex(platformId);

        for (const auto& seed : seedList)
        {
            if (bAddSeed)
                m_assetSeedManager->AddSeedAsset(seed.m_assetRelativePath, platformFlag);
            else
                m_assetSeedManager->RemoveSeedAsset(seed.m_assetRelativePath, platformFlag);
        }
    }

    void ApplicationManager::MergeLevelAssetHints(AssetPackInfoList fileList, AssetPackInfoMap& infoMap, IdAssetIdListMap& assetIdMap, AzFramework::PlatformFlags platformFlags)
    {
        for (const auto& filename : fileList)
        {
            ReadAssetHints(
                filename.m_assetRelativePath, platformFlags,
                [&](const AssetPackInfo& packInfo)
                {
                    AddAssetPackInfoToMap(infoMap, packInfo);
                    assetIdMap[packInfo.m_packId].emplace(packInfo.m_assetId);
                });
        }
    }

    AZ::Outcome<void, AZStd::string> ApplicationManager::DoPreBundlingStep(BundlesParams& params, AllBundleSetting& allBundleSettings)
    {
        using namespace AzToolsFramework;
        // If no platform was input we want to loop over all possible platforms and make bundles for whatever we find
        if (params.m_platformFlags == AzFramework::PlatformFlags::Platform_NONE)
        {
            params.m_platformFlags = AzFramework::PlatformFlags::AllNamedPlatforms;
        }

        // Load or generate Bundle Settings
        AzFramework::PlatformFlags allPlatformsInBundle = AzFramework::PlatformFlags::Platform_NONE;
        if (params.m_bundleSettingsFile.AbsolutePath().empty())
        {
            // Verify input file path formats before looking for platform-specific versions
            auto fileExtensionOutcome = AssetFileInfoList::ValidateAssetListFileExtension(params.m_assetListFile.AbsolutePath());
            if (!fileExtensionOutcome.IsSuccess())
            {
                return AZ::Failure(fileExtensionOutcome.GetError());
            }

            AZStd::vector<FilePath> allAssetListFilePaths =
                GetAllPlatformSpecificFilesOnDisk(params.m_assetListFile, params.m_platformFlags);

            // Create temporary Bundle Settings structs for every Asset List file
            for (const auto& assetListFilePath : allAssetListFilePaths)
            {
                AssetBundleSettings bundleSettings;
                bundleSettings.m_assetFileInfoListPath = assetListFilePath.AbsolutePath();
                bundleSettings.m_platform = GetPlatformIdentifier(assetListFilePath.AbsolutePath());
                allPlatformsInBundle |= AzFramework::PlatformHelper::GetPlatformFlag(bundleSettings.m_platform);
                allBundleSettings.emplace_back(AZStd::make_pair(bundleSettings, params));
            }
        }
        else
        {
            // Verify input file path formats before looking for platform-specific versions
            auto fileExtensionOutcome =
                AssetBundleSettings::ValidateBundleSettingsFileExtension(params.m_bundleSettingsFile.AbsolutePath());
            if (!fileExtensionOutcome.IsSuccess())
            {
                return AZ::Failure(fileExtensionOutcome.GetError());
            }

            AZStd::vector<FilePath> allBundleSettingsFilePaths =
                GetAllPlatformSpecificFilesOnDisk(params.m_bundleSettingsFile, params.m_platformFlags);

            // Attempt to load all Bundle Settings files (there may be one or many to load)
            for (const auto& bundleSettingsFilePath : allBundleSettingsFilePaths)
            {
                AZ::Outcome<AssetBundleSettings, AZStd::string> loadBundleSettingsOutcome =
                    AssetBundleSettings::Load(bundleSettingsFilePath.AbsolutePath());
                if (!loadBundleSettingsOutcome.IsSuccess())
                {
                    return AZ::Failure(loadBundleSettingsOutcome.GetError());
                }

                allBundleSettings.emplace_back(AZStd::make_pair(loadBundleSettingsOutcome.TakeValue(), params));
                allPlatformsInBundle |= AzFramework::PlatformHelper::GetPlatformFlag(allBundleSettings.back().first.m_platform);
            }
        }

        return AZ::Success();
    }

    AZ::Outcome<void, AZStd::string> ApplicationManager::ListFilesInArchiveAndRename(const AZStd::string& bundleFilePath, PathPackInfoMap& outInfoMap, bool allowOverwrites)
    {
        //F:\Work\Bitraider\GIT\o3de\o3de\test_pc*.pak
        AZStd::string filter = bundleFilePath;
        AzFramework::StringFunc::Path::StripExtension(filter);
        filter += "*.pak";
        AZStd::string folderPath = bundleFilePath;
        AzFramework::StringFunc::Path::StripFullName(folderPath);
        AZ::IO::SystemFile::FindFiles(
            filter.data(),
            [&](AZStd::string_view filename, bool isFile) -> bool
            {
                if (isFile)
                {
                    // rename pak to bpak
                    AZStd::string filenameNoExt;
                    AzFramework::StringFunc::Path::GetFileName(filename.data(), filenameNoExt);
                    AZStd::string bpakFile = folderPath + "\\" + filenameNoExt + BPakExtension;
                    AZStd::string pakFile = folderPath + "\\" + filenameNoExt + PakExtension;
                    if (!AZ::IO::SystemFile::Rename(pakFile.c_str(), bpakFile.c_str(), allowOverwrites))
                        return false/*AZ::Failure(AZStd::string("Failed to rename pak file to bpak file"))*/;

                    if (!AZ::IO::FileIOBase::GetInstance()->Exists(bpakFile.c_str()))
                    {
                        AZ_Error(AppWindowName, false, "Archive '%s' does not exist!", bpakFile.c_str());
                        return false;
                    }

                    auto archive = m_archive->OpenArchive(bpakFile, {}, AZ::IO::INestedArchive::FLAGS_READ_ONLY);
                    if (!archive)
                    {
                        AZ_Error(AppWindowName, false, "Failed to open archive file '%s'", bpakFile.c_str());
                        return false;
                    }

                    AZStd::vector<AZ::IO::Path> fileEntries;
                    int result = archive->ListAllFiles(fileEntries);
                    AZStd::vector<AZStd::string> outFileEntries;

                    AZStd::string bpakFileName;
                    AzFramework::StringFunc::Path::GetFullFileName(bpakFile.c_str(), bpakFileName); // we only get the filename since we're only be profiling from the same folder and not sub-folders.

                    for (const auto& path : fileEntries)
                    {
                        auto assetRelativePath = path.String();
                        auto handle = archive->FindFile(assetRelativePath); // get the Handle so we can work on it.
                        auto fileEntry = reinterpret_cast<AZ::IO::ZipDir::FileEntry*>(handle);
                        
                        outInfoMap[assetRelativePath].m_assetRelativePath = assetRelativePath;
                        outInfoMap[assetRelativePath].m_size = fileEntry->nEOFOffset - fileEntry->nFileHeaderOffset;
                        outInfoMap[assetRelativePath].m_offset = fileEntry->nFileHeaderOffset;
                        outInfoMap[assetRelativePath].m_bundlePath = bpakFileName;

                        AZ_Printf(
                            AppWindowName, "%s %u %u %u\n", assetRelativePath.c_str(), outInfoMap[assetRelativePath].m_size,
                            fileEntry->nFileHeaderOffset, fileEntry->nEOFOffset)
                    }

                    

                    //AZStd::binary_semaphore listCompleteSemaphore;
                    //bool result = false;
                    //auto listFilesResponseLambda = [&]([[maybe_unused]] bool success, AZStd::string consoleOutput)
                    //{
                    //    result = success;
                    //    AZ_Printf(AppWindowName, "%s", consoleOutput.c_str());
                    //    Platform::ParseConsoleOutputFromListFilesInArchive(consoleOutput, bpakFile, outInfoMap);
                    //    listCompleteSemaphore.release();
                    //};

                    //AZ::Uuid handle = AZ::Uuid::Create();
                    //AZStd::vector<AZStd::string> fileEntries;
                    //AzToolsFramework::ArchiveCommandsBus::Broadcast(
                    //    &AzToolsFramework::ArchiveCommands::ListFilesInArchive, bpakFile, fileEntries, handle, listFilesResponseLambda);

                    //const int operationSleepMS = 20;
                    //bool operationCompleted = false;
                    //while (!operationCompleted)
                    //{
                    //    operationCompleted = listCompleteSemaphore.try_acquire_for(AZStd::chrono::milliseconds(operationSleepMS));
                    //    // When the archive operation is completed, the response lambda is queued on the the tick bus.
                    //    // This loop will keep executing queued events on the tickbus until the response unlocks the semaphore.
                    //    AZ::TickBus::ExecuteQueuedEvents();
                    //}

                    if (result != AZ::IO::ZipDir::ZD_ERROR_SUCCESS)
                    {
                        return false/*AZ::Failure(AZStd::string::format(
                            "Unable to get archive information, target Bundle file path is ( %s ).", bpakFile.c_str()))*/;
                    }
                }

                return true;
            });

        return AZ::Success();
    }

    bool ApplicationManager::SeedsOperationRequiresCatalog(const SeedsParams& params)
    {
        return params.m_addSeedList.size() /*|| params.m_addPlatformToAllSeeds || params.m_updateSeedPathHint || params.m_print*/;
    }

    bool ApplicationManager::RunPlatformSpecificAssetListCommands(const AssetListsParams& params, AzFramework::PlatformFlags platformFlags)
    {
        using namespace AzToolsFramework;
        auto platformIds = AzFramework::PlatformHelper::GetPlatformIndices(platformFlags);
        auto platformIdsInterpreted = AzFramework::PlatformHelper::GetPlatformIndicesInterpreted(platformFlags);

        AZStd::unordered_set<AZ::Data::AssetId> exclusionList;
        AZStd::vector<AZStd::string> wildcardPatternExclusionList;

        for (const AZStd::string& asset : params.m_skipList)
        {
            // Is input a wildcard pattern?
            if (LooksLikeWildcardPattern(asset))
            {
                wildcardPatternExclusionList.emplace_back(asset);
                continue;
            }

            // Is input a valid asset in the cache?
            AZ::Data::AssetId assetId = m_assetSeedManager->GetAssetIdByPath(asset, platformFlags);
            if (assetId.IsValid())
            {
                exclusionList.emplace(assetId);
            }

            //TODO: we need to remove assets that are excluded before writing them to the assethints file.
        }

        AZStd::atomic_uint failureCount = 0;
        AZ::parallel_for_each(platformIdsInterpreted.begin(), platformIdsInterpreted.end(),
            [this, &params, &failureCount, &exclusionList, &wildcardPatternExclusionList](AzFramework::PlatformId platformId)
            {
                AzFramework::PlatformFlags platformFlags = AzFramework::PlatformHelper::GetPlatformFlagFromPlatformIndex(platformId);
                auto platformIndices = AzFramework::PlatformHelper::GetPlatformIndicesInterpreted(platformFlags);

                FilePath platformSpecificPakAssetHintFilePath =
                    FilePath(params.m_assetHintsFile.AbsolutePath(), AzFramework::PlatformHelper::GetPlatformName(platformId));
                AZStd::string pakAssetHintFileAbsolutePath = platformSpecificPakAssetHintFilePath.AbsolutePath();

                // iterate to each seed list based on pack id from high to lowest pack id value
                AssetPackInfoMap allAssetMap;
                for (auto seedIter = params.m_seedList.rbegin(); seedIter != params.m_seedList.rend(); ++seedIter)
                {
                    AddOrRemoveSeeds(platformId, seedIter->second, true); // Add Seeds

                    // get the dependency of seeds with the same pack id
                    AssetFileInfoList assetFileInfoList = m_assetSeedManager->GetDependencyList(platformIndices[0], exclusionList, nullptr, wildcardPatternExclusionList);

                    AssetPackInfoMap assetMap;
                    // add to asset map
                    for (auto& assetFileInfo : assetFileInfoList.m_fileInfoList)
                    {
                        BRAssetBundler::AddAssetPackInfoToMap(assetMap, assetFileInfo.m_assetId, assetFileInfo.m_assetRelativePath, seedIter->first);
                    }


                    // copy the pack id to the seed group's descendants
                    AssetGraphWalker debugInfo;
                    debugInfo.CascadeValuesToMap(assetMap, params.m_levelsPackIdMapping, platformId, exclusionList, wildcardPatternExclusionList);
                    
                    AddOrRemoveSeeds(platformId, seedIter->second, false); // Remove Seeds

                    allAssetMap.insert(assetMap.begin(), assetMap.end());
                }

                IdPackInfoListMap idPackInfoListMap;
                ConvertMapToPackIdKeyedMap(allAssetMap, idPackInfoListMap);

                AZ::StringFunc::Path::ReplaceExtension(pakAssetHintFileAbsolutePath, PakAssetHintsExtension);
                AZ_TracePrintf(BRAssetBundler::AppWindowName, "Saving Pak Asset Hints File to ( %s )...\n", pakAssetHintFileAbsolutePath.c_str());
                WriteAssetHints(idPackInfoListMap, pakAssetHintFileAbsolutePath);
            });

        return true;
    }

    AZStd::vector<FilePath> ApplicationManager::GetAllPlatformSpecificFilesOnDisk(
        const FilePath& platformIndependentFilePath, AzFramework::PlatformFlags platformFlags)
    {
        using namespace AzToolsFramework;
        AZStd::vector<FilePath> platformSpecificPaths;

        if (platformIndependentFilePath.AbsolutePath().empty())
        {
            return platformSpecificPaths;
        }

        FilePath testFilePath;

        for (AZStd::string_view platformName : AzFramework::PlatformHelper::GetPlatformsInterpreted(platformFlags))
        {
            testFilePath = FilePath(platformIndependentFilePath.AbsolutePath(), platformName);
            if (!testFilePath.AbsolutePath().empty() && AZ::IO::FileIOBase::GetInstance()->Exists(testFilePath.AbsolutePath().c_str()))
            {
                platformSpecificPaths.emplace_back(testFilePath.AbsolutePath());
            }
        }

        return platformSpecificPaths;
    }

    AZ::Outcome<void, AZStd::string> ApplicationManager::ApplyBundleSettingsOverrides(
        AzToolsFramework::AssetBundleSettings& bundleSettings,
        const AZStd::string& assetListFilePath,
        const AZStd::string& outputBundleFilePath,
        int bundleVersion,
        int maxBundleSize)
    {
        using namespace AzToolsFramework;

        // Asset List file path
        if (!assetListFilePath.empty())
        {
            FilePath platformSpecificPath = FilePath(assetListFilePath, bundleSettings.m_platform);
            if (platformSpecificPath.AbsolutePath().empty())
            {
                return AZ::Failure(AZStd::string::format(
                    "Failed to apply Bundle Settings overrides: ( %s ) is incompatible with input Bundle Settings file.",
                    assetListFilePath.c_str()));
            }
            bundleSettings.m_assetFileInfoListPath = platformSpecificPath.AbsolutePath();
        }

        // Output Bundle file path
        if (!outputBundleFilePath.empty())
        {
            FilePath platformSpecificPath = FilePath(outputBundleFilePath, bundleSettings.m_platform);
            if (platformSpecificPath.AbsolutePath().empty())
            {
                return AZ::Failure(AZStd::string::format(
                    "Failed to apply Bundle Settings overrides: ( %s ) is incompatible with input Bundle Settings file.",
                    outputBundleFilePath.c_str()));
            }
            bundleSettings.m_bundleFilePath = platformSpecificPath.AbsolutePath();
        }

        // Bundle Version
        if (bundleVersion > 0 && bundleVersion <= AzFramework::AssetBundleManifest::CurrentBundleVersion)
        {
            bundleSettings.m_bundleVersion = bundleVersion;
        }

        // Max Bundle Size
        if (maxBundleSize > 0 && maxBundleSize <= AssetBundleSettings::GetMaxBundleSizeInMB())
        {
            bundleSettings.m_maxBundleSizeInMB = maxBundleSize;
        }

        return AZ::Success();
    }

    AZ::Outcome<AzFramework::PlatformFlags, AZStd::string> ApplicationManager::GetPlatformArg(const AZ::CommandLine* parser)
    {
        using namespace AzFramework;
        PlatformFlags platform = AzFramework::PlatformFlags::Platform_NONE;
        if (!parser->HasSwitch(PlatformArg))
        {
            return AZ::Success(platform);
        }

        size_t numValues = parser->GetNumSwitchValues(PlatformArg);
        if (numValues <= 0)
        {
            return AZ::Failure(AZStd::string::format("Invalid command: \"--%s\" must have at least one value.", PlatformArg));
        }

        for (int platformIdx = 0; platformIdx < numValues; ++platformIdx)
        {
            AZStd::string platformStr = parser->GetSwitchValue(PlatformArg, platformIdx);
            platform |= AzFramework::PlatformHelper::GetPlatformFlag(platformStr);
        }

        return AZ::Success(platform);
    }

    AzFramework::PlatformFlags ApplicationManager::GetInputPlatformFlagsOrEnabledPlatformFlags(
        AzFramework::PlatformFlags inputPlatformFlags)
    {
        using namespace AzToolsFramework;
        if (inputPlatformFlags != AzFramework::PlatformFlags::Platform_NONE)
        {
            return inputPlatformFlags;
        }

        // If no platform was specified, defaulting to platforms specified in the asset processor config files
        AzFramework::PlatformFlags platformFlags = GetEnabledPlatformFlags(
            AZStd::string_view{ AZ::Utils::GetEnginePath() }, AZStd::string_view{ AZ::Utils::GetEnginePath() },
            AZStd::string_view{ AZ::Utils::GetProjectPath() });
        [[maybe_unused]] auto platformsString = AzFramework::PlatformHelper::GetCommaSeparatedPlatformList(platformFlags);

        AZ_TracePrintf(AppWindowName, "No platform specified, defaulting to platforms ( %s ).\n", platformsString.c_str());
        return platformFlags;
    }

    AZ::Outcome<BundlesParamsList, AZStd::string> ApplicationManager::ParseBundleSettingsAndOverrides(
        const AZ::CommandLine* parser, const char* commandName)
    {
        // Read in Bundle Settings File args
        auto bundleSettingsOutcome = GetArgsList<FilePath>(parser, BundleSettingsFileArg, commandName);
        if (!bundleSettingsOutcome.IsSuccess())
        {
            return AZ::Failure(bundleSettingsOutcome.GetError());
        }

        // Read in Asset List File args
        auto assetListOutcome = GetArgsList<FilePath>(parser, AssetListFileArg, commandName);
        if (!assetListOutcome.IsSuccess())
        {
            return AZ::Failure(assetListOutcome.GetError());
        }

        // Read in Output Bundle Path args
        auto bundleOutputPathOutcome = GetArgsList<FilePath>(parser, OutputBundlePathArg, commandName);
        if (!bundleOutputPathOutcome.IsSuccess())
        {
            return AZ::Failure(bundleOutputPathOutcome.GetError());
        }

        AZStd::vector<FilePath> bundleSettingsFileList = bundleSettingsOutcome.TakeValue();
        AZStd::vector<FilePath> assetListFileList = assetListOutcome.TakeValue();
        AZStd::vector<FilePath> outputBundleFileList = bundleOutputPathOutcome.TakeValue();

        size_t bundleSettingListSize = bundleSettingsFileList.size();
        size_t assetFileListSize = assetListFileList.size();
        size_t outputBundleListSize = outputBundleFileList.size();

        // * We are validating the following cases here
        // * AssetFileList should always be equal to outputBundleList size even if they are of zero length.
        // * BundleSettingList can be a zero size list if the number of elements in assetFileList matches the number of elements in
        // outputBundleList.
        // * If bundleSettingList contains non zero elements than either it should have the same number of elements as in assetFileList or
        // the number of elements in assetFileList should be zero.
        if (bundleSettingListSize)
        {
            if (assetFileListSize != outputBundleListSize)
            {
                return AZ::Failure(AZStd::string::format(
                    "Invalid command:  \"--%s\" and \"--%s\" are required and should contain the same number of args.", AssetListFileArg,
                    OutputBundlePathArg));
            }
            else if (bundleSettingListSize != assetFileListSize && assetFileListSize != 0)
            {
                return AZ::Failure(AZStd::string::format(
                    "Invalid command: \"--%s\", \"--%s\" and \"--%s\" should contain the same number of args.", BundleSettingsFileArg,
                    AssetListFileArg, OutputBundlePathArg));
            }
        }
        else
        {
            if (assetFileListSize != outputBundleListSize)
            {
                return AZ::Failure(AZStd::string::format(
                    "Invalid command:  \"--%s\" and \"--%s\" are required and should contain the same number of args.", AssetListFileArg,
                    OutputBundlePathArg));
            }
        }

        size_t expectedListSize = AZStd::max(assetFileListSize, bundleSettingListSize);

        // Read in Bundle Version args
        auto bundleVersionOutcome = GetArgsList<AZStd::string>(parser, BundleVersionArg, commandName);
        if (!bundleVersionOutcome.IsSuccess())
        {
            return AZ::Failure(bundleVersionOutcome.GetError());
        }

        AZStd::vector<AZStd::string> bundleVersionList = bundleVersionOutcome.TakeValue();
        size_t bundleVersionListSize = bundleVersionList.size();

        if (bundleVersionListSize != expectedListSize && bundleVersionListSize >= 2)
        {
            if (expectedListSize != 1)
            {
                return AZ::Failure(AZStd::string::format(
                    "Invalid command: Number of args in \"--%s\" can either be zero, one or %zu. Actual size detected %zu.",
                    BundleVersionArg, expectedListSize, bundleVersionListSize));
            }
            else
            {
                return AZ::Failure(AZStd::string::format(
                    "Invalid command: Number of args in \"--%s\" is %zu. Expected number of args is one.", BundleVersionArg,
                    bundleVersionListSize));
            }
        }

        // Read in Max Bundle Size args
        auto maxBundleSizeOutcome = GetArgsList<AZStd::string>(parser, MaxBundleSizeArg, commandName);
        if (!maxBundleSizeOutcome.IsSuccess())
        {
            return AZ::Failure(maxBundleSizeOutcome.GetError());
        }

        AZStd::vector<AZStd::string> maxBundleSizeList = maxBundleSizeOutcome.TakeValue();
        size_t maxBundleListSize = maxBundleSizeList.size();

        if (maxBundleListSize != expectedListSize && maxBundleListSize >= 2)
        {
            if (expectedListSize != 1)
            {
                return AZ::Failure(AZStd::string::format(
                    "Invalid command: Number of args in \"--%s\" can either be zero, one or %zu. Actual size detected %zu.",
                    MaxBundleSizeArg, expectedListSize, maxBundleListSize));
            }
            else
            {
                return AZ::Failure(AZStd::string::format(
                    "Invalid command: Number of args in \"--%s\" is %zu. Expected number of args is one.", MaxBundleSizeArg,
                    maxBundleListSize));
            }
        }

        // Read in Platform arg
        auto platformOutcome = GetPlatformArg(parser);
        if (!platformOutcome.IsSuccess())
        {
            return AZ::Failure(platformOutcome.GetError());
        }

        // Read in Allow Overwrites flag
        bool allowOverwrites = parser->HasSwitch(AllowOverwritesFlag);
        BundlesParamsList bundleParamsList;

        for (int idx = 0; idx < expectedListSize; idx++)
        {
            BundlesParams bundleParams;
            bundleParams.m_bundleSettingsFile = bundleSettingListSize ? bundleSettingsFileList[idx] : FilePath();
            bundleParams.m_assetListFile = assetFileListSize ? assetListFileList[idx] : FilePath();
            bundleParams.m_outputBundlePath = outputBundleListSize ? outputBundleFileList[idx] : FilePath();
            if (bundleVersionListSize)
            {
                bundleParams.m_bundleVersion =
                    bundleVersionListSize == 1 ? AZStd::stoi(bundleVersionList[0]) : AZStd::stoi(bundleVersionList[idx]);
            }

            if (maxBundleListSize)
            {
                bundleParams.m_maxBundleSizeInMB =
                    maxBundleListSize == 1 ? AZStd::stoi(maxBundleSizeList[0]) : AZStd::stoi(maxBundleSizeList[idx]);
            }

            bundleParams.m_platformFlags = platformOutcome.GetValue();
            bundleParams.m_allowOverwrites = allowOverwrites;
            bundleParamsList.emplace_back(bundleParams);
        }

        return AZ::Success(bundleParamsList);
    }

    template<typename T>
    AZ::Outcome<AZStd::vector<T>, AZStd::string> ApplicationManager::GetArgsList(
        const AZ::CommandLine* parser, const char* argName, const char* subCommandName, bool isRequired)
    {
        AZStd::vector<T> args;

        if (!parser->HasSwitch(argName))
        {
            if (isRequired)
            {
                return AZ::Failure(
                    AZStd::string::format("Invalid command: \"--%s\" is required when running \"%s\".", argName, subCommandName));
            }

            return AZ::Success(args);
        }

        size_t numValues = parser->GetNumSwitchValues(argName);

        for (int idx = 0; idx < numValues; ++idx)
        {
            args.emplace_back(T(parser->GetSwitchValue(argName, idx)));
        }

        return AZ::Success(args);
    }

    IdPackInfoListMap ApplicationManager::GetAddSeedArgList(const AZ::CommandLine* parser, AssetPackInfoList* levelAssetHints)
    {
        IdPackInfoListMap seedPackInfoList;
        size_t numAddSeedArgs = parser->GetNumSwitchValues(AddSeedArg);
        AZ::IO::Path fullPath, projectPath = AZ::IO::PathView(AZ::Utils::GetProjectPath());
        for (size_t addSeedIndex = 0; addSeedIndex < numAddSeedArgs; ++addSeedIndex)
        {
            AZ::u32 uPackId = 0;
            auto addSeedVal = parser->GetSwitchValue(AddSeedArg, addSeedIndex);
            auto assetHint = addSeedVal;
            auto firstMarkerPos = addSeedVal.find(PackIdFirstMarker);
            if (firstMarkerPos != AZStd::string::npos)
            {
                assetHint = addSeedVal.substr(0, firstMarkerPos);
                auto secondMarkerPos = addSeedVal.find(PackIdSecondMarker);
                if (secondMarkerPos == AZStd::string::npos)
                {
                    AZ_Warning(AppWindowName, false, "Expected a second marker(']') after finding the first one.");
                    secondMarkerPos = addSeedVal.size();
                }
                    
                uPackId = AZStd::stoi(addSeedVal.substr(firstMarkerPos+1, secondMarkerPos-1));
            }

            // Trim if path has separator at start
            if (assetHint.starts_with(AZ::IO::PosixPathSeparator) || assetHint.starts_with(AZ::IO::WindowsPathSeparator))
            {
                assetHint = assetHint.substr(1, assetHint.size() - 1);
            }

            AZStd::to_lower(assetHint.begin(), assetHint.end());

            // Get the Levels paths. there could be multiple levels in a pak.
            AZ::IO::Path assetPath(AZ::IO::PosixPathSeparator); // force it to use posix separators for consistency
            assetPath /= assetHint;

            seedPackInfoList[uPackId].emplace(AssetPackInfo(AZ::Data::AssetId(), assetPath.LexicallyNormal().String(), uPackId));
            if (assetPath.Match(LevelsPathPattern)) // check if asset path belongs to a level
            {
                assetPath.ReplaceExtension(AssetHintsExtension); // replace extension to `.assethints`
                fullPath = projectPath / assetPath;
                if (levelAssetHints != nullptr)
                {
                    levelAssetHints->emplace(AssetPackInfo(fullPath.String()));
                }
                    
            }
                
        }
        return seedPackInfoList;
    }

    AZStd::vector<AZStd::string> ApplicationManager::GetSkipArgList(const AZ::CommandLine* parser)
    {
        AZStd::vector<AZStd::string> skipList;
        size_t numArgs = parser->GetNumSwitchValues(SkipArg);
        for (size_t argIndex = 0; argIndex < numArgs; ++argIndex)
        {
            skipList.push_back(parser->GetSwitchValue(SkipArg, argIndex));
        }
        return skipList;
    }

    bool ApplicationManager::RunSeedsCommands(const AZ::Outcome<SeedsParams, AZStd::string>& paramsOutcome)
    {
        using namespace AzFramework;
        if (!paramsOutcome.IsSuccess())
        {
            AZ_Error(AppWindowName, false, paramsOutcome.GetError().c_str());
            return false;
        }

        SeedsParams params = paramsOutcome.GetValue();

        if (SeedsOperationRequiresCatalog(params))
        {
            // Asset Catalog
            auto catalogOutcome = InitAssetCatalog(params.m_platformFlags, params.m_assetCatalogFile.AbsolutePath());
            if (!catalogOutcome.IsSuccess())
            {
                AZ_Error(AppWindowName, false, catalogOutcome.GetError().c_str());
                return false;
            }
        }

        // Seed List File
        AZStd::string seedAssetHintFile = params.m_seedListFile.AbsolutePath();
        AzFramework::StringFunc::Path::ReplaceExtension(seedAssetHintFile, SeedAssetHintsExtension);
        AssetPackInfoMap allAssetMap;
        bool fileExists = AZ::IO::FileIOBase::GetInstance()->Exists(seedAssetHintFile.c_str());
        if (fileExists)
        {
            ReadAssetHints(
                seedAssetHintFile, params.m_platformFlags,
                [&](const AssetPackInfo& packInfo)
                {
                    AddAssetPackInfoToMap(allAssetMap, packInfo);
                });
        }

        // Add Seeds
        for (const auto& seed : params.m_addSeedList)
        {
            for (auto packInfo : seed.second)
            {
                packInfo.m_assetId = GetAssetIdByPath(packInfo.m_assetRelativePath, params.m_platformFlags);
                AddAssetPackInfoToMap(allAssetMap, packInfo);
            }
        }

        // Remove Seeds
        for (const auto& seed : params.m_removeSeedList)
        {
            auto assetId = GetAssetIdByPath(seed, params.m_platformFlags);
            RemoveAssetPackInfoFromMap(allAssetMap, assetId);
        }

        IdPackInfoListMap idPackInfoListMap;
        ConvertMapToPackIdKeyedMap(allAssetMap, idPackInfoListMap);

        // Save
        AZ_TracePrintf(AppWindowName, "Saving Seed Asset Hints List to ( %s )...\n", seedAssetHintFile.c_str());
        WriteAssetHints(idPackInfoListMap, seedAssetHintFile);
        AZ_TracePrintf(AppWindowName, "Save successful!\n");

        return true;
    }

    bool ApplicationManager::RunAssetListsCommands(const AZ::Outcome<AssetListsParams, AZStd::string>& paramsOutcome)
    {
        using namespace AzFramework;
        if (!paramsOutcome.IsSuccess())
        {
            AZ_Error(AppWindowName, false, paramsOutcome.GetError().c_str());
            return false;
        }

        AssetListsParams params = paramsOutcome.GetValue();

        // Asset Catalog
        auto catalogOutcome = InitAssetCatalog(params.m_platformFlags, params.m_assetCatalogFile.AbsolutePath());
        if (!catalogOutcome.IsSuccess())
        {
            AZ_Error(AppWindowName, false, catalogOutcome.GetError().c_str());
            return false;
        }

        // Seed List Files
        AZ::Outcome<void, AZStd::string> seedListOutcome;
        AZStd::string seedListFileAbsolutePath;
        for (const FilePath& seedListFile : params.m_seedListFiles)
        {
            seedListFileAbsolutePath = seedListFile.AbsolutePath();
            AzFramework::StringFunc::Path::ReplaceExtension(seedListFileAbsolutePath, SeedAssetHintsExtension);
            if (!AZ::IO::FileIOBase::GetInstance()->Exists(seedListFileAbsolutePath.c_str()))
            {
                AZ_Error(
                    AppWindowName, false, "Cannot load Seed List file ( %s ): File does not exist.\n", seedListFileAbsolutePath.c_str());
                return false;
            }

            seedListOutcome = ReadAssetHints(
                seedListFileAbsolutePath, params.m_platformFlags,
                [&](const AssetPackInfo& packInfo)
                {
                    params.m_seedList[packInfo.m_packId].emplace(packInfo);
                });

            if (!seedListOutcome.IsSuccess())
            {
                AZ_Error(AppWindowName, false, seedListOutcome.GetError().c_str());
                return false;
            }
        }

        MergeLevelAssetHints(params.m_levelAssetHints, params.m_levelsAssetIdMapping, params.m_levelsPackIdMapping, params.m_platformFlags);
        
        if (!RunPlatformSpecificAssetListCommands(params, params.m_platformFlags))
        {
            return false;
        }

        return true;
    }

    bool ApplicationManager::RunBundlesCommands(const AZ::Outcome<BundlesParamsList, AZStd::string>& paramsOutcome)
    {
        using namespace AzToolsFramework;
        if (!paramsOutcome.IsSuccess())
        {
            AZ_Error(AppWindowName, false, paramsOutcome.GetError().c_str());
            return false;
        }

        BundlesParamsList paramsList = paramsOutcome.GetValue();
        AllBundleSetting allBundleSettings;
        for (BundlesParams& params : paramsList)
        {
            auto preBundlingOutcome = DoPreBundlingStep(params, allBundleSettings);
            if (!preBundlingOutcome.IsSuccess())
            {
                AZ_Error(AppWindowName, false, preBundlingOutcome.GetError().c_str());
                return false;
            }
        }

        AZStd::atomic_uint failureCount = 0;

        // Create all Bundles
        AZ::parallel_for_each(
            allBundleSettings.begin(), allBundleSettings.end(),
            [this, &failureCount](AZStd::pair<AzToolsFramework::AssetBundleSettings, BundlesParams> bundleSettings)
            {
                BundlesParams params = bundleSettings.second;
                auto overrideOutcome = ApplyBundleSettingsOverrides(
                    bundleSettings.first, params.m_assetListFile.AbsolutePath(), params.m_outputBundlePath.AbsolutePath(),
                    params.m_bundleVersion, params.m_maxBundleSizeInMB);
                if (!overrideOutcome.IsSuccess())
                {
                    // Metric event has already been sent
                    AZ_Error(AppWindowName, false, overrideOutcome.GetError().c_str());
                    failureCount.fetch_add(1, AZStd::memory_order::memory_order_relaxed);
                    return;
                }

                PathPackInfoMap infoMap;
                FilePath assetListFilePath(bundleSettings.first.m_assetFileInfoListPath);
                AZStd::string assetListHintFile = assetListFilePath.AbsolutePath();
                AzFramework::StringFunc::Path::ReplaceExtension(assetListHintFile, PakAssetHintsExtension);
                // TODO: this part of the code is dealing with each platform so reading asset hints with platform flags is unnecessary
                ReadAssetHints(
                    assetListHintFile, params.m_platformFlags,
                    [&](const AssetPackInfo& packInfo)
                    {
                        AddAssetPackInfoToMap(infoMap, packInfo);
                    });


                FilePath bundleFilePath(bundleSettings.first.m_bundleFilePath);
                AZStd::string profilingLog = bundleFilePath.AbsolutePath();
                AzFramework::StringFunc::Path::ReplaceExtension(profilingLog, ProfilingLogExtension);
                // Check if we are performing a destructive overwrite that the user did not approve
                if (!params.m_allowOverwrites && AZ::IO::FileIOBase::GetInstance()->Exists(profilingLog.c_str()))
                {
                    AZ_Error(
                        AppWindowName, false,
                        "Profiling log ( %s ) already exists, running this command would perform a destructive overwrite.\n\n"
                        "Run your command again with the ( --%s ) arg if you want to save over the existing file.",
                        profilingLog.c_str(), AllowOverwritesFlag);
                    failureCount.fetch_add(1, AZStd::memory_order::memory_order_relaxed);
                    return;
                }

                AZ_TracePrintf(AppWindowName, "Creating Profiling Log ( %s )...\n", profilingLog.c_str());
                PathPackInfoMap archiveInfoMap;
                auto listFilesOutcome = ListFilesInArchiveAndRename(bundleFilePath.AbsolutePath(), archiveInfoMap, params.m_allowOverwrites);
                if (!listFilesOutcome.IsSuccess())
                {
                    AZ_Error(AppWindowName, false, listFilesOutcome.GetError().c_str());
                    failureCount.fetch_add(1, AZStd::memory_order::memory_order_relaxed);
                    return;
                }

                IdPackInfoListMap infoListMap;
                ConvertMapToPackIdKeyedMap(infoMap, infoListMap); // convert it to a more easier struct to process

                auto writeLogsOutcome = WriteProfilingLogs(profilingLog, infoListMap, archiveInfoMap);
                if (!writeLogsOutcome.IsSuccess())
                {
                    // Metric event has already been sent
                    AZ_Error(AppWindowName, false, writeLogsOutcome.GetError().c_str());
                    failureCount.fetch_add(1, AZStd::memory_order::memory_order_relaxed);
                    return;
                }
                
                AZ_TracePrintf(AppWindowName, "Profiling Log ( %s ) created successfully!\n", profilingLog.c_str());
            });

        return failureCount == 0;
    }

    AZStd::string ApplicationManager::GetBinaryArgOptionFailure(const char* arg1, const char* arg2)
    {
        const char FailureMessage[] = "Missing argument: Either %s or %s must be supplied";
        return AZStd::string::format(FailureMessage, arg1, arg2);
    }

    AZ::u32 ApplicationManager::LaunchProcess(const AZStd::string& exePath, const AZStd::string& commandLineArgs)
    {
        auto assetBundlerJob = [=]()
        {
            AzFramework::ProcessLauncher::ProcessLaunchInfo info;
            info.m_commandlineParameters = exePath + " " + commandLineArgs;

            info.m_showWindow = false;
            
            AZStd::unique_ptr<AzFramework::ProcessWatcher> watcher(
                AzFramework::ProcessWatcher::LaunchProcess(info, AzFramework::ProcessCommunicationType::COMMUNICATOR_TYPE_STDINOUT));

            AZStd::string consoleOutput;
            AZ::u32 exitCode = 0;
            if (watcher)
            {
                AZStd::string consoleBuffer;
                while (watcher->IsProcessRunning(&exitCode))
                {
                    watcher->WaitForProcessToExit(g_sleepDuration, &exitCode);
                    AZ::u32 outputSize = watcher->GetCommunicator()->PeekOutput();
                    if (outputSize)
                    {
                        consoleBuffer.resize(outputSize);
                        watcher->GetCommunicator()->ReadOutput(consoleBuffer.data(), outputSize);
                        consoleOutput += consoleBuffer;
                    }
                }
            }
            AZ_Printf(AppWindowName, consoleOutput.c_str());

            return exitCode;
        };
        return assetBundlerJob();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////
    // Formatting for Output Text
    ////////////////////////////////////////////////////////////////////////////////////////////

    bool ApplicationManager::OnPreError(const char* window, const char* fileName, int line, [[maybe_unused]] const char* func, const char* message)
    {
        printf("\n");
        printf("[ERROR] - %s:\n", window);

        if (m_showVerboseOutput)
        {
            printf("(%s - Line %i)\n", fileName, line);
        }

        printf("%s", message);
        printf("\n");
        return true;
    }

    bool ApplicationManager::OnPreWarning(const char* window, const char* fileName, int line, [[maybe_unused]] const char* func, const char* message)
    {
        printf("\n");
        printf("[WARN] - %s:\n", window);

        if (m_showVerboseOutput)
        {
            printf("(%s - Line %i)\n", fileName, line);
        }

        printf("%s", message);
        printf("\n");
        return true;
    }

    bool ApplicationManager::OnPrintf(const char* window, const char* message)
    {
        if (window == BRAssetBundler::AppWindowName || (m_showVerboseOutput && window == BRAssetBundler::AppWindowNameVerbose))
        {
            printf("%s", message);
            return true;
        }

        return !m_showVerboseOutput;
    }
} // namespace BRAssetBundler
