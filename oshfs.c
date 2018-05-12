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
    long i;
    struct LinkList *prev;
    struct LinkList * next;
} *aMemHeap, *aMemHead;

struct filenode {
    char *filename;
    struct LinkList content;
    struct LinkList *heap;
    struct stat *st;
    struct filenode *next;
    struct filenode *prev;
};

static const size_t size = 4 * 1024 *(size_t) 1024;  //总大小，4GB, 4 * 1024 * 1024 * (size_t)1024
static char *mem[4];   //指向内存位置的指针的数组，size 和 mem 共同决定  blocksize , 64*1024
static size_t blocknr;
static size_t blocksize;
static size_t FileNodeAddressSpace;
static size_t LinkListAddressSpace;
static size_t num_of_file_nodes_in_a_block;
static size_t num_of_link_list_nodes_in_a_block;

#define Initialize 0
#define Malloc 1
#define Free 2

#define MemFree 0
#define DataAppend 1
#define MemUse 2
#define MemTableInit 3

static struct filenode *root = NULL;

static struct filenode * file_node_address_space_init(int mode, struct filenode * pass, int address_space_num, int node_num_to_initialize){
    size_t headersize;
    headersize = sizeof(struct filenode *);
    switch (mode)
    {
        case Initialize:{
            struct filenode *temp, *last;
            char *file_node_address_space = mem[address_space_num];
            size_t nodesize = (sizeof(struct filenode) + 256 + sizeof(stat));
            
            memset(file_node_address_space, 0, blocksize);

            file_node_address_space += headersize;

            if (pass) 
                pass->next = (struct filenode*)file_node_address_space;

            memcpy(mem[address_space_num], &file_node_address_space, headersize);

            last = NULL;
            for(long i = 0; i < node_num_to_initialize; i++){
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
            char *file_node_address_space = mem[FileNodeAddressSpace];
            memcpy(&pointer_to_available, file_node_address_space, headersize);
            if(!pointer_to_available)
                exit(1);
            memcpy(file_node_address_space, &pointer_to_available->next, headersize);
            return pointer_to_available;
        }
        case Free:{
            struct filenode * pointer_to_available;
            char *file_node_address_space = mem[FileNodeAddressSpace];
            memcpy(&pointer_to_available, file_node_address_space, headersize);
            if(!pass)
                break;
            pass->next = pointer_to_available;
            memcpy(file_node_address_space, &pass, headersize);
        }break;
        default:
            break;
    }
    return NULL;
}
static struct LinkList* link_list_address_space_init(int mode, struct LinkList * pass, int address_space_num, int node_num_to_initialize){
    size_t headersize;
    headersize = sizeof(struct LinkList *);
    switch (mode)
    {
        case Initialize:{
            struct LinkList *temp, *last;
            char *link_list_address_space = mem[address_space_num];
            size_t nodesize = sizeof(struct LinkList);
            
            memset(link_list_address_space, 0, blocksize);

            link_list_address_space += headersize;

            if (pass) 
                pass->next = (struct LinkList*)link_list_address_space;

            memcpy(mem[address_space_num], &link_list_address_space, headersize);

            last = NULL;
            for(long i = 0; i < node_num_to_initialize; i++){
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
            struct LinkList * pointer_to_available;
            char *link_list_address_space = mem[LinkListAddressSpace];
            memcpy(&pointer_to_available, link_list_address_space, headersize);
            if(!pointer_to_available)
                exit(1);
            memcpy(link_list_address_space, &pointer_to_available->next, headersize);
            return pointer_to_available;
        }
        case Free:{
            struct LinkList * pointer_to_available;
            char *link_list_address_space = mem[LinkListAddressSpace];
            memcpy(&pointer_to_available, link_list_address_space, headersize);
            if(!pass)
                break;
            pass->next = pointer_to_available;
            memcpy(link_list_address_space, &pass, headersize);

        }break;
        default:
            break;
    }
    return NULL;
}

static struct LinkList* append_link_list(size_t i, struct LinkList *heap, int mode){
    if(i == -1)         //若是文件内容的头节点
        return heap;
    heap->next = link_list_address_space_init(Malloc, NULL, 0, 0);
    heap->next->prev = heap;
    heap = heap->next;
    heap->i = i;
    heap->next = NULL;
    switch (mode)
    {
        case MemFree: 
            munmap(mem[i], blocksize);
            break;
        case MemUse :
            mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
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

static long pop_link_list(struct LinkList *head){
    struct LinkList* temp= head->next;
    if (!head || !temp)
        return -1;
    size_t i = temp->i;
    if(temp->next){
        temp->next->prev = head;
        head->next = temp->next;
        link_list_address_space_init(Free, temp, 0, 0);
        return i;
    }
    else{
        head->next = temp->next;
        if(head == aMemHead)
            aMemHeap = aMemHead;
        link_list_address_space_init(Free, temp, 0, 0);
        return i;
    }
}

static struct LinkList * del_link_list_from_tail(struct LinkList * old_heap){
    struct LinkList * heap = old_heap;
    if(heap->i == -1 || heap->prev == NULL) 
        return heap;
    else{
        heap = old_heap->prev;
        heap->next = NULL;
        link_list_address_space_init(Free, old_heap, 0, 0);
        return heap;
    }
}

static struct LinkList * get_or_create_link_list_node(int offset, struct LinkList *node, struct LinkList ** pointer_to_heap, int flag){
    while(offset >= 0 && node != NULL){
        struct LinkList *temp;
        temp = node;
        node = node->next ;
        offset--;
        if(node == NULL && flag == 1){
            node = append_link_list(pop_link_list(aMemHead), temp, MemUse);
            *pointer_to_heap = node;
        }
    }
    return node;
}

static struct filenode *get_filenode(const char *name)
{
    struct filenode *node = root;
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
    // struct filenode *new = (struct filenode *)malloc(sizeof(struct filenode));
    struct filenode *new = file_node_address_space_init(Malloc, NULL, 0, 0);
    new->filename = (char *)(new + sizeof(struct filenode));   //存文件名
    memcpy(new->filename, filename, strlen(filename) + 1);
    new->st = (struct stat *)(new + 256);   //存文件状态
    memcpy(new->st, st, sizeof(struct stat));
    new->prev = NULL;
    new->next = root;
    if(root != NULL)
        root->prev = new;                                       //创建链表
    new->content.next = NULL;
    new->content.i = -1;
    new->heap = & new->content;
    new->content.prev = NULL;
    root = new;
}

static void *oshfs_init(struct fuse_conn_info *conn)
{
    blocknr = sizeof(mem) / sizeof(mem[0]);
    blocksize = size / blocknr;
    
    num_of_file_nodes_in_a_block = (blocksize - sizeof(struct filenode *)) / (sizeof(struct filenode) + 256 + sizeof(struct stat));
    num_of_link_list_nodes_in_a_block = (blocksize - sizeof(struct LinkList *)) / sizeof(struct LinkList);
    // Demo 1
    int i;
    
    FileNodeAddressSpace = blocknr - 1;

    long node_num_to_initialize;
    node_num_to_initialize = blocknr;
    struct filenode *lastf = NULL;
    for(i = 0 ; node_num_to_initialize > 0 ; node_num_to_initialize -= num_of_file_nodes_in_a_block, i++){
        mem[FileNodeAddressSpace - i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        lastf = file_node_address_space_init(Initialize, lastf, FileNodeAddressSpace - i, num_of_file_nodes_in_a_block);
    }
    
    LinkListAddressSpace = blocknr - 1 - i;
    node_num_to_initialize = blocknr * 2;
    struct LinkList *lastl = NULL;
    for(i = 0 ; node_num_to_initialize > 0 ; node_num_to_initialize -= num_of_file_nodes_in_a_block, i++){
        mem[LinkListAddressSpace - i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        lastl = link_list_address_space_init(Initialize, lastl, LinkListAddressSpace - i, num_of_link_list_nodes_in_a_block);
    }

    aMemHead = link_list_address_space_init(Malloc, NULL, 0,0);
    aMemHeap = aMemHead;

    size_t blocknr_boundry = LinkListAddressSpace - i;

    for(i = 0; i <= blocknr_boundry; i++) {
        aMemHeap = append_link_list(i, aMemHeap, MemTableInit);
    }
    aMemHead->i = blocknr_boundry + 1;
    aMemHead->prev = NULL;
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
    struct filenode *node = root;
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
    time(&now);
    st.st_mode = S_IFREG | 0644;
    st.st_uid = fuse_get_context()->uid;
    st.st_gid = fuse_get_context()->gid;
    st.st_nlink = 1;
    st.st_size = 0;
    st.st_atime = now;
    st.st_ctime = now;
    st.st_mtime = now;
    create_filenode(path + 1, &st);
    return 0;
}

static int oshfs_open(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    struct filenode *node = get_filenode(path);
    node->st->st_size = offset + size;
    size_t temp = size;
    size_t block_num = node->st->st_size/blocksize + 1;

    size_t block_offset = offset/blocksize;   
    size_t length_written = 0;
    size_t length_to_write_first = 0;

    struct LinkList *block_writing;

    block_writing = get_or_create_link_list_node(block_offset, &node->content, &node->heap, 1);

    length_to_write_first = ( (offset % blocksize + size) > blocksize) ? blocksize - offset % blocksize : size ;
    
    memcpy(mem[block_writing->i] + offset%blocksize, buf, length_to_write_first);

    size -= length_to_write_first;

    length_written += length_to_write_first;

    while(size > 0){
        block_writing = get_or_create_link_list_node(0, block_writing, &node->heap, 1);
        if(size < blocksize){
            memcpy(mem[ block_writing -> i ], buf + length_written, size);
            break;
        }
        else
            memcpy(mem[ block_writing -> i], buf + length_written, blocksize);
        length_written += blocksize;
        size -= blocksize;
    }
    
    return temp;
}

static int oshfs_truncate(const char *path, off_t size)
{
    struct filenode *node = get_filenode(path);
    if(size > node->st->st_size){
        for(int i = size / blocksize - node->st->st_size / blocksize; i > 0 ; i--)
            node->heap = append_link_list(pop_link_list(aMemHead), node->heap, MemUse);
    }
    else {
        for(int i = node->st->st_size / blocksize - size / blocksize; i > 0 ; i--){
            aMemHeap = append_link_list(node->heap->i, aMemHeap, MemFree);
            node->heap = del_link_list_from_tail(node->heap);
        }
    }
    node->st->st_size = size;
    return 0;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    struct filenode *node = get_filenode(path);
    int ret = size, sizeread;
    if(offset + size > node->st->st_size)
        ret = node->st->st_size - offset;
    sizeread = ret;
    size_t length_to_read_first, block_offset;
    size_t length_read = 0;
    block_offset = offset / blocksize;
    length_to_read_first = (offset % blocksize + size) > blocksize ? blocksize - offset % blocksize : size;

    struct LinkList * block_being_read;
    block_being_read = get_or_create_link_list_node(block_offset, &node->content, NULL, 0);
    memcpy(buf, mem[block_being_read->i] + offset % blocksize, length_to_read_first);

    ret -= length_to_read_first;
    length_read += length_to_read_first;

    while(ret > 0){
        block_being_read = get_or_create_link_list_node(0, block_being_read, NULL, 0);
        if(ret <= blocksize){
            memcpy(buf + length_read, mem[block_being_read->i], ret);
            break;
        } 
        else
            memcpy(buf + length_read, mem[block_being_read->i], blocksize);
        ret -= blocksize;
        length_read += blocksize;
        
    }
    return sizeread;
}

static int oshfs_unlink(const char *path)
{  
    // Not Implemented
    struct filenode *file_to_unlink;
    struct LinkList *data,*next ;
    file_to_unlink = get_filenode(path);

    if(file_to_unlink == NULL)
        return 0;

    data = &file_to_unlink->content;    //content
    while(data != NULL){
        next = data->next;
        aMemHeap = append_link_list(data->i, aMemHeap, MemFree);
        link_list_address_space_init(Free, data, 0, 0);
        data = next;
    } 
    if (file_to_unlink->prev) 
        file_to_unlink->prev->next = file_to_unlink->next;
    if (file_to_unlink->next)
        file_to_unlink->next->prev = file_to_unlink->prev;
    if(file_to_unlink == root)
        root = root->next;
    file_node_address_space_init(Free, file_to_unlink, 0, 0);
    
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
    char *s[]={"/media/guanxiux/Data/OSH/3-guanxiux","-f", "-s" ,"mountpoint"};
    return fuse_main(4, s, &op, NULL);
    // return fuse_main(argc, argv, &op, NULL);
}
