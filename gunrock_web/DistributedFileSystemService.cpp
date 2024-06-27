#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sstream>
#include <iostream>
#include <map>
#include <string>
#include <algorithm>
#include <cstring>
#include <vector>

#include "DistributedFileSystemService.h"
#include "ClientError.h"
#include "ufs.h"
#include "WwwFormEncodedDict.h"


using namespace std;

DistributedFileSystemService::DistributedFileSystemService(string diskFile) : HttpService("/ds3/")
{
  this->fileSystem = new LocalFileSystem(new Disk(diskFile, UFS_BLOCK_SIZE));
}  

//this function similar to ls
void DistributedFileSystemService::get(HTTPRequest *request, HTTPResponse *response) 
{
    vector<string> pathComponents = request->getPathComponents();
    
    //check if request path starts w ds3
    if (pathComponents[0] != "ds3") 
    {
        throw ClientError::badRequest();
    }
    
    int current = UFS_ROOT_DIRECTORY_INODE_NUMBER;
    
    //iterate through path components
    for (size_t i = 1; i < pathComponents.size(); ++i)
     {
        int next = fileSystem->lookup(current, pathComponents[i]);
        if (next < 0) 
        {
            throw ClientError::notFound();
        }
        current = next;
    }
    
    inode_t inode;
    fileSystem->stat(current, &inode);
    
    if (inode.type == UFS_DIRECTORY) 
    {
        // directory listing
        string out;
        vector<dir_ent_t> entries(inode.size / sizeof(dir_ent_t));
        fileSystem->read(current, entries.data(), inode.size);
        
        //entry sorting by name
        sort(entries.begin() + 2, entries.end(), [](const dir_ent_t& a, const dir_ent_t& b) {return strcmp(a.name, b.name) < 0;});
        
        for (size_t i = 2; i < entries.size(); ++i) 
        {
            inode_t childInode;
            fileSystem->stat(entries[i].inum, &childInode);
            out += entries[i].name;
            if (childInode.type == UFS_DIRECTORY) 
            {
                out += "/";
            }
            out += "\n";
        }
        response->setBody(out);
    } 
    else 
    {
        //hand file contents
        char buffer[inode.size];
        fileSystem->read(current, buffer, inode.size);
        //string fileContent(buffer.begin(), buffer.end());
        string str(buffer);
        response->setBody(str);
    }
}

void DistributedFileSystemService::put(HTTPRequest *request, HTTPResponse *response) //done
{
    fileSystem->disk->beginTransaction();

    try {
        //function from httprequest
        //getting path componenets from the request
        vector<string> pathComponents = request->getPathComponents();

        //intializing parent inode num to root dir
        int parentInodeNumber = UFS_ROOT_DIRECTORY_INODE_NUMBER;

        //path component iteration
        for (int i = 1; i < static_cast<int>(pathComponents.size()); i++) 
        {
            string currentComponent = pathComponents[i];

            //when this is not the last component a directory is made
            if (i < static_cast<int>(pathComponents.size()) - 1) 
            {
                int childInodeNumber = fileSystem->create(parentInodeNumber, UFS_DIRECTORY, currentComponent);
                if (childInodeNumber == -EINVALIDTYPE) 
                {
                    throw ClientError::conflict();
                }
                parentInodeNumber = childInodeNumber;
            }
            //when this is the last, create create a regular file
            else 
            {
                int childInodeNumber = fileSystem->create(parentInodeNumber, UFS_REGULAR_FILE, currentComponent);
                if (childInodeNumber == -EINVALIDTYPE) 
                {
                    throw ClientError::conflict();
                }
                //write request
                string requestBody = request->getBody();
                fileSystem->write(childInodeNumber, requestBody.c_str(), requestBody.length() + 1);
            }
        }
    }
    catch (...) 
    {
      //rollback if error
      fileSystem->disk->rollback();
      throw;
    }
    //if nothing happens then commit
    fileSystem->disk->commit();
    response->setBody("");
} 

void DistributedFileSystemService::del(HTTPRequest *request, HTTPResponse *response) 
{
    vector<string> names = request->getPathComponents();
    string dir_ent_name = names.back();
    names.pop_back();

    int parentInodeNum = UFS_ROOT_DIRECTORY_INODE_NUMBER;
    for (size_t i = 1; i < names.size(); i++) 
    {
        parentInodeNum = fileSystem->lookup(parentInodeNum, names[i]);
        if (parentInodeNum < 0) 
        {
            throw ClientError::notFound();
        }
    }

    fileSystem->disk->beginTransaction();
    try {
        //deleting the object
        int deleteResult = fileSystem->unlink(parentInodeNum, dir_ent_name);
        if (deleteResult == -EINVALIDINODE) 
        {
          throw ClientError::notFound();
        } 
        else if (deleteResult == -EINVALIDNAME) 
        {
          throw ClientError::badRequest();
        } 
        else if (deleteResult == -EDIRNOTEMPTY) 
        {
          throw ClientError::badRequest();
        } 
        else if (deleteResult == -EUNLINKNOTALLOWED) 
        {
          throw ClientError::badRequest();
        }

        else if (deleteResult == -ENOTENOUGHSPACE) 
        {
          throw ClientError::insufficientStorage();
        } 
        fileSystem->disk->commit();
    } 
    catch (const ClientError &e) 
    {
        response->setStatus(e.status_code);
        response->setBody(e.what());
        fileSystem->disk->rollback();
    }
}