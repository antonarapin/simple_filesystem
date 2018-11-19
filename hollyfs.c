/* hollyfs.c */

// Implements a simple filesystem that uses the swap partition

#include "hollyfs.h"
#include <linux/module.h> 
#include <linux/fs.h> 	
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>



// initializing the cache that will contain the newly created inodes
struct kmem_cache *hollyfs_inode_cache = NULL;

// this function is designed to read through the contents (files) of a directory 
static int hollyfs_iterate(struct file *filp, struct dir_context *ctx)
{
	int i;
	struct inode *inode;
	hollyfs_inode *hfs_inode;
	struct super_block *sb;
	struct buffer_head *bh;
	struct hollyfs_directory_record *cur_rec;

	if(ctx->pos)
	{
		return 0;
	}

	// here we are retrieving the inode that corresponds to the passed in file object
	inode = filp->f_inode;
	// super block is retrieved from the pointer to the super block that the inode of the passed in file possesses, since this is the only super block for a given partition 
	sb = inode->i_sb;
	// when the inode is created, its i_private member is set to its reference in the inode cache, thus in order to acquire the pointer to the cache entry for the given inode, we can just retrieve the correctly casted i_private property of the inode
	hfs_inode = (hollyfs_inode *)inode->i_private;
	// when the inode is created, its cache entry contains the type of the inode, which corresponds to either a directory or a file
	// since in this method we are iterating through the contents of a directory, if the inode is referring to an object of a type other that HOLLYFS_FILE_TYPE_DIR, it is not a directory
	// thus we need to print a corresponding message to the log and return an error code
	if(!(hfs_inode->type == HOLLYFS_FILE_TYPE_DIR))
	{
		printk("Not a directory!\n");
		return -ENOTDIR;
	}
	// when the inode is created, the chache entry for that inode, which is referred to by hfs_inode in this case, has the position of the data block for that particular inode on the disk stored in the data_block_num property
	bh = sb_bread(sb, hfs_inode->data_block_num); 
	// here we are reading the data stored in the data block that corresponds to the given inode
	// the data in the retrieved data block contains the contents of the given directory inode, as is set up and edited every time a new inode in a given directory is created
	cur_rec = (struct hollyfs_directory_record *)bh->b_data;
	// here we are iterating over all of the children of a given directory, the number of which is stored in the dir_child_count property of hfs_inode
	for(i = 0; i < hfs_inode->dir_child_count; i++)
	{
		// here the dir_emit function call is used to fill the ctx at a current position with the data of a given size, namely the filename of the current child and its inode number
		dir_emit(ctx, cur_rec->filename, HOLLYFS_FILENAME_MAX, cur_rec->inode_no, DT_UNKNOWN);
		// the ctx position pointer is incremented to move the pointer by the size of the child file description size hollyfs_directory_record, moving to the spot that is next to be filled with the data from the next child
		ctx->pos += sizeof(hollyfs_directory_record);
		// increment the index of the data on the disk that points to the directory contents, which essentially moves the pointer to the next child's data in the current directory's context data block
		cur_rec++; // pointer arithmetic
	}
	// release the buffer head pointer so that there is no more reference to it and it will not get corruped, since we do not need the reference to the current directory's content's data block any more
	brelse(bh);
	// at this point everything was executed 
	return 0;

}

// this struct assigns the special operations for the inodes that are directories, such as the root directory inode that is created in the 
const struct file_operations hollyfs_dir_ops = {
	.iterate = hollyfs_iterate, // whenever the call to iterate operation is attempted, the hollyfs_iterate function call is triggered, imposing the custom way of iteration over inodes
	.owner = THIS_MODULE, // setting the owner pointer to the current module so that the module is not unloaded while it is in use

};

// function declarations, the implementation of which follows below
static int hollyfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool x);
struct dentry *hollyfs_lookup(struct inode *parent, struct dentry *child, unsigned int flags);
static int hollyfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);

// this struct defines the operations on the hollyfs onodes
static const struct inode_operations hollyfs_inode_ops = {
	.create = hollyfs_create, // operation that is executed when attempting to create inode
	.lookup = hollyfs_lookup, // operation that is executed when the lookup is attempted
	.mkdir = hollyfs_mkdir, // operation that is executed when attempting to create a directory in hollyfs_type file system
};

