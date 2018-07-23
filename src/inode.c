#include "pnlfs.h"

/* That function ask a struct inode to the VFS */
struct inode *pnlfs_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *i;
	struct buffer_head * bh;
	struct pnlfs_inode* tmp_inode;
	struct pnlfs_inode_info *inode_info;
	unsigned long index;

	pr_info("%s Start\n",  __func__);

	/* Get an inode from the VFS */
	i = iget_locked(sb, ino);


	if (!i)
		return ERR_PTR(-ENOMEM);

	/* If the inode was in cach, it is returned immediatly */
	if (!(i->i_state & I_NEW))
		return i;

	/* Get the index of inode */
	index = do_div(ino, PNLFS_INODES_PER_BLOCK);

	/* Read the block containing the node */
	bh = sb_bread(sb, ino+1);
	if (!bh) {
		iget_failed(i);
		return ERR_PTR(-EIO);
	}

	/* Fill the inode struct */
	tmp_inode = (struct pnlfs_inode *) bh->b_data;

	i->i_mode = le16_to_cpu(tmp_inode[index].mode);
	i->i_op = &i_op;
	i->i_fop = &i_fop;
	i->i_sb = sb;
	i->i_ino = ino;
	i->i_size = le32_to_cpu(tmp_inode[index].filesize);

	if (S_ISDIR(i->i_mode)){
		i->i_blocks = 1;
	}
	else if (S_ISREG(i->i_mode)){
		i->i_blocks = le32_to_cpu(tmp_inode[index].nr_used_blocks);
	}
	else{	
		brelse(bh);
		iget_failed(i);
		return ERR_PTR(-EFAULT);
	}

	i->i_ctime = i->i_atime = i->i_mtime = CURRENT_TIME;
	inode_info = container_of(i, struct pnlfs_inode_info, vfs_inode);
	inode_info->index_block = le32_to_cpu(tmp_inode[index].index_block);
	inode_info->nr_entries = le32_to_cpu(tmp_inode[index].nr_entries);

	/* Function used to unlock the new created inode */
	unlock_new_inode(i);

	pr_info("%s End\n",  __func__);

	return i;

}

/* Free the inode bit at index ino in the bitmap */
int pnlfs_free_inode(struct super_block *sb, unsigned long ino)
{
	struct pnlfs_sb_info *sb_info;
	pr_info("%s : freeing inode %ld\n",  __func__, ino);

	sb_info = sb->s_fs_info;
	bitmap_set(&sb_info->ifree_bitmap[ino >> 6],ino & 0x3f, 1);
	sb_info->nr_free_inodes++;
	return 0;
}

/* Register a new inode in the bitmap and return its index */
unsigned long pnlfs_reserv_new_inode(struct super_block *sb)
{
	struct pnlfs_sb_info *sbi;
	unsigned long new_ino = 0;
	unsigned long first_bit =0;
	int size_bitmap,size_ifree_bitmap;
	int i = 0;

	sbi = sb->s_fs_info;
	size_bitmap = sizeof(sbi->ifree_bitmap[0])<<3;
	size_ifree_bitmap = 512 * sbi->nr_ifree_blocks;

	/* Found first bit avilable from the bitmap  */
 	for (i=0;i<size_ifree_bitmap;i++){
    	first_bit = find_first_bit(&sbi->ifree_bitmap[i], size_bitmap);
		new_ino += first_bit;
		/* Check result */
		if (first_bit /= size_bitmap)
			break;
	}

	/* Set the bit in the bitmap to acknowledge its record */
	bitmap_clear(&sbi->ifree_bitmap[new_ino >> 6], new_ino & 0x3f, 1);
	sbi->nr_free_inodes--;

	pr_info("%s : new inode is %ld\n",  __func__, new_ino);
	return new_ino;
}

/* Search for inode with macthing name in directory */
unsigned long pnlfs_find_inode(struct inode *dir, struct dentry *dentry)
{
	struct pnlfs_dir_block *dir_block;
	struct pnlfs_inode_info *dir_info;
	struct buffer_head *bh;
	struct pnlfs_file *files;
	int i;
	unsigned long ino;

	/* Get the block */
	dir_info = container_of(dir, struct pnlfs_inode_info, vfs_inode);
	bh = sb_bread(dir->i_sb, dir_info->index_block);
	if (!bh)
		return -EIO;

	/* Get the files */
	dir_block = (struct pnlfs_dir_block *) bh->b_data;
	files = dir_block->files;
	brelse(bh);

	/* For each files in the block, compare the name */
	for (i = 0; i < PNLFS_MAX_DIR_ENTRIES; i++) {
		if (strncmp(files[i].filename, dentry->d_name.name,
			    dentry->d_name.len) == 0) {
			ino = files[i].inode;
			/* Return the inode if founded */
			pr_info("%s : inode founded is %ld\n",  __func__, ino);
			return ino;
		}
	}

	pr_info("%s : No inode founded\n",  __func__);
	return 0;
}

