#pragma once

#include <array>
#include "DataStructs.h"


class DirectoryData
{
	static constexpr std::array<char, 7> MAGIC_NUMBER = {'M','Y','D','I','R','1','3'};
	static constexpr size_t IO_BUFFER_SIZE = (1U << 20U); //1MB
	static constexpr size_t HASH_BUFFER_SIZE = (1U << 16U); //64KB
	static constexpr size_t MAX_FILE_NUM = 1048576;

	//owns the DirTreNodes 
	std::vector<DirTreeNode*> theIndex_;
	std::vector<FileInfo> fileEntries_;

	//std::unordered_multiset<FileInfo, FileInfo::HashFunction, FileInfo::IsEqual> fileGroups_{MAX_FILE_NUM};

	fs::path workDir_;

	void releaseChildren();

	fs::path getFsFilePath(DirTreeNodeRef dirRef, bool withRoot = false) const;

	DirTreeNode* toPtr(DirTreeNodeRef idx) const;

	bool writeNameTree(std::ostream& out);
	bool readNameTree(std::istream& in);

	bool writeFile(std::ostream& out, const FileInfo& file);
	bool writeFiles(std::ostream& out);
	bool unpackFiles(std::istream& in);

	void recreateEmptyDirs();
	bool findDuplicates();
	bool computeParialHshes
		(std::pair<std::vector<FileInfo>::iterator, std::vector<FileInfo>::iterator> range);

	bool computeFullHshes
		(std::pair<std::vector<FileInfo>::iterator, std::vector<FileInfo>::iterator> range);

public:
	bool preProcessSourceDir(const std::string &directory);
	~DirectoryData();
	void clearDirTree();

	bool write(std::ostream& out);

	bool read(std::istream& in);
};
