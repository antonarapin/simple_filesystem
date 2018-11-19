/* hollyfs.c */

/* This program impelements a rudimentary filesystem on the /dev/sdaX partition given
It uses open / read / write / close on this filesystem, so it should be a RAW device file
so that this program talks directly to a device driver and can read/write bytes directly.

The given partiton should not be mounted!

The program implements the filesystem to do the following:
open / read / write / close files on the partition using the interface provided in this program
Store files in a single, root directory structure (non-recursive)
*/

#include "hollyfs.h"
#include <stdio.h> // Provides printf
#include <stdlib.h> // provides malloc
#include <fcntl.h> // provides open system call
#include <unistd.h> // provides write and lseek and close 


// static / global becuase it's used in all methods and it's a pain to pass around
static int fd;

// Reads the entire contents of one block and write it into region pointed to by void *data
// This is a utility method that I wrote before I realized that I don't need it!
int read_from_block(int block_num, void *data, int data_size)
{
	off_t s_out = lseek(fd, block_num * HOLLYFS_BLOCK_SIZE, SEEK_SET);
	if(s_out == -1)
		return -1;
	return read(fd, data, data_size);
}

// similar to read_from_block, this method writes the data pointed to by void *data
// into the disk at block index number block_num
// This is used to write the superblock, and the root folder inode in main
int write_to_block(int block_num, void *data, int data_size)
{
	// lseek seeks to a specific byte but we are given a block num so we convert
	// the correct byte = the block size * the block number
	// seek_set means that the byte number we give is treated as an absolute number
	// instead of a offset from the current position
	off_t s_out = lseek(fd, block_num * HOLLYFS_BLOCK_SIZE, SEEK_SET);
	if(s_out == -1)
		return -1;
	// Actually write the data and return any error code in one line
	return write(fd, data, data_size);
}



// The main program writes the superblock to block 0 and the root folder i-node to i-node 0 
// by default the entire FS is empty except for the root folder.
int main(char *argv[])
{

	// open disk
	fd = open("/dev/sda3", O_RDWR);
	printf("fd: %d\n", fd);


	// Write superblock to disk, superblock is at block 0
	hollyfs_superblock *sb = malloc(sizeof(hollyfs_superblock));
	printf("Generating new superblock\n");
	// Fill in the data for the superblock
	sb->magic_num = HOLLYFS_MAGIC_NUM;
	sb->fs_size = 1056; // 1 superblock, 32 inodes blocks at the end (starting at block number 1024), 1023 data blocks
	sb->inode_count = 1; // will use 1 i-node immediately for root folder (below)
	// block map already insantiated
	sb->block_map[0] = 1;

	// I will use an entire block for the superblock struct, but most of the block space is wasted
	// because hollyfs_superblock is only a few ints and a bitmap
	write_to_block(0, sb, sizeof(hollyfs_superblock));

	// Done with the superblock, this isn't strictly necessary since the program is so short
	free(sb);

	/*
	* Here is a rough outline of the hollyfs partition
	---------------------------------------------------
	|sb|      1024 data blocks       | 32 inode blocks|
	---------------------------------------------------
	*/

	// Write root folder inode to disk (in first inode, and using first data block for storage
	printf("Writing new root folder inode\n");
	hollyfs_inode *root = malloc(sizeof(hollyfs_inode));

	root->inode_num = 0;
	root->data_block_num = HOLLYFS_DATA_BLOCK_BASE;
	root->file_size = 0;
	root->dir_child_count = 0; // this folder starts empty
	root->type = HOLLYFS_FILE_TYPE_DIR;

	// Copy this struct to disk, using INODE_BLOCK_BASE because that is the index of the first i-node
	write_to_block(HOLLYFS_INODE_BLOCK_BASE, root, sizeof(hollyfs_inode));
	free(root);


	int res;
	res = close(fd); // close the disk "file"
	if(res == -1)
		printf("Error closing!\n");
	
	printf("Done!\n");
	return 0;
}
