#include "DataStructs.h"
#include <iostream>
#include <vector>
#include <endian.h>

extern bool verbose;

DirTreeNodeRef DirTreeNode::findChildByName(const std::string& nameToFind)
{
	auto it = children_.find(nameToFind);
	if( it == children_.end())
	{
		return 0;
	}

	return *it;
}

//Stealing one bit from the parent_ to store information if the
//node is directory or specifically empty directory
void DirTreeNode::setIsEmptyDir(bool in)
{
	parent_ = in ? (parent_ | DIR_MASK) : (parent_ & ~DIR_MASK);
}

bool DirTreeNode::isEmptyDir() const
{
	return parent_ & DIR_MASK;
}

DirTreeNodeRef DirTreeNode::addChild(std::vector<DirTreeNode*>& index, DirTreeNodeRef parentIdx, std::string nameIn)
{
	if(verbose) std::cout << "DirTreeNode::addChild name=" << nameIn << "\n";

	DirTreeNode* newNode = new DirTreeNode(index);
	//theIndex_.push_back(newNode);

	newNode->name_ = std::move(nameIn);
	newNode->parent_ = parentIdx;

	//DirTreeNodeRef ref;
	//ref.refU.ptr = newNode;
	DirTreeNodeRef childIdx = index.size() - 1;
	children_.insert(childIdx);
	//return *children.insert(ref).first;
	setIsEmptyDir(false);
	return childIdx;
}

bool DirTreeNode::TranspComparator::operator()(DirTreeNodeRef left, DirTreeNodeRef right) const
{
	return indexRef_.at(left)->name_ < indexRef_.at(right)->name_;
	//return left.refU.ptr->name < right.refU.ptr->name;
}

bool DirTreeNode::TranspComparator::operator()(const std::string& left, DirTreeNodeRef right) const
{
	return left < indexRef_.at(right)->name_;
	//return left < right.refU.ptr->name;
}

bool DirTreeNode::TranspComparator::operator()(DirTreeNodeRef left, const std::string& right) const
{
	return indexRef_.at(left)->name_ < right;
	//return left.refU.ptr->name < right;
}

//void DirTreeNode::releaseChildren()
//{
//	for (auto* elem : theIndex_)
//	{
//		elem->children_.clear();
//	}
//}

//DirTreeNode::~DirTreeNode()
//{
//	if(theIndex_.empty())
//	{
//		return;
//	}
//	
//	std::vector<DirTreeNode*> elemsToDelete;
//	elemsToDelete.swap(theIndex_);
//
//	for (auto* elem : elemsToDelete)
//	{
//		if(elem == this)
//		{
//			continue;
//		}
//		delete elem;
//	}
//}

//DirTreeNode* DirTreeNode::toPtr(DirTreeNodeRef idx)
//{
//	if (idx == 0)
//	{
//		return nullptr;
//	}
//	return theIndex_.at(idx);
//};

//assuming that the time machine is using like ext4 filesystem 
//meaning that the max filename lenght is 255 bytes
//not handling encoding conversions here
void DirTreeNode::writeString(std::ostream& out, const std::string& name)
{
	uint8_t len = name.size();
	out.write(reinterpret_cast<const char*>(&len), sizeof(len));
	out.write(name.data(), len);
}

std::string DirTreeNode::readString(std::istream& in)
{
	uint8_t len;
	in.read(reinterpret_cast<char*>(&len), sizeof(len));
	std::string name(len, '\0');
	in.read(name.data(), len);
	return name;
}

void DirTreeNode::writeRef(std::ostream& out, DirTreeNodeRef val)
{
	write_le(out, val);
}

DirTreeNodeRef DirTreeNode::readRef(std::istream& in)
{
	return read_le<DirTreeNodeRef>(in);
}


void DirTreeNode::write(std::ostream& out) const
{
	writeRef(out, parent_);
	writeString(out, name_);
}

void DirTreeNode::read(std::istream& in)
{
	parent_ = readRef(in);
	name_ = readString(in);
}

