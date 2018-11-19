/* hollyfs.h */

// Contains mostly the structs used in hollyfs.c which is a rudimentary kern-space file system



const unsigned int HOLLYFS_MAGIC_NUM = 77;
const unsigned int HOLLYFS_BLOCK_SIZE = 4096;
#define HOLLYFS_DATA_BLOCK_COUNT 1023
const unsigned int HOLLYFS_DATA_BLOCK_BASE = 1;
const unsigned int HOLLYFS_INODE_BLOCK_BASE = 1024;
const unsigned int HOLLYFS_FILE_TYPE_DIR = 1;
const unsigned int HOLLYFS_FILE_TYPE_FILE = 2;
#define HOLLYFS_FILENAME_MAX 255


// This is stored in the first 4096B block 
struct hollyfs_superblock {
	unsigned int magic_num;
	unsigned int fs_size; // blocks
	unsigned int inode_count;
	unsigned short block_map[HOLLYFS_DATA_BLOCK_COUNT];  // inefficient but there is no boolean in C for the kernel
};
typedef struct hollyfs_superblock hollyfs_superblock;

struct hollyfs_inode {
	unsigned int inode_num;
	unsigned int data_block_num; //limitation right now, only one block / file
	unsigned int file_size;
	unsigned int dir_child_count;
	unsigned int type; // DIR or FILE
};
typedef struct hollyfs_inode hollyfs_inode;

struct hollyfs_directory_record {
	char filename[HOLLYFS_FILENAME_MAX];
	unsigned int inode_no;
};
typedef struct hollyfs_directory_record hollyfs_directory_record;

/*
struct dentry {
	char name[4];
	unsigned int num_files;
	unsigned long files[100][2];
};
typedef struct dentry dentry;
*/
