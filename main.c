#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N_DBLOCKS       10
#define N_IBLOCKS       4
#define MAX_FILE_LEN    512

long    file_size   = 0;    // File size
int     block_size  = 0;    // Block size, here is 512
int     inode_num   = 0;    // Number of i-node, here is 20
int     dblock_num  = 0;    // Number of block data occupy every file
int     block_num   = 0;    // Number of block data occupy total
int     data_offset = 0;    // Data offset from beginning of the file
int     index_num   = 0;    // Number of index in a block
FILE    *sf;                // Origin file of disk image
FILE    *df;                // Processed file of disk image

typedef struct __super_block {
    int size;
    int inode_offset;
    int data_offset;
    int swap_offset;
    int free_inode;
    int free_iblock;
} SUPER_BLOCK;

typedef struct __inode {
    int next_inode;
    int protect;
    int nlink;
    int size;
    int uid;
    int gid;
    int ctime;
    int mtime;
    int atime;
    int dblocks[N_DBLOCKS];
    int iblocks[N_IBLOCKS];
    int i2block;
    int i3block;
} INODE;

typedef struct __read_file_pos {                      // Record the physical and logical position of a every blocks in one file
    int     data_block_idx;                           // Physical position of a block
    int     global_idx;                               // Logical position of a block, used to unify the blocks into a correct file
} RFP;

typedef struct __write_file_pos {                     // Record the content and logical position to write into the new disk image
    char    bf[MAX_FILE_LEN];                         // Content of block
    int     global_idx;                               // Logical position of a block, used to unify the blocks into a correct file
} WFP;

//   Origin disk image          Processed disk image     Files contained by the disk
char source_file[MAX_FILE_LEN], dest_file[MAX_FILE_LEN], target[MAX_FILE_LEN];

/*
 * @brief       access the inode, record iblocks offset with RFP struct
 * @param       rfp             struct used to record the iblocks offset of current file
 * @param       src             target disk image
 * @param       type            distinct the type of index, 1 for 1-index, 2 for 2-index, 3 for 3-index
 * @param       iblock          used only for 1-index, distinct the start position of 1-index (iblocks[0:3])
 * @param       in              inode of current file
 *
 * @return      NULL
 *
 */
void access_iblocks(RFP* rfp, FILE* src, int type, int iblock, INODE* in) {
    int     i;
    long    cur_file_pointer;
    int     iblocks[index_num];
    switch (type) {
        case 1:
            cur_file_pointer = ftell(src);
            fseek(src, (iblock + data_offset) * block_size, SEEK_SET);
            for (i = 0; i < index_num; i++) {
                fread(&rfp[dblock_num].data_block_idx, sizeof(int), 1, src);
                if (rfp[dblock_num].data_block_idx > (file_size / block_size) || rfp[dblock_num].data_block_idx <= 0) break;
                rfp[dblock_num].global_idx = dblock_num;
                dblock_num++;
            }
            fseek(src, cur_file_pointer, SEEK_SET);
            break;
        case 2:
            cur_file_pointer = ftell(src);
            fseek(src, (in->i2block + data_offset) * block_size, SEEK_SET);
            for (i = 0; i < index_num; i++) {
                fread(&iblocks[i], sizeof(int), 1, src);
                access_iblocks(rfp, src, 1, iblocks[i], NULL);
            }
            fseek(src, cur_file_pointer, SEEK_SET);
            break;
        case 3:
            cur_file_pointer = ftell(src);
            fseek(src, (in->i3block + data_offset) * block_size, SEEK_SET);
            for (i = 0; i < index_num; i++) {
                fread(&iblocks[i], sizeof(int), 1, src);
                access_iblocks(rfp, src, 2, 0, in);
            }
            fseek(src, cur_file_pointer, SEEK_SET);
            break;
        default: break;
    }
}

/*
 * @brief       modify the iblocks after defrag
 * @param       file_block_num  rest blocks need to be processed
 * @param       target disk     image
 * @param       type            distinct the type of index, 1 for 1-index, 2 for 2-index, 3 for 3-index
 * @param       dblock_idx      current data block offset
 * @param       iblock_idx      current index block offset
 * @param       cnt             round, used only for 1-index [0:127]
 *
 * @return      NULL
 *
 */
