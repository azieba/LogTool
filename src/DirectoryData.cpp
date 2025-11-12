#include "DirectoryData.h"
#include "DataStructs.h"
#include <algorithm>
#include <iostream>
#include <xxhash.h>
#include <zstd.h>

extern bool verbose;

bool DirectoryData::preProcessSourceDir(const std::string& directory)
{
	workDir_ = directory;
	workDir_ = fs::canonical(workDir_);

	if (!fs::exists(workDir_) || !fs::is_directory(workDir_))
	{
		std::cerr << "Error: " << workDir_ << " is not a directory." << std::endl;
		return false;
	}

	std::cout << "Pre-processing " << workDir_ << '\n';

	auto* pRoot = new DirTreeNode(theIndex_);
	pRoot->name_ = workDir_.filename();
	if(verbose) std::cout << "root_.name_=" << pRoot->name_ << '\n';

	auto workDirDepth = std::distance(workDir_.begin(), workDir_.end());
	if(verbose) std::cout << "workDirDepth=" << workDirDepth << '\n';


	//each filename will be a node in the tree
	//aproximating some sane value
	theIndex_.reserve(MAX_FILE_NUM*2);
	fileEntries_.reserve(MAX_FILE_NUM);

	//Loading the directory structure into the tree structure 
	for (const auto &dir_entry : fs::recursive_directory_iterator(workDir_,
				fs::directory_options::skip_permission_denied))
	{
		if(verbose) std::cout << "preProcess: dir_entry=" << dir_entry << "\n";

		if (dir_entry.is_symlink())
		{
			std::cerr << "Warrning: Ignoring dir entry " << dir_entry
				<< " of unsupported type.\nSymlinks are not supported.\n";
			continue;
		}

		if (!dir_entry.is_directory() && !dir_entry.is_regular_file())
		{
			std::cerr << "Warrning: Ignoring dir entry " << dir_entry
				<< " of unsupported type.\nOnly normal files and directories are supported.\n";
			continue;
		}

		auto pathIt = dir_entry.path().begin();
		//To work on relative path
		std::advance(pathIt, workDirDepth);

		DirTreeNode* pCurrNode = pRoot;
		DirTreeNodeRef currIdx = 0;
		DirTreeNode* pNextNode = nullptr;
		DirTreeNodeRef nextNodeIdx = 0;
		 
		for(;pathIt != dir_entry.path().end();++pathIt)
		{
			nextNodeIdx = pCurrNode->findChildByName(*pathIt);
			pNextNode = toPtr(nextNodeIdx);
			if( nextNodeIdx == 0) 
			{
				//new node -> we should be at the end of the path
				assert(std::next(pathIt) == dir_entry.path().end());
				break;
			}

			pCurrNode = pNextNode;
			currIdx = nextNodeIdx;
		}


		if (dir_entry.is_regular_file())
		{
			std::ifstream f(dir_entry.path(), std::ios::binary);
			if(!f.good())
			{
				std::cerr << dir_entry << " unreadable, skipping.\n";
				continue;
			}

			DirTreeNodeRef ref = pCurrNode->addChild(theIndex_, currIdx, *pathIt);

			auto& fileInfo = fileEntries_.emplace_back();
			fileInfo.dirRefs_.push_back(ref);
			if(dir_entry.file_size() > std::numeric_limits<typeof(fileInfo.size_)>::max())
			{
				std::cerr << dir_entry.path() << " file too big\n";
				return false;
			}
			//fileGroups_.emplace(dir_entry.file_size(), ref);

			fileInfo.size_ = dir_entry.file_size();
		}
		else if(dir_entry.is_directory())
		{
			DirTreeNodeRef ref = pCurrNode->addChild(theIndex_, currIdx, *pathIt);
			toPtr(ref)->setIsEmptyDir(true);
		}
		else
		{
			std::cerr << dir_entry << " is neither file or directory, skipping.\n";
		}
			 

		//fileInfo a = {dir_entry};
		//allEntries.push_back(a);
		//allEntries.emplace_back(dir_entry);
	}

	theIndex_.shrink_to_fit();
	//The children list in each node was required to build
	//the tree, after thet it is not needed. For constructing
	//directory paths we start from the bottom so only parents
	//are needed
	releaseChildren();
	fileEntries_.shrink_to_fit();

	if(verbose) std::cout << "Data trimming completed." << std::endl;

	findDuplicates();
	
	if(verbose)
	{
		for(const auto& file : fileEntries_)
		{
			std::cout << "FileFsPath:" << getFsFilePath(file.dirRefs_.at(0)) << std::endl;
		}

		std::cout << "Number of files=" << fileEntries_.size() << '\n';
		std::cout << "Number of dir items=" << theIndex_.size() << '\n';
	}
	
	return true;;
}

