#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <string.h>
#include <filesystem>

using std::cout;
using std::vector;
using std::fstream;
using std::ios;
using std::string;
using std::size_t;

#define BLOCK_SIZE 512 // bytes
#define NUM_BLOCKS 4096
#define NUM_INODES 128 // stored in blocks 2 to 9 (8 blocks / 32 bytes = 128 inodes)
#define META_BLOCKS 10
#define MAGIC_NUM 7428
#define VDISK_FILE_NAME "vdisk"
#define INODE_SIZE 32 // bytes

typedef struct inode_t {
    int size = 0;
    int flags = 0; // 0 for file, 1 for dir
    short direct[10] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    short singleIndirect = -1;
    short doubleIndirect = -1;
} inode;

typedef struct dirEntry_t {
    char inode = 0;
    char name[31] = {0};
} dirEntry;

typedef struct dirBlock_t {
    dirEntry entries[16];
} dirBlock;

inode DIR_INODE = {BLOCK_SIZE, 1};

typedef struct indirectBlock_t {
    short blockPointers[256] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
} indirectBlock;

typedef struct superblock_t {
    int magicNum = MAGIC_NUM;
    int numBlocks = NUM_BLOCKS;
    int numInodes = NUM_INODES;
    char freeInodes[NUM_INODES] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    inode root = DIR_INODE;
} superblock;

typedef struct freeblock_t {
    unsigned char free[NUM_BLOCKS/8];
} freeblock;

class VDiskDriver {
private:
    fstream disk;
    vector<int> freeBlocks;
    vector<int> freeInodes;
    int freeBlockClock;
public:
    VDiskDriver() {
        freeInodes.push_back(-1); // inode 0 is not valid
        freeBlockClock = 0;
    }

    /**
     * Mounts the virtual disk file if matching format
     * 
     * Returns 0 on success and 1 on failure
     */
    int mount() {
        disk.open(VDISK_FILE_NAME, ios::in | ios::binary);
        // read the superblock
        superblock super;
        disk.read(reinterpret_cast<char*>(&super), sizeof(superblock));
        // if the magic number does not match the mount fails
        if (super.magicNum != MAGIC_NUM) return 1;
        // read the free inodes array
        for (int i = 0; i < NUM_INODES; i++)
            freeInodes.push_back(super.freeInodes[i]);
        // read the freeblock
        disk.seekg(BLOCK_SIZE);
        freeblock free;
        disk.read(reinterpret_cast<char*>(&free), 512);
        // store the freeblock bits in the free vector
        for (int i = 0; i < NUM_BLOCKS/8; i++)
            for (int j = 7; j >= 0; j--)
                freeBlocks.push_back((free.free[i] & (1 << j)) >> j);
        if (disk.bad()) return 1;
        disk.close();
        return 0;
    }

    /**
     * Initializes a formatted virtual disk file
     * 
     * Returns 0 on success and 1 on failure
     */
    int format() {
        disk.open(VDISK_FILE_NAME, ios::out | ios::binary | ios::trunc);
        // write the superblock
        superblock super;
        super.root.direct[0] = 10;
        disk.write(reinterpret_cast<char*>(&super), BLOCK_SIZE);
        // write the freeblock
        freeblock free;
        free.free[0] = 0b00000000;
        free.free[1] = 0b00011111;
        for (int i = 2; i < BLOCK_SIZE; i++)
            free.free[i] = 0b11111111;
        disk.write(reinterpret_cast<char*>(&free), BLOCK_SIZE);
        // write the rest of the metablocks
        for (int i = 2; i < META_BLOCKS; i++)
            disk.write("x", BLOCK_SIZE);
        // write the root directory block (10)
        dirBlock root;
        root.entries[0].inode = -1;
        strcpy(root.entries[0].name, ".");
        root.entries[1].inode = -1;
        strcpy(root.entries[1].name, "..");
        disk.write(reinterpret_cast<char*>(&root), BLOCK_SIZE);
        // write the rest of the blocks
        for (int i = META_BLOCKS + 1; i < NUM_BLOCKS; i++)
            disk.write("x", BLOCK_SIZE);
        if (disk.bad()) return 1;
        disk.close();
        return 0;
    }