void modify_iblocks(int file_block_num, FILE* dest, int type, int* dblock_idx, int* iblock_idx, int cnt) {
    int     i;
    long    cur_file_pointer;

    switch (type) {
        case 1:
            cur_file_pointer = ftell(dest);
            fseek(dest, (*iblock_idx + data_offset) * block_size, SEEK_SET);
            for (i = 0; i < index_num; i++) {
                int tmp;
                if (i + (cnt) * index_num < file_block_num) {
                    tmp = (*dblock_idx)++;
                }
                else {
                    tmp = 0;
                }
                fwrite(&tmp, sizeof(int), 1, dest);
            }
            (*iblock_idx)++;
            fseek(dest, cur_file_pointer, SEEK_SET);
            break;
        case 2:
            cur_file_pointer = ftell(dest);
            fseek(dest, (((*iblock_idx)++) + data_offset) * block_size, SEEK_SET);
            for (i = 0; i < index_num; i++) {
                int tmp;
                if (i < file_block_num) {
                    tmp = *iblock_idx;
                    modify_iblocks(file_block_num, dest, 1, dblock_idx, iblock_idx, i);
                }
                else {
                    tmp = 0;
                }
                fwrite(&tmp, sizeof(int), 1, dest);
            }
            fseek(dest, cur_file_pointer, SEEK_SET);
            break;
        case 3:
            cur_file_pointer = ftell(dest);
            fseek(dest, (((*iblock_idx)++) + data_offset) * block_size, SEEK_SET);
            for (i = 0; i < index_num; i++) {
                int tmp;
                if (i < file_block_num) {
                    tmp = *iblock_idx;
                    modify_iblocks(file_block_num, dest, 2, dblock_idx, iblock_idx, 0);
                }
                else {
                    tmp = 0;
                }
                fwrite(&tmp, sizeof(int), 1, dest);
            }
            fseek(dest, cur_file_pointer, SEEK_SET);
            break;
        default: break;
    }
}

/*
 * @brief       used for qsort(), sort blocks by physical position
 * @param       a               first op
 * @param       b               second op
 *
 * @return      int             >0 to swap, <0 don't swap
 */
int cmp_physical(const void* a, const void* b) {
    return ((RFP*)a)->data_block_idx > ((RFP*)b)->data_block_idx;
}

/*
 * @brief       used for qsort(), sort blocks by logical position
 * @param       a               first op
 * @param       b               second op
 *
 * @return      int             >0 to swap, <0 don't swap
 */
int cmp_logical(const void* a, const void* b) {
    return ((WFP*)a)->global_idx > ((WFP*)b)->global_idx;
}

/*
 * @brief       process every inode, write into new disk image, also extract it to a real file
 * @param       in              target inode to be processed
 * @param       src             Origin disk image
 * @param       dest            target disk image
 * @param       new_file        real file of target inode
 *
 * @return      NULL
 */