bool DirectoryData::findDuplicates()
{
	std::cout << "Looking for duplicates\n";

	//Sort by file size
	auto sizeSorter =
		[](const FileInfo& feLeft, const FileInfo& feRight)
		{
			return feLeft.size_ < feRight.size_;
		};


	auto HashSorter =
		[](const FileInfo& feLeft, const FileInfo& feRight)
		{
			return feLeft.partialHash_ < feRight.partialHash_;
		};


	auto FullHashSorter =
		[](const FileInfo& feLeft, const FileInfo& feRight)
		{
			return std::tie(feLeft.fullHash_.high64, feLeft.fullHash_.low64)
				< std::tie(feRight.fullHash_.high64, feRight.fullHash_.low64);
		};
	
	std::sort(fileEntries_.begin(), fileEntries_.end(), sizeSorter);

	for (auto it = fileEntries_.begin(); it != fileEntries_.end();)
	{
		auto range = std::equal_range(it, fileEntries_.end(), *it, sizeSorter);

		if(std::distance(range.first, range.second) > 1)
		{
			if(!computeParialHshes(range))
			{
				return false;
			}

			std::sort(range.first, range.second, HashSorter);

			for( auto it2 = range.first; it2 < range.second;)
			{

				auto hashRange = std::equal_range(it2, range.second, *it2, HashSorter);

				if(std::distance(hashRange.first, hashRange.second) > 1)
				{
					if(!computeFullHshes(hashRange))
					{
						return false;
					}

					std::sort(hashRange.first, hashRange.second, FullHashSorter);
					
					for( auto it3 = hashRange.first; it3 < hashRange.second; ++it3)
					{
						if(verbose) std::cout << getFsFilePath(it3->dirRefs_.at(0)) << ":size = " << it2->size_
							<< " hash=" << it2->partialHash_ << " fullHash=" << it3->fullHash_.high64
								<< ',' << it3->fullHash_.low64 << '\n';

						//TODO: To make it 100% sure also a full byte by byte comparison should be done
						//For a test program this is fine, no lives will be lost, particularly for 1M of files
						//the chance is effectively zero
					}
				}

				it2 = hashRange.second;
			}
		}

		it = range.second;
	}

	return true;
}

bool DirectoryData::computeParialHshes
		(std::pair<std::vector<FileInfo>::iterator, std::vector<FileInfo>::iterator> range)
{
	if(verbose) std::cout << "computeParialHshes num=" << std::distance(range.first, range.second) << '\n';
	for (auto it = range.first; it != range.second; ++it)
	{
		//empty files are ok with 0 hashes
		if(it->size_ == 0)
		{
			continue;
		}
		fs::path filePath = getFsFilePath(it->dirRefs_.at(0));
		filePath = workDir_ / filePath;

		std::ifstream fileIn(filePath, std::ios::binary);
		if(!fileIn.good())
		{
			std::cerr << "Could not open " << filePath << " for calculating parial hash.\n";
			return false;
		}

		std::array<char, HASH_BUFFER_SIZE> buffer{};
		fileIn.read(buffer.data(), buffer.size());

		it->partialHash_ = XXH64(buffer.data(), fileIn.gcount(), 113);

		fileIn.close();
		
		//uint64_t h = 1469598103934665603ull; // FNV-1a base
		//for (std::streamsize i = 0; i < bytes_read; ++i) {
		//	h ^= static_cast<unsigned char>(buffer[i]);
		//	h *= 1099511628211ull;
		//}
	}

	return true;
}


bool DirectoryData::computeFullHshes
		(std::pair<std::vector<FileInfo>::iterator, std::vector<FileInfo>::iterator> range)
{
	auto* pState = XXH3_createState();

	for (auto it = range.first; it != range.second; ++it)
	{
		//empty files are ok with 0 hashes
		if(it->size_ == 0)
		{
			continue;
		}

		XXH3_128bits_reset(pState);

		fs::path filePath = getFsFilePath(it->dirRefs_.at(0));
		filePath = workDir_ / filePath;

		std::ifstream fileIn(filePath, std::ios::binary);
		if(!fileIn.good())
		{
			std::cerr << "Could not open " << filePath << " for calculating full hash.\n";
			return false;
		}

		std::array<char, IO_BUFFER_SIZE> buffer{};

		while (fileIn.good())
		{
			fileIn.read(buffer.data(), buffer.size());
			XXH3_128bits_update(pState, buffer.data(), fileIn.gcount());
		}

		it->fullHash_ = XXH3_128bits_digest(pState);

		fileIn.close();
		
		//uint64_t h = 1469598103934665603ull; // FNV-1a base
		//for (std::streamsize i = 0; i < bytes_read; ++i) {
		//	h ^= static_cast<unsigned char>(buffer[i]);
		//	h *= 1099511628211ull;
		//}
	}
	
	XXH3_freeState(pState);

	return true;
}