/* Register a new block in the bitmap and return its index */
int pnlfs_reserv_new_block(struct super_block *sb)
{
	struct pnlfs_sb_info *sbi;
	unsigned long new_bloc = 0;
	unsigned long first_bit =0;
	int size_bitmap,size_bfree_bitmap;
	int i = 0;

	sbi = sb->s_fs_info;
	size_bitmap = sizeof(sbi->bfree_bitmap[0])<<3;
	size_bfree_bitmap = 512 * sbi->nr_bfree_blocks;

	/* Found first bit avilable from the bitmap  */
 	for (i=0;i<size_bfree_bitmap;i++){
    	first_bit = find_first_bit(&sbi->bfree_bitmap[i], size_bitmap);
		new_bloc += first_bit;
		/* Check result */
		if (first_bit /= size_bitmap)
			break;
	}

	/* Set the bit in the bitmap to acknowledge its record */
	bitmap_clear(&sbi->bfree_bitmap[new_bloc >> 6], new_bloc & 0x3f, 1);
	sbi->nr_free_blocks--;

	pr_info("%s : new bloc is %ld\n",  __func__, new_bloc);
	return new_bloc;
}

/* Free the inode bit at index ino in the bitmap */
int pnlfs_free_block(struct super_block *sb, int bno)
{
	struct buffer_head *data_bh;
	struct pnlfs_sb_info *sb_info;

	pr_info("%s : freeing block %d\n",  __func__, bno);

	sb_info = sb->s_fs_info;
	if (!(data_bh = sb_bread(sb, bno)))
		return -EIO;

	/* Reset block value */
	memset(data_bh->b_data, 0, PNLFS_BLOCK_SIZE);

	/* Free the block index in the bitmap */
	bitmap_set(&sb_info->bfree_bitmap[bno >> 6], bno & 0x3f, 1);
	sb_info->nr_free_blocks++;

	mark_buffer_dirty(data_bh);
	brelse(data_bh);

	pr_info("%s : Block %d freed\n",  __func__, bno);
	return 0;
}

/* Change the block according to the dentry */
int pnlfs_add_entry
(struct inode * dir, struct dentry * dentry, unsigned long ino)
{
	struct pnlfs_sb_info *sbi;
	struct pnlfs_file *files;
	struct pnlfs_inode_info *dir_info;
	struct buffer_head *bh;
	struct pnlfs_dir_block *blk;
	int i;

	pr_info("%s : Start\n", __func__);

	/* Get dir_info */
	sbi = dir->i_sb->s_fs_info;
	dir_info = container_of(dir, struct pnlfs_inode_info, vfs_inode);

	/* Read the block */
	if (!(bh = sb_bread(dir->i_sb, dir_info->index_block)))
		return -EIO;
	blk = (struct pnlfs_dir_block *) bh->b_data;

	/* For each files of the block : */
	files = blk->files;
	for (i = 0; i < PNLFS_MAX_DIR_ENTRIES; i++) {
		if (le32_to_cpu(files[i].inode) == 0) {
			/* Set the files in the block */
			files[i].inode = cpu_to_le32(ino);
			strncpy(files[i].filename, dentry->d_name.name,
				dentry->d_name.len);
			dir_info->nr_entries++;
			mark_inode_dirty(dir);
			mark_buffer_dirty(bh);
			brelse(bh);

			pr_info("%s : End\n", __func__);
			return 0;
		}
	}

	pr_info("%s : Error\n", __func__);
	brelse(bh);
	return -EPERM;
}

/* Remove the dentry from the block */
int pnlfs_delete_entry(struct inode *dir, unsigned long ino)
{
	struct pnlfs_inode_info *dir_info;
	struct pnlfs_dir_block *dblk;
	struct buffer_head *bh;
	struct pnlfs_file *files;
	int i;
	pr_info("%s : Start\n", __func__);

	/* Get the block */
	dir_info = container_of(dir, struct pnlfs_inode_info, vfs_inode);
	if (!(bh = sb_bread(dir->i_sb, dir_info->index_block)))
		return -EIO;
	dblk = (struct pnlfs_dir_block *) bh->b_data;

	/* For each files from the block */
	files = dblk->files;
	for (i = 0; i < PNLFS_MAX_DIR_ENTRIES; i++) {
		if (files[i].inode == ino) {

			/* Reset block value */
			memset(&files[i], 0, sizeof(files[i]));
			dir_info->nr_entries--;

			dir->i_mtime = CURRENT_TIME;
			mark_inode_dirty(dir);
			mark_buffer_dirty(bh);

			pr_info("%s : End, entry %ld removed\n", __func__, ino);
			brelse(bh);
			return 0;
		}
	}

	pr_info("%s : Error\n", __func__);
	brelse(bh);
	return -ENOENT;
}

/*****************************
****inode_operations
*****************************/

/* Search for inodes */
static struct dentry *pnlfs_lookup
(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	struct pnlfs_inode_info *inode_info;
	struct buffer_head *bh;
	struct pnlfs_dir_block *dir_block;
	struct pnlfs_file *files;
	struct inode *inode;
	int i;

	pr_info("%s Start\n",  __func__);

	/* Read block */
	inode_info = container_of(dir, struct pnlfs_inode_info, vfs_inode);
	bh = sb_bread(dir->i_sb, inode_info->index_block);
	if (bh == NULL)
		return ERR_PTR(-EIO);

	/* Get files contained in that inode */
	dir_block = (struct pnlfs_dir_block *) bh->b_data;
	files = dir_block->files;
	brelse(bh);
	inode = NULL;
	/* Search in the block for the looked inode */
	for (i = 0; i < PNLFS_MAX_DIR_ENTRIES; i++) {
		if(!strcmp(files[i].filename, dentry->d_name.name)){
			inode = pnlfs_iget(dir->i_sb, files[i].inode);
			if (IS_ERR(inode))
				return (ERR_PTR(PTR_ERR(inode)));
			break;
		}
	}

	/* Add dentry to hash queues */
	d_add(dentry, inode);

	pr_info("%s End\n",  __func__);
	return NULL;
}

/* Set new inode and save change to the block */
static int pnlfs_create
(struct inode *dir, struct dentry *dentry, umode_t mode, bool unused)
{
	struct pnlfs_sb_info *sbi;
	struct inode *i;
	struct buffer_head *bh;
	struct pnlfs_file_index_block *index_block;
	struct pnlfs_inode_info *dir_info;
	struct pnlfs_inode_info *new_i_info;
	unsigned long new_i ;
	int new_b;

	pr_info("%s Start\n",  __func__);
	sbi = (struct pnlfs_sb_info *) dir->i_sb->s_fs_info;

	/* Get inode info */ 
	dir_info = container_of(dir, struct pnlfs_inode_info, vfs_inode);

	/* Check for errors */
	if (dir_info->nr_entries >= PNLFS_MAX_DIR_ENTRIES) 
		return -ENOSPC;
	if (dentry->d_name.len > PNLFS_FILENAME_LEN) 
		return -ENAMETOOLONG;
	if((new_i = pnlfs_reserv_new_inode(dir->i_sb))== sbi->nr_inodes)
		return -ENOSPC;
	if((new_b=pnlfs_reserv_new_block(dir->i_sb))== sbi->nr_blocks){
		pnlfs_free_inode(dir->i_sb, new_i);
		return -ENOSPC;
	}

	/* Set the new inode */
	i = new_inode(dir->i_sb);
	i->i_mode = mode;						
	i->i_sb = dir->i_sb;					
	i->i_op = dir->i_op;					
	i->i_fop = dir->i_fop;					
	i->i_ino = new_i;						
	i->i_blocks = 1;	
	i->i_ctime = i->i_atime = i->i_mtime = CURRENT_TIME;

	/* Set the new inode info */
	new_i_info = container_of(i, struct pnlfs_inode_info, vfs_inode);
	new_i_info->index_block = new_b;
	new_i_info->nr_entries = 0;

	pr_info("%s New inode : %lu, block is %d, name is %s \n",
	 __func__, new_i, new_i_info->index_block, dentry->d_name.name);

	/* Change the block according to the dentry */
	if (pnlfs_add_entry(dir, dentry, new_i))
		goto err1;

	/* If regular file */
	if (S_ISREG(mode)) {		
		if (!(bh = sb_bread(i->i_sb, new_b)))
			goto err2;
		index_block = (struct pnlfs_file_index_block *) bh->b_data;
		if((new_b = pnlfs_reserv_new_block(dir->i_sb))== sbi->nr_blocks)
		{
			goto err3;
		}
		index_block->blocks[0] = cpu_to_le32(new_b);
		mark_buffer_dirty(bh);
		brelse(bh);
	}

	inode_init_owner(i, dir, mode);
	insert_inode_hash(i);
	d_instantiate(dentry, i);
	mark_inode_dirty(i);
	mark_inode_dirty(dir);

	pr_info("%s End\n", __func__);
	return 0;
 err3:
	brelse(bh);
 err2:
	pnlfs_delete_entry(dir, new_i);
 err1:
	pnlfs_free_inode(dir->i_sb, new_i);
	pnlfs_free_block(dir->i_sb, new_i_info->index_block);
	iput(i);

	pr_info("%s Error\n", __func__);
	return -EIO;
}

static int pnlfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct buffer_head *bh;
	unsigned long ino;
	struct pnlfs_inode_info *inode_info;
	struct pnlfs_file_index_block *fblk;
	int *blocks, i;
	int err;

	pr_info("%s Start\n", __func__);

	/* Check for errors */
	if (d_really_is_negative(dentry))
		return -ENOENT;
	if (!(ino = pnlfs_find_inode(dir, dentry)))
		return -ENOENT;
	if ((err = pnlfs_delete_entry(dir, ino))) 
		return err;

	/* Get the block */
	inode_info = container_of(
		dentry->d_inode, struct pnlfs_inode_info, vfs_inode);
	if (!(bh = sb_bread(dentry->d_inode->i_sb, inode_info->index_block)))
		return -EIO;
	fblk = (struct pnlfs_file_index_block *) bh->b_data;
	blocks = fblk->blocks;
	brelse(bh);

	/* If regular file */
	if (S_ISREG(dentry->d_inode->i_mode)) {
		/* Free the bitmap for each block of the file */
		for (i = 0; i < dentry->d_inode->i_blocks; i++)
			pnlfs_free_block(dentry->d_inode->i_sb, le32_to_cpu(blocks[i]));
	}

	/* Free the bitmaps */	
	pnlfs_free_block(dentry->d_inode->i_sb, inode_info->index_block);
	pnlfs_free_inode(dir->i_sb, dentry->d_inode->i_ino);
	mark_inode_dirty(dir);

	pr_info("%s End\n", __func__);
	return 0;
}

/* Create a directory */
static int pnlfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int return_value;
	pr_info("%s Start\n", __func__);
	return_value = pnlfs_create(dir, dentry, mode | S_IFDIR, false);
	pr_info("%s End\n", __func__);
	return return_value;
}

/* Create a directory */
static int pnlfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct pnlfs_inode_info *dir_info;
	pr_info("%s Start\n", __func__);

	dir_info = container_of(
		dentry->d_inode, struct pnlfs_inode_info, vfs_inode);
	if (dir_info->nr_entries)
		return -ENOTEMPTY;

	pr_info("%s End\n", __func__);
	return pnlfs_unlink(dir, dentry);
}

/* Change the name of a dentry */
int pnlfs_rename
(struct inode *old_dir,
 struct dentry *old_dentry,
 struct inode *new_dir,
 struct dentry *new_dentry,
 unsigned int flags)
{
	struct inode *new_i, *old_i;
	int err = 0;

	pr_info("%s Start\n", __func__);
	
	old_i = old_dentry->d_inode;
	if((new_i = new_dentry->d_inode))	
	{
		if (S_ISREG(new_i->i_mode)){
			if((err=pnlfs_unlink(new_dir, new_dentry)))return err;
		}
		else if (S_ISDIR(new_i->i_mode)){
			if((err=pnlfs_rmdir(new_dir, new_dentry)))return err;
		}
		else {return -EINVAL;}
	}
	if((err=pnlfs_delete_entry(old_dir, old_dentry->d_inode->i_ino)))
		return err;

	err=pnlfs_add_entry(new_dir, new_dentry, old_i->i_ino);

	pr_info("%s End\n", __func__);
	return err;
}

struct inode_operations i_op = {
	.lookup = pnlfs_lookup,
	.create = pnlfs_create,
	.unlink = pnlfs_unlink,
	.mkdir = pnlfs_mkdir,
	.rmdir = pnlfs_rmdir,
	.rename = pnlfs_rename
};