void process_File(INODE *in, FILE *src, FILE *dest,  FILE *new_file) {
    int     file_block_num = (in->size - 1) / block_size + 1;
    int     idx_block_num = (file_block_num - N_DBLOCKS) / index_num + 1;
    long    cur_file_pos;
    RFP     rfp[file_block_num];
    WFP     wfp[file_block_num];

    dblock_num = 0;
    int i;
    for (i = 0; i < N_DBLOCKS; i++) {
        if (in->dblocks[i]) {
            rfp[dblock_num].data_block_idx = in->dblocks[i];
            rfp[dblock_num].global_idx = dblock_num;
            dblock_num++;
        }
    }
    if (file_block_num > N_DBLOCKS) {
        for (i = 0; i < idx_block_num; i++) {
            if (i < N_IBLOCKS) access_iblocks(rfp, src, 1, in->iblocks[i], NULL);
        }
    }

    if (file_block_num > N_DBLOCKS + N_IBLOCKS * index_num) {
        access_iblocks(rfp, src, 2, 0, in);
    }

    if (file_block_num > N_DBLOCKS + N_IBLOCKS * index_num + index_num * index_num) {
        access_iblocks(rfp, src, 3, 0, in);
    }

    // Sort all dblocks of this file by pyhsical position to read the content more convenient.
    qsort(rfp, (size_t)file_block_num, sizeof(RFP), cmp_physical);
    cur_file_pos = ftell(src);
    fseek(src, 0, SEEK_SET);
    for (i = 0; i < dblock_num; i++) {
        int offset = i == 0 ? data_offset : -(rfp[i - 1].data_block_idx + 1);
        fseek(src, (rfp[i].data_block_idx + offset) * block_size, SEEK_CUR);
        fread(&wfp[i].bf, block_size, 1, src);
    }
    fseek(src, cur_file_pos, SEEK_SET);
    for (i = 0; i < dblock_num; i++) {
        wfp[i].global_idx = rfp[i].global_idx;
    }
    // Sort all dblocks of this file by logical position to ensure the correctness of the file
    qsort(wfp, (size_t)file_block_num, sizeof(WFP), cmp_logical);
    fseek(dest, 0, SEEK_END);
    //fseek(new_file, 0, SEEK_END);
    for (i = 0; i < dblock_num; i++) {
        fwrite(&wfp[i].bf, block_size, 1, dest);
        fwrite(&wfp[i].bf, block_size, 1, new_file);
    }

}

/*
 * @brief       update every inode after rearrangement
 * @param       in              target inode to be processed
 * @param       dest            target disk image
 * @param       dblock_idx      current data block offset
 * @param       iblock_idx      current index block offset
 *
 * @return      NULL
 */
void update_inodes(INODE *in, FILE *dest, int* dblock_idx, int* iblock_idx) {
    int     file_block_num = ((in->size) - 1) / block_size + 1;
    int     idx_block_num = (file_block_num - N_DBLOCKS) / index_num + 1;
    int     i;

    for (i = 0; i < N_DBLOCKS; i++) {
        if (!in->dblocks[i]) break;
        in->dblocks[i] = (*dblock_idx)++;
    }

    if (file_block_num > N_DBLOCKS) {
        for (i = 0; i < idx_block_num; i++) {
            if (i < N_IBLOCKS) {
                in->iblocks[i] = *iblock_idx;
                modify_iblocks(file_block_num - N_DBLOCKS, dest, 1, dblock_idx, iblock_idx, i);
            }
        }
    }

    if (file_block_num > N_DBLOCKS + N_IBLOCKS * index_num) {
        in->i2block = *iblock_idx;
        modify_iblocks(file_block_num - N_DBLOCKS - N_IBLOCKS * index_num, dest, 2, dblock_idx, iblock_idx, 0);
    }

    if (file_block_num > N_DBLOCKS + N_IBLOCKS * index_num + index_num * index_num) {
        in->i3block = *iblock_idx;
        modify_iblocks(file_block_num - N_DBLOCKS - N_IBLOCKS * index_num - index_num * index_num, dest, 3, dblock_idx, iblock_idx, 0);
    }
}

/*
 * @brief       convert integer to string
 * @param       src             integer to be converted
 * @param       buf             buffer
 *
 * @return      NULL
 */
void ITOA(int src, char* buf) {
    int i = 0;
    int st = 0;
    int ed = 0;
    while (src) {
        buf[i] = (char)((src % 10) + '0');
        src /= 10;
        i++;
    }
    ed = i - 1;
    while (st < ed) {
        char tmp = buf[st];
        buf[st] = buf[ed];
        buf[ed] = tmp;
        st++;
        ed--;
    }
    buf[i] = '\0';
}