// this function implements the code for the creation of a new inode at a given directory
static int hollyfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool x)
{
	struct super_block *sb; // this is the pointer to the super block for holly file system partition
	hollyfs_superblock *sb_ondisk; // this is the pointer to the super block data stored on disk
  	hollyfs_inode *hfs_inode; // pointer to the new inode's reference in the cache 
	hollyfs_inode *parent_dir_inode; // pointer to the inode of the parent directory, as we need the information about the directory in which we are creating a new inode
	hollyfs_inode *parent_dir_inode_ondisk; // pointer to the data for the parent directory inode stored on disk
	hollyfs_directory_record *dir_contents_datablock; // pointer to the data on the disk that is referred to by the directory inode
	// setting up the required variables and pointers that we need to create a new inode
	struct inode *inode;
	struct buffer_head *bh;
	uint64_t count; // variable to store the number of inodes currently present in our file system
	int i; // counter that will be used to iterate through the data block map to find a free data block to refer our new inode to

	// retieving the super block pointer from the super block that was assigned to the current directory inode
	sb = dir->i_sb;
	inode = new_inode(sb); // new inode yay
	inode->i_sb = sb; // since the new inode is in the same directory, it is obviously in the same partition wit hthe same file system, so it has the same super block
	inode->i_op = &hollyfs_inode_ops; // point the operations to the hollyfs_inode_ops, so when operations are executed on the newly created inode, the definitiona from hollyfs_inode_ops struct are used
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode); // since the inode was just created, set up its access, modification and change time to the current time
	sb_ondisk = (hollyfs_superblock *)sb->s_fs_info; // getting the pointer to the super block data on disk that is pointed to by the s_fs_info member of the super block struct
	count = (uint64_t)(sb_ondisk->inode_count); // get the current number of inodes from the super block data that is stored on the disk
	// prit out the log about the count of the inode that we are currently creating
	printk("There are %llu inodes, this will be inode number %llu!\n", count, count+1);
	count++; // increment the counter of the number of inodes, since we have just created a new one
	inode->i_ino = count; // the id number of the newly created inode is its number in the list of inodes that is kept track of by the super block
	sb_ondisk->inode_count = count; // update the super block data on the disk to account for the new inode, increasing the number of inodes counting variable of the super block data
	// here we are allocating a spot in the hollyfs_inode_cache to store the newly created node's reference in the cache
	hfs_inode = kmem_cache_alloc(hollyfs_inode_cache, GFP_KERNEL);
	inode->i_private = hfs_inode; // we are storing the reference to the corresponding spot in the cache in the newly created inode's i_private variable
	hfs_inode->inode_num = count; // we set the count number, which is the id of our new inode, to the property of the cached inode, so that there is a reference to what inode in the memory this particulat cached inode refers to
	hfs_inode->type = HOLLYFS_FILE_TYPE_FILE; // setting the file type to correspond to file rather than directory
	// here we print out the name of the file and its corresponding inode number, this is the inode that we just created
	printk("Creating new file!  name: %s  inode: %d\n", dentry->d_name.name, hfs_inode->inode_num);
	hfs_inode->file_size = 1; // setting up the file size in the cached inode reference to 1

	// here we are using the previously initialized variable i to cycle through the block map stored in the super block's data
	// since the values in the block map correspond to the availability of the data blocks, we stop the loop once we find an unoccupied data block
	// we mark the adta block reference in the block map as occupied, since that is where we are going to store the file data
	for(i = 0; i < HOLLYFS_DATA_BLOCK_COUNT; i++){
		if(sb_ondisk->block_map[i] == 0)
		{
			sb_ondisk->block_map[i] = 1;
			break;
		}
	}
	// here we need to add the HOLLYFS_DATA_BLOCK_BASE to the variable i, since the piece fo memory that is devoted to blocks does not start at the very first memory location
	// but rather it starts at the location HOLLYFS_DATA_BLOCK_BASE, from which we need to go i more locations in order to reach the unoccupied data block that we previously found
	hfs_inode->data_block_num = i + HOLLYFS_DATA_BLOCK_BASE;
	// in the fill_sb function we stored the reference to the inode data on the disk into the i_private member of the hollyfs_inode struct, so directories have the reference to their inode data in i_private
	parent_dir_inode = (hollyfs_inode *)dir->i_private;
	parent_dir_inode->dir_child_count++; // we change the parent directory's inode data on disk, namely we increment the number of child files, since we have just created an inode for the new file in this directory
	bh = sb_bread(sb, parent_dir_inode->data_block_num); // here we are retrieving the pointer to the data block that belongs to the current directory's contents to which we are adding a new file
	dir_contents_datablock = (hollyfs_directory_record *)bh->b_data; // here weare retrieving the current directory's content's data
	// careful not overright existing records
	dir_contents_datablock+=(parent_dir_inode->dir_child_count - 1); // here we moving the location to which we need to write the new file's data by the number that is parent_dir_inode->dir_child_count - 1 away from the current directory content's location, 
	//since we are adding the parent_dir_inode->dir_child_count - 1 th child's information relative to the first child's information that is already in the directory's content
	// also, we need to copy the name of the file to the 
	strcpy(dir_contents_datablock->filename, dentry->d_name.name); // we need to store new child's name, which we copy from the passed in denty as a string
	dir_contents_datablock->inode_no = hfs_inode->inode_num; // also, we need to store new child's number, as it is referenced in the cache

	// here we are marking current buffer head dirty, in order to update the data on the disk
	// this is required because at this point the contents of the currenty directory on the disk are not the same as the one's we have just updated, thus since there is a data discrepancy between what we have edited and what is on disk, the data block that stores the current directory's contents is dirty
	mark_buffer_dirty(bh);
	// sync_dirty_buffer function essentially synchronizes the current data in bh with the data on the disk, so essentially we are writing the information that we added to the contents of the current directory to the corresponding data block on disk, pointed by bh
	sync_dirty_buffer(bh);
	// release the buffer head pointer so that there is no more reference to it and it will not get corruped, since we do not need the reference to the current directory's content's data block on disk
	brelse(bh);
	// here we are retrieving the reference to the inode of the current directory on disk, which is at the spot parent_dir_inode->inode_num from the point where the memory devoted to inodes starts, so from HOLLYFS_INODE_BLOCK_BASE
	bh = sb_bread(sb, parent_dir_inode->inode_num + HOLLYFS_INODE_BLOCK_BASE);
	parent_dir_inode_ondisk = (hollyfs_inode *)bh->b_data; // here we are retrieving the data that correspond's to current directory's inode from the disk
	parent_dir_inode_ondisk->dir_child_count = parent_dir_inode->dir_child_count; // now we are updating the number of children that current directory's inode keeps track of, since we have just added a new file to the directory and to the memory, thus we have to update the directory's inode's information 

	mark_buffer_dirty(bh); // similar to the directory's content's data block, the inode data on the disk was just updated, thus it no longer corresponds to what is right not on the disk at that particular memory location, so the memory spot is dirty
	sync_dirty_buffer(bh); // here we are synchronizing the data, so we are writing the newly updated inode data to the corresponding point on the disk
	brelse(bh); // once again, we are releasing the reference to the current directory's inode on the dist from the variable bh, so that we do not mess it up accidentaly later on and so that we have finished working with it
	// we set the current directory's inode to be the owner of the newly created file's inode, so the hierarchy of files and folders is preserved
	inode_init_owner(inode, dir, mode);
	d_add(dentry, inode); // now we just need to insert the newly created inode into the dentry so that the .lookup operation can look up the new node in the parent directory
	// returning zero to signify the successful completion of this function
	return 0;
}

