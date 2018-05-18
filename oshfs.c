#define FUSE_USE_VERSION 26
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/mman.h>

// 1. 设计文件系统块大小blocksize。
// 2. 设计一段连续的文件系统地址空间，文件系统地址空间大小为size，则共有blocknr=size/blocksize个block。
// 3.自行设计存储算法，将文件系统的所有内容（包括元数据和链表指针等）均映射到文件系统地址空间。
// 4.将文件系统地址空间切分为blocknr个大小为blocksize的块，并在需要时将对应块通过mmap映射到进程地址空间，在不再需要时通过munmap释放对应内存。
// 5.（选做）选做内容包括但不限于：实现目录操作、修改文件的各种元数据、健壮的错误处理等。


//On  success, mmap() returns a pointer to the mapped area.  On error, the value MAP_FAILED (that is, (void *) -1) is returned, and errno is set to indicate the cause of the error.

struct LinkList {
    char *mem_block;
    struct LinkList *prev;
    struct LinkList * next;
};

struct filenode {
    char *filename;
    struct LinkList *content;
    struct LinkList *heap;
    struct stat *st;
    struct filenode *next;
    struct filenode *prev;
};

#define MEMNUM (64*1024)
static char * mem_space_starting_point;
static size_t size = 4 * 1024 * 1024 *(size_t) 1024;
  

static size_t blocksize;
static size_t blocknr;
static int offset_to_blocksize;
static int offset_to_blocknr;
static int offset_to_root;
static int offset_to_LinkListAddressSpace;
static int offset_to_FileNodeAddressSpace;
static int offset_to_aMemHead;
static int offset_to_aMemHeap;
static int offset_to_MemSpace;

int State;

#define Initialize 0
#define Malloc 1
#define Free 2

#define MemFree 0
#define DataAppend 1
#define MemUse 2
#define MemTableInit 3

#define Writing 1
#define NotWriting 2

#define Reading 1
#define NotReading 2

static struct filenode * file_node_address_space_init(int mode, struct filenode * pass, size_t space_scale){
    size_t headersize;
    headersize = sizeof(struct filenode *);
    char * file_node_address_space = *(char **)(mem_space_starting_point + offset_to_FileNodeAddressSpace);
    switch (mode)
    {
        case Initialize:{
            struct filenode *temp, *last;
            size_t nodesize = (sizeof(struct filenode) + 256 + sizeof(struct stat)), nodenr = 0;

            nodenr = space_scale / nodesize;
            
            memset(file_node_address_space, 0, space_scale);

            file_node_address_space += headersize;

            if (pass) 
                pass->next = (struct filenode*)file_node_address_space;

            memcpy(file_node_address_space - headersize, &file_node_address_space, headersize);

            last = NULL;
            for(long i = 0; i < nodenr; i++){
                temp = (struct filenode *)(file_node_address_space + i * nodesize);
                if(last)
                    last->next = temp;
                temp->next = NULL;
                temp->prev = last;
                last = temp;
            }
            return last;
        }
        case Malloc:{
            struct filenode * pointer_to_available;
            pointer_to_available = *(struct filenode **)(file_node_address_space);
            if(!pointer_to_available)
                return NULL;
            memcpy(file_node_address_space, &(pointer_to_available->next), headersize);
            if(pointer_to_available -> next){
            pointer_to_available->next -> prev = NULL;
            pointer_to_available->next = NULL;  
            }
            return pointer_to_available;
        }
        case Free:{
            if(!pass)
                break;
            
            struct filenode * pointer_to_available = *(struct filenode **)(file_node_address_space);
            pass->next = pointer_to_available;
            pass->prev = NULL;
            memcpy(file_node_address_space, &pass, headersize);
        }break;
        default:
            break;
    }
    return NULL;
}

