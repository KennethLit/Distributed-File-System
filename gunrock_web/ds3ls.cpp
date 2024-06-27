#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>
#include <vector>
#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;

bool compare_dir_ent_t(dir_ent_t const& dir_ent1, dir_ent_t const& dir_ent2) {
  return strcmp(dir_ent1.name, dir_ent2.name) < 0;
}

// list contents
void list(LocalFileSystem& localFileSystem, string directoryName, int inodeNumber) {
  inode_t inode;
  localFileSystem.stat(inodeNumber, &inode);

  // error catch
  if (inode.type == UFS_REGULAR_FILE) {
    return;
  }

  char buffer[inode.size];
  localFileSystem.read(inodeNumber, buffer, inode.size);

  // number of directory entry calculation
  int numDirectoryEntries = inode.size / sizeof(dir_ent_t);
  vector<dir_ent_t> directoryEntries(numDirectoryEntries);

  cout << "Directory " << directoryName << "\n";

  // copy from vector to the buffer
  for (int i = 0; i < numDirectoryEntries; i++) {
    memcpy(&directoryEntries[i], buffer + (i * sizeof(dir_ent_t)), sizeof(dir_ent_t));
  }

  sort(directoryEntries.begin(), directoryEntries.end(), compare_dir_ent_t);

  for (const auto& entry : directoryEntries) 
  {
    cout << entry.inum << "\t" << entry.name << "\n";
  }
  cout << "\n";

  // skipping the . and ..
  for (int i = 2; i < numDirectoryEntries; i++) {
    list(localFileSystem, directoryName + directoryEntries[i].name + "/", directoryEntries[i].inum);
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    cout << argv[0] << ": diskImageFile" << endl;
    return 1;
  }

  Disk disk(argv[1], UFS_BLOCK_SIZE);
  LocalFileSystem localFileSystem(&disk);

  list(localFileSystem, "/", UFS_ROOT_DIRECTORY_INODE_NUMBER);

  return 0;
}
