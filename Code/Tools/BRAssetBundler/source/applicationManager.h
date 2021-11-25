/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzToolsFramework/Application/ToolsApplication.h>
#include <AzToolsFramework/Asset/AssetSeedManager.h>
#include <AzToolsFramework/Asset/AssetBundler.h>
#include <AzCore/Debug/TraceMessageBus.h>
#include <source/utils.h>
#include <AzToolsFramework/AssetCatalog/PlatformAddressedAssetCatalogManager.h>
#include <AzFramework/Archive/IArchive.h>

namespace BRAssetBundler
{
    struct SeedsParams
    {
        AZ_CLASS_ALLOCATOR(SeedsParams, AZ::SystemAllocator, 0);

        FilePath m_seedListFile;
        IdPackInfoListMap m_addSeedList;
        AZStd::vector<AZStd::string> m_removeSeedList;

        bool m_ignoreFileCase = false;
        AZ::u32 m_packId = DefaultPackIdValue;
        AzFramework::PlatformFlags m_platformFlags = AzFramework::PlatformFlags::Platform_NONE;
        FilePath m_assetCatalogFile;
    };

    struct AssetListsParams
    {
        AZ_CLASS_ALLOCATOR(AssetListsParams, AZ::SystemAllocator, 0);

        //! mapping for asset id to a list of AssetPackInfo
        AssetPackInfoMap m_levelsAssetIdMapping;
        //! mapping for pack id to a list of AssetPackInfo
        IdAssetIdListMap m_levelsPackIdMapping;
        IdPackInfoListMap m_seedList;
        AssetPackInfoList m_levelAssetHints;

        AZStd::vector<FilePath> m_seedListFiles;
        AZStd::vector<AZStd::string> m_skipList;

        bool m_print = false;
        bool m_allowOverwrites = false;
        AZ::u32 m_packId = DefaultPackIdValue;

        AzFramework::PlatformFlags m_platformFlags = AzFramework::PlatformFlags::Platform_NONE;
        FilePath m_assetHintsFile;
        FilePath m_assetCatalogFile;
    };

    struct BundleSettingsParams
    {
        AZ_CLASS_ALLOCATOR(BundleSettingsParams, AZ::SystemAllocator, 0);

        FilePath m_bundleSettingsFile;
        FilePath m_assetListFile;
        FilePath m_outputBundlePath;

        int m_bundleVersion = -1;
        int m_maxBundleSizeInMB = -1;

        bool m_print = false;

        AzFramework::PlatformFlags m_platformFlags = AzFramework::PlatformFlags::Platform_NONE;
    };

    struct BundlesParams
    {
        AZ_CLASS_ALLOCATOR(BundlesParams, AZ::SystemAllocator, 0);

        FilePath m_bundleSettingsFile;
        FilePath m_assetListFile;
        FilePath m_outputBundlePath;

        int m_bundleVersion = -1;
        int m_maxBundleSizeInMB = -1;
        AZ::u32 m_packId = DefaultPackIdValue;

        AzFramework::PlatformFlags m_platformFlags = AzFramework::PlatformFlags::Platform_NONE;

        bool m_allowOverwrites = false;
    };

    typedef AZStd::vector<BundlesParams> BundlesParamsList;

    struct BundleSeedParams
    {
        AZ_CLASS_ALLOCATOR(BundleSeedParams, AZ::SystemAllocator, 0);

        IdPackInfoListMap m_addSeedList;
        AssetPackInfoList m_levelAssetHints;

        BundlesParams m_bundleParams;
    };

    using AllBundleSetting = AZStd::vector<AZStd::pair<AzToolsFramework::AssetBundleSettings, BundlesParams>>;

    struct MergeAssetHintsParams
    {
        AZ_CLASS_ALLOCATOR(MergeAssetHintsParams, AZ::SystemAllocator, 0);

        AZStd::vector<FilePath> m_assetHintsFiles;
        FilePath m_outputSampLogPath;

        AzFramework::PlatformFlags m_platformFlags = AzFramework::PlatformFlags::Platform_NONE;