static struct LinkList* link_list_address_space_init(int mode, struct LinkList * pass, size_t space_scale){
    size_t headersize;
    headersize = sizeof(struct LinkList *);
    char * link_list_address_space = *(char **)(mem_space_starting_point + offset_to_LinkListAddressSpace);
    switch (mode)
    {
        case Initialize:{
            struct LinkList *temp, *last;

            size_t nodesize = sizeof(struct LinkList), nodenr;
            
            nodenr = space_scale / nodesize;

            memset(link_list_address_space, 0, space_scale);

            link_list_address_space += headersize;

            if (pass) 
                pass->next = (struct LinkList*)link_list_address_space;

            memcpy((link_list_address_space - headersize), &link_list_address_space, headersize);

            last = NULL;
            for(long i = 0; i < nodenr; i++){
                temp = (struct LinkList *)(link_list_address_space + i * nodesize);
                if(last)
                    last->next = temp;
                temp->next = NULL;  
                temp->prev = last;
                last = temp;
            }
            return last;
        }
        case Malloc:{
            struct LinkList * pointer_to_available = *(struct LinkList **)(link_list_address_space);
            if(!pointer_to_available)
                return NULL; 
            memcpy(link_list_address_space, &(pointer_to_available->next), headersize);
            if(pointer_to_available -> next){
            pointer_to_available->next -> prev = NULL;
            pointer_to_available->next = NULL;  
            }
            return pointer_to_available;
        }
        case Free:{
            if(!pass)
                break;
            struct LinkList * pointer_to_available = *(struct LinkList **)(link_list_address_space);
            pass->next = pointer_to_available;
            pass->prev = NULL;
            memcpy(link_list_address_space, &pass, headersize);

        }break;
        default:
            break;
    }
    return NULL;
}

