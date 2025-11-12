#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <cassert>
#include <filesystem>
#include <vector>
#include <fstream>
#include <xxhash.h>

namespace fs = std::filesystem;

//Up to 4GB 

using DirTreeNodeRef = uint32_t;
constexpr DirTreeNodeRef DIR_MASK = 1U << (sizeof(DirTreeNodeRef) * 8 - 1);
constexpr DirTreeNodeRef REF_MAX = ~DIR_MASK;



template <typename T>
void write_le(std::ostream& out, T val)
{
	if constexpr (sizeof(T) == sizeof(uint16_t))
	{
		uint16_t v = htole16(static_cast<uint16_t>(val));
		out.write(reinterpret_cast<const char*>(&v), sizeof(v));
	}
	else if constexpr (sizeof(T) == sizeof(uint32_t))
	{
		uint32_t v = htole32(static_cast<uint32_t>(val));
		out.write(reinterpret_cast<const char*>(&v), sizeof(v));
	}
	else if constexpr (sizeof(T) == sizeof(uint64_t))
	{
		uint64_t v = htole64(static_cast<uint64_t>(val));
		out.write(reinterpret_cast<const char*>(&v), sizeof(v));
	}
	else if constexpr (sizeof(T) == sizeof(uint8_t))
	{
		out.write(reinterpret_cast<const char*>(&val), sizeof(val));
	}
	else
	{
		static_assert(false, "Unsupported integer size for write_le()");
	}
}

template <typename T>
T read_le(std::istream& in)
{
	T val{};

	if constexpr (sizeof(T) == sizeof(uint16_t))
	{
		uint16_t v;
		in.read(reinterpret_cast<char*>(&v), sizeof(v));
		val = static_cast<T>(le16toh(v));
	}
	else if constexpr (sizeof(T) == sizeof(uint32_t))
	{
		uint32_t v;
		in.read(reinterpret_cast<char*>(&v), sizeof(v));
		val = static_cast<T>(le32toh(v));
	}
	else if constexpr (sizeof(T) == sizeof(uint64_t))
	{
		uint64_t v;
		in.read(reinterpret_cast<char*>(&v), sizeof(v));
		val = static_cast<T>(le64toh(v));
	}
	else if constexpr (sizeof(T) == sizeof(uint8_t))
	{
		in.read(reinterpret_cast<char*>(&val), sizeof(val));
	}
	else
	{
		static_assert(false, "Unsupported integer size for read_le()");
	}

	return val;
}


struct DirTreeNode
{
	//Flat sequence of the directory nodes. It is required
	//for serialisation. This index translates save positions
	//to pointers to actual objects
	//static std::vector<DirTreeNode*> theIndex_;

	explicit DirTreeNode(std::vector<DirTreeNode*>& index):
		children_(TranspComparator(index))
	{
		//DirTreeNode::theIndex_.push_back(this);
		index.push_back(this);
		//assert(DirTreeNode::theIndex_.size()-1 <= std::numeric_limits<DirTreeNodeRef>::max());
		assert(index.size()-1 <= ~REF_MAX);
	}

	//~DirTreeNode();

	struct TranspComparator
	{
		using is_transparent = void; // enables heterogeneous lookup
		
		const std::vector<DirTreeNode*>& indexRef_; 

		explicit TranspComparator(const std::vector<DirTreeNode*>& idx)
			: indexRef_(idx)
		{}

		bool operator()(DirTreeNodeRef left, const DirTreeNodeRef right) const;
		bool operator()(const std::string& left, DirTreeNodeRef right) const;
		bool operator()(DirTreeNodeRef left, const std::string& right) const;

	};
	using DirTreeNodeSet = std::set<DirTreeNodeRef, TranspComparator>;


	DirTreeNodeRef parent_{0};
	std::string name_;
	DirTreeNodeSet children_;


	//std::vector<DirTreeNodeRef> files;
	DirTreeNodeRef findChildByName(const std::string& nameToFind);
	DirTreeNodeRef addChild(std::vector<DirTreeNode*>& index, DirTreeNodeRef, std::string name);

	void write(std::ostream& out) const;
	void read(std::istream& in);

	static void writeRef(std::ostream& out, DirTreeNodeRef val);
	static DirTreeNodeRef readRef(std::istream& in);

	static void writeString(std::ostream& out, const std::string& name);
	static std::string readString(std::istream& in);

	void setIsEmptyDir(bool in);
	bool isEmptyDir() const;
};


struct FileInfo
{
	struct IsEqual
	{
		bool operator()(const FileInfo& left, const FileInfo& right) const
		{
			return left.size_ == right.size_;
		}
	};

	struct HashFunction
	{
		std::size_t operator()(const FileInfo& file) const
		{
			return file.size_;
		}
	};

	using FileSizeType = uint32_t;

	FileInfo(FileSizeType size, DirTreeNodeRef nameRef):
		size_(size)
	{
		dirRefs_.push_back(nameRef);
	};

	FileInfo() = default;

	FileSizeType size_{};
	XXH64_hash_t partialHash_{}; 
	XXH128_hash_t fullHash_{}; 
	//size_t fullHash;
	//size_t group;
	std::vector<DirTreeNodeRef> dirRefs_;

	void swap(FileInfo& other)
	{
		size_ = other.size_;
		partialHash_ = other.partialHash_;
		fullHash_ = other.fullHash_;
		dirRefs_.swap(other.dirRefs_);
	}

	//std::string getFilePath() const;
	//fs::path getFsFilePath() const;
};
