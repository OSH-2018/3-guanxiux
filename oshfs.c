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

static char *mem[64 * 1024 + 2];  
static size_t blocknr = 64 * 1024;
static size_t blocksize = 64 * 1024;
static size_t FileNodeAddressSpace;
static size_t LinkListAddressSpace;
char *blockbuffer;
int State;
int ProcessState;

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

#define Starting 1
#define Processing 2

static struct filenode *root = NULL;

static struct filenode * file_node_address_space_init(int mode, struct filenode * pass, size_t space_scale){
    size_t headersize;
    headersize = sizeof(struct filenode *);
    switch (mode)
    {
        case Initialize:{
            struct filenode *temp, *last;
            char *file_node_address_space = mem[FileNodeAddressSpace];
            size_t nodesize = (sizeof(struct filenode) + 256 + sizeof(struct stat));
            
            memset(file_node_address_space, 0, space_scale);

            file_node_address_space += headersize;

            if (pass) 
                pass->next = (struct filenode*)file_node_address_space;

            memcpy(mem[FileNodeAddressSpace], &file_node_address_space, headersize);

            last = NULL;
            for(long i = 0; i < blocknr; i++){
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
                return NULL;
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

static struct LinkList* link_list_address_space_init(int mode, struct LinkList * pass, size_t space_scale){
    size_t headersize;
    headersize = sizeof(struct LinkList *);
    switch (mode)
    {
        case Initialize:{
            struct LinkList *temp, *last;
            char *link_list_address_space = mem[LinkListAddressSpace];
            size_t nodesize = sizeof(struct LinkList);
            
            memset(link_list_address_space, 0, space_scale);

            link_list_address_space += headersize;

            if (pass) 
                pass->next = (struct LinkList*)link_list_address_space;

            memcpy(mem[LinkListAddressSpace], &link_list_address_space, headersize);

            last = NULL;
            for(long i = 0; i < 2 * blocknr; i++){
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
                return NULL;
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
    heap->next = link_list_address_space_init(Malloc, NULL, 0);
    if(heap->next){
        heap->next->prev = heap;
        heap = heap->next;
        heap->i = i;
        heap->next = NULL;
    }
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
        link_list_address_space_init(Free, temp, 0);
        return i;
    }
    else{
        head->next = temp->next;
        if(head == aMemHead)
            aMemHeap = aMemHead;
        link_list_address_space_init(Free, temp, 0);
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
        link_list_address_space_init(Free, old_heap, 0);
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
            if(temp == node)
                return NULL;
            *pointer_to_heap = node;
        }
    }
    return node;
}



// static void write_catch(const char *buf, size_t size, off_t offset, int mode, struct LinkList **heap){
//     if(State == NotWriting)
//         return;
//     if (mode == NotTriggered){
//         size_t  length_to_write, buf_covered = 0;
        
//         // length_to_write = (size + offset % blocksize > blocksize) ? blocksize - offset % blocksize : size ;
//         // memcpy(blockbuffer + offset % blocksize, buf, length_to_write );
//         // buf_covered += length_to_write;
//         // block_buffer_length_written = offset % blocksize + length_to_write;

//         // if( length_to_write + offset % blocksize >= blocksize ){
//         //     memcpy(mem[block_writing->i], blockbuffer, blocksize);
//         //     block_writing = get_or_create_link_list_node(0, block_writing, heap, 1);
//         //     memset(blockbuffer, 0 , blocksize);
//         //     block_buffer_length_written = 0;
//         //     tag = Written;
//         // }

//     if(mode == Triggered && tag == NotWritten){
//         State = NotWriting;
//         memcpy(mem[block_writing->i], blockbuffer, block_buffer_length_written);
//         memset(blockbuffer, 0 , blocksize);
//         block_buffer_length_written = 0;
//     }
//     if(mode == Triggered && tag == Written){
//         State = NotWriting;
//         aMemHeap = append_link_list(block_writing->i, aMemHeap, MemFree);
//         link_list_address_space_init(Free, block_writing, 0);
//     }
//     return;
// }

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
    struct filenode *new = file_node_address_space_init(Malloc, NULL, 0);
    new->filename = (char *)((char *)new + sizeof(struct filenode));   //存文件名
    memcpy(new->filename, filename, strlen(filename) + 1);
    new->st = (struct stat *)((char *)new + 256);   //存文件状态
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
    size_t scale_of_file_node_address_space = 8 + (sizeof(struct filenode) + 256 + sizeof(struct stat)) * (blocknr + 2);
    FileNodeAddressSpace = blocknr + 1;
    mem[FileNodeAddressSpace] = mmap(NULL, scale_of_file_node_address_space, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    file_node_address_space_init(Initialize, NULL, scale_of_file_node_address_space);

    size_t scale_of_link_list_address_space = 2 * (blocknr + 2) *sizeof(struct LinkList);    
    LinkListAddressSpace = blocknr;
    mem[LinkListAddressSpace] = mmap(NULL, scale_of_link_list_address_space, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    link_list_address_space_init(Initialize, NULL, scale_of_link_list_address_space);

    aMemHead = link_list_address_space_init(Malloc, NULL, 0);
    aMemHeap = aMemHead;

    size_t blocknr_boundry = LinkListAddressSpace - 1;

    for(int i = 0; i < blocknr_boundry; i++) {
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
    State = NotWriting;
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
    st.st_blksize = blocksize;
    st.st_blocks = 0;
    st.st_dev = dev;
    create_filenode(path + 1, &st);
    return 0;
}

static int oshfs_open(const char *path, struct fuse_file_info *fi)
{
    State = NotWriting;
    ProcessState = Starting;
    return 0;
}



// static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
    
//     if (State == NotWriting) {
//         FileNodeWriting = get_filenode(path);
//         State = Writing;
//         block_writing = &FileNodeWriting->content;
//         size_t block_offset = offset/blocksize; 
//         block_writing = get_or_create_link_list_node(block_offset, &FileNodeWriting->content, &FileNodeWriting->heap, 1);
//     }
//     if (State == Writing) {
//         FileNodeWriting->st->st_size = offset + size;
//         write_catch(buf, size, offset, NotTriggered, &(FileNodeWriting->heap));
//     }
//     return size;
// }


struct LinkList *block_writing = NULL;
struct filenode * FileNodeWriting = NULL;
static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    if (State == NotWriting){
        FileNodeWriting = get_filenode(path);
    }
    ProcessState = Starting;
    FileNodeWriting->st->st_size = offset + size;
    size_t buf_covered = 0;  
    if (size <= 0)
        return size;
    while(buf_covered < size){
        if (State == NotWriting){
            size_t block_offset = offset/blocksize;
            block_writing = get_or_create_link_list_node(block_offset, &FileNodeWriting->content, &FileNodeWriting->heap, 1);
            State = Writing;
        }
        else {
            if( (ProcessState == Starting || buf_covered > 0) && (offset + buf_covered) % blocksize == 0){
                block_writing = get_or_create_link_list_node(0, block_writing, &FileNodeWriting->heap, 1);
                if(block_writing == NULL)
                    return -ENOSPC;
                FileNodeWriting->st->st_blocks += 1;
            }
            // if(buf_covered == size)
            //         break;
        }
        ProcessState = Processing;
        if(size - buf_covered <= blocksize - (offset + buf_covered) % blocksize){
            memcpy(mem[block_writing->i] + (offset + buf_covered) % blocksize, buf + buf_covered, size - buf_covered);
            buf_covered = size;
        }
        else{
            memcpy(mem[block_writing->i] + (offset + buf_covered) % blocksize, buf + buf_covered, blocksize - (offset + buf_covered) % blocksize);
            buf_covered += blocksize - (offset + buf_covered) % blocksize; 
        }
        
    }
    return size;  
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


struct filenode *FileNodeReading = NULL;
struct LinkList *block_reading = NULL;
static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    if (State == NotReading){
        FileNodeReading = get_filenode(path);
    }
    size_t buf_covered = 0, ret;
    ProcessState = Starting;
    ret = FileNodeReading->st->st_size < size + offset ? FileNodeReading->st->st_size - offset : size;
    if (ret == 0)
    return size;
    while(buf_covered < ret){
        if (State == NotReading){
            size_t block_offset = offset/blocksize;
            block_reading = get_or_create_link_list_node(block_offset, &FileNodeReading->content, &FileNodeWriting->heap, 0);
            State = Reading;
        }
        else {
            if( ( ProcessState == Starting || buf_covered > 0) && (offset + buf_covered) % blocksize == 0 ){
                block_reading = get_or_create_link_list_node(0, block_reading, &FileNodeReading->heap, 0);
                if(block_reading == NULL)
                    return -ENOSPC;
                FileNodeReading->st->st_blocks += 1;
            }
        }
        ProcessState = Processing;
        if(ret - buf_covered <= blocksize - (offset + buf_covered) % blocksize){
            memcpy(buf + buf_covered, mem[block_reading->i] + (offset + buf_covered) % blocksize, ret - buf_covered);
            buf_covered = size;
        }
        else{
            memcpy(buf + buf_covered, mem[block_reading->i] + (offset + buf_covered) % blocksize, blocksize - (offset + buf_covered) % blocksize);
            buf_covered += blocksize - (offset + buf_covered) % blocksize; 
        }
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
    while(data != NULL){
        next = data->next;
        aMemHeap = append_link_list(data->i, aMemHeap, MemFree);
        link_list_address_space_init(Free, data, 0);
        data = next;
    } 
    if (file_to_unlink->prev) 
        file_to_unlink->prev->next = file_to_unlink->next;
    if (file_to_unlink->next)
        file_to_unlink->next->prev = file_to_unlink->prev;
    if(file_to_unlink == root)
        root = root->next;
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