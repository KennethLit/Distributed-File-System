
#include <iostream>
#include <string>
#include <vector>
#include <assert.h>
#include <cstring>
#include <cmath>
#include <algorithm>

#include "LocalFileSystem.h"
#include "ufs.h"

using namespace std;


LocalFileSystem::LocalFileSystem(Disk *disk) {
  this->disk = disk;
}

void LocalFileSystem::readSuperBlock(super_t *super) //done
{
  char buffer[UFS_BLOCK_SIZE];
  disk->readBlock(0, buffer);
  memcpy(super, buffer, sizeof(super_t));
}

int LocalFileSystem::lookup(int parentInodeNumber, string name) //done
{
    inode_t inode;
    int returnStatus = stat(parentInodeNumber, &inode);

    //error check
    if (returnStatus == -EINVALIDINODE || inode.type != UFS_DIRECTORY) 
    {
        return -EINVALIDINODE;
    }

    //calculating the number of blocks occupied by the directory entries 
    int blocks = (inode.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    int bytesLeft = inode.size;
    int entriesPerBlock = UFS_BLOCK_SIZE / sizeof(dir_ent_t);

    dir_ent_t dir_ent;

    for (int i = 0; i < blocks; ++i) 
    {
        char buffer[UFS_BLOCK_SIZE];
        disk->readBlock(inode.direct[i], buffer);

        // # of directories in current block
        int entries = bytesLeft / sizeof(dir_ent_t);
        if (entries > entriesPerBlock) 
        {
            entries = entriesPerBlock;
        }

        // checking over each directory
        for (int j = 0; j < entries; ++j) 
        {
            memcpy(&dir_ent, buffer + j * sizeof(dir_ent_t), sizeof(dir_ent_t));
            // matching
            if (name == dir_ent.name) 
            {
                return dir_ent.inum;
            }
        }

        // reduction to one block
        bytesLeft -= UFS_BLOCK_SIZE;
    }
    return -ENOTFOUND;
}

int LocalFileSystem::stat(int inodeNumber, inode_t *inode) //done
{
  //creating and reading the super block from disk
  super_t super;
  readSuperBlock(&super);

  //catch if it is in valid range
  if (inodeNumber < 0 || inodeNumber > super.num_inodes - 1) 
  {
    return -EINVALIDINODE;
  }

  //array to hold the list of nodes
  inode_t inodeList[super.num_inodes];
  readInodeRegion(&super, inodeList);

  //copy data to the pointer
  memcpy(inode, inodeList + inodeNumber, sizeof(inode_t));

  return 0;
}

int LocalFileSystem::read(int inodeNumber, void *buffer, int size) //done
  {

  inode_t inode;
  int returnStatus = stat(inodeNumber, &inode);

  //valid check
  if (returnStatus == -EINVALIDINODE) 
  {
    return -EINVALIDINODE;
  }

  //valid check
  if (size > MAX_FILE_SIZE || size < 0) {
    return -EINVALIDSIZE;
  }

  //read based on the requested size
  int blocks = (inode.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;

  char bufferTemp[UFS_BLOCK_SIZE * blocks];

  for (int i = 0; i < blocks; i++) {
    disk->readBlock(inode.direct[i], bufferTemp + i * UFS_BLOCK_SIZE);
  }

  memcpy(buffer, bufferTemp, size);
  
  //return # of bytes read
  return size;
}

int LocalFileSystem::create(int parentInodeNumber, int type, string name) //done
{
    //validating name
    if (name.empty() || name.length() > DIR_ENT_NAME_SIZE) {
        return -EINVALIDNAME;
    }

    //check if it exists
    int existingInodeNumber = lookup(parentInodeNumber, name);
    if (existingInodeNumber != -ENOTFOUND && existingInodeNumber != -EINVALIDINODE) 
    {
        inode_t existingInode;
        int statResult = stat(existingInodeNumber, &existingInode);

        //checks
        if (statResult == -EINVALIDINODE) 
        {
            return -EINVALIDINODE;
        }
        if (existingInode.type == type) 
        {
            return existingInodeNumber;
        } 
        else 
        {
            return -EINVALIDTYPE;
        }
    }

    if (existingInodeNumber == -EINVALIDINODE) 
    {
        return -EINVALIDINODE;
    }

    super_t super;
    readSuperBlock(&super);

    inode_t parentInode;
    int parentStatResult = stat(parentInodeNumber, &parentInode);
    if (parentStatResult == -EINVALIDINODE) {
        return -EINVALIDINODE;
    }

    //check if theres enough space in the directory
    if ((type == UFS_REGULAR_FILE && !diskHasSpace(&super, 1, 0, 0)) ||
        (type == UFS_DIRECTORY && !diskHasSpace(&super, 1, 0, 1))) {
        return -ENOTENOUGHSPACE;
    }

    //bitmapping to check if theres a free inode
    std::vector<unsigned char> inodeBitmap(UFS_BLOCK_SIZE * super.inode_bitmap_len, 0);
    readInodeBitmap(&super, inodeBitmap.data());

    int freeInodeNumber = -1;
    for (int byteIndex = 0; byteIndex < UFS_BLOCK_SIZE * super.inode_bitmap_len && freeInodeNumber == -1; ++byteIndex) 
    {
        for (int bitIndex = 0; bitIndex < 8; ++bitIndex) {
            if ((inodeBitmap[byteIndex] & (1 << bitIndex)) == 0) 
            {
                freeInodeNumber = byteIndex * 8 + bitIndex;
                inodeBitmap[byteIndex] |= (1 << bitIndex);
                break;
            }
        }
    }

    //read parent inodes directory entries
    std::vector<char> buffer(parentInode.size);
    read(parentInodeNumber, buffer.data(), parentInode.size);

    //creating new directory entries
    int numDirEntries = parentInode.size / sizeof(dir_ent_t);
    std::vector<dir_ent_t> dirEntries(numDirEntries + 1);

    for (int i = 0; i < numDirEntries; ++i) {
        memcpy(&dirEntries[i], buffer.data() + i * sizeof(dir_ent_t), sizeof(dir_ent_t));
    }

    //initializing the new directory entry
    dir_ent_t newDirEntry;
    newDirEntry.inum = freeInodeNumber;
    memset(newDirEntry.name, 0, sizeof(newDirEntry.name));
    memcpy(newDirEntry.name, name.c_str(), name.size());
    dirEntries[numDirEntries] = newDirEntry;

    //updating the parent inode size then write it back
    parentInode.size += sizeof(dir_ent_t);
    std::vector<inode_t> inodeList(super.num_inodes);
    readInodeRegion(&super, inodeList.data());
    inodeList[parentInodeNumber] = parentInode;

    //write updated directory entries to parent inodes blocks
    int blocksNeeded = (parentInode.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    std::vector<char> tempBuffer(UFS_BLOCK_SIZE * blocksNeeded, 0);
    memcpy(tempBuffer.data(), dirEntries.data(), parentInode.size);

    for (int i = 0; i < blocksNeeded; ++i) 
    {
        disk->writeBlock(parentInode.direct[i], tempBuffer.data() + i * UFS_BLOCK_SIZE);
    }

    inode_t newInode;
    newInode.type = type;
    if (type == UFS_REGULAR_FILE) 
    {
        newInode.size = 0;
    } else 
    {
        newInode.size = 2 * sizeof(dir_ent_t);

        std::vector<unsigned char> dataBitmap(UFS_BLOCK_SIZE * super.data_bitmap_len, 0);
        readDataBitmap(&super, dataBitmap.data());

        int newBlockNumber = -1;
        for (int byteIndex = 0; byteIndex < UFS_BLOCK_SIZE * super.data_bitmap_len && newBlockNumber == -1; ++byteIndex) {
            for (int bitIndex = 0; bitIndex < 8; ++bitIndex) {
                if ((dataBitmap[byteIndex] & (1 << bitIndex)) == 0) {
                    dataBitmap[byteIndex] |= (1 << bitIndex);
                    newBlockNumber = byteIndex * 8 + bitIndex;
                    break;
                }
            }
        }

        if (newBlockNumber == -1) return -ENOTENOUGHSPACE;

        newInode.direct[0] = newBlockNumber + super.data_region_addr;

        // Create . and .. directory entries
        dir_ent_t one, two;
        one.inum = freeInodeNumber;
        memset(one.name, 0, sizeof(one.name));
        memcpy(one.name, ".", 1);

        two.inum = parentInodeNumber;
        memset(two.name, 0, sizeof(two.name));
        memcpy(two.name, "..", 2);

        std::vector<dir_ent_t> initialEntries = {one, two};
        std::vector<char> initialBuffer(UFS_BLOCK_SIZE, 0);
        memcpy(initialBuffer.data(), initialEntries.data(), initialEntries.size() * sizeof(dir_ent_t));

        disk->writeBlock(newInode.direct[0], initialBuffer.data());
        writeDataBitmap(&super, dataBitmap.data());
    }

    //update inodelist and write to inode bitmap
    inodeList[freeInodeNumber] = newInode;
    writeInodeRegion(&super, inodeList.data());
    writeInodeBitmap(&super, inodeBitmap.data());

    return freeInodeNumber;
}

int LocalFileSystem::write(int inodeNumber, const void *buffer, int size) //done
{
    // retrieving for a given inode number
    inode_t inode;
    int returnStatus = stat(inodeNumber, &inode);

    // checks
    if (returnStatus != 0) 
    {
        return returnStatus;
    }

    if (size > MAX_FILE_SIZE || size < 0) 
    {
        return -EINVALIDSIZE;
    }

    if (inode.type != UFS_REGULAR_FILE) {
        return -EINVALIDTYPE;
    }

    super_t super;
    readSuperBlock(&super);

    // calculating # of blocks needed for new file size
    int newBlocks = (size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    int currentBlocks = (inode.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;

    if (newBlocks > currentBlocks) 
    {
        // in case more blocks are needed
        int extraBlocks = newBlocks - currentBlocks;

        // check if there's enough space
        if (!diskHasSpace(&super, 0, 0, extraBlocks)) 
        {
            return -ENOTENOUGHSPACE;
        }

        // allocate additional blocks
        std::vector<int> newBlockNumbers;
        std::vector<unsigned char> dataBitMap(UFS_BLOCK_SIZE * super.data_bitmap_len);
        readDataBitmap(&super, dataBitMap.data());

        for (int i = 0; i < UFS_BLOCK_SIZE * super.data_bitmap_len; i++) 
        {
            for (int j = 0; j < 8; j++) {
                if ((dataBitMap[i] & (1 << j)) == 0) 
                {
                    dataBitMap[i] |= (1 << j);
                    newBlockNumbers.push_back(i * 8 + j + super.data_region_addr);
                    if (newBlockNumbers.size() == static_cast<std::vector<int>::size_type>(extraBlocks)) 
                    {
                        writeDataBitmap(&super, dataBitMap.data());
                        break;
                    }
                }
            }
            if (newBlockNumbers.size() == static_cast<std::vector<int>::size_type>(extraBlocks)) 
            {
                break;
            }
        }

        // updating new block numbers
        for (size_t i = 0; i < newBlockNumbers.size(); i++) 
        {
            inode.direct[currentBlocks + i] = newBlockNumbers[i];
        }

    } 
    else if (newBlocks < currentBlocks) 
    {
        // freeing unused data blocks
        std::vector<unsigned char> dataBitMap(UFS_BLOCK_SIZE * super.data_bitmap_len);
        readDataBitmap(&super, dataBitMap.data());

        for (int i = newBlocks; i < currentBlocks; i++) {
            int blockNumber = inode.direct[i] - super.data_region_addr;
            int byteIndex = blockNumber / 8;
            int bitIndex = blockNumber % 8;
            dataBitMap[byteIndex] &= ~(1 << bitIndex);
        }

        writeDataBitmap(&super, dataBitMap.data());
    }

    // writing data
    std::vector<char> tempBuffer(newBlocks * UFS_BLOCK_SIZE, 0);
    memcpy(tempBuffer.data(), buffer, size);

    for (int i = 0; i < newBlocks; i++) 
    {
        disk->writeBlock(inode.direct[i], tempBuffer.data() + i * UFS_BLOCK_SIZE);
    }

    // Update the inode size and table
    inode.size = size;
    std::vector<inode_t> inodeList(super.num_inodes);
    readInodeRegion(&super, inodeList.data());
    inodeList[inodeNumber] = inode;
    writeInodeRegion(&super, inodeList.data());

    return size;
}

int LocalFileSystem::unlink(int parentInodeNumber, string name) //done
{
    //prevent unlinking "." and ".."
    if (name == "." || name == "..") 
    {
        return -EUNLINKNOTALLOWED;
    }

    //lookup child inode #
    int childInodeNumber = lookup(parentInodeNumber, name);
    if (childInodeNumber == -EINVALIDINODE) 
    {
        return -EINVALIDINODE;
    } 
    else if (childInodeNumber == -ENOTFOUND) 
    {
        return 0;
    }
    //getting child inode
    inode_t childInode;
    stat(childInodeNumber, &childInode);

    //empty check
    if (childInode.type == UFS_DIRECTORY && childInode.size > (int)(sizeof(dir_ent_t) * 2)) 
    {
        return -EDIRNOTEMPTY;
    }

    super_t super;
    readSuperBlock(&super);
    int numBlocks = (childInode.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;

    //deallocating block data
    std::vector<unsigned char> dataBitmap(UFS_BLOCK_SIZE * super.data_bitmap_len, 0);
    readDataBitmap(&super, dataBitmap.data());

    for (int i = 0; i < numBlocks; ++i) 
    {
        int blockNum = childInode.direct[i] - super.data_region_addr;
        dataBitmap[blockNum / 8] &= ~(1 << (blockNum % 8));
    }
    writeDataBitmap(&super, dataBitmap.data());

    //inode deallocation
    std::vector<unsigned char> inodeBitmap(UFS_BLOCK_SIZE * super.inode_bitmap_len, 0);
    readInodeBitmap(&super, inodeBitmap.data());
    inodeBitmap[childInodeNumber / 8] &= ~(1 << (childInodeNumber % 8));
    writeInodeBitmap(&super, inodeBitmap.data());

    inode_t parentInode;
    if (stat(parentInodeNumber, &parentInode) < 0) 
    {
        return -EINVALIDINODE;
    }

    // reading parent directory entries
    std::vector<char> dirBuffer(parentInode.size, 0);
    if (read(parentInodeNumber, dirBuffer.data(), parentInode.size) < 0) 
    {
        return -EINVALIDINODE;
    }

    //converting buffer to directory entries
    int numEntries = parentInode.size / sizeof(dir_ent_t);
    std::vector<dir_ent_t> dirEntries(numEntries);
    memcpy(dirEntries.data(), dirBuffer.data(), parentInode.size);

    //remove corresponding inode entry
    auto it = std::remove_if(dirEntries.begin(), dirEntries.end(), [childInodeNumber](const dir_ent_t &entry) 
    {
        return entry.inum == childInodeNumber;
    });

    if (it == dirEntries.end()) 
    {
        return -EINVALIDNAME;
    }

    dirEntries.erase(it, dirEntries.end());

    //updatign parent inode size and writing it back
    parentInode.size -= sizeof(dir_ent_t);
    std::vector<inode_t> inodeList(super.num_inodes);
    readInodeRegion(&super, inodeList.data());
    inodeList[parentInodeNumber] = parentInode;
    writeInodeRegion(&super, inodeList.data());

    //write back
    std::vector<char> newDirBuffer(parentInode.size, 0);
    memcpy(newDirBuffer.data(), dirEntries.data(), parentInode.size);

    int newBlocks = (parentInode.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    for (int i = 0; i < newBlocks; ++i) 
    {
        disk->writeBlock(parentInode.direct[i], newDirBuffer.data() + i * UFS_BLOCK_SIZE);
    }

    return 0;
}

//my helper function defenitions
void LocalFileSystem::readInodeBitmap(super_t *super, unsigned char *inodeBitmap) //done
{
  for (int i = 0; i < super->inode_bitmap_len; i++)
  {
    //temp buffer then read from the disk into the temp buffer
    char buffer[UFS_BLOCK_SIZE];
    disk->readBlock(super->inode_bitmap_addr + i, buffer);
    //copying the contents into an inode bitmap array
    memcpy(inodeBitmap + i * UFS_BLOCK_SIZE, buffer, sizeof(UFS_BLOCK_SIZE));
  }
}

void LocalFileSystem::readDataBitmap(super_t *super, unsigned char *dataBitmap) //done
{
  //basically the same thing as inode bitmap
  for (int i = 0; i < super->data_bitmap_len; i++) 
  {
    char buffer[UFS_BLOCK_SIZE];
    disk->readBlock(super->data_bitmap_addr + i, buffer);
    memcpy(dataBitmap + i * UFS_BLOCK_SIZE, buffer, sizeof(UFS_BLOCK_SIZE));
  }
}

void LocalFileSystem::readInodeRegion(super_t *super, inode_t *inodes) //done

{

  char buffer[UFS_BLOCK_SIZE * super->inode_region_len];
  for (int i = 0; i < super->inode_region_len; i++) 
  {
    //reading the block from disk into corresponding position of the buffer
    disk->readBlock(super->inode_region_addr + i, buffer + i * UFS_BLOCK_SIZE);
  }
  // copy the contents of the buffer into the inodes array
  memcpy(inodes, buffer, sizeof(inode_t) * super->num_inodes);
}

void LocalFileSystem::writeDataBitmap(super_t* super, unsigned char *dataBitmap) //done
{
  for (int i = 0; i < super->data_bitmap_len; i++) 
  {
    //just reverse of read
    char buffer[UFS_BLOCK_SIZE];
    memcpy(buffer, dataBitmap + UFS_BLOCK_SIZE * i, UFS_BLOCK_SIZE);
    disk->writeBlock(super->data_bitmap_addr + i, buffer);
  }
}

void LocalFileSystem::writeInodeRegion(super_t *super, inode_t *inodes) //done
{
  for (int i = 0; i < super->inode_region_len; i++) 
  {
    //same as previous write
    char buffer[UFS_BLOCK_SIZE];
    memcpy(buffer, inodes + UFS_BLOCK_SIZE * i, UFS_BLOCK_SIZE);
    disk->writeBlock(super->inode_region_addr + i, buffer);
  }
}

void LocalFileSystem::writeInodeBitmap(super_t* super, unsigned char *inodeBitmap) //done
{
  for (int i = 0; i < super->inode_bitmap_len; i++) 
  {
    char buffer[UFS_BLOCK_SIZE];
    memcpy(buffer, inodeBitmap + UFS_BLOCK_SIZE * i, UFS_BLOCK_SIZE);
    disk->writeBlock(super->inode_bitmap_addr + i, buffer);
  }
}

bool LocalFileSystem::diskHasSpace(super_t *super, int numInodesNeeded, int numDataBytesNeeded, int numDataBlocksNeeded) //done
{
    //calculating the total blocks required
    int requiredBlocks = (numDataBytesNeeded + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE + numDataBlocksNeeded;

    //initializing and reading the data bitmap
    std::vector<unsigned char> dataBitMap(UFS_BLOCK_SIZE * super->data_bitmap_len, 0);
    readDataBitmap(super, dataBitMap.data());

    // counting allocated data blocks
    int allocatedBlocks = 0;
    for (int i = 0; i < UFS_BLOCK_SIZE * super->data_bitmap_len; i++) 
    {
        for (int j = 0; j < 8; j++) {
            if ((dataBitMap[i] & (1 << j)) != 0) 
            {
                allocatedBlocks++;
            }
        }
    }

    // Check if the required blocks are available
    bool blockRequirement = (super->num_data - allocatedBlocks >= requiredBlocks);

    // Initialize and read the inode bitmap
    std::vector<unsigned char> inodeBitMap(UFS_BLOCK_SIZE * super->inode_bitmap_len, 0);
    readInodeBitmap(super, inodeBitMap.data());

    // Count the allocated inodes
    int inodesAllocated = 0;
    for (int i = 0; i < UFS_BLOCK_SIZE * super->inode_bitmap_len; i++) 
    {
        for (int j = 0; j < 8; j++) {
            if ((inodeBitMap[i] & (1 << j)) != 0) 
            {
                inodesAllocated++;
            }
        }
    }

    // Check if the required inodes are available
    bool inodeRquirement = (super->num_inodes - inodesAllocated >= numInodesNeeded);

    // Return true if both block and inode requirements are met, otherwise return false
    return (inodeRquirement && blockRequirement);
}