#include "vdd.hxx"
#include <filesystem>

using std::filesystem::exists;

typedef struct returnInodes_t {
    int inodeNum;
    int parentInodeNum;
} returnInodes;

class FileSystem {
private:
    VDiskDriver driver;
    bool fileOpen;
    int openFileInodeNum;
    inode openFileInode;
    size_t openFileReadPointer;
    size_t openFileWritePointer;

    returnInodes getInode(string& path) {
        if (path[0] != '/') return returnInodes {-3, -3}; // invalid path
        path.erase(0, 1);
        size_t pos = path.find("/");
        string token = path.substr(0, pos);
        int parentInodeNum = -1; // inode number for root dir
        int inodeNum = getSubDirInodeNum(parentInodeNum, token);
        path.erase(0, pos + 1);
        while (pos != string::npos && inodeNum != -2) {
            pos = path.find("/");
            token = path.substr(0, pos);
            parentInodeNum = inodeNum;
            inodeNum = getSubDirInodeNum(parentInodeNum, token);
            path.erase(0, pos + 1);
        }
        return returnInodes {inodeNum, parentInodeNum};
    }

    int getSubDirInodeNum(int parentInodeNum, string name) {
        if (name == "") return -3; // name cannot be empty
        inode parentInode;
        driver.getInode(parentInodeNum, reinterpret_cast<char*>(&parentInode));
        if (parentInode.flags != 1) return -3; // parent is not a dir
        dirBlock dir;
        if (driver.readBlock(reinterpret_cast<char*>(&dir), parentInode.direct[0]) == 1) return -3; // failed to read
        for (dirEntry entry : dir.entries) {
            if (entry.name == name) {
                return entry.inode;
            }
        }
        return -2; // not found
    }

    int writeBytesToDisk(char* buffer, int bytesToWrite, short& blockNum) {
        if (bytesToWrite > BLOCK_SIZE) return 1;
        else if (bytesToWrite < BLOCK_SIZE) { // partial block write
            char block[BLOCK_SIZE];
            bool allocate = false;
            if (blockNum == -1) { // allocate new block
                allocate = true;
                blockNum = driver.getFreeBlock();
                if (blockNum == -1) return 1; // failed to get a free block
            } else {
                if (driver.readBlock(block, blockNum) != 0) return 1; // failed to read block
            }
            int byteOffset = openFileWritePointer % BLOCK_SIZE;
            // copy (bytesToWrite) bytes from buffer into block at (byteOffset)
            memcpy(block + byteOffset, buffer, bytesToWrite);
            if (allocate) { // allocate new block
                if (driver.writeBlock(block, blockNum) != 0) return 1; // failed to write block
            } else { // just update existing block
                if (driver.updateBlock(block, blockNum) != 0) return 1; // failed to update block
            }
        } else { // full block update
            if (blockNum == -1) { // allocate new block
                blockNum = driver.getFreeBlock();
                if (blockNum == -1) return 1; // failed to get a free block
                if (driver.writeBlock(buffer, blockNum) != 0) return 1; // failed to write block
            } else { // just update existing block
                if (driver.updateBlock(buffer, blockNum) != 0) return 1; // failed to update block
            }
        }
        return 0;
    }

    int writeThroughSingleIndirect(char* buffer, int bytesToWrite, short& singleIndirect, int index) {
        indirectBlock indirect;
        if (singleIndirect == -1) { // allocate it
            singleIndirect = driver.getFreeBlock();
            if (singleIndirect == -1) return 1; // failed to get a free block
            if (writeBytesToDisk(buffer, bytesToWrite, indirect.blockPointers[index]) != 0) return 1; // failed to write bytes
            if (driver.writeBlock(reinterpret_cast<char*>(&indirect), singleIndirect) != 0) return 1; // failed to write indirect block
        } else { // read it
            if (driver.readBlock(reinterpret_cast<char*>(&indirect), singleIndirect) != 0) return 1; // failed to read indirect block
            if (writeBytesToDisk(buffer, bytesToWrite, indirect.blockPointers[index]) != 0) return 1; // failed to write bytes
            if (driver.updateBlock(reinterpret_cast<char*>(&indirect), singleIndirect) != 0) return 1; // failed to update indirect block
        }
        return 0;
    }

