/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid, Angela Demke Brown
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2022 Angela Demke Brown
 */

/**
 * CSC369 Assignment 4 - vsfs driver implementation.
 */

#include <libgen.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "vsfs.h"
#include "fs_ctx.h"
#include "options.h"
#include "util.h"
#include "bitmap.h"
#include "map.h"

//NOTE: All path arguments are absolute paths within the vsfs file system and
// start with a '/' that corresponds to the vsfs root directory.
//
// For example, if vsfs is mounted at "/tmp/my_userid", the path to a
// file at "/tmp/my_userid/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "/tmp/my_userid/dir/" will be passed to
// FUSE callbacks as "/dir".


/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool vsfs_init(fs_ctx *fs, vsfs_opts *opts)
{
	size_t size;
	void *image;
	
	// Nothing to initialize if only printing help
	if (opts->help) {
		return true;
	}

	// Map the disk image file into memory
	image = map_file(opts->img_path, VSFS_BLOCK_SIZE, &size);
	if (image == NULL) {
		return false;
	}

	return fs_ctx_init(fs, image, size);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in vsfs_init().
 */
static void vsfs_destroy(void *ctx)
{
	fs_ctx *fs = (fs_ctx*)ctx;
	if (fs->image) {
		munmap(fs->image, fs->size);
		fs_ctx_destroy(fs);
	}
}

/** Get file system context. */
static fs_ctx *get_fs(void)
{
	return (fs_ctx*)fuse_get_context()->private_data;
}


/* Returns the inode number for the element at the end of the path
 * if it exists.  If there is any error, return -1.
 * Possible errors include:
 *   - The path is not an absolute path
 *   - An element on the path cannot be found
 */
static int path_lookup(const char *path,  vsfs_ino_t *ino) {
	if(path[0] != '/') {
		fprintf(stderr, "Not an absolute path\n");
		return -ENOSYS;
	} 

	// TODO: complete this function and any helper functions
	if (strcmp(path, "/") == 0) {
		*ino = VSFS_ROOT_INO;
		return 0;
	}

	char path_str[VSFS_NAME_MAX];
	strcpy(path_str, path);
	char *token = strtok(path_str, "/");
	fs_ctx *fs = get_fs();
	vsfs_inode *dir_inode = &(fs->itable[VSFS_ROOT_INO]);
	int curr_inum = -1;
	vsfs_blk_t block;
	vsfs_blk_t *ind_block;
	vsfs_dentry *entry;

	for(vsfs_blk_t i = 0; i < dir_inode->i_blocks; i++){
		if(i < VSFS_NUM_DIRECT){
			block = dir_inode->i_direct[i];
		}else if (i == VSFS_NUM_DIRECT){ //indirect
			ind_block = (vsfs_blk_t *) (fs->image + dir_inode->i_indirect * VSFS_BLOCK_SIZE);
			block = ind_block[i - VSFS_NUM_DIRECT];
		}else{
			block = ind_block[i - VSFS_NUM_DIRECT];
		}

		entry = (vsfs_dentry *) (fs->image + block * VSFS_BLOCK_SIZE);
		for(int j = 0; j < (int) (VSFS_BLOCK_SIZE / sizeof(vsfs_dentry)); j++){
			if(entry[j].ino != VSFS_INO_MAX && strcmp(entry[j].name, token) == 0){
				 curr_inum = entry[j].ino;
			}
		}
	}

	if(curr_inum == -1){
		return -ENOENT;
	}
	dir_inode = &(fs->itable[curr_inum]);
	*ino = (vsfs_ino_t) curr_inum;
	
	return curr_inum;
}

/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * Errors: none
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int vsfs_statfs(const char *path, struct statvfs *st)
{
	(void)path;// unused
	fs_ctx *fs = get_fs();
	vsfs_superblock *sb = fs->sb; /* Get ptr to superblock from context */
	
	memset(st, 0, sizeof(*st));
	st->f_bsize   = VSFS_BLOCK_SIZE;   /* Filesystem block size */
	st->f_frsize  = VSFS_BLOCK_SIZE;   /* Fragment size */
	// The rest of required fields are filled based on the information 
	// stored in the superblock.
        st->f_blocks = sb->num_blocks;     /* Size of fs in f_frsize units */
        st->f_bfree  = sb->free_blocks;    /* Number of free blocks */
        st->f_bavail = sb->free_blocks;    /* Free blocks for unpriv users */
	st->f_files  = sb->num_inodes;     /* Number of inodes */
        st->f_ffree  = sb->free_inodes;    /* Number of free inodes */
        st->f_favail = sb->free_inodes;    /* Free inodes for unpriv users */

	st->f_namemax = VSFS_NAME_MAX;     /* Maximum filename length */

	return 0;
}

/**
 * Get file or directory attributes.
 *
 * Implements the lstat() system call. See "man 2 lstat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors);
 *       it should include any metadata blocks that are allocated to the 
 *       inode (for vsfs, that is the indirect block). 
 *
 * NOTE2: the st_mode field must be set correctly for files and directories.
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int vsfs_getattr(const char *path, struct stat *st)
{
	if (strlen(path) >= VSFS_PATH_MAX) return -ENAMETOOLONG;
	fs_ctx *fs = get_fs();

	memset(st, 0, sizeof(*st));
	//TODO: lookup the inode for given path and, if it exists, fill in the
	// required fields based on the information stored in the inode
	vsfs_inode *inode;
	vsfs_ino_t inum;
	int ret = path_lookup(path, &inum);
	if(ret == -ENOENT){
		return ret;
	}
	if(ret == -ENOTDIR){
		return ret;
	}
	inode = (vsfs_inode *) &(fs->itable[inum]);
	st->st_blocks = inode->i_blocks;
	st->st_mode = inode->i_mode;
	st->st_nlink = inode->i_nlink;
	st->st_size = inode->i_size;
	st->st_mtim = inode->i_mtime;
	
	return 0;

}


/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler(buf, name, NULL, 0)
 * for each directory entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int vsfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
	//TODO: lookup the directory inode for given path and iterate through its
	// directory entries
	(void)offset;// unused
    (void)fi;// unused
	fs_ctx *fs = get_fs();


	vsfs_inode *dir_inode;
	vsfs_ino_t inum;
	path_lookup(path, &inum);
	dir_inode = &(fs->itable[inum]);

	vsfs_blk_t block;
	vsfs_blk_t *ind_block;
	vsfs_dentry *entry;

	for(vsfs_blk_t i = 0; i < dir_inode->i_blocks; i++){
		if(i < VSFS_NUM_DIRECT){
			block = dir_inode->i_direct[i];
		}else if (i == VSFS_NUM_DIRECT){ //indirect
			ind_block = (vsfs_blk_t *) (fs->image + dir_inode->i_indirect * VSFS_BLOCK_SIZE);
			block = ind_block[i - VSFS_NUM_DIRECT];
		}else{
			block = ind_block[i - VSFS_NUM_DIRECT];
		}

		entry = (vsfs_dentry *) (fs->image + block * VSFS_BLOCK_SIZE);
		for(int j = 0; j < (int) (VSFS_BLOCK_SIZE / sizeof(vsfs_dentry)); j++){
			if(entry[j].ino != VSFS_INO_MAX){
				if(filler(buf, entry[j].name, NULL, 0) != 0){
					return -ENOMEM;
				}
			}
		}
	}
	return 0;
	
}


/**
 * Create a directory.
 *
 * Implements the mkdir() system call.
 *
 * You do NOT need to implement this function. 
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the directory to create.
 * @param mode  file mode bits.
 * @return      0 on success; -errno on error.
 */
static int vsfs_mkdir(const char *path, mode_t mode)
{
	mode = mode | S_IFDIR;
	fs_ctx *fs = get_fs();

	//OMIT: create a directory at given path with given mode
	(void)path;
	(void)mode;
	(void)fs;
	return -ENOSYS;
}

/**
 * Remove a directory.
 *
 * Implements the rmdir() system call.
 *
 * You do NOT need to implement this function. 
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOTEMPTY  the directory is not empty.
 *
 * @param path  path to the directory to remove.
 * @return      0 on success; -errno on error.
 */
static int vsfs_rmdir(const char *path)
{
	fs_ctx *fs = get_fs();

	//OMIT: remove the directory at given path (only if it's empty)
	(void)path;
	(void)fs;
	return -ENOSYS;
}

/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int vsfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)fi;// unused
	assert(S_ISREG(mode));
	fs_ctx *fs = get_fs();
	vsfs_superblock *sb = fs->sb;

	//TODO: create a file at given path with given mode
	char path_str[VSFS_PATH_MAX];
	char path_strr[VSFS_PATH_MAX];
	char directory[VSFS_NAME_MAX];
	char file_name[VSFS_NAME_MAX];
	vsfs_ino_t dir_inum;
	vsfs_inode *dir_inode;

	strcpy(path_str, path);
	strcpy(path_strr, path);
	strcpy(file_name, basename(path_strr));

	//get dir info
	strcpy(directory, dirname(path_str));
	path_lookup(directory, &dir_inum);
	dir_inode = &(fs->itable[dir_inum]);

	//allocate new inode
	vsfs_ino_t inum;
	vsfs_inode *file_inode;
	if(bitmap_alloc(fs->ibmap, sb->num_inodes, &inum) == -1){
		return -ENOSPC;
	}
	file_inode = &(fs->itable[inum]);
	file_inode->i_blocks = 0;
	file_inode->i_mode = mode;
	file_inode->i_nlink = 1;
	file_inode->i_size = 0;
	clock_gettime(CLOCK_REALTIME, &(file_inode->i_mtime));
	sb->free_inodes--;

	//allocate at current block if can
	bool need_block = true;
	vsfs_blk_t block;
	vsfs_blk_t *ind_block;
	vsfs_dentry *entry;

	for(vsfs_blk_t i = 0; i < dir_inode->i_blocks; i++){
		if(i < VSFS_NUM_DIRECT){
			block = dir_inode->i_direct[i];
		}else if (i == VSFS_NUM_DIRECT){ //indirect
			ind_block = (vsfs_blk_t *) (fs->image + dir_inode->i_indirect * VSFS_BLOCK_SIZE);
			block = ind_block[i - VSFS_NUM_DIRECT];
		}else{
			block = ind_block[i - VSFS_NUM_DIRECT];
		}

		entry = (vsfs_dentry *) (fs->image + block * VSFS_BLOCK_SIZE);
		for(int j = 0; j < (int) (VSFS_BLOCK_SIZE / sizeof(vsfs_dentry)); j++){
			if(entry[j].ino == VSFS_INO_MAX){
				entry[j].ino = inum;
				strcpy(entry[j].name, file_name);
				need_block = false;
				break;
			}
		}
		if(!need_block){
			break;
		}
	}

	//allocate at new block
	vsfs_blk_t new_block;
	vsfs_blk_t *new_ind_block;
	vsfs_dentry *new_entry;
	if(need_block){
		if(bitmap_alloc(fs->dbmap, (fs->size / VSFS_BLOCK_SIZE), &new_block) == -1){
			return -ENOSPC;
		}
		sb->free_blocks--;

		//load the entry
		new_entry = (vsfs_dentry *) (fs->image + new_block * VSFS_BLOCK_SIZE);
		new_entry[0].ino = inum;
		strcpy(entry[0].name, file_name);
		for(int h = 1; h < (int) (VSFS_BLOCK_SIZE / sizeof(vsfs_dentry)); h++){
			new_entry[h].ino = VSFS_INO_MAX;
		}
		
		//add as a direct block
		if(dir_inode->i_blocks < VSFS_NUM_DIRECT){ 
			dir_inode->i_direct[dir_inode->i_blocks-1] = new_block;
		}else{ //add as an indirect block
			vsfs_blk_t *ind_addr = (vsfs_blk_t *) (fs->image + dir_inode->i_indirect * VSFS_BLOCK_SIZE);
			new_ind_block = &ind_addr[dir_inode->i_blocks - VSFS_NUM_DIRECT];
			new_ind_block[0] = new_block;
		}

		//modify dir inode info
		dir_inode->i_blocks++;\
		dir_inode->i_size += VSFS_BLOCK_SIZE;
		clock_gettime(CLOCK_REALTIME, &(dir_inode->i_mtime));
	}
	return 0;

}