bool DirectoryData::writeNameTree(std::ostream& out)
{
	if (theIndex_.size() <= 1 || fileEntries_.empty())
	{
		std::cerr << "Empty directory or no files!\n";
		return false;
	}

	//std::ostream out("logDump", std::ios::binary | std::ios::trunc);
	
	//write the number of nodes that will be written
	DirTreeNode::writeRef(out, theIndex_.size());

	for(auto* pNode : theIndex_)
	{
		pNode->write(out);
	}

	return true;
}


bool DirectoryData::readNameTree(std::istream& in)
{
	DirTreeNodeRef numNodes = DirTreeNode::readRef(in);

	while(numNodes--)
	{
		auto* pNode = new DirTreeNode(theIndex_);
		pNode->read(in);
		if(verbose) std::cout << "Node read: " << pNode->name_ << '\n';
	}

	if(verbose) std::cout << "Number of dir items=" << theIndex_.size() << '\n';

	return true;
}

bool DirectoryData::writeFile(std::ostream& out, const FileInfo& file)
{
	//writing number of file names for this file 
	if(file.dirRefs_.size() == 0)
	{
		std::cerr << "Warning: File with no names passed for writing.\n";
		return true;
	}

	DirTreeNode::writeRef(out, file.dirRefs_.size());
	for(DirTreeNodeRef nameRef : file.dirRefs_)
	{
		//writing name references for each file
		//multiple file references for duplicates
		DirTreeNode::writeRef(out, nameRef);
	}

	write_le(out, file.size_);
	fs::path filePath = getFsFilePath(file.dirRefs_.at(0));
	filePath = workDir_ / filePath;
	if(verbose)
	{
		std::cout << "Full file path for writing=" << filePath << '\n';
		std::cout << "file size for writing=" << file.size_ << '\n';
	}

	if(file.size_ == 0)
	{
		if(verbose) std::cout << "Empty file written\n";
		return true;
	}

	std::ifstream fileIn(filePath, std::ios::binary);
	if(!fileIn.good() || !out.good())
	{
		return false;
	}
	out << fileIn.rdbuf();
	fileIn.close();

	return true;
}

bool DirectoryData::writeFiles(std::ostream& out)
{
	//writing number of file to write
	DirTreeNode::writeRef(out, fileEntries_.size());

	FileInfo pendingFile{};

	for(auto& file : fileEntries_)
	{
		if( pendingFile.size_ == file.size_ &&
				pendingFile.partialHash_ == file.partialHash_ &&
				pendingFile.fullHash_.high64 == file.fullHash_.high64 &&
				pendingFile.fullHash_.low64 == file.fullHash_.low64)
		{
			//aggregating list of names of same content files
			if(verbose) std::cout << "writeFiles: Duplicate detected.\n";
			pendingFile.dirRefs_.push_back(file.dirRefs_.at(0));
			continue;
		}

		if (!pendingFile.dirRefs_.empty() && !writeFile(out, pendingFile))
		{
			return false;
		}

		pendingFile.swap(file);
		file.dirRefs_.clear();
	}

	//after the loop ends we always have the pending file to write
	return writeFile(out, pendingFile);
}


