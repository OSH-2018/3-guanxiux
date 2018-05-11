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
} *aMemHeap, aMemHead;

struct filenode {
    char *filename;
    struct LinkList content;
    struct LinkList *heap;
    struct stat *st;
    struct filenode *next;
    struct filenode *prev;
};

static const size_t size = 4 * 1024;  //总大小，4GB, 4 * 1024 * 1024 * (size_t)1024
static char *mem[1024];   //指向内存位置的指针的数组，size 和 mem 共同决定  blocksize , 64*1024
static size_t blocknr;
static size_t blocksize;

static struct filenode *root = NULL;

static struct LinkList* append_link_list(size_t i, struct LinkList *heap){
    if(i == -1)
        return heap;
    heap->next =(struct LinkList *) malloc(sizeof(struct LinkList));
    heap->next->prev = heap;
    heap = heap->next;
    heap->i = i;
    heap->next = NULL;
    return heap;
}

static size_t pop_link_list(struct LinkList *head){
    struct LinkList* temp= head->next;
    temp->next->prev = head;
    head->next = temp->next;
    size_t i = temp->i;
    free(temp);
    return i;
}

static struct LinkList * del_link_list_from_tail(struct LinkList * old_heap){
    struct LinkList * heap = old_heap;
    if(heap->i == -1)
        return heap;
    if(heap->prev == NULL)
        return old_heap;
    else{
        heap = old_heap->prev;
        heap->next = NULL;
        free(old_heap);
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
            node = append_link_list(pop_link_list(&aMemHead), temp);
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
    struct filenode *new = (struct filenode *)malloc(sizeof(struct filenode));
    new->filename = (char *)malloc(strlen(filename) + 1);   //存文件名
    memcpy(new->filename, filename, strlen(filename) + 1);
    new->st = (struct stat *)malloc(sizeof(struct stat));   //存文件状态
    memcpy(new->st, st, sizeof(struct stat));
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

    // Demo 1
    int i;
    aMemHeap = &aMemHead;
    for(i = 0; i < blocknr - 1; i++) {
        mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        aMemHeap = append_link_list(i, aMemHeap);
        // memset(mem[i], 0, blocksize);
    }
    aMemHead.i = i;
    aMemHead.prev = NULL;
    mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    // for(int i = 0; i < blocknr; i++) {
    //     munmap(mem[i], blocksize);
    // }
    /*
    // Demo 2
    mem[0] = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    for(int i = 0; i < blocknr; i++) {
        mem[i] = (char *)mem[0] + blocksize * i;
        // memset(mem[i], 0, blocksize);
    }
    for(int i = 0; i < blocknr; i++) {
        munmap(mem[i], blocksize);
    }*/
    return NULL;
}

static int oshfs_getattr(const char *path, struct stat *stbuf)
{
    int ret = 0;
    struct filenode *node = get_filenode(path);   //找到path的文件的指针，或者NULL
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
    
    // node->content = realloc(node->content, offset + size);
    // memcpy(node->content + offset, buf, size);
    return temp;
}

static int oshfs_truncate(const char *path, off_t size)
{
    struct filenode *node = get_filenode(path);
    if(size > node->st->st_size){
        for(int i = size / blocksize - node->st->st_size / blocksize; i > 0 ; i--)
            node->heap = append_link_list(pop_link_list(&aMemHead), node->heap);
    }
    else {
        for(int i = node->st->st_size / blocksize - size / blocksize; i > 0 ; i--)
            aMemHeap = append_link_list(node->heap->i, aMemHeap);
            node->heap = del_link_list_from_tail(node->heap);
    }
    node->st->st_size = size;
    return 0;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    struct filenode *node = get_filenode(path);
    int ret = size;
    if(offset + size > node->st->st_size)
        ret = node->st->st_size - offset;
    size_t length_to_read, block_offset;
    size_t length_read = 0;
    block_offset = (offset + size) / blocksize;
    length_to_read = (offset % blocksize + size) > blocksize ? blocksize - offset % blocksize : size;

    struct LinkList * block_being_read;
    block_being_read = get_or_create_link_list_node(block_offset, &node->content, NULL, 0);
    memcpy(buf, mem[block_being_read->i] + offset % blocksize, length_to_read);

    while(1){
        ret -= length_to_read;
        length_read += length_to_read;
        block_being_read = get_or_create_link_list_node(0, block_being_read, NULL, 0);
        if(ret <= blocksize){
            memcpy(buf + length_read, mem[block_being_read->i], ret);
            break;
        } 
        else
            memcpy(buf + length_read, mem[block_being_read->i], blocksize);
        length_to_read = blocksize;
    }
    return ret;
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
    next = data->next;
    free(data);
    data = next;
    while(data != NULL){
        next = data->next;
        aMemHeap = append_link_list(data->i, aMemHeap);
        free(data);
        data = next;
    }
    
    if (file_to_unlink->prev) 
        file_to_unlink->prev->next = file_to_unlink->next;
    
    if (file_to_unlink->next)
        file_to_unlink->next->prev = file_to_unlink->prev;
    free(file_to_unlink->st);           //stat
    free(file_to_unlink->filename);     //filename
    free(file_to_unlink);
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
