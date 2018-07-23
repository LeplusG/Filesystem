#include "pnlfs.h"

MODULE_DESCRIPTION("pnlfs");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Leplus");

struct pnlfs_sb_info *sbi;

/* That function undo all the change made by fill_super function */
static void pnlfs_put_super(struct super_block *sb)
{
	pr_info("%s Start\n",  __func__);
	sbi = sb->s_fs_info;
	if (sbi->bfree_bitmap)
		kfree(sbi->bfree_bitmap);
	if (sbi->ifree_bitmap)
		kfree(sbi->ifree_bitmap);
	if (sbi)
		kfree(sb->s_fs_info);
	pr_info("%s End\n",  __func__);
}

/* Inode constructor */
static struct inode *pnlfs_alloc_inode(struct super_block *sb)
{
	struct pnlfs_inode_info *i;

	pr_info("%s Start\n",  __func__);
	i = kmalloc(sizeof(*i), GFP_KERNEL);
	if (!i)
		return ERR_PTR(-ENOMEM);
	inode_init_once(&i->vfs_inode);
	return &i->vfs_inode;
	pr_info("%s End\n",  __func__);
}

/* Inode destructor */
static void pnlfs_destroy_inode(struct inode *i)
{
	struct pnlfs_inode_info * pnlfs_i;
	pr_info("%s Start\n",  __func__);
	pnlfs_i = container_of(i, struct pnlfs_inode_info, vfs_inode);
	kfree(pnlfs_i);
	pr_info("%s End\n",  __func__);
}

int pnlfs_sync_fs(struct super_block *sb, int wait)
{
	struct buffer_head *bh, *bitmap_bh;
	struct pnlfs_superblock *superblk;
	unsigned long *bitmap;
	int i, j;

	pr_info("%s Start\n",  __func__);

	sbi = sb->s_fs_info;

	if (!(bh=sb_bread(sb,0)))
		return -EIO;

	superblk=(struct pnlfs_superblock *) bh->b_data;
	superblk->nr_blocks = le32_to_cpu(sbi->nr_blocks);
	superblk->nr_inodes = le32_to_cpu(sbi->nr_inodes);
	superblk->nr_istore_blocks = le32_to_cpu(sbi->nr_istore_blocks);
	superblk->nr_ifree_blocks = le32_to_cpu(sbi->nr_ifree_blocks);
	superblk->nr_bfree_blocks = le32_to_cpu(sbi->nr_bfree_blocks);
	superblk->nr_free_blocks = le32_to_cpu(sbi->nr_free_blocks);
	superblk->nr_free_inodes = le32_to_cpu(sbi->nr_free_inodes);

	pr_info("%s PNLFS Information on superblock \n"
		"\tmagic		= %x\n"
		"\tnr_block 		= %d\n"
		"\tnr_inodes 		= %d\n"
		"\tnr_istore_blocks 	= %d\n"
		"\tnr_ifree_blocks 	= %d\n"
		"\tnr_bfree_blocks 	= %d\n"
		"\tnr_free_inodes 	= %d\n"
		"\tnr_free_blocks 	= %d\n",
		__func__, le32_to_cpu(superblk->magic),
		superblk->nr_blocks, sbi->nr_inodes,
		superblk->nr_istore_blocks, sbi->nr_ifree_blocks,
		superblk->nr_bfree_blocks, sbi->nr_free_inodes,
		superblk->nr_free_blocks);

	/* Read the block containing ifree bitmap */
	for (i = 0; i < sbi->nr_ifree_blocks; i++) {
		bitmap_bh = sb_bread(sb, 1 + sbi->nr_istore_blocks + i);
		if (!bitmap_bh) {
			return -EIO;
		}
		/* Initiate the bitmap of free inodes  */
		bitmap = (unsigned long *) bitmap_bh->b_data;
		for (j = 0; j < 512; j++) {
			sbi->ifree_bitmap[j] = le64_to_cpu(bitmap[j]);
		}

		brelse(bitmap_bh);
	}

	/* Read the block containing blocks bitmap */
	for (i = 0; i < sbi->nr_bfree_blocks; i++) {
		bitmap_bh = sb_bread(sb,1 + sbi->nr_bfree_blocks
					+ sbi->nr_istore_blocks + i);
		if (!bitmap_bh) {
			return -EIO;
		}
		/* Initiate the bitmap of free blocks  */
		bitmap = (unsigned long *) bitmap_bh->b_data;
		for (j = 0; j < 512; j++) {
			sbi->bfree_bitmap[j] = le64_to_cpu(bitmap[j]);
		}

		brelse(bitmap_bh);
	}
	brelse(bh);
	return 0;
}