    int writeThroughDoubleIndirect(char* buffer, int bytesToWrite, int index) {
        indirectBlock doubleIndirect;
        if (openFileInode.doubleIndirect == -1) { // allocate it
            openFileInode.doubleIndirect = driver.getFreeBlock();
            if (openFileInode.doubleIndirect == -1) return 1; // failed to get a free block
            if (writeThroughSingleIndirect(buffer, bytesToWrite, doubleIndirect.blockPointers[index/256], index % 256) != 0) return 1; // failed to write through single indirect
            if (driver.writeBlock(reinterpret_cast<char*>(&doubleIndirect), openFileInode.doubleIndirect) != 0) return 1; // failed to write indirect block
        } else { // read it
            if (driver.readBlock(reinterpret_cast<char*>(&doubleIndirect), openFileInode.doubleIndirect) != 0) return 1; // failed to read indirect block
            if (writeThroughSingleIndirect(buffer, bytesToWrite, doubleIndirect.blockPointers[index/256], index % 256) != 0) return 1; // failed to write through single indirect
            if (driver.updateBlock(reinterpret_cast<char*>(&doubleIndirect), openFileInode.doubleIndirect) != 0) return 1; // failed to update indirect block
        }
        return 0;
    }

    int readBytesFromDisk(char* buffer, int bytesToRead, short& blockNum) {
        if (blockNum == -1) return 1; // trying to read unallocated block
        if (bytesToRead > BLOCK_SIZE) return 1;
        else if (bytesToRead < BLOCK_SIZE) { // partial block read
            char block[BLOCK_SIZE];
            if (driver.readBlock(block, blockNum) != 0) return 1; // failed to read block
            int byteOffset = openFileReadPointer % BLOCK_SIZE;
            // copy (bytesToRead) bytes from block at (byteOffset) into buffer
            memcpy(buffer, block + byteOffset, bytesToRead);
        } else { // full block read
            if (driver.readBlock(buffer, blockNum) != 0) return 1; // failed to read block
        }
        return 0;
    }

    int readThroughSingleIndirect(char* buffer, int bytesToRead, short& singleIndirect, int index) {
        if (singleIndirect == -1) return 1; // trying to read unallocated single indirect block
        else { // read it
            indirectBlock indirect;
            if (driver.readBlock(reinterpret_cast<char*>(&indirect), singleIndirect) != 0) return 1; // failed to read indirect block
            if (readBytesFromDisk(buffer, bytesToRead, indirect.blockPointers[index]) != 0) return 1; // failed to read bytes
        }
        return 0;
    }

    int readThroughDoubleIndirect(char* buffer, int bytesToRead, int index) {
        if (openFileInode.doubleIndirect == -1) return 1; // trying to read unallocated double indirect block
        else { // read it
            indirectBlock doubleIndirect;
            if (driver.readBlock(reinterpret_cast<char*>(&doubleIndirect), openFileInode.doubleIndirect) != 0) return 1; // failed to read indirect block
            if (readThroughSingleIndirect(buffer, bytesToRead, doubleIndirect.blockPointers[index/256], index % 256) != 0) return 1; // failed to read through single indirect
        }
        return 0;
    }

    int freeSingleIndirect(int singleIndirect) {
        if (singleIndirect != -1) {
            indirectBlock single;
            if (driver.readBlock(reinterpret_cast<char*>(&single), singleIndirect) != 0) return 1; // failed to read single indirect block
            for (int i = 0; i < 256; i++) {
                if (single.blockPointers[i] != -1) {
                    if (driver.freeBlock(single.blockPointers[i]) != 0) return 1; // failed to free block
                }
            }
            if (driver.freeBlock(singleIndirect) != 0) return 1; // failed to free single indirect block
        }
        return 0;
    }

public:
    FileSystem() {
        if (!exists(VDISK_FILE_NAME)) 
            driver.format();
        driver.mount();
        fileOpen = false;
    }