// this function is executed upon the lookup operation to retrieve the dentry of the parent directory in order to look up a file
struct dentry *hollyfs_lookup(struct inode *parent, struct dentry *child, unsigned int flags)
{
	// here we just simulate functionality, so we simply print out a look up message to the log and return null
	printk("HollyFS lookup called!\n");
	return NULL;
};

// this function is called when the attempt to create a directory for an inode is made, its is referenced by as a custom .mkdir operation for hollyfs_inode_ops
static int hollyfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	// just printing a message to the log, signifying that the operation was called
	printk("Creating directory!\n");
	return 0;
};

// the function that is being called when .destroy_inode operation of our custom super block is executed
static void hollyfs_destroy_inode(struct inode *inode)
{
	// simply prints out the destry message to the log
	printk("Holly FS destroy inode called!\n");
}

// implementation of the .put_super operation for the super block of our custom file system
static void hollyfs_put_super(struct super_block *sb)
{
	// here the pointer to the super block on disk is acquired for no practical reason
	hollyfs_superblock *sb_ondisk;
	// the s_fs_info pointer of the super block contains our custom super block on the disk
	sb_ondisk = (hollyfs_superblock *)sb->s_fs_info;
	// the log is printed out when this method is run
	printk("Holly FS put super called!\n");
}