static struct LinkList* append_link_list(char * mem_block, struct LinkList *heap, int mode){
    if(mem_block == NULL)         //若是文件内容的头节点
        return heap;
    heap->next = link_list_address_space_init(Malloc, NULL, 0);
    if(heap->next){
        heap->next->prev = heap;
        heap = heap->next;
        heap->mem_block = mem_block;
        heap->next = NULL;
    }
    switch (mode)
    {
        case MemFree: 
            munmap(mem_block, blocksize);
            break;
        case MemUse :
            mem_block = mmap(mem_block, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            break;
        case DataAppend: 
            break;
        case MemTableInit:
            break;
        default:
            break;
    }
    return heap;
}

static char * pop_link_list(struct LinkList *head){
    struct LinkList* temp= head->next;
    if (!head || !temp)
        return NULL;
    char *mem_block = temp->mem_block;
    if(temp->next){
        temp->next->prev = head;
        head->next = temp->next;
        link_list_address_space_init(Free, temp, 0);
        return mem_block;
    }
    else{
        struct LinkList **pointer_to_aMemHead, **pointer_to_aMemHeap;
        pointer_to_aMemHead = (struct LinkList **)(mem_space_starting_point + offset_to_aMemHead);
        pointer_to_aMemHeap = (struct LinkList **)(mem_space_starting_point + offset_to_aMemHeap);
        head->next = temp->next;
        if(head == *pointer_to_aMemHead)
            *pointer_to_aMemHeap = *pointer_to_aMemHead;
        link_list_address_space_init(Free, temp, 0);
        return mem_block;
    }
}

static struct LinkList * del_link_list_from_tail(struct LinkList * old_heap){
    struct LinkList * heap = old_heap;
    if(heap->mem_block == NULL || heap->prev == NULL) 
        return heap;
    else{
        heap = old_heap->prev;
        heap->next = NULL;
        link_list_address_space_init(Free, old_heap, 0);
        return heap;
    }
}

static struct LinkList * get_or_create_link_list_node(int offset, struct LinkList *node, struct LinkList ** pointer_to_heap, int flag){
    struct LinkList * aMemHead;
    aMemHead = *(struct LinkList **)(mem_space_starting_point + offset_to_aMemHead);
    while(offset >= 0 && node != NULL){
        struct LinkList *temp;
        temp = node;
        node = node->next ;
        offset--;
        if(node == NULL && flag == 1){
            node = append_link_list(pop_link_list(aMemHead), temp, MemUse);
            if(temp == node || node == NULL)
                return NULL;
            *pointer_to_heap = node;
        }
    }
    return node;
}

static struct filenode *get_filenode(const char *name)
{
    struct filenode *node;
    memcpy(&node, mem_space_starting_point + offset_to_root, sizeof(struct filenode *));
    while(node) {
        if(strcmp(node->filename, name + 1) != 0)
            node = node->next;
        else
            return node;
    }
    return NULL;
}

static void create_filenode(const char *filename, const struct stat *st)
{ 
    struct filenode *new = file_node_address_space_init(Malloc, NULL, 0), **pointer_to_root;
    pointer_to_root = (struct filenode **)(mem_space_starting_point + offset_to_root);
    new->filename = (char *)((char *)new + sizeof(struct filenode));   //存文件名
    memcpy(new->filename, filename, strlen(filename) + 1);
    new->st = (struct stat *)((char *)new + 256);   //存文件状态
    memcpy(new->st, st, sizeof(struct stat));
    new->prev = NULL;
    new->next = *pointer_to_root;
    if(*pointer_to_root != NULL)
        (*pointer_to_root)->prev = new;                                       //创建链表
    new->content = link_list_address_space_init(Malloc, NULL, 0 );
    new->content->mem_block = NULL;
    new->heap = new->content;
    new->content->next = NULL;
    new->content->prev = NULL;
    *pointer_to_root = new;
}

// rewrite filenode root, aMemHead, aMemHeap, LinkListAddressSpace, FileNodeAddressSpace;

static void *oshfs_init(struct fuse_conn_info *conn)
{
    static char *mem[MEMNUM];  
    offset_to_blocksize = 0;
    offset_to_blocknr = sizeof(size_t);
    offset_to_root = 2 * sizeof(size_t);
    offset_to_FileNodeAddressSpace = offset_to_root + sizeof(struct filenode *);
    offset_to_LinkListAddressSpace = offset_to_FileNodeAddressSpace + sizeof( char *);
    offset_to_aMemHead = offset_to_LinkListAddressSpace + sizeof(char *);
    offset_to_aMemHeap = offset_to_aMemHead + sizeof(struct LinkList *);
    offset_to_MemSpace = offset_to_aMemHeap + sizeof(struct LinkList *);

    blocksize = size / (sizeof(mem)/sizeof(mem[0]));
    blocknr = (size - 256 - offset_to_MemSpace - 16) / (blocksize + (sizeof(struct filenode) + 256 + sizeof(struct stat)) + 3 * sizeof(struct LinkList)) + 2;
    mem_space_starting_point = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    memcpy(mem_space_starting_point + offset_to_blocksize, &blocksize, sizeof(size_t));
    
    memcpy(mem_space_starting_point + offset_to_blocknr, &blocknr, sizeof(size_t));
    
    mem[0] = mem_space_starting_point + offset_to_MemSpace;
    for(int i = 0 ; i < blocknr - 2; i++){
        mem[i] = (char *)mem[0] + i * blocksize;
    }

    char * FileNodeAddressSpace, * LinkListAddressSpace;

    memset(mem_space_starting_point + offset_to_root, 0, sizeof(struct filenode *));

    size_t scale_of_file_node_address_space = 8 + (sizeof(struct filenode) + 256 + sizeof(struct stat)) * (blocknr - 2);    
    FileNodeAddressSpace = mem_space_starting_point + size - scale_of_file_node_address_space;
    memcpy(mem_space_starting_point + offset_to_FileNodeAddressSpace, &FileNodeAddressSpace, sizeof(char *));
    file_node_address_space_init(Initialize, NULL, scale_of_file_node_address_space);

    size_t scale_of_link_list_address_space = 8 + (3 * (blocknr - 2) + 1) * sizeof(struct LinkList);    
    
    LinkListAddressSpace = FileNodeAddressSpace - scale_of_link_list_address_space - 128;
    memcpy(mem_space_starting_point + offset_to_LinkListAddressSpace, &LinkListAddressSpace, sizeof(char *));
    link_list_address_space_init(Initialize, NULL, scale_of_link_list_address_space);

    struct LinkList *aMemHead, *aMemHeap;
    
    aMemHead = link_list_address_space_init(Malloc, NULL, 0);
    aMemHeap = aMemHead;

    for(int i = 0; i < blocknr - 2; i++) {
        munmap(mem[i], blocksize);
        aMemHeap = append_link_list(mem[i], aMemHeap, MemTableInit);
    }
    aMemHead->mem_block = NULL;
    aMemHead->prev = NULL;
    memcpy(mem_space_starting_point + offset_to_aMemHead, &aMemHead, sizeof(aMemHead));
    memcpy(mem_space_starting_point + offset_to_aMemHeap, &aMemHeap, sizeof(aMemHeap));
    return NULL;
}

static int oshfs_getattr(const char *path, struct stat *stbuf)
{
    int ret = 0;
    struct filenode *node = get_filenode(path);  
    if(strcmp(path, "/") == 0) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;
    } else if(node) {
        memcpy(stbuf, node->st, sizeof(struct stat));
    } else {
        ret = -ENOENT;
    }
    return ret;
}