    /**
     * Creates a directory given a valid path that doesn't exist
     * 
     * Returns 0 on success and 1 on failure
     */
    int mkdir(string& path) {
        returnInodes fetchedInodes = getInode(path);
        if (fetchedInodes.inodeNum != -2) return 1; // dir already exists or invalid path
        inode parentInode;
        if (driver.getInode(fetchedInodes.parentInodeNum, reinterpret_cast<char*>(&parentInode)) != 0) return 1; // failed to read inode
        dirBlock parentDir;
        if (driver.readBlock(reinterpret_cast<char*>(&parentDir), parentInode.direct[0]) != 0) return 1; // failed to read block
        for (dirEntry& entry : parentDir.entries) {
            if (entry.inode == 0) { // find a free entry in parent dir
                entry.inode = driver.getFreeInode();
                if (entry.inode == -1) return 1; // could not find a free inode
                strcpy(entry.name, path.c_str());
                inode newInode = DIR_INODE;
                newInode.direct[0] = driver.getFreeBlock();
                if (newInode.direct[0] == -1) return 1; // could not find a free block
                dirBlock newDir;
                newDir.entries[0].inode = entry.inode;
                strcpy(newDir.entries[0].name, ".");
                newDir.entries[1].inode = fetchedInodes.parentInodeNum;
                strcpy(newDir.entries[1].name, "..");
                if (driver.writeBlock(reinterpret_cast<char*>(&newDir), newInode.direct[0]) != 0) return 1; // failed to write dir block
                if (driver.setInode(entry.inode, reinterpret_cast<char*>(&newInode)) != 0) return 1; // failed to write inode of dir
                if (driver.updateBlock(reinterpret_cast<char*>(&parentDir), parentInode.direct[0]) != 0) return 1; // failed to update block of parent dir
                return 0;
            }
        }
        return 1; // dir is full (all entries are being used)
    }

    /**
     * Removes a directory given a path of an existing empty directory
     * 
     * Returns 0 on success and 1 on failure
     */
    int rmdir(string& path) {
        returnInodes fetchedInodes = getInode(path);
        if (fetchedInodes.inodeNum <= 0) return 1; // dir not found or invalid path or root
        inode childInode;
        if (driver.getInode(fetchedInodes.inodeNum, reinterpret_cast<char*>(&childInode)) != 0) return 1; // failed to read inode
        if (childInode.flags != 1) return 1; // path is not a dir
        dirBlock childDir;
        if (driver.readBlock(reinterpret_cast<char*>(&childDir), childInode.direct[0]) != 0) return 1; // failed to read block
        for (int i = 2; i < 16; i++) {
            if (childDir.entries[i].inode != 0) return 1; // dir is not empty
        }
        inode parentInode;
        if (driver.getInode(fetchedInodes.parentInodeNum, reinterpret_cast<char*>(&parentInode)) != 0) return 1; // failed to read inode
        dirBlock parentDir;
        if (driver.readBlock(reinterpret_cast<char*>(&parentDir), parentInode.direct[0]) != 0) return 1; // failed to read block
        for (dirEntry& entry : parentDir.entries) {
            if (entry.inode == fetchedInodes.inodeNum) {
                entry.inode = 0;
                if (driver.freeBlock(childInode.direct[0]) != 0) return 1; // failed to free dir block
                if (driver.freeInode(fetchedInodes.inodeNum) != 0) return 1; // failed to free inode of dir
                if (driver.updateBlock(reinterpret_cast<char*>(&parentDir), parentInode.direct[0]) != 0) return 1; // failed to update block of parent dir
                return 0;
            }
        }
        return 1; // could not find inode in parent dir
    }