        bool m_allowOverwrites = false;
    };

    class ApplicationManager 
        : public AzToolsFramework::ToolsApplication
        , public AZ::Debug::TraceMessageBus::Handler
    {
    public:
        ApplicationManager(int* argc, char*** argv);
        virtual ~ApplicationManager();

        virtual bool Init();
        void DestroyApplication();
        virtual bool Run();

        ////////////////////////////////////////////////////////////////////////////////////////////
        // AzFramework::Application overrides
        AZ::ComponentTypeList GetRequiredSystemComponents() const override;
        ////////////////////////////////////////////////////////////////////////////////////////////
        // 
        ////////////////////////////////////////////////////////////////////////////////////////////
        // TraceMessageBus Interface
        bool OnPreError(const char* window, const char* fileName, int line, const char* func, const char* message) override;
        bool OnPreWarning(const char* window, const char* fileName, int line, const char* func, const char* message) override;
        bool OnPrintf(const char* window, const char* message) override;
        ////////////////////////////////////////////////////////////////////////////////////////////

    private:

        ////////////////////////////////////////////////////////////////////////////////////////////
        // Get Generic Command Info
        CommandType GetCommandType(const AzFramework::CommandLine* parser, bool suppressErrors);
        bool ShouldPrintHelp(const AZ::CommandLine* parser);
        bool ShouldPrintVerbose(const AZ::CommandLine* parser);
        AZStd::string GetCleanCommandLine(const AZ::CommandLine* parser, CommandType commandType);
        void InitArgValidationLists();
        ////////////////////////////////////////////////////////////////////////////////////////////

        ////////////////////////////////////////////////////////////////////////////////////////////
        // Store Detailed Command Info and Validate parser input (command correctness)
        AZ::Outcome<SeedsParams, AZStd::string> ParseSeedsCommandData(const AzFramework::CommandLine* parser);
        AZ::Outcome<AssetListsParams, AZStd::string> ParseAssetListsCommandData(const AzFramework::CommandLine* parser);
        AZ::Outcome<BundlesParamsList, AZStd::string> ParseBundlesCommandData(const AzFramework::CommandLine* parser);
        AZ::Outcome<MergeAssetHintsParams, AZStd::string> ParseMergeAssetHintsCommandData(const AzFramework::CommandLine* parser);
        
        AZ::Outcome<void, AZStd::string> ValidateInputArgs(const AzFramework::CommandLine* parser, const AZStd::vector<const char*>& validArgList);
        AZ::Outcome<AZStd::string, AZStd::string> GetFilePathArg(const AzFramework::CommandLine* parser, const char* argName, const char* subCommandName, bool isRequired = false);
        template <typename T>
        AZ::Outcome<AZStd::vector<T>, AZStd::string> GetArgsList(const AzFramework::CommandLine* parser, const char* argName, const char* subCommandName, bool isRequired = false);
        IdPackInfoListMap GetAddSeedArgList(const AzFramework::CommandLine* parser, AZ::u32 globalPackId = DefaultPackIdValue, AssetPackInfoList* levelAssetHints = nullptr);
        AZStd::vector<AZStd::string> GetSkipArgList(const AzFramework::CommandLine* parser);
        ////////////////////////////////////////////////////////////////////////////////////////////

        ////////////////////////////////////////////////////////////////////////////////////////////
        // Run Commands and Validate param data (value correctness)
        bool RunSeedsCommands(const AZ::Outcome<SeedsParams, AZStd::string>& paramsOutcome);
        bool RunAssetListsCommands(const AZ::Outcome<AssetListsParams, AZStd::string>& paramsOutcome);
        bool RunBundlesCommands(const AZ::Outcome<BundlesParamsList, AZStd::string>& paramsOutcome);
        bool RunMergeAssetHintsCommands(const AZ::Outcome<MergeAssetHintsParams, AZStd::string>& paramsOutcome);
        ////////////////////////////////////////////////////////////////////////////////////////////
         
        ////////////////////////////////////////////////////////////////////////////////////////////
        // Helpers
        AZ::Outcome<void, AZStd::string> InitAssetCatalog(AzFramework::PlatformFlags platforms, const AZStd::string& assetCatalogFile = AZStd::string());
        bool RunPlatformSpecificAssetListCommands(const AssetListsParams& params, AzFramework::PlatformFlags platformFlags);
        AZStd::vector<FilePath> GetAllPlatformSpecificFilesOnDisk(const FilePath& platformIndependentFilePath, AzFramework::PlatformFlags platformFlags = AzFramework::PlatformFlags::Platform_NONE);
        AZ::Outcome<void, AZStd::string> ApplyBundleSettingsOverrides(
            AzToolsFramework::AssetBundleSettings& bundleSettings,
            const AZStd::string& assetListFilePath,
            const AZStd::string& outputBundleFilePath,
            int bundleVersion,
            int maxBundleSize);
        AZ::Outcome<AzFramework::PlatformFlags, AZStd::string> GetPlatformArg(const AzFramework::CommandLine* parser);
        AzFramework::PlatformFlags GetInputPlatformFlagsOrEnabledPlatformFlags(AzFramework::PlatformFlags inputPlatformFlags);
        AZ::Outcome<BundlesParamsList, AZStd::string> ParseBundleSettingsAndOverrides(const AzFramework::CommandLine* parser, const char* commandName);
        //! Error message to display when neither of two optional arguments was found
        static AZStd::string GetBinaryArgOptionFailure(const char* arg1, const char* arg2);
        AZ::u32 LaunchProcess(const AZStd::string& exePath, const AZStd::string& commandLineArgs);
        void AddOrRemoveSeeds(AzFramework::PlatformId platformId, AssetPackInfoList seedList, bool bAddSeed);
        //! Give a list of asset hints of particular levels the function will open and parse them and merge it to one AssetPackInfoMap
        void MergeLevelAssetHints(AssetPackInfoList fileList, AssetPackInfoMap& infoMap, IdAssetIdListMap& packInfoMap, AzFramework::PlatformFlags platformFlags, AZ::u32 globalPackId = DefaultPackIdValue);
        void MergeArchiveInfo(PathPackInfoMap archiveInfoMap, PathPackInfoMap& destInfoMap);
        AZ::Outcome<void, AZStd::string> DoPreBundlingStep(BundlesParams& params, AllBundleSetting& allBundleSettings);
        //! list all files in an archive(pak) and rename it to bpak.
        AZ::Outcome<void, AZStd::string> ListFilesInArchiveAndRename(const AZStd::string& bundleFilePath, PathPackInfoMap& outInfoMap, bool allowOverwrites);

        bool SeedsOperationRequiresCatalog(const SeedsParams& params);
        ////////////////////////////////////////////////////////////////////////////////////////////

        ////////////////////////////////////////////////////////////////////////////////////////////
        // Output Help Text
        void OutputHelp(CommandType commandType);
        void OutputHelpSeeds();
        void OutputHelpAssetLists();
        void OutputHelpComparisonRules();
        void OutputHelpCompare();
        void OutputHelpBundleSettings();
        void OutputHelpBundles();
        void OutputHelpBundleSeed();
        void OutputHelpMergeAssetHints();
        ////////////////////////////////////////////////////////////////////////////////////////////

        AZStd::unique_ptr<AzToolsFramework::AssetSeedManager> m_assetSeedManager;
        AZStd::unique_ptr<AzToolsFramework::PlatformAddressedAssetCatalogManager> m_platformCatalogManager;

        bool m_showVerboseOutput = false;
        AZStd::string m_currentProjectName;

        CommandType m_commandType = CommandType::Invalid;
        AZ::IO::IArchive* m_archive = nullptr;

        AZStd::vector<const char*> m_allSeedsArgs;
        AZStd::vector<const char*> m_allAssetListsArgs;
        AZStd::vector<const char*> m_allBundlesArgs;
        AZStd::vector<const char*> m_allMergeHintsArgs;
    };
}
