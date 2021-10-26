namespace BRAssetBundler
{
    namespace Platform
    {
        void ParseConsoleOutputFromListFilesInArchive(const AZStd::string& consoleOutput, AZStd::vector<AZStd::string>& fileEntries, AZStd::vector<AZ::u32>& fileOffsets, AZStd::vector<AZ::u32>& fileSizes)
        {
            AZStd::vector<AZStd::string> fileEntryData;
            AzFramework::StringFunc::Tokenize(consoleOutput.c_str(), fileEntryData, "\n");
            int startingLineIdx = 3; // first line that might contain the file name
            for (size_t lineIdx = startingLineIdx; lineIdx < fileEntryData.size(); ++lineIdx)
            {
                AZStd::string& line = fileEntryData[lineIdx];
                AZStd::vector<AZStd::string> lineEntryData;
                AzFramework::StringFunc::Tokenize(line.c_str(), lineEntryData, " ");
                AZStd::string& fileName = lineEntryData.back();

                if(fileName.back() == AZ_CORRECT_FILESYSTEM_SEPARATOR)
                {
                    // if the filename ends with a separator
                    // than it indicates that this is a directory
                    continue;
                }

                if(fileName.compare("-------") == 0)
                {
                    return;
                }

                fileEntries.emplace_back(fileName);
            }
        }
    }
}