    /**
     * Reads a block with specified block number
     * and stores contents in a byte buffer
     * 
     * Returns 0 on success and 1 on failure
     */
    int readBlock(char* buffer, int blockNum) {
        // block number out of range
        if (blockNum < 0 || blockNum >= NUM_BLOCKS) return 1;
        // attempting to read a block that is free
        if (freeBlocks[blockNum] == 1) return 1;
        // read the specified block
        disk.open(VDISK_FILE_NAME, ios::in | ios::binary);
        disk.seekg(blockNum * BLOCK_SIZE);
        disk.read(buffer, BLOCK_SIZE);
        if (disk.bad()) return 1;
        disk.close();
        return 0;
    }

    /**
     * Allocates and writes the contents in input byte buffer 
     * to the block with specified block number
     * 
     * Returns 0 on success and 1 on failure
     */
    int writeBlock(char* buffer, int blockNum) {
        // block number out of range
        if (blockNum < 0 || blockNum >= NUM_BLOCKS) return 1;
        // attempting to write a block that is not free
        if (freeBlocks[blockNum] == 0) return 1;
        // write the specified block 
        disk.open(VDISK_FILE_NAME, ios::in | ios::out | ios::binary);
        disk.seekp(blockNum * BLOCK_SIZE);
        disk.write(buffer, BLOCK_SIZE);
        // update the block to not free in vector then on free block
        freeBlocks[blockNum] = 0;
        disk.seekg(BLOCK_SIZE + blockNum/8);
        char byte[1];
        disk.read(byte, 1);
        byte[0] &= ~(1 << (7 - blockNum % 8));
        disk.seekp(BLOCK_SIZE + blockNum/8);
        disk.write(byte, 1);
        if (disk.bad()) return 1;
        disk.close();
        return 0;
    }

    /**
     * Updates the block with specified block number 
     * with the contents in input byte buffer 
     * 
     * Returns 0 on success and 1 on failure
     */
    int updateBlock(char* buffer, int blockNum) {
        // block number out of range
        if (blockNum < 0 || blockNum >= NUM_BLOCKS) return 1;
        // attempting to update a block that is free
        if (freeBlocks[blockNum] == 1) return 1;
        // update the specified block 
        disk.open(VDISK_FILE_NAME, ios::in | ios::out | ios::binary);
        disk.seekp(blockNum * BLOCK_SIZE);
        disk.write(buffer, BLOCK_SIZE);
        if (disk.bad()) return 1;
        disk.close();
        return 0;
    }

    /**
     * Frees the block with specified block number
     * 
     * Returns 0 on success and 1 on failure
     */
    int freeBlock(int blockNum) {
        // block number out of range
        if (blockNum < 0 || blockNum >= NUM_BLOCKS) return 1;
        // attempting to write a block that is already free
        if (freeBlocks[blockNum] == 1) return 1;
        // update the block to free in vector then on free block
        freeBlocks[blockNum] = 1;
        disk.open(VDISK_FILE_NAME, ios::in | ios::out | ios::binary);
        disk.seekg(BLOCK_SIZE + blockNum/8);
        char byte[1];
        disk.read(byte, 1);
        byte[0] |= 1 << (7 - blockNum % 8);
        disk.seekp(BLOCK_SIZE + blockNum/8);
        disk.write(byte, 1);
        if (disk.bad()) return 1;
        disk.close();
        return 0;
    }

    /**
     * Returns the block number of the first free block
     * utilizing a clock hand algorithm to avoid 
     * repeatedly allocating the same blocks
     * 
     * Returns -1 on failure (no free blocks were found)
     */
    int getFreeBlock() {
        for (int i = META_BLOCKS; i < NUM_BLOCKS; i++) {
            if (freeBlocks[freeBlockClock + META_BLOCKS] == 1) {
                int freeBlockNum = freeBlockClock + META_BLOCKS;
                freeBlockClock = (freeBlockClock + 1) % (NUM_BLOCKS - META_BLOCKS);
                return freeBlockNum;
            }
            freeBlockClock = (freeBlockClock + 1) % (NUM_BLOCKS - META_BLOCKS);
        }
        return -1;
    }

    /**
     * Reads the root directory inode into a given buffer
     * 
     * Returns 0 on success and 1 on failure
     */
    int getRootInode(char* rootInode) {
        disk.open(VDISK_FILE_NAME, ios::in | ios::binary);
        disk.seekg(sizeof(superblock) - INODE_SIZE);
        disk.read(rootInode, INODE_SIZE);
        if (disk.bad()) return 1;
        disk.close();
        return 0;
    }

