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
    if (argc != 2) {
        cout << argv[0] << ": diskImageFile" << endl;
        return 1;
    }

    Disk newDisk = Disk(argv[1], UFS_BLOCK_SIZE);
    LocalFileSystem fs(&newDisk);
    super_t super;
    fs.readSuperBlock(&super);

    // print data for the super block
    cout << "Super" << endl;
    cout << "inode_region_addr " << super.inode_region_addr << endl;
    cout << "data_region_addr " << super.data_region_addr << endl;
    cout << endl;

    
    // inode bitmaps
    cout << "Inode bitmap" << endl;

    //array to hold bitmap data
    unsigned char inodeBitmap[super.inode_bitmap_len * UFS_BLOCK_SIZE];
    memset(inodeBitmap, 0, sizeof(inodeBitmap));
    fs.readInodeBitmap(&super, inodeBitmap);
    for (int i = 0; i < super.inode_bitmap_len * UFS_BLOCK_SIZE; i++) 
    {
        cout << (unsigned int)inodeBitmap[i] << " ";
    }
    cout << endl << endl;

    // basically the same as bitmap printing
    cout << "Data bitmap" << endl;
    unsigned char dataBitmap[super.data_bitmap_len * UFS_BLOCK_SIZE];
    memset(dataBitmap, 0, sizeof(dataBitmap));
    fs.readDataBitmap(&super, dataBitmap);
    for (int i = 0; i < super.data_bitmap_len * UFS_BLOCK_SIZE; i++) {
        cout << (unsigned int)dataBitmap[i] << " ";
    }
    cout << endl;
    
    return 0;
}