    /**
     * Opens a file given a path if it exists or creates it if it does not
     * 
     * Returns 0 on success and 1 on failure
     */
    int open(string& path) {
        if (fileOpen) return 1; // another file is already open
        openFileReadPointer = 0;
        openFileWritePointer = 0;
        returnInodes fetchedInodes = getInode(path);
        if (fetchedInodes.inodeNum == -2) { // file not found, have to create it
            inode parentInode;
            if (driver.getInode(fetchedInodes.parentInodeNum, reinterpret_cast<char*>(&parentInode)) != 0) return 1; // failed to read inode
            dirBlock parentDir;
            if (driver.readBlock(reinterpret_cast<char*>(&parentDir), parentInode.direct[0]) != 0) return 1; // failed to read block
            for (dirEntry& entry : parentDir.entries) {
                if (entry.inode == 0) { // find a free entry in parent dir
                    entry.inode = driver.getFreeInode();
                    if (entry.inode == -1) return 1; // could not find a free inode
                    strcpy(entry.name, path.c_str());
                    inode newInode;
                    if (driver.setInode(entry.inode, reinterpret_cast<char*>(&newInode)) != 0) return 1; // failed to write inode of new file
                    if (driver.updateBlock(reinterpret_cast<char*>(&parentDir), parentInode.direct[0]) != 0) return 1; // failed to update block of parent dir
                    openFileInodeNum = entry.inode;
                    openFileInode = newInode;
                    fileOpen = true;
                    return 0;
                }
            }
            return 1; // parent dir is full (all entries are being used)
        } else if (fetchedInodes.inodeNum > 0) { // file is found, just open it
            openFileInodeNum = fetchedInodes.inodeNum;
            if (driver.getInode(openFileInodeNum, reinterpret_cast<char*>(&openFileInode)) != 0) return 1; // failed to read inode
            if (openFileInode.flags != 0) return 1; // path is not a file
            fileOpen = true;
            return 0;
        } else return 1; // invalid path
    }

    /**
     * Moves the write head in the open file 
     * to the given number of bytes from the start of the file
     * 
     * Returns 0 on success and 1 on failure
     */
    int seekw(size_t count) {
        if (!fileOpen) return 1; // no file has been opened
        if (count > openFileInode.size) return 1;
        openFileWritePointer = count;
        return 0;
    }

    /**
     * Writes the given number of bytes from the buffer
     * into the open file at the write head's current position
     * 
     * Returns 0 on success and 1 on failure
     */
    int write(char* buffer, size_t count) {
        if (!fileOpen) return 1; // no file has been opened
        int blocksNeeded = (openFileWritePointer + count) / BLOCK_SIZE + ((openFileWritePointer + count) % BLOCK_SIZE != 0);
        int startingBlock = openFileWritePointer / BLOCK_SIZE;
        for (int i = startingBlock; i < blocksNeeded; i++) {
            int bytesToWrite = BLOCK_SIZE * (i + 1) - openFileWritePointer - (BLOCK_SIZE - openFileWritePointer % BLOCK_SIZE - count) * (i == blocksNeeded - 1);
            if (i < 10) { // write to direct blocks
                if (writeBytesToDisk(buffer, bytesToWrite, openFileInode.direct[i]) != 0) return 1; // failed to write bytes
            } else if (i >= 10 && i < 266) { // pass through single indirect block
                if (writeThroughSingleIndirect(buffer, bytesToWrite, openFileInode.singleIndirect, i-10) != 0) return 1; // failed to write through single indirect block
            } else if (i >= 266 && i < 266+256*256) { // pass through double indirect block
                if (writeThroughDoubleIndirect(buffer, bytesToWrite, i-266) != 0) return 1; // failed to write through double indirect block
            } else return 1; // file too large, size not supported by file system
            openFileWritePointer += bytesToWrite;
            buffer += bytesToWrite;
            count -= bytesToWrite;
            if (openFileWritePointer > openFileInode.size) openFileInode.size = openFileWritePointer;
        }
        if (driver.updateInode(openFileInodeNum, reinterpret_cast<char*>(&openFileInode)) != 0) return 1; // failed to update inode
        return 0;
    }