static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    struct filenode *node , * root;
    memcpy(&root, mem_space_starting_point + offset_to_root, sizeof(struct filenode *));
    node = root;
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    while(node) {
        filler(buf, node->filename, node->st, 0);
        node = node->next;
    }
    return 0;
}

static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)
{
    struct stat st;
    time_t now;
    struct tm *timenow;
    size_t blocksize = *((int *)(mem_space_starting_point + offset_to_blocksize));
    time(&now);
    st.st_mode = S_IFREG | 0644;
    st.st_uid = fuse_get_context()->uid;
    st.st_gid = fuse_get_context()->gid;
    st.st_nlink = 1;
    st.st_size = 0;
    st.st_atime = now;
    st.st_ctime = now;
    st.st_mtime = now;
    st.st_blksize = blocksize;
    st.st_blocks = 0;
    st.st_dev = dev;
    create_filenode(path + 1, &st);
    return 0;
}

static int oshfs_open(const char *path, struct fuse_file_info *fi)
{
    State = NotWriting;
    time_t now;
    time(&now);

    struct filenode *file_opened = NULL;
    file_opened = get_filenode(path);
    if(file_opened){
        file_opened->st->st_atime = now;
    }
    return 0;
}


struct LinkList *block_writing = NULL;
struct filenode * FileNodeWriting = NULL;
size_t block_offset = 0;
static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    if (State == NotWriting){
        FileNodeWriting = get_filenode(path);
        blocksize = *((int *)(mem_space_starting_point + offset_to_blocksize));
        block_offset = offset/blocksize;
        block_writing = get_or_create_link_list_node(block_offset, FileNodeWriting->content, &FileNodeWriting->heap, 1);
        if(FileNodeWriting){
            time_t now;
            time(&now);
            FileNodeWriting->st->st_mtime = now;
        }
        State = Writing;
    }
    if(!FileNodeWriting)
        return -ENOENT;
    size_t buf_covered = 0, next_block_offset = 0;  
    if (size <= 0)
        return size;
    while(buf_covered < size){
        next_block_offset = (offset + buf_covered)/blocksize;
        if(next_block_offset == block_offset + 1){
            block_writing = get_or_create_link_list_node(0, block_writing, &FileNodeWriting->heap, 1);
        }
        else {
            if (block_offset != next_block_offset)
                block_writing = get_or_create_link_list_node(next_block_offset, FileNodeWriting->content, &FileNodeWriting->heap, 1);
        }
        block_offset = next_block_offset;
        if(block_writing == NULL || block_writing->mem_block == NULL)
            return -ENOSPC;
        if(size - buf_covered <= blocksize - (offset + buf_covered) % blocksize){
            memcpy(block_writing->mem_block + (offset + buf_covered) % blocksize, buf + buf_covered, size - buf_covered);
            buf_covered = size;
        }
        else{
            memcpy(block_writing->mem_block + (offset + buf_covered) % blocksize, buf + buf_covered, blocksize - (offset + buf_covered) % blocksize);
            buf_covered += blocksize - (offset + buf_covered) % blocksize; 
        }
        
    }
    FileNodeWriting->st->st_size = offset + size;
    FileNodeWriting->st->st_blocks = (offset + size) / blocksize + 1;
    return size;  
}

static int oshfs_truncate(const char *path, off_t size)
{
    struct filenode *node = get_filenode(path);
    size_t blocksize = *((int *)(mem_space_starting_point + offset_to_blocksize));
    struct LinkList **pointer_to_aMemHead, **pointer_to_aMemHeap;
    pointer_to_aMemHead = (struct LinkList **)(mem_space_starting_point + offset_to_aMemHead);
    pointer_to_aMemHeap = (struct LinkList **)(mem_space_starting_point + offset_to_aMemHeap);
    if(size > node->st->st_size){
        for(int i = size / blocksize - node->st->st_size / blocksize + ((size % blocksize == 0)?0:1); i > 0 ; i--)
            node->heap = append_link_list(pop_link_list(*pointer_to_aMemHead), node->heap, MemUse);
    }
    else {
        for(int i = node->st->st_size / blocksize - size / blocksize - ((size % blocksize == 0)?0:1); i > 0 ; i--){
            *pointer_to_aMemHeap = append_link_list(node->heap->mem_block, *pointer_to_aMemHeap, MemFree);
            node->heap = del_link_list_from_tail(node->heap);
        }
    }
    node->st->st_size = size;
    return 0;
}