/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int vsfs_unlink(const char *path)
{
	fs_ctx *fs = get_fs();
	vsfs_superblock *sb = fs->sb;

	//TODO: remove the file at given path
	char path_str[VSFS_PATH_MAX];
	char path_strr[VSFS_PATH_MAX];
	char directory[VSFS_NAME_MAX];
	char file_name[VSFS_NAME_MAX];
	vsfs_ino_t dir_inum;
	vsfs_ino_t file_inum;
	vsfs_inode *dir_inode;
	vsfs_inode *file_inode;

	strcpy(path_str, path);
	strcpy(path_strr, path);
	
	//get dir info
	strcpy(directory, dirname(path_str));
	path_lookup(directory, &dir_inum);
	dir_inode = &(fs->itable[dir_inum]);

	//get file info
	strcpy(file_name, basename(path_strr));
	path_lookup(file_name, &file_inum);
	file_inode = &(fs->itable[file_inum]);

	//empty the entry in directory
	vsfs_blk_t block;
	vsfs_blk_t *ind_block;
	vsfs_dentry *entry;
	for(vsfs_blk_t j = 0; j < dir_inode->i_blocks; j++){
		if(j < VSFS_NUM_DIRECT){
			block = dir_inode->i_direct[j];
		}else if (j == VSFS_NUM_DIRECT){ //indirect
			ind_block = (vsfs_blk_t *) (fs->image + dir_inode->i_indirect * VSFS_BLOCK_SIZE);
			block = ind_block[j - VSFS_NUM_DIRECT];
		}else{
			block = ind_block[j - VSFS_NUM_DIRECT];
		}

		entry = (vsfs_dentry *) (fs->image + block * VSFS_BLOCK_SIZE);
		for(int h = 0; h < (int) (VSFS_BLOCK_SIZE / sizeof(vsfs_dentry)); h++){
			if(entry[h].ino != VSFS_INO_MAX && entry[h].ino == file_inum){
				entry[h].ino = VSFS_INO_MAX;
				clock_gettime(CLOCK_REALTIME, &(dir_inode->i_mtime));
				break;
			}
		}
	}

	//empty the data blocks if necessary
	if(file_inode->i_size != 0){
		bool indirect = false;
		vsfs_blk_t block;
		vsfs_blk_t *ind_block;

		for(vsfs_blk_t i = 0; i < file_inode->i_blocks; i++){
			if(i < VSFS_NUM_DIRECT){
				block = file_inode->i_direct[i];
			}else if (i == VSFS_NUM_DIRECT){ //indirect
				ind_block = (vsfs_blk_t *) (fs->image + file_inode->i_indirect * VSFS_BLOCK_SIZE);
				indirect = true;
				block = ind_block[i - VSFS_NUM_DIRECT];
			}else{
				block = ind_block[i - VSFS_NUM_DIRECT];
			}
			bitmap_free(fs->dbmap, fs->size / VSFS_BLOCK_SIZE, block);
			sb->free_blocks++;
		}
		if(indirect){
			bitmap_free(fs->dbmap, fs->size / VSFS_BLOCK_SIZE, file_inode->i_indirect);
			sb->free_blocks++;
		}
	}

	//empty the inode
	bitmap_free(fs->ibmap, sb->num_inodes, file_inum);
	sb->free_inodes++;

	return 0;
}