int main(int argc, char* argv[]) {
    strcpy(source_file, argv[1]);
    strcpy(dest_file, argv[1]);
    strcpy(dest_file + strlen(argv[1]), "-defrag");

    FILE *sf;
    if ((sf = fopen(source_file, "r")) == NULL) {
        perror("File open failed.\n");
    }
    else {
        fseek(sf, 0, SEEK_END);
        file_size = ftell(sf);
        fseek(sf, 0, SEEK_SET);
    }
    FILE *df;
    if ((df = fopen(dest_file, "w+")) == NULL) {
        perror("File open failed.\n");
    }

    // Read boot from source file.
    void *boot = malloc(512);
    fread(boot, 512, 1, sf);
    fwrite(boot, 512, 1, df);
    data_offset++;
    free(boot);

    // Read super block from source file.
    void *sb = malloc(512);
    fread(sb, 512, 1, sf);
    fwrite(sb, 512, 1, df);
    data_offset++;
    block_size = ((SUPER_BLOCK*)sb)->size;
    inode_num = (block_size * (((SUPER_BLOCK*)sb)->data_offset - ((SUPER_BLOCK*)sb)->inode_offset)) / sizeof(INODE);
    data_offset += ((SUPER_BLOCK*)sb)->data_offset - ((SUPER_BLOCK*)sb)->inode_offset;
    index_num = block_size / 4;
    printf("Before: %8d Free Blocks.\n", (int)(file_size / block_size - ((SUPER_BLOCK*)sb)->free_iblock));
    // Read i-nodes from source file, beginning: 0 * 512 + 1024, end = 4 * 512 + 1024
    // i-nodes: [1024, 3172], 2048 - x * sizeof(INODE) < block_size, x = 20, 20 i-nodes in total
    int i;
    void* in = malloc(sizeof(INODE));
    for (i = 0; i < inode_num; i++) {
        fread(in, sizeof(INODE), 1, sf);
        block_num += (((INODE*)in)->size - 1) / 512 + 1;
        fwrite(in, sizeof(INODE), 1, df);
    }
    fwrite(in, block_size * (((SUPER_BLOCK*)sb)->data_offset - ((SUPER_BLOCK*)sb)->inode_offset) - inode_num * sizeof(INODE), 1, df);
    fwrite(sb, block_size, 1, df);      // To validate the offset 1, cuz 0 is invalid offset

    fseek(sf, 2 * block_size, SEEK_SET);
    free(in);
    free(sb);

    // Process every i-nodes
    INODE* In = (INODE*)malloc(sizeof(INODE));
    for (i = 0; i < inode_num; i++) {
        ITOA(i + 1, (char*)&target);
        fread(In, sizeof(INODE), 1, sf);
        if (!In->nlink) {
            printf("%d\n", i);
            continue;
        }
        else {
            FILE *tar = fopen(target, "w+");
            process_File(In, sf, df, tar);
            fclose(tar);
        }
    }

    // Update every i-nodes after the data blocks are sorted
    int dblock_idx = 1;                 // Between INODES and DBLOCKS, there's a free block, origin offset is 1
    int iblock_idx = block_num + 1;     // IBLOCKS are after DBLOCKS
    fseek(df, 2 * block_size, SEEK_SET);
    for (i = 0; i < inode_num; i++) {
        long cur_file_pointer = ftell(df);
        fread(In, sizeof(INODE), 1, df);
        if (!In->nlink) continue;
        update_inodes(In, df, &dblock_idx, &iblock_idx);
        fseek(df, cur_file_pointer, SEEK_SET);
        fwrite(In, sizeof(INODE), 1, df);
    }
    free(In);

    fseek(df, (iblock_idx + data_offset) * block_size, SEEK_SET);
    void *free_block = malloc(block_size);

    for (i = 0; i < (file_size / block_size) - iblock_idx - data_offset; i++) {
        fwrite(free_block, block_size, 1, df);
    }
    free(free_block);

    SUPER_BLOCK *new_sb = (SUPER_BLOCK*)malloc(block_size);
    fseek(df, block_size, SEEK_SET);
    fread(new_sb, block_size, 1, df);

    new_sb->free_iblock = iblock_idx;
    fseek(df, block_size, SEEK_SET);
    fwrite(new_sb, block_size, 1, df);
    printf("After : %8d Free Blocks.\n", (int)(file_size / block_size - new_sb->free_iblock));
    free(new_sb);

    fclose(sf);
    fclose(df);
    return 0;
}

//EOF