    /**
     * Moves the read head in the open file 
     * to the given number of bytes from the start of the file
     * 
     * Returns 0 on success and 1 on failure
     */
    int seekr(size_t count) {
        if (!fileOpen) return 1; // no file has been opened
        if (count > openFileInode.size) return 1;
        openFileReadPointer = count;
        return 0;
    }

    /**
     * Reads the given number of bytes from the open file 
     * at the write head's current position into the buffer
     * 
     * Returns 0 on success and 1 on failure
     */
    int read(char* buffer, size_t count) {
        if (!fileOpen) return 1; // no file has been opened
        int blocksNeeded = (openFileReadPointer + count) / BLOCK_SIZE + ((openFileReadPointer + count) % BLOCK_SIZE != 0);
        int startingBlock = openFileReadPointer / BLOCK_SIZE;
        for (int i = startingBlock; i < blocksNeeded; i++) {
            int bytesToRead = BLOCK_SIZE * (i + 1) - openFileReadPointer - (BLOCK_SIZE - openFileReadPointer % BLOCK_SIZE - count) * (i == blocksNeeded - 1);
            if (i < 10) { // read from direct blocks
                if (readBytesFromDisk(buffer, bytesToRead, openFileInode.direct[i]) != 0) return 1; // failed to read bytes
            } else if (i >= 10 && i < 266) { // pass through single indirect block
                if (readThroughSingleIndirect(buffer, bytesToRead, openFileInode.singleIndirect, i-10) != 0) return 1; // failed to read through single indirect block
            } else if (i >= 266 && i < 266+256*256) { // pass through double indirect block
                if (readThroughDoubleIndirect(buffer, bytesToRead, i-266) != 0) return 1; // failed to read through double indirect block
            } else return 1; // file too large, size not supported by file system
            openFileReadPointer += bytesToRead;
            buffer += bytesToRead;
            count -= bytesToRead;
        }
        return 0;
    }

    /**
     * Closes the open file
     * 
     * Returns 0 on success and 1 on failure
     */
    int close() {
        if (!fileOpen) return 1; // no file has been opened
        if (driver.updateInode(openFileInodeNum, reinterpret_cast<char*>(&openFileInode)) != 0) return 1; // failed to update inode
        fileOpen = false;
        return 0;
    }

    /**
     * Removes a file given its path freeing its inode 
     * and all its data blocks
     * 
     * Returns 0 on success and 1 on failure
     */
    int remove(string& path) {
        returnInodes fetchedInodes = getInode(path);
        if (fetchedInodes.inodeNum <= 0) return 1; // file not found or invalid path
        if (fileOpen && openFileInodeNum == fetchedInodes.inodeNum) return 1; // the open file needs to be closed before removing it
        inode childInode;
        if (driver.getInode(fetchedInodes.inodeNum, reinterpret_cast<char*>(&childInode)) != 0) return 1; // failed to read inode
        if (childInode.flags != 0) return 1; // path is not a file
        for (int i = 0; i < 10; i++) {
            if (childInode.direct[i] != -1) {
                if (driver.freeBlock(childInode.direct[i]) != 0) return 1; // failed to free direct block
            }
        }
        if (freeSingleIndirect(childInode.singleIndirect) != 0) return 1; // failed to free blocks through single indirect
        if (childInode.doubleIndirect != -1) {
            indirectBlock doubleIndirect;
            if (driver.readBlock(reinterpret_cast<char*>(&doubleIndirect), childInode.doubleIndirect) != 0) return 1; // failed to read double indirect block
            for (int i = 0; i < 256; i++) {
                if (freeSingleIndirect(doubleIndirect.blockPointers[i]) != 0) return 1; // failed to free blocks through single indirect
            }
            if (driver.freeBlock(childInode.doubleIndirect) != 0) return 1; // failed to free double indirect block
        }
        inode parentInode;
        if (driver.getInode(fetchedInodes.parentInodeNum, reinterpret_cast<char*>(&parentInode)) != 0) return 1; // failed to read inode
        dirBlock parentDir;
        if (driver.readBlock(reinterpret_cast<char*>(&parentDir), parentInode.direct[0]) != 0) return 1; // failed to read block
        for (dirEntry& entry : parentDir.entries) {
            if (entry.inode == fetchedInodes.inodeNum) {
                entry.inode = 0;
                if (driver.freeInode(fetchedInodes.inodeNum) != 0) return 1; // failed to free inode of dir
                if (driver.updateBlock(reinterpret_cast<char*>(&parentDir), parentInode.direct[0]) != 0) return 1; // failed to update block of parent dir
                return 0;
            }
        }
        return 1; // could not find inode in parent dir

    }