static
int pnlfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct pnlfs_inode_info *inode_info;
	struct buffer_head *bh;
	struct pnlfs_inode *i;
	int index;
	unsigned long bno;

	pr_info("%s Start writing on inode %lu\n",  __func__, inode->i_ino);

	inode_info = container_of(inode, struct pnlfs_inode_info, vfs_inode);
	bno = inode->i_ino;
	index = do_div(bno, PNLFS_INODES_PER_BLOCK);
	
	if (!(bh = sb_bread(inode->i_sb, bno + 1)))
		return -EIO;

	i = (struct pnlfs_inode *) bh->b_data;
	i[index].mode = cpu_to_le32(inode->i_mode);
	i[index].index_block = cpu_to_le32(inode_info->index_block);
	i[index].filesize = cpu_to_le32(inode->i_size);

	if (S_ISDIR(inode->i_mode)){
		i[index].nr_entries = cpu_to_le32(inode_info->nr_entries);
	}
	else if (S_ISREG(inode->i_mode)){
		i[index].nr_used_blocks = cpu_to_le32(inode->i_blocks);
	}
	else{	
		brelse(bh);
		return -EFAULT;
	}
	mark_buffer_dirty(bh);
	brelse(bh);

	pr_info("%s End\n",  __func__);
	return 0;
}

/* List of operations */
struct super_operations pnlfs_op = {
	.put_super = pnlfs_put_super,
	.alloc_inode = pnlfs_alloc_inode,
	.destroy_inode = pnlfs_destroy_inode,
	.write_inode = pnlfs_write_inode,
	.sync_fs = pnlfs_sync_fs,
};