/**
 * Change the modification time of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only need to implement the setting of modification time (mtime).
 *       Timestamp modifications are not recursive. 
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * Errors: none
 *
 * @param path   path to the file or directory.
 * @param times  timestamps array. See "man 2 utimensat" for details.
 * @return       0 on success; -errno on failure.
 */
static int vsfs_utimens(const char *path, const struct timespec times[2])
{
	fs_ctx *fs = get_fs();
	vsfs_inode *ino = NULL;
	
	//TODO: update the modification timestamp (mtime) in the inode for given
	// path with either the time passed as argument or the current time,
	// according to the utimensat man page
	
	// 0. Check if there is actually anything to be done.
	if (times[1].tv_nsec == UTIME_OMIT) {
		// Nothing to do.
		return 0;
	}

	// 1. TODO: Find the inode for the final component in path
	vsfs_ino_t inum;
	path_lookup(path, &inum);
	ino = &(fs->itable[inum]);
	
	// 2. Update the mtime for that inode.
	//    This code is commented out to avoid failure until you have set
	//    'ino' to point to the inode structure for the inode to update.
	if (times[1].tv_nsec == UTIME_NOW) {
		if (clock_gettime(CLOCK_REALTIME, &(ino->i_mtime)) != 0) {
			// clock_gettime should not fail, unless you give it a
			// bad pointer to a timespec.
			assert(false);
		}
	} else {
		ino->i_mtime = times[1];
	}

	return 0;
}


vsfs_blk_t find_last_block(fs_ctx *fs, vsfs_inode *inode){
	vsfs_blk_t block;
	vsfs_blk_t *ind_block;
    for(vsfs_blk_t i = 0; i < inode->i_blocks; i++){
		if(i < VSFS_NUM_DIRECT){
			block = inode->i_direct[i];
		}else if (i == VSFS_NUM_DIRECT){ //indirect
			ind_block = (vsfs_blk_t *) (fs->image + inode->i_indirect * VSFS_BLOCK_SIZE);
			block = ind_block[i - VSFS_NUM_DIRECT];
		}else{
			block = ind_block[i - VSFS_NUM_DIRECT];
		}
    }
    return block; 
}

/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, the new uninitialized range at the end must be
 * filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   EFBIG   write would exceed the maximum file size. 
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int vsfs_truncate(const char *path, off_t size)
{
	fs_ctx *fs = get_fs();

	//TODO: set new file size, possibly "zeroing out" the uninitialized range
	vsfs_ino_t inum;
	vsfs_inode *inode;
	path_lookup(path, &inum);
	inode = &(fs->itable[inum]);

	uint64_t size_before = inode->i_size;
	if(size == 0){ //make it empty
		vsfs_blk_t block;
		vsfs_blk_t *ind_block;
		for(vsfs_blk_t i = 0; i < inode->i_blocks; i++){
			if(i < VSFS_NUM_DIRECT){
				block = inode->i_direct[i];
			}else if (i == VSFS_NUM_DIRECT){ //indirect
				ind_block = (vsfs_blk_t *) (fs->image + inode->i_indirect * VSFS_BLOCK_SIZE);
				block = ind_block[i - VSFS_NUM_DIRECT];
			}else{
				block = ind_block[i - VSFS_NUM_DIRECT];
			}
			bitmap_free(fs->dbmap, fs->size / VSFS_BLOCK_SIZE, block);
		}
		inode->i_blocks = 0;
		return 0;
	}else if((uint64_t) size < size_before){ //shrink
		uint64_t empty_size = size_before - (uint64_t)size;
		vsfs_blk_t last_block_num;

		if(size_before % VSFS_BLOCK_SIZE != 0){ //clear the last not full block
			empty_size -= (size_before % VSFS_BLOCK_SIZE);
			last_block_num = find_last_block(fs, inode);
			bitmap_free(fs->dbmap, fs->sb->num_blocks, last_block_num);
			inode->i_blocks--;
			fs->sb->free_blocks++;
		}
		while(empty_size >= VSFS_BLOCK_SIZE){ //clear the rest
			empty_size -= VSFS_BLOCK_SIZE;
			last_block_num = find_last_block(fs, inode);
			bitmap_free(fs->dbmap, fs->sb->num_blocks, last_block_num);
			inode->i_blocks--;
			fs->sb->free_blocks++;
		}
		inode->i_size = (uint64_t)size;
		return 0;
	}else{ //extend file
		return -ENOSYS;
	}
	
}