    /**
     * Writes the root directory inode in the superblock from a given buffer
     * 
     * Returns 0 on success and 1 on failure
     */
    int setRootInode(char* rootInode) {
        disk.open(VDISK_FILE_NAME, ios::in | ios::out | ios::binary);
        disk.seekp(sizeof(superblock) - INODE_SIZE);
        disk.write(rootInode, INODE_SIZE);
        if (disk.bad()) return 1;
        disk.close();
        return 0;
    }

    /**
     * Reads an inode into a given buffer given the inode number
     * 
     * Returns 0 on success and 1 on failure
     */
    int getInode(int inodeNum, char* inode) {
        if (inodeNum == -1) return getRootInode(inode);
        // inode number out of range
        if (inodeNum <= 0 || inodeNum > NUM_INODES) return 1;
        // attempting to read a free inode
        if (freeInodes[inodeNum] == 1) return 1;
        // read the specified inode
        disk.open(VDISK_FILE_NAME, ios::in | ios::binary);
        disk.seekg(BLOCK_SIZE*2 + INODE_SIZE*(inodeNum-1));
        disk.read(inode, INODE_SIZE);
        if (disk.bad()) return 1;
        disk.close();
        return 0;
    }

    /**
     * Allocates and writes an inode from a given buffer given the inode number
     * 
     * Returns 0 on success and 1 on failure
     */
    int setInode(int inodeNum, char* inode) {
        if (inodeNum == -1) return setRootInode(inode);
        // inode number out of range
        if (inodeNum <= 0 || inodeNum > NUM_INODES) return 1;
        // attempting to write an inode that is not free
        if (freeInodes[inodeNum] == 0) return 1;
        // write the specified inode to table
        disk.open(VDISK_FILE_NAME, ios::in | ios::out | ios::binary);
        disk.seekp(BLOCK_SIZE*2 + INODE_SIZE*(inodeNum-1));
        disk.write(inode, INODE_SIZE);
        // update the inode to not free in vector then in superblock
        freeInodes[inodeNum] = 0;
        disk.seekp(sizeof(superblock) - INODE_SIZE - NUM_INODES + inodeNum - 1);
        disk.put(0);
        if (disk.bad()) return 1;
        disk.close();
        return 0;
    }

    /**
     * Updates an inode from a given buffer given the inode number
     * 
     * Returns 0 on success and 1 on failure
     */
    int updateInode(int inodeNum, char* inode) {
        if (inodeNum == -1) return setRootInode(inode);
        // inode number out of range
        if (inodeNum <= 0 || inodeNum > NUM_INODES) return 1;
        // attempting to update a free inode
        if (freeInodes[inodeNum] == 1) return 1;
        // update the specified inode in table
        disk.open(VDISK_FILE_NAME, ios::in | ios::out | ios::binary);
        disk.seekp(BLOCK_SIZE*2 + INODE_SIZE*(inodeNum-1));
        disk.write(inode, INODE_SIZE);
        if (disk.bad()) return 1;
        disk.close();
        return 0;
    }

    /**
     * Frees the inode with specified inode number
     * 
     * Returns 0 on success and 1 on failure
     */
    int freeInode(int inodeNum) {
        // inode number out of range
        if (inodeNum <= 0 || inodeNum > NUM_INODES) return 1;
        // attempting to free an inode that is already free
        if (freeInodes[inodeNum] == 1) return 1;
        // update the inode to free in vector then in superblock
        freeInodes[inodeNum] = 0;
        disk.open(VDISK_FILE_NAME, ios::in | ios::out | ios::binary);
        disk.seekp(sizeof(superblock) - INODE_SIZE - NUM_INODES + inodeNum - 1);
        disk.put(1);
        if (disk.bad()) return 1;
        disk.close();
        return 0;
    }

    /**
     * Returns the inode number of the first free inode
     * 
     * Returns -1 on failure (no free inodes were found)
     */
    int getFreeInode() {
        for (int i = 1; i <= NUM_INODES; i++) {
            if (freeInodes[i] == 1) {
                return i;
            }
        }
        return -1;
    }

};
