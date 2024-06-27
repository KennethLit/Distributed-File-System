#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>
#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;

int main(int argc, char *argv[]) 
{
    if (argc != 3) {
        cout << argv[0] << ": diskImageFile inodeNumber" << endl;
        return 1;
    }

  // getting the disk image file name / inode number. Use 3 for ex
  string diskImageFile = argv[1];
  int inodeNumber = atoi(argv[2]);


  //images for the disk object and lfs
  Disk newDisk = Disk(diskImageFile, UFS_BLOCK_SIZE);
  LocalFileSystem lfs = LocalFileSystem(&newDisk);


  //getting the inode information for a num
  inode_t inode;
  int returnStatus = lfs.stat(inodeNumber, &inode);
  if (returnStatus == -EINVALIDINODE) {
    return 1;
  }

  cout << "File blocks" << endl;
  for (int i = 0; i < (inode.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE; i++) 
  {
    cout << inode.direct[i] << endl;
  }
  cout << endl;

  cout << "File data" << endl;

  //creating the buffer to hold the file data
  char buffer[inode.size];
  buffer[inode.size] = '\0';

  //read into the buffer and output it after
  int out = lfs.read(inodeNumber, buffer, inode.size);
  if (out == -EINVALIDINODE || out == -EINVALIDSIZE) {
    return 1;
  }

  cout << buffer;
}