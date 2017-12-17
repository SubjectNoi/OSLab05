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
FILE    *sf;
FILE    *df;
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

typedef struct __read_file_pos {
    int     data_block_idx;
    int     global_idx;
} RFP;

typedef struct __write_file_pos {
    char    bf[MAX_FILE_LEN];
    int     global_idx;
} WFP;

char source_file[MAX_FILE_LEN], dest_file[MAX_FILE_LEN];

void access_iblocks(RFP* rfp, FILE *src, int type, int iblock, INODE* in) {
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
                rfp[dblock_num].global_idx = dblock_num++;
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

int cmp_physical(const void* a, const void* b) {
    return ((RFP*)a)->data_block_idx > ((RFP*)b)->data_block_idx;
}
int cmp_logical(const void* a, const void* b) {
    return ((WFP*)a)->global_idx > ((WFP*)b)->global_idx;
}

void process_File(INODE *in, FILE *src, FILE *dest) {
    int     file_block_num = (in->size - 1) / block_size + 1;
    int     idx_block_num = (file_block_num - N_DBLOCKS) / index_num + 1;
    long    cur_file_pos;
    RFP rfp[file_block_num];
    WFP wfp[file_block_num];

    int i;
    for (i = 0; i < N_DBLOCKS; i++) {
        if (in->dblocks[i]) {
            rfp[dblock_num].data_block_idx = in->dblocks[i];
            rfp[dblock_num].global_idx = dblock_num++;
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

    qsort(rfp, (size_t)file_block_num, sizeof(RFP), cmp_physical);
    cur_file_pos = ftell(src);
    fseek(src, 0, SEEK_SET);
    for (i = 0; i < dblock_num; i++) {
        int offset = i == 0 ? 6 : -(rfp[i - 1].data_block_idx - 1);
        fseek(src, (rfp[i].data_block_idx + offset) * block_size, SEEK_CUR);
        fread(&wfp[i].bf, block_size, 0, src);
        printf("%s\n", wfp[i].bf);
    }
    fseek(src, cur_file_pos, SEEK_SET);
    for (i = 0; i < dblock_num; i++) {
        wfp[i].global_idx = rfp[i].global_idx;
    }

    qsort(wfp, (size_t)file_block_num, sizeof(WFP), cmp_logical);
    fseek(dest, 0, SEEK_END);
    for (i = 0; i < dblock_num; i++) {
        fwrite(&wfp[i].bf, block_size, 1, dest);
    }

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
    //char *info = "\nFUCKYOURMATHER!FUCKYOURMATHER!FUCKYOURMATHER!FUCKYOURMATHER!FUCKYOURMATHER!FUCKYOURMATHER!FUCKYOURMATHER!FUCKYOURMATHER!FUCKYOURMATHER!FUCKYOURMATHER!FUCKYOURMATHER!FUCKYOURMATHER!FUCKYOURMATHER!FUCKYOURMATHER!\n\0";
    //fwrite(info, strlen(info), 1, df);
    fseek(sf, 2 * block_size, SEEK_SET);
    free(in);
    free(sb);
    int size = 0;
    // Process every i-nodes
    for (i = 0; i < inode_num; i++) {
        dblock_num = 0;
        INODE* in = (INODE*)malloc(sizeof(INODE));
        fread(in, sizeof(INODE), 1, sf);
        size += in->size;
        if (!in->nlink) continue;
        else {
            process_File(in, sf, df);
        }
    }
    printf("%d\n", file_size);
    printf("%d", size);
    fclose(sf);
    fclose(df);
    return 0;
}