    /**
     * Returns the open file's size or -1 if no file is open
     */
    size_t getOpenFileSize() {
        if (fileOpen)
            return openFileInode.size;
        else return -1;
    }
};

int main() {

    FileSystem fs;
    string path;
    // test mkdir
    cout << "Testing mkdir:\n";
    path = "/home123";
    cout << fs.mkdir(path) << '\n';
    path = "/home123";
    cout << fs.mkdir(path) << '\n';
    path = "fdsgsf";
    cout << fs.mkdir(path) << '\n';
    path = "/home123/folder";
    cout << fs.mkdir(path) << "\n\n";
    // test opening and writing a file
    cout << "Testing open:\n";
    path = "/home123/folder/myfile1";
    cout << fs.open(path) << '\n';
    // series of small writes
    cout << "Testing write:\n";
    for (int i = 0; i < 10; i++) {
        cout << fs.write("hello darkness my old friend\n", 29) << '\n';
    }
    // series of bigger writes spanning multiple blocks and reaching single indirect and double indirect
    for (int i = 0; i < 500; i++) {
        cout << fs.write("goodbye lights my new friend\ngoodbye lights my new friend\ngoodbye lights my new friend\ngoodbye lights my new friend\ngoodbye lights my new friend\ngoodbye lights my new friend\ngoodbye lights my new friend\ngoodbye lights my new friend\ngoodbye lights my new friend\ngoodbye lights my new friend\n", 290) << ' ';
        cout << fs.write("hello darkness my old friend\nhello darkness my old friend\nhello darkness my old friend\nhello darkness my old friend\nhello darkness my old friend\nhello darkness my old friend\nhello darkness my old friend\nhello darkness my old friend\nhello darkness my old friend\nhello darkness my old friend\n", 290) << ' ';
    }
    // test getOpenFileSize
    cout << "\nTesting getOpenFileSize:\n";
    cout << fs.getOpenFileSize() << "\n\n";
    // test read
    cout << "Testing read:\n";
    char buffer[fs.getOpenFileSize()];
    cout << fs.read(buffer, fs.getOpenFileSize()) << "\n\n";
    fs.close();
    fstream file;
    file.open("myfile1-from-vdisk", ios::out | ios::binary | ios::trunc);
    file.write(buffer, fs.getOpenFileSize());
    file.close();
    // test remove
    cout << "Testing remove:\n";
    path = "/home123/folder/myfile1";
    cout << fs.remove(path) << "\n\n";
    // test rmdir
    cout << "Testing rmdir:\n";
    path = "/home123";
    cout << fs.rmdir(path) << '\n';
    path = "fdsgsf";
    cout << fs.rmdir(path) << '\n';
    path = "/home123/folder";
    cout << fs.rmdir(path) << '\n';
    path = "/home123";
    cout << fs.rmdir(path) << '\n';
    path = "/home123";
    cout << fs.rmdir(path) << "\n\n";

    return 0;
}