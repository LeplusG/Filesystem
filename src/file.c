#include "pnlfs.h"

//static ssize_t pnlfs_read(struct file *filp, char __user *buf, size_t count,
//loff_t *pos);
//static ssize_t pnlfs_write(struct file *filp, const char __user *buf,
//size_t count, loff_t *pos);

/* Function called iteratively, with the same context */
static int pnlfs_iterate_shared(struct file *file, struct dir_context *ctx)
{
	struct inode * inode;
	struct pnlfs_inode_info *inode_info;
	struct buffer_head *bh;
	struct pnlfs_dir_block *dir_block;
	struct pnlfs_file *f;
	int i;
	bool err = false;
	pr_info("%s Start\n", __func__);

	/* Check if the name length is not too long */
	if (file->f_path.dentry->d_name.len > PNLFS_FILENAME_LEN)
		return -ENAMETOOLONG;

	/* Add the file . and the file .. */
	if(!dir_emit_dots(file, ctx))
		return -EACCES;

	/* Get the inode of current file */
	inode = file_inode(file);
	inode_info = container_of(inode, struct pnlfs_inode_info, vfs_inode);
	/* Check if the cursor hasn't passed all the files */
	if (ctx->pos >= (le32_to_cpu(inode_info->nr_entries) + 2)) {
    pr_info("%s End\n", __func__);
		return 0;
  }

	/* Read the block of the inode */
	bh = sb_bread(inode->i_sb, inode_info->index_block);
	if (bh == NULL)
		return -EIO;

	/* Get the list of files */
	dir_block = (struct pnlfs_dir_block *) bh->b_data;
	brelse(bh);
	f = dir_block->files;

	/* For each files, add it to the list via dit_emit function */
	for (i = 0; i < PNLFS_MAX_DIR_ENTRIES; i++) {
		if (f[i].inode == 0)
			continue;
		if ((err = !dir_emit(ctx, f[i].filename, strlen(f[i].filename),
			f[i].inode, DT_UNKNOWN))) {
			break;
		}
	}

	/* Increase the offset */
	ctx->pos += inode_info->nr_entries;

	pr_info("%s Next\n", __func__);
	if(err)
		return -EACCES;
	return 0;
}

/* Read block */
static ssize_t pnlfs_read
(struct file * filp, char __user * buf, size_t count, loff_t * pos)
{
	struct inode *inode;
	struct pnlfs_inode_info *file_info;
	struct pnlfs_file_index_block *index_block;
	struct buffer_head *bh;
	int i = 0, offset, *blocks;
	loff_t max, index;

	pr_info("%s Start\n", __func__);
	inode = file_inode(filp);

	/* If end of file */
	if (*pos >= inode->i_size)
		return 0;

	/* Get the block */
	file_info = container_of(inode, struct pnlfs_inode_info, vfs_inode);
	if (!(bh = sb_bread(inode->i_sb, file_info->index_block)))
		return -EIO;

	/* Get information on the file */
	index_block = (struct pnlfs_file_index_block *) bh->b_data;
	brelse(bh);
	blocks = index_block->blocks;
	index = filp->f_pos >> 12;					/* Initial index of cursor */
	max = inode->i_size - filp->f_pos;	/* Maximum read */
	if(!(bh=sb_bread(inode->i_sb, le32_to_cpu(index_block->blocks[index]))))
		return -EIO;
	offset = filp->f_pos & 0x1fff;			/* Initial offset of block */

	/* Let's read ! */
	while (count > 0 && max > 0) {
		/* Copy data to user buffer */
		put_user(bh->b_data[offset], buf++);
		/* Update counters */
		max--;
		count--;
		filp->f_pos++;
		offset++;
		i++;
		/* Offset touch end of block */
		if (offset > PNLFS_BLOCK_SIZE) {
			/* Stop if current block is final block */
			if (index == inode->i_blocks) {
				break;
			}
			/* Get new block */
			brelse(bh);
			index++;
			offset = 0;
			bh = sb_bread(inode->i_sb,le32_to_cpu(blocks[index]));
			if(!(bh))
				return -EIO;
		}
	}
	*pos += i;											/* Update offset */
	inode->i_atime = CURRENT_TIME;	/* Update inode */
	brelse(bh);
	pr_info("%s : %d bytes red\n", __func__, i);
	pr_info("%s End\n", __func__);
	return i;
}

/* Write block */
ssize_t pnlfs_write(struct file *filp, const char __user *buf, size_t count,
		    loff_t *pos)
{
	struct inode *inode;
	struct pnlfs_sb_info *sb_info;
	struct super_block *sb;
	struct pnlfs_inode_info *file_info;
	struct pnlfs_file_index_block *index_block;
	struct buffer_head *blk, *data;
	int i = 0, *blocks;
	int new_block;
	loff_t offset, index;

	pr_info("%s Start\n", __func__);

	inode = file_inode(filp);	/* Get file inode */
	sb = inode->i_sb;
	if (inode->i_size == PNLFS_MAX_FILESIZE) /* Check for error */
		return -EFBIG;

	file_info = container_of(inode, struct pnlfs_inode_info, vfs_inode);
	sb_info = inode->i_sb->s_fs_info;

	/* If Append mode, the offset is set to the end of file */
  if (filp->f_flags & O_APPEND)
		filp->f_pos = inode->i_size;

	/* Get blocks */
	if (!(blk=sb_bread(inode->i_sb, file_info->index_block)))
		return -EIO;
	index_block = (struct pnlfs_file_index_block *) blk->b_data;
	blocks = index_block->blocks;

	offset = filp->f_pos & 0x1fff;	/* Initial offset in the block */
	index = filp->f_pos >> 12;			/* Initial index of the cursor */

	/* Read the blocks */
	data = sb_bread(inode->i_sb, le32_to_cpu(blocks[index]));
	if (!data){
		brelse(blk);
		return -EIO;
	}

	/* Let's write ! */
	while (count > 0 && inode->i_size < PNLFS_MAX_FILESIZE) {
		/* Get data from the user buffer */
		get_user(data->b_data[offset], buf++);
		/* Update counter */
		count--;
		offset++;
		filp->f_pos++;
		i++;
		/* If offset touch the end of block */
		if (offset > PNLFS_BLOCK_SIZE) {
			/* Mark the buffer as dirty */
			mark_buffer_dirty(data);
			brelse(data);
			index++;
			/* If index touch the number of blocks reserved to the inode */
			if (index == inode->i_blocks) {
				new_block = pnlfs_reserv_new_block(sb);
				if (new_block == sb_info->nr_blocks)
					return -ENOSPC;
				blocks[index] = cpu_to_le32(new_block);
				inode->i_blocks++;
				mark_buffer_dirty(blk);
			}
			/* Read new block */
			data = sb_bread(inode->i_sb,le32_to_cpu(blocks[index]));
			if (!data){
				brelse(blk);
				return -EIO;
			}
			/* Reset the offset */
			offset = 0;
		}
	}
	/* Update counter and inode information */
	*pos += i;
	if (filp->f_pos > inode->i_size) {
		inode->i_size = filp->f_pos;
	}

 	mark_inode_dirty(inode);
	brelse(blk);
	mark_buffer_dirty(data);
	brelse(data);

	pr_info("%s : %d bytes written\n", __func__, i);
	pr_info("%s : End\n", __func__);

	return i;
}


struct file_operations i_fop = {
	.iterate_shared = pnlfs_iterate_shared,
	.read = pnlfs_read,
	.write = pnlfs_write
};
