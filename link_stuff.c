#include "include.h"

extern char * totalPath();

/**
 * TODO: - Assure we are only writing to a dir(step 3)
 * 		 - Double and Indirect blocks
 */
 
int symlink(char *pathname, char *parameter){	
	printf("entering symlink\n");
	char buf[1024];
	// symlink oldNAME  newNAME    e.g. symlink /a/b/c /x/y/z
	  // ASSUME: oldNAME has <= 60 chars, inlcuding the ending NULL byte.
	int dev;
	int old_ino     = my_getino(&dev, pathname);
	if (old_ino == 0){ printf("%s does not exist\n", pathname); return 0; } //(1). verify oldNAME exists (either a DIR or a REG file)
	int ptn_blk     = (old_ino - 1) / 8 + bg_inode_table;
	int ptn_off     = (old_ino - 1) % 8;
	get_block(dev, ptn_blk, buf);
	INODE *ptn_inode = (INODE*)buf + ptn_off;
	
	if ( !(S_ISDIR(ptn_inode->i_mode) || S_ISREG(ptn_inode->i_mode)) ){
		printf("Invalid file type\n");
		return;
	}
	
	//(2). creat a FILE /x/y/z
	mkdir_creat(parameter);
	
	int ino = my_getino(&dev, parameter);
	//assume ino is not 0
	int blk    = (ino - 1) / 8 + bg_inode_table;
	int offset = (ino - 1) % 8;
	get_block(dev, blk, buf);
	
	INODE *param_ino  = (INODE*)buf + offset;
	
	//(3). change /x/y/z's type to LNK (0120000)=(1010.....)=0xA...
	printf("copying name\n");
	strcpy((char*)param_ino->i_block, parameter); 	//(4). write the string oldNAME into the i_block[ ], which has room for 60 chars.
	
	put_block(dev, blk, buf);
}
 
int my_link(char *pathname, char *parameter){
	char buf[1024];
	char the_buf2[1024]= { 0 };
	char tempParam[200] = { 0 };
	int ino = getino(&dev, pathname);

	strcpy(tempParam, parameter);
	int ino_base = getino(&dev, dirname(tempParam));

	if (ino == 0){ return 0;}
	if(ino_base == 0) {return 0;}

	// The Minode trying to be copied
	MINODE *mip_to_copy = iget(dev,ino);
	// The minode of the base
	MINODE *mip_base = iget(dev,ino_base);

	printf("\n\nThe pathname: %s the parameter: %s \n",pathname, parameter);
	
	// Check to make sure file being copied is Dir
	if ((mip_to_copy->INODE.i_mode & 0xF000) == 0x4000){
		printf("CANNOT LINK TO A DIRECTORY\n");
		return 0;
	}
	
	// Check if base exists and is Dir
	if(mip_base == 0 || (mip_base->INODE.i_mode & 0xF000) != 0x4000){
		printf("Issue Creating a link in this directory\n");
		return 0;
	}

	// Check if file-to-link already exists
	char path_new_file[200] = { 0 };
	if(parameter[0] == '/'){
		strcpy(path_new_file, parameter);
		printf("The total new path = %s\n", path_new_file);

	}else {
		strcpy(path_new_file, totalPath());
		strcat(path_new_file, "/");
		strcat(path_new_file, parameter );
		printf("The total new path = %s\n", path_new_file);
	}
	if(getino(&dev, path_new_file) != 0){
		printf("File to link already exists \n");
		return 0;
	}

	// The base inode
	INODE *ibase = &(mip_base->INODE);
	// The inode to copy
	INODE *to_copy = &(mip_to_copy->INODE);
	
	char sec_buf[1024];
	int i;
	char *cp;
	DIR *dp;

	int new_length = ideal_len(strlen(basename(parameter)));
	int current_dir_length = 0;
	int remain = 0;
	
	//Traverse all i_blocks to assure there is room
	for (i = 0; i < 12; i++){
		if (ibase->i_block[i] == 0) break;
		
		get_block(fd, ibase->i_block[i], sec_buf);
		cp = sec_buf;
		dp = (DIR *)sec_buf;
		
		int blk = ibase->i_block[i];
		printf("step to LAST entry in data block %d\n", blk);

		// Traverse all the directories in the given block...
		while (cp + dp->rec_len < sec_buf + BLKSIZE){
			current_dir_length = ideal_len(dp->name_len);
			printf("dp->rec_len = %d current_dir_length = %d\n", dp->rec_len, current_dir_length);			
			char temp[256];
			strncpy(temp, dp->name, dp->name_len);
			temp[dp->name_len] = 0;
			printf("Traversing %s\n", temp);
			cp += dp->rec_len;
			dp = (DIR*)cp;
			remain = dp->rec_len - current_dir_length;

		}// We are at the end of the block
		

		// Now to write the new node
		if (remain >= new_length){
			INODE * updated_inode = 0;
			printf("remain = %d current_dir_length = %d\n", remain, current_dir_length);
			printf("hit case where remain >= needed_length\n");	
	
			/* Enter the new entry as the last entry and trim the previous entry to its ideal length*/
			int ideal = ideal_len(dp->name_len);
			dp->rec_len = ideal;
			cp += dp->rec_len;
			dp = (DIR*)cp;

			// Obtain the inode to copy, update it, and write it back to its appropriate blk
			int cur_blk    = (mip_to_copy->ino - 1) / 8 + bg_inode_table;
			int cur_offset = (mip_to_copy->ino - 1) % 8;
			printf("Setting a block in blk: %d", cur_blk);
			get_block( dev, cur_blk, the_buf2);
			updated_inode = (INODE *)the_buf2 + cur_offset;
			updated_inode->i_links_count ++;
			put_block(fd, cur_blk, the_buf2);

			// Give 'dp' the inode we just obtained
			dp->inode    = mip_to_copy->ino;
			dp->rec_len  = remain;
			dp->name_len = strlen(basename(parameter));
			strncpy(dp->name, basename(parameter), dp->name_len);

			// Update the base node's i_block[i] To correctly show the dp we just obtained
			put_block(fd, ibase->i_block[i], sec_buf);

			// Lastly, put away those pesky minodes
			iput(mip_base);
			iput(mip_to_copy);
			return 0;

		}else{
			printf("\n\nNO SPACE IN CURRENT DATA BLOCK\n\n");
			getchar();
		}
	}
	
	iput(mip_base);
	iput(mip_to_copy);
	return 0;
	
}









/*

                        HOW TO unlink
===========================================================================
                       unlink pathname

(1). get pathname's INODE into memory

(2). verify it's a FILE (REG or LNK), can not be a DIR; 

(3). decrement INODE's i_links_count by 1;

(4). if i_links_count == 0 ==> rm pathname by

        deallocate its data blocks by:

     Write a truncate(INODE) function, which deallocates ALL the data blocks
     of INODE. This is similar to printing the data blocks of INODE.

        deallocate its INODE;
     
(5). Remove childName=basename(pathname) from the parent directory by

        rm_child(parentInodePtr, childName)

     which is the SAME as that in rmdir or unlink file operations.


*/

INODE *truncate_ino(INODE *i){
	int k;
	for (k = 0; k < 15; k++){
		i->i_block[k] = 0;
	}
	return i;
}

int my_unlink(char *ptn){
	if (strlen(ptn) == 0){
		return 0;
	}
	char buf[1024];
	int dev     = 		      0;
	int ptn_ino = getino(&dev, ptn); //(1). get pathname's INODE into memory
	if (ptn_ino == 0){
		printf("%s does not exist\n", ptn);
		return;
	}
	
	int blk    = (ptn_ino - 1) / 8 + bg_inode_table;
	int offset = (ptn_ino - 1) % 8;
	
	get_block(dev, blk, buf);
	
	INODE *ptn_inode = (INODE*)buf + offset;
		
	if (S_ISDIR(ptn_inode->i_mode)){ //(2). verify it's a FILE (REG or LNK), can not be a DIR; 
		printf("Cannot unlink a director\n");
		return;
	}
	
	ptn_inode->i_links_count--; //(3). decrement INODE's i_links_count by 1;
	if (ptn_inode->i_links_count == 0){
		ptn_inode = truncate_ino(ptn_inode);
	}	
	char temp[256];
	strcpy(temp, ptn);
	
	char *childName  = basename(ptn);
	char *parentName = dirname(temp);
	
	printf("childName = %s parentName = %s\n", childName, parentName);
	
	getchar();
	
	int parent_ino    = getino(&dev, parentName);
	int parent_blk    = (parent_ino - 1) / 8 + bg_inode_table; 
	int parent_offset = (parent_ino - 1) % 8;
	get_block(dev, parent_blk, buf);
	 
	MINODE *parentInodePtr = iget(&dev, parent_ino);
	 
	rm_child(parentInodePtr, childName);
}

















