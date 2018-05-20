# 内存文件系统
3-guanxiux created by GitHub Classroom
## 内存设置构成: 
  将 mmap 到的完整的内存划分为 blocknr 个块: 
  + 将大小为 size 的内存的头指针设为 第 0 号 mem 块的头指针. 第 0 号块存储这个文件系统的信息, 有 blocksize, blocknr, 文件链表的头节点 root, 到文件结点地址空间(存储文件链表的结点的 mem 块)的指针 FileNodeAddressSpace, 到链表结点地址空间(存储有关 mem 块使用情况的链表结点的 mem 块)的指针 LinkListAddressSpace, 可用 mem 块链表的头指针 aMemHead, 可用 mem 块链表的尾指针 aMemHeap. 这个块的大小通过储存的各个变量的大小和确定. 各个变量在这个块上的存储位置姑且当作某种协议, 用全局变量确定.
  
  + 第 1 号块到 blocknr - 3 号块依次紧挨前一个块, 大小是 blocksize, 用作存储文件内容的块.
  + 第 blocknr - 2 号块是 LinkListAddressSpace, 保险起见地址与前一个块的尾部相隔 128, 存储着记录 mem 块的使用情况的链表, 在其中维护的链表有, 各个文件内容所在的块指针的链表(头指针尾指针存储在每一个文件的 filenode 上), 可用的 mem 块的队列(头指针 aMemHead ,尾指针 aMemHeap), 这个块中空余的未被使用的链表结点栈(头指针存储在这个块头 8 个字节上, 记为 pointer_to_available). 因为上述的链表都可以使用同一种结构体如下作为结点, 所以将它们存在同一个块中.为确保结点数目足够, 块的大小暂定为这种结构体的大小与3 * (block - 2) 的乘积.
  ```
  struct LinkList {
    char *mem_block;
    struct LinkList *prev;
    struct LinkList * next;
};
```
  + 第 blocknr - 1 号块是 FileNodeAddressSpace, 地址与前一个块的尾部相隔 128, 存储记录文件信息的链表, 在其中维护的链表有, 储存内存中存在的文件的信息的链表(头指针为 root), 这个块中空余未被使用的链表结点栈(头指针存储在这个块头 8 个字节上, 记为 pointer_to_available). 块的大小是 filenode 大小加 256 加 stat 结构体大小的和与(blocknr - 2)的乘积. 每个 filenode 结构体后都设置存储文件名和 stat 结构体的空间.
  ```
  struct filenode {
    char *filename;
    struct LinkList *content;
    struct LinkList *heap;
    struct stat *st;
    struct filenode *next;
    struct filenode *prev;
};
```