// struct that defines custom operations for interactions with the super block
static const struct super_operations hollyfs_super_ops = {
	.destroy_inode = hollyfs_destroy_inode, // operation that is being executed when the inode is destroyed, implemented in hollyfs_destroy_inode function
	.put_super = hollyfs_put_super, // operation that is being called before the freeing of the super block 
};


// this function checks the frmat of the file system on the partition using magic number and fills the file system's super block with the
// appropriate operation references and parameters, creating and adding a new inode for the root file directory
static int hollyfs_fill_sb(struct super_block *sb, void *data, int silent)
{
	struct buffer_head *bh;
	struct hollyfs_superblock *sb_ondisk;
	struct inode *root_inode = NULL;
	hollyfs_inode *root_inode_ondisk;

	// initialize a buffer head with a given super block, starting at block number 0 of the super block's device, since super block is the very first block
	bh = sb_bread(sb, 0);
	// here we are reading the data that is stored on the dist at the location pointed to by the buffer head
	// since bufer head points to the very first block on the partition, it points to the super block
	// thus sb_ondisk will point to the superblock on disk
	sb_ondisk = (struct hollyfs_superblock *)bh->b_data;

	// check whether the magic number of the superblock on the disk coincides with the custom file system's magic number
	// if the numbers are different, the file system is of a wrong type, super block is not properly set up, or super block is corrupted
	if(sb_ondisk->magic_num != HOLLYFS_MAGIC_NUM)
	{
		printk("Incorrect Magic Number!\n");
		brelse(bh);
		return 1;
	}

	// set the appropriate properties for the super block
	// here we are setting the limit to the size of the super block, making it of the standard block size defined for our custom file system
	sb->s_maxbytes = HOLLYFS_BLOCK_SIZE;
	// setting the super operations of the super block to the custom struct of operations defined in hollyfs_super_ops
	sb->s_op = &hollyfs_super_ops;
	// set the current super block specific file system info to the super block located on disk
	sb->s_fs_info = sb_ondisk;

	// creating a new inode in the peartition to which current super block is attached
	// this new inode is the root file directory inode
	root_inode = new_inode(sb);
	// set the number of the inode (its id, index in the array of inodes) to the HOLLYFS_INODE_BLOCK_BASE, which points to the place 
	// where the list of inodes starts o nthe partition, since root inode is our first inode, that is in the very beginning of the inode list
	root_inode->i_ino = HOLLYFS_INODE_BLOCK_BASE;
	// here we are setting up the permission bits for the newly created root inode, checking if it is a directory, in which case S_IFDIR would have a non-zero value
	// furthermore, we are passing NULL as root inode in not contained in any other directory, so all directories on the partition are its children
	inode_init_owner(root_inode, NULL, S_IFDIR|0777);
	// set the root node's super block pointer to the current super block
	root_inode->i_sb = sb;
	// make operations of the root inode refer to the operations listed in hollyfs_inode_ops struct
	root_inode->i_op = &hollyfs_inode_ops; 
	// set the root inode file operations to the ones that are defined in the holly_dir_ops struct
	root_inode->i_fop = &hollyfs_dir_ops; 
	// setting the last access time, modification time and change time to the current time, as this is the time when the root indoe was created
	root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = CURRENT_TIME;
	// point buffer header to the region at HOLLYFS_INODE_BLOCK_BASE in the partition on which current super block is stored
	// in other words point it to the location of the root inode
	bh = sb_bread(sb, HOLLYFS_INODE_BLOCK_BASE);
	// get the data that is stored on disk where the root inode is supposed to be stores
	root_inode_ondisk = (hollyfs_inode *)bh->b_data;
	// set the private info of the root inode to the data that is stored on the disk, thus linking the newly created root inode to its appropriate location on the disk
	root_inode->i_private = root_inode_ondisk;
	// here we are linking the root inode pointer of the super block to the newly created root inode
	sb->s_root = d_make_root(root_inode);
	// if there is a NULL or something inappropriate in the super block's root inode pointer, it means that the root inode was not created properly
	if(!sb->s_root)
	{
		printk("hollyfs failed creating root directory\n");
		return -ENOMEM;
	}

	// if everything works, we can finish up filling the super block, which included the creation of the root inode, 
	// release the buffer head pointer so that there is no more reference to it and it will not get corruped, since we do not need it anymore, and return
	printk("Finished reading / building root folder inode!\n");
	brelse(bh);
	return 0;
	
}