void *get_offset_pos(vsfs_inode *inode, uint64_t offset, fs_ctx *fs){
	vsfs_blk_t header = offset / VSFS_BLOCK_SIZE;
	vsfs_blk_t curr_block;

	if(header > 0){
		if(header < VSFS_NUM_DIRECT){
			curr_block = inode->i_direct[header];
		}else{
			vsfs_blk_t *ind_addr = (vsfs_blk_t *) (fs->image + inode->i_indirect * VSFS_BLOCK_SIZE);
			curr_block = ind_addr[header - VSFS_NUM_DIRECT];
		}
	}else{ //offset == 0 or multiple of block size
		curr_block = inode->i_direct[0];
	}
	return fs->image + (curr_block - header) * VSFS_BLOCK_SIZE + offset;
}


/**
 * Read data from a file.
 *
 * Implements the pread() system call. Must return exactly the number of bytes
 * requested except on EOF (end of file). Reads from file ranges that have not
 * been written to must return ranges filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int vsfs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	vsfs_ino_t inum;
	vsfs_inode *inode;
	path_lookup(path, &inum);
	inode = &(fs->itable[inum]);

	//TODO: read data from the file at given offset into the buffer

	if(inode->i_size == 0){ //read nothing
		buf[size] = '\0';
		return 0;
	}
	if(inode->i_size <= (uint64_t) offset){ //read whole
		memset(buf, 0, size);
        buf[size] = '\0';
        return 0;
	}
	void *off_pos = get_offset_pos(inode, offset, fs);
	if(inode->i_size >= offset + size){ //read proper size
		memcpy(buf, off_pos, size);
		buf[size] = '\0';
		return size;
	}else{ //read size is larger than file size
		vsfs_blk_t last_block = find_last_block(fs, inode);
		uint64_t overflow = inode->i_size % VSFS_BLOCK_SIZE;
		void *end_pos = fs->image + last_block * VSFS_BLOCK_SIZE + overflow;
		int valid_size = end_pos - off_pos;
		memcpy(buf, off_pos, valid_size);
		memset(buf + valid_size, 0, size - valid_size);
		buf[size] = '\0';
		return valid_size;
	}
}

/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Must return exactly the number of bytes
 * requested except on error. If the offset is beyond EOF (end of file), the
 * file must be extended. If the write creates a "hole" of uninitialized data,
 * the new uninitialized range must filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   EFBIG   write would exceed the maximum file size 
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int vsfs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: write data from the buffer into the file at given offset, possibly
	// "zeroing out" the uninitialized range
	vsfs_ino_t inum;
	vsfs_inode *inode;
	path_lookup(path, &inum);
	inode = &(fs->itable[inum]);

	if(inode->i_size >= offset + size){
		void *off_pos = get_offset_pos(inode, offset, fs);
		memcpy(off_pos, buf, size);
		clock_gettime(CLOCK_REALTIME, &(inode->i_mtime));
		return size;
	}else{ //extend file
		return -ENOSYS;
	}

}


static struct fuse_operations vsfs_ops = {
	.destroy  = vsfs_destroy,
	.statfs   = vsfs_statfs,
	.getattr  = vsfs_getattr,
	.readdir  = vsfs_readdir,
	.mkdir    = vsfs_mkdir,
	.rmdir    = vsfs_rmdir,
	.create   = vsfs_create,
	.unlink   = vsfs_unlink,
	.utimens  = vsfs_utimens,
	.truncate = vsfs_truncate,
	.read     = vsfs_read,
	.write    = vsfs_write,
};

int main(int argc, char *argv[])
{
	vsfs_opts opts = {0};// defaults are all 0
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (!vsfs_opt_parse(&args, &opts)) return 1;

	fs_ctx fs = {0};
	if (!vsfs_init(&fs, &opts)) {
		fprintf(stderr, "Failed to mount the file system\n");
		return 1;
	}

	return fuse_main(args.argc, args.argv, &vsfs_ops, &fs);
}
