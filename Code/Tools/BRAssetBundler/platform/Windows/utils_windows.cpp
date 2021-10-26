#include <source/utils.h>
namespace BRAssetBundler
{
    namespace Platform
    {
        void ParseConsoleOutputFromListFilesInArchive(const AZStd::string& consoleOutput, const AZStd::string& bundlePath, PathPackInfoMap& outInfoMap)
        {
            AZStd::string bpakFile;
            AzFramework::StringFunc::Path::GetFullFileName(bundlePath.c_str(), bpakFile); // we only get the filename since we're only be profiling from the same folder and not sub-folders.

            AZStd::vector<AZStd::string> fileEntryData;
            AzFramework::StringFunc::Tokenize(consoleOutput.c_str(), fileEntryData, "\r\n");
            for (size_t slotNum = 0; slotNum < fileEntryData.size(); ++slotNum)
            {
                AZStd::string& line = fileEntryData[slotNum];
                if (AzFramework::StringFunc::StartsWith(line, "Path = "))
                {
                    if ((slotNum + 16) < fileEntryData.size())
                    {
                        // We're checking one past each entry we find for the Folder entry and skipping anything marked as a folder
                        // See sample output above
                        if (AzFramework::StringFunc::StartsWith(fileEntryData[slotNum + 1], "Folder = -"))
                        {
                            AzFramework::StringFunc::Replace(line, "Path = ", "", false, true);
                            auto relativePath = line;
                            outInfoMap[relativePath].m_assetRelativePath = line;
                            
                            line = fileEntryData[slotNum + 3]; // get the packed size
                            AzFramework::StringFunc::Replace(line, "Packed Size = ", "", false, true);
                            outInfoMap[relativePath].m_size = AZStd::stoi(line);

                            line = fileEntryData[slotNum + 16]; // get the offset
                            AzFramework::StringFunc::Replace(line, "Offset = ", "", false, true);
                            outInfoMap[relativePath].m_offset = AZStd::stoi(line);

                            outInfoMap[relativePath].m_bundlePath = bpakFile;

                            slotNum+=16;
                        }
                    }
                }
            }
        }
    }
}