// mounts the device with a given file system on it, returning the dentry of the root, that has a reference to the root inode
static struct dentry *hollyfs_mount(struct file_system_type *type, int flags, char const *dev, void *data)
{
	
	// mounting the file system that resides on a block device, which is /dev/sda3 in our case
	// mount_bdev returns a root catalogue entry that is created by the file system
	// also the given type (hollyfs_type), flags and specific device are passed in, along with the 
	// function hollyfs_fill_sb that fills the superblock of newly created partition that hosts our filesystem with appropriate parameters
	struct dentry *entry = mount_bdev(type, flags, dev, data, hollyfs_fill_sb);

	// if the returned dentry is an error code, then the mounting operation with mount_bdev failed, 
	// otherwise, it was successful and the acquired root dentry can be returned 
	if(IS_ERR(entry))
	{
		printk("hollyfs failed mounting!\n");
	}
	else
	{
		printk("hollyfs mounted!\n");
	}
	return entry;
}


// struct that defines a file system type hollyfs_type, which rests the appropriate properties
static struct file_system_type hollyfs_type = {
	.owner = THIS_MODULE, // setting the owner pointer to the current module so that the module is not unloaded while it is in use
	.name = "hollyfs", // name of the file system
	.mount = hollyfs_mount, // the operation that is called when the mount operation is performed
	.kill_sb = kill_block_super, // setup the cleanup method for the file system
	.fs_flags = FS_REQUIRES_DEV, // FS_REQUIRES_DEV flag signifies that file system of the given type is not a virtual file system, thus requires an actual device, such as /dev/sda3
};


// initializes the module by creating the cache for indes of the file system and registering the file system
static int __init init_hollyfs(void)
{
	// Printing the initialization message
	int ret;
	printk("Loaded hollyfs module.\n");

	// creating a cache to store the hollyfs_inode objects
	hollyfs_inode_cache = kmem_cache_create("hollyfs_inode_cache", 
							sizeof(struct hollyfs_inode), 
							0, 
							(SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD), 
							NULL);
	
	// recording the result returned by register_filesystem function call in order to check 
	// whether the filesystem registration procedure was successfull, in which case printing the success message
	// we pass in the holly_fs struct reference to register a filesystem on the type defined in hollyfs_type struct
	ret = register_filesystem(&hollyfs_type);
	if(ret == 0)
		printk("Registered hollyfs filesystem\n");

	
	// the initialization of the holy_fs file system was successful if it was registered successfully, 
	// thus the return value is equal to what was returned by register_filesystem function call
	return ret;
}

// the implementation of the exit function for the module
static void __exit exit_hollyfs(void)
{

	int ret; // initialized to record the return value from the call below
	// here we are attempting to unregister the previously registered file system of the type hollyfs_type, also this function call calls the proper destruction operation functions for super block and inodes
	ret = unregister_filesystem(&hollyfs_type); 
	// if the file system is unregistered successfully, the return code stored in ret variable should be equal to 0
	if(ret == 0)
		printk("Unregistered hollyfs filesystem\n");
	// just printing out the message signifying the removal of the hollyfs module, since all its functionality was in the registration of the custom file system that was just unregistered
	printk("Removed hollyfs module.\n");
}

// here we are setting up the functions that will be called when the module is initialized and exited
module_init(init_hollyfs);
module_exit(exit_hollyfs);

// here we are setting up some description properties for our custom file system module
MODULE_LICENSE("NONE");
MODULE_AUTHOR("Your name!");
MODULE_DESCRIPTION("Implements a simple filesystem.");