bool DirectoryData::unpackFiles(std::istream& in)
{
	//1MB buffer
	std::array<char, IO_BUFFER_SIZE> buffer; 
	if(verbose) std::cout << "IO_BUFFER_SIZE=" << IO_BUFFER_SIZE << '\n';

	//Read the number of files
	DirTreeNodeRef numFiles = DirTreeNode::readRef(in);
	if(verbose) std::cout << numFiles << " to unpack\n";

	while(numFiles--)
	{
		FileInfo fileInfo{};
		DirTreeNodeRef numNames = DirTreeNode::readRef(in);
		while(numNames--)
		{
			DirTreeNodeRef ref = DirTreeNode::readRef(in);
			fileInfo.dirRefs_.push_back(ref);
		}

		auto path = getFsFilePath(fileInfo.dirRefs_.at(0),true);
		if(verbose) std::cout << "Writing " << path << std::endl;
		fs::create_directories(path.parent_path());
		std::ofstream out(getFsFilePath(fileInfo.dirRefs_.at(0),true), std::ios::binary);

		if(!out.good())
			return false;

		fileInfo.size_ = read_le<FileInfo::FileSizeType>(in);
		if(verbose) std::cout << "file size read:" << fileInfo.size_ << '\n';

		while (fileInfo.size_ > 0 && in )
		{
			auto chunk = std::min<std::streamsize>(buffer.size(), fileInfo.size_);
			in.read(buffer.data(), chunk);
			auto bytes_read = in.gcount();
			out.write(buffer.data(), bytes_read);
			fileInfo.size_ -= bytes_read;
		}

		out.close();

		//make copies if more then one dirRef
		if(fileInfo.dirRefs_.size() > 1)
		{
			bool first = true;
			for( auto dirRef : fileInfo.dirRefs_)
			{
				if(first)
				{
					first = false;
					continue;
				}

				auto dupPath = getFsFilePath(dirRef,true);
				fs::create_directories(dupPath.parent_path());
				if(verbose) std::cout << "Copying file " << path << " to " << dupPath << "\n";
				fs::copy_file(path, dupPath, fs::copy_options::overwrite_existing);
				//The numFiles read at the beginning includes duplicates
				//so we need the adjustment
				numFiles--;
			}
		}
	}

	return true;
}

bool DirectoryData::write(std::ostream& out)
{
	std::cout << "Writing directory data.\n";

	out.write(MAGIC_NUMBER.data(), MAGIC_NUMBER.size());

	if(!writeNameTree(out))
	{
		std::cerr << "Name Tree writing falure.\n";
		return false;
	}
	;

	if(!writeFiles(out))
	{
		std::cerr << "Files writing failure.\n";
		return false;
	}

	return true;
}

bool DirectoryData::read(std::istream& in)
{
	std::array<char, MAGIC_NUMBER.size()> magicNumBuff{};
	in.read(magicNumBuff.data(), MAGIC_NUMBER.size());
	if(magicNumBuff != MAGIC_NUMBER)
	{
		std::cerr << "File format check failed!\n";
		return false;
	}

	std::cout << "Extracting to current directory.\n";

	if(!readNameTree(in))
	{
		std::cerr << "Error: reading directory data failed.\n";
		return false;
	}

	if(!unpackFiles(in))
	{
		std::cerr << "Error: Unpacking files failed.\n";
		return false;
	}

	recreateEmptyDirs();

	return true;
}

void DirectoryData::recreateEmptyDirs()
{
	if(verbose) std::cout << "Empty Dirs:\n";

	for(auto it = theIndex_.crbegin(); it != theIndex_.crend(); ++it)
	{
		if((*it)->isEmptyDir())
		{
			auto dir = getFsFilePath(std::distance(it, theIndex_.crend()-1),true);
			if(verbose) std::cout << dir << '\n';
			fs::create_directories(dir);
		}
	}
}

void DirectoryData::releaseChildren()
{
	for (auto* elem : theIndex_)
	{
		elem->children_.clear();
	}
}


void DirectoryData::clearDirTree()
{
	if(theIndex_.empty())
	{
		return;
	}
	
	for (auto* pNode : theIndex_)
	{
		delete pNode;
	}
	
	theIndex_.clear();
}

DirectoryData::~DirectoryData()
{
	clearDirTree();
}


DirTreeNode* DirectoryData::toPtr(DirTreeNodeRef idx) const
{
	idx = idx & ~DIR_MASK;
	if (idx == 0)
	{
		return nullptr;
	}
	return theIndex_.at(idx);
};

//std::string DirectoryData::getFilePath(DirTreeNodeRef dirRef) const
//{
//	std::string ret;
//
//	auto* dirNode = toPtr(dirRef);
//	while (dirNode)
//	{
//		if (!ret.empty() && !dirNode->name_.empty())
//		{
//			ret.insert(ret.begin(), fs::path::preferred_separator);
//		}
//
//		ret.insert(0, dirNode->name_);
//
//		dirNode = toPtr(dirNode->parent_);
//	}
//
//	return ret;
//}


fs::path DirectoryData::getFsFilePath(DirTreeNodeRef dirRef, bool withRoot) const
{
	fs::path ret;

	auto* dirNode = toPtr(dirRef);
	while (dirNode)
	{
		if(ret.empty())
		{
			ret = dirNode->name_;
		}
		else
		{
			ret = dirNode->name_ / ret;
		}

		dirNode = toPtr(dirNode->parent_);
	}

	if(withRoot)
	{
		//root node is at idx 0 and it is also interpreted as nullptr
		//it is not used in the loop above. Compensating for it
		ret = theIndex_.at(0)->name_ / ret;
	}

	return ret;
}