struct filenode *FileNodeReading = NULL;
struct LinkList *block_reading = NULL;
static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    if (State == NotReading){
        FileNodeReading = get_filenode(path);
        blocksize = *((int *)(mem_space_starting_point + offset_to_blocksize));
        block_offset = offset/blocksize;
        block_reading = get_or_create_link_list_node(block_offset, FileNodeReading->content, &FileNodeWriting->heap, 0);
        State = Reading;
    }
    if(!FileNodeReading)
        return -ENOENT;
        
    size_t buf_covered = 0, ret = 0, next_block_offset = 0;
    ret = FileNodeReading->st->st_size < size + offset ? (FileNodeReading->st->st_size - offset) : size;
    if (ret == 0)
        return size;
    while(buf_covered < ret){
        next_block_offset = (offset + buf_covered)/blocksize;
        if(next_block_offset == block_offset + 1){
            block_reading = get_or_create_link_list_node(0, block_reading, &FileNodeReading->heap, 0);
        }
        else {
            if (block_offset != next_block_offset)
                block_reading = get_or_create_link_list_node(next_block_offset, FileNodeReading->content, &FileNodeReading->heap, 0);
        }
        block_offset = next_block_offset;
        if(block_reading == NULL || block_reading->mem_block == NULL)
            return 0;
        
        if(ret - buf_covered <= blocksize - (offset + buf_covered) % blocksize){
            memcpy(buf + buf_covered, block_reading->mem_block + (offset + buf_covered) % blocksize, ret - buf_covered);
            buf_covered = size;
        }
        else{
            memcpy(buf + buf_covered, block_reading->mem_block + (offset + buf_covered) % blocksize, blocksize - (offset + buf_covered) % blocksize);
            buf_covered += blocksize - (offset + buf_covered) % blocksize; 
        }
    }
    return ret;  
}

static int oshfs_unlink(const char *path)
{  
    struct filenode *file_to_unlink, **pointer_to_root;
    struct LinkList *data,*next , **pointer_to_aMemHeap, **pointer_to_aMemHead;
    file_to_unlink = get_filenode(path);

    if(file_to_unlink == NULL)
        return 0;
    pointer_to_aMemHeap = (struct LinkList **)(mem_space_starting_point + offset_to_aMemHeap);
    pointer_to_root = (struct filenode **)(mem_space_starting_point + offset_to_root);
    data = file_to_unlink->content;    //content
    while(data != NULL){
        next = data->next;
        *pointer_to_aMemHeap = append_link_list(data->mem_block, *pointer_to_aMemHeap, MemFree);
        link_list_address_space_init(Free, data, 0);
        data = next;
    } 
    if (file_to_unlink->prev) 
        file_to_unlink->prev->next = file_to_unlink->next;
    if (file_to_unlink->next)
        file_to_unlink->next->prev = file_to_unlink->prev;
    if(file_to_unlink == *pointer_to_root)
        *pointer_to_root = (*pointer_to_root)->next;
    file_node_address_space_init(Free, file_to_unlink, 0);
    
    return 0;
}

static const struct fuse_operations op = {
    .init = oshfs_init,
    .getattr = oshfs_getattr,
    .readdir = oshfs_readdir,
    .mknod = oshfs_mknod,
    .open = oshfs_open,
    .write = oshfs_write,
    .truncate = oshfs_truncate,
    .read = oshfs_read,
    .unlink = oshfs_unlink,
};

int main(int argc, char *argv[])
{
    // char *s[]={"/media/guanxiux/Data/OSH/3-guanxiux","-f", "-s" ,"mountpoint"};
    // return fuse_main(4, s, &op, NULL);
    return fuse_main(argc, argv, &op, NULL);
}