/* CallBack called during mount, fill the super block */
static int pnlfs_fill_super(struct super_block *sb, void *data, int flags)
{
	struct inode *root;
	struct buffer_head *bh, *bitmap_bh;
	struct pnlfs_superblock *tmp_sb;
	unsigned long *bitmap;
	int i, j, err;

	pr_info("%s Start\n",  __func__);

	/* Part 1 of the subject : Init the struct super_block */
	sb->s_magic = PNLFS_MAGIC;
	sb->s_blocksize = PNLFS_BLOCK_SIZE;
	sb->s_maxbytes = PNLFS_MAX_FILESIZE;

	/**
	 * Read a pointer to the buffer in cache of the block 0, from the
	 * partition sb. The size correspond to sb->s_blocksize, and the data
	 * are accessible from the buffer_head struct returned
	 */
	pr_info("%s Get buffer_head\n",  __func__);
	bh = sb_bread(sb, 0);
	if (!bh) return -EIO;

	/* Compare the Magic number */
	pr_info("%s Compare the Magic number\n",  __func__);
  tmp_sb = (struct pnlfs_superblock *) bh->b_data;
	if (le32_to_cpu(tmp_sb->magic) != PNLFS_MAGIC) 
	{
		err = -EPERM;
		goto exit;
	}
	brelse(bh);

	/* Set operations */
	sb->s_op = &pnlfs_op;

	/* Allocate the pnlfs_sb_info struct */
	pr_info("%s Compare the Magic number\n",  __func__);
	sbi = kzalloc(sizeof(struct pnlfs_sb_info), GFP_KERNEL);
	if(!sbi) {
		err = -ENOMEM;
		goto exit;
	}
	sbi->nr_blocks = le32_to_cpu(tmp_sb->nr_blocks);
	sbi->nr_inodes = le32_to_cpu(tmp_sb->nr_inodes);
	sbi->nr_istore_blocks = le32_to_cpu(tmp_sb->nr_istore_blocks);
	sbi->nr_ifree_blocks = le32_to_cpu(tmp_sb->nr_ifree_blocks);
	sbi->nr_bfree_blocks = le32_to_cpu(tmp_sb->nr_bfree_blocks);
	sbi->nr_free_blocks = le32_to_cpu(tmp_sb->nr_free_blocks);
	sbi->nr_free_inodes = le32_to_cpu(tmp_sb->nr_free_inodes);

	pr_info("%s PNLFS Information on superblock \n"
		"\tmagic		= %x\n"
		"\tnr_block 		= %d\n"
		"\tnr_inodes 		= %d\n"
		"\tnr_istore_blocks 	= %d\n"
		"\tnr_ifree_blocks 	= %d\n"
		"\tnr_bfree_blocks 	= %d\n"
		"\tnr_free_inodes 	= %d\n"
		"\tnr_free_blocks 	= %d\n",
		__func__, le32_to_cpu(tmp_sb->magic),
		sbi->nr_blocks, sbi->nr_inodes,
		sbi->nr_istore_blocks, sbi->nr_ifree_blocks,
		sbi->nr_bfree_blocks, sbi->nr_free_inodes,
		sbi->nr_free_blocks);

	/* Allocate the bitmap of free inodes */
	sbi->ifree_bitmap = (unsigned long*) kmalloc(512* sizeof(unsigned long),
			GFP_KERNEL);
	if (!sbi->ifree_bitmap) {
		err = -ENOMEM;
		goto exit1;
	}

	/* Read the block containing ifree bitmap */
	for (i = 0; i < sbi->nr_ifree_blocks; i++) {
		bitmap_bh = sb_bread(sb, 1 + sbi->nr_istore_blocks + i);
		if (!bitmap_bh) {
			err = -EIO;
			goto exit3;
		}
		/* Initiate the bitmap of free inodes  */
		bitmap = (unsigned long *) bitmap_bh->b_data;
		for (j = 0; j < 512; j++) {
			sbi->ifree_bitmap[j] = le64_to_cpu(bitmap[j]);
		}
		brelse(bitmap_bh);
	}

	/* Allocate the bitmap of free blocks */
	sbi->bfree_bitmap = (unsigned long*) kmalloc(512* sizeof(unsigned long),
			GFP_KERNEL);
	if (!sbi->bfree_bitmap) {
		err = -ENOMEM;
		goto exit2;
	}

	/* Read the block containing blocks bitmap */
	for (i = 0; i < sbi->nr_bfree_blocks; i++) {
		bitmap_bh = sb_bread(sb,1 + sbi->nr_ifree_blocks
					+ sbi->nr_istore_blocks + i);
		if (!bitmap_bh) {
			err = -EIO;
			goto exit3;
		}
		/* Initiate the bitmap of free blocks  */
		bitmap = (unsigned long *) bitmap_bh->b_data;
		for (j = 0; j < 512; j++) {
			sbi->bfree_bitmap[j] = le64_to_cpu(bitmap[j]);
		}
		brelse(bitmap_bh);
	}
	sb->s_fs_info = sbi;
	// Partie 2

	/* Ask an inode to the VFS */
	root = pnlfs_iget(sb, 0); 
	if (IS_ERR(root)) {
		err = PTR_ERR(root);
		goto exit3;
	}

	/* Set this inode as the root inode */
	inode_init_owner(root, NULL, root->i_mode);
	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		pr_warn("%s Cannot create root inode\n",  __func__);
		err = -ENOMEM;
		goto exit4;
	}
	pr_warn("%s End\n",  __func__);
	return 0;

	exit4:
		iput(root);
	exit3:
		kfree(sbi->bfree_bitmap);
	exit2:
		kfree(sbi->ifree_bitmap);
	exit1:
		kfree(sbi);
	exit:
		return err;
}

/* Function called for mounting, set in file_system_type */
static struct dentry *pnlfs_mount
(struct file_system_type *fs, int flags, const char *dev, void *data)
{
	struct dentry * entry;
	pr_info("%s Start\n",  __func__);
	entry = mount_bdev(fs, flags, dev, data, pnlfs_fill_super);
	if (IS_ERR(entry))
		pr_err("%s Mounting Error\n",  __func__);
	else
		pr_info("%s End\n",  __func__);
	return entry;
}

/* Function called for unmounting, set in file_system_type */
static void pnlfs_kill_super(struct super_block *sb)
{
	pr_info("%s Start\n",  __func__);
	kill_block_super(sb);
	pr_info("%s End\n", __func__);
}

static struct file_system_type pnlfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "pnlfs",
	.mount		= pnlfs_mount,
	.kill_sb	= pnlfs_kill_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

/* Init function */
int __init init_pnlfs_fs(void)
{
	int err;
	pr_info("%s Start\n",  __func__);
	err = register_filesystem(&pnlfs_fs_type);
	if (err)
	{
		pr_err("%s Registering error : %d\n",  __func__, err);
		return err;
	}
	return 0;
	pr_info("%s End\n", __func__);
}

/* Exit function */
void __exit exit_pnlfs_fs(void)
{
	pr_info("%s Start\n", __func__);
	unregister_filesystem(&pnlfs_fs_type);
}

module_init(init_pnlfs_fs);
module_exit(exit_pnlfs_fs);