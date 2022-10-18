# File System Project

## Installation

Compile with:

```g++ -std=c++17 fs.cxx -o fs```

Run comprehensive test with:

```./fs```

## Features

Support for 4096 blocks of 512 bytes including 10 blocks reserved for metadata

Support for 128 inodes not including the root directory inode

Inodes with direct and single and double indirection for non-contiguous data storage

Support for files with size up to (10 + 256 + 256×256)×512 = 33690624 bytes = 32.13 MB

Support for directories containing up to 14 files and/or directories

Utilizing a clock hand algorithm for free block allocation (helps avoid repeatedly writing the same blocks on disk)

Functionality to create files, read or write any number of bytes, and remove files

Functionality to seek in files with separate read and write pointers

Functionality to create and remove directories

Support for parsing paths with multiple levels

## API

### FileSystem:
```c++
/**
 * Creates a directory given a valid path that doesn't exist
 * 
 * Returns 0 on success and 1 on failure
 */
int mkdir(string& path)

/**
 * Removes a directory given a path of an existing empty directory
 * 
 * Returns 0 on success and 1 on failure
 */
int rmdir(string& path)

/**
 * Opens a file given a path if it exists or creates it if it does not
 * 
 * Returns 0 on success and 1 on failure
 */
int open(string& path)

/**
 * Moves the write head in the open file 
 * to the given number of bytes from the start of the file
 * 
 * Returns 0 on success and 1 on failure
 */
int seekw(size_t count)

/**
 * Writes the given number of bytes from the buffer
 * into the open file at the write head's current position
 * 
 * Returns 0 on success and 1 on failure
 */
int write(char* buffer, size_t count)

/**
 * Moves the read head in the open file 
 * to the given number of bytes from the start of the file
 * 
 * Returns 0 on success and 1 on failure
 */
int seekr(size_t count)

/**
 * Reads the given number of bytes from the open file 
 * at the write head's current position into the buffer
 * 
 * Returns 0 on success and 1 on failure
 */
int read(char* buffer, size_t count)

/**
 * Closes the open file
 * 
 * Returns 0 on success and 1 on failure
 */
int close()

/**
 * Removes a file given its path freeing its inode 
 * and all its data blocks
 * 
 * Returns 0 on success and 1 on failure
 */
int remove(string& path)

/**
 * Returns the open file's size or -1 if no file is open
 */
size_t getOpenFileSize()
```

### VDiskDriver:
```c++
/**
 * Mounts the virtual disk file if matching format
 * 
 * Returns 0 on success and 1 on failure
 */
int mount()

/**
 * Initializes a formatted virtual disk file
 * 
 * Returns 0 on success and 1 on failure
 */
int format()

/**
 * Reads a block with specified block number
 * and stores contents in a byte buffer
 * 
 * Returns 0 on success and 1 on failure
 */
int readBlock(char* buffer, int blockNum)

/**
 * Allocates and writes the contents in input byte buffer 
 * to the block with specified block number
 * 
 * Returns 0 on success and 1 on failure
 */
int writeBlock(char* buffer, int blockNum)

/**
 * Updates the block with specified block number 
 * with the contents in input byte buffer 
 * 
 * Returns 0 on success and 1 on failure
 */
int updateBlock(char* buffer, int blockNum)

/**
 * Frees the block with specified block number
 * 
 * Returns 0 on success and 1 on failure
 */
int freeBlock(int blockNum)

/**
 * Returns the block number of the first free block
 * utilizing a clock hand algorithm to avoid 
 * repeatedly allocating the same blocks
 * 
 * Returns -1 on failure (no free blocks were found)
 */
int getFreeBlock()

/**
 * Reads the root directory inode into a given buffer
 * 
 * Returns 0 on success and 1 on failure
 */
int getRootInode(char* rootInode)

/**
 * Writes the root directory inode in the superblock 
 * from a given buffer
 * 
 * Returns 0 on success and 1 on failure
 */
int setRootInode(char* rootInode)

/**
 * Reads an inode into a given buffer given the inode number
 * 
 * Returns 0 on success and 1 on failure
 */
int getInode(int inodeNum, char* inode)

/**
 * Allocates and writes an inode from a given buffer 
 * given the inode number
 * 
 * Returns 0 on success and 1 on failure
 */
int setInode(int inodeNum, char* inode)

/**
 * Updates an inode from a given buffer given the inode number
 * 
 * Returns 0 on success and 1 on failure
 */
int updateInode(int inodeNum, char* inode)

/**
 * Frees the inode with specified inode number
 * 
 * Returns 0 on success and 1 on failure
 */
int freeInode(int inodeNum)

/**
 * Returns the inode number of the first free inode
 * 
 * Returns -1 on failure (no free inodes were found)
 */
int getFreeInode